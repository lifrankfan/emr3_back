#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/delay.h>
#include <linux/highmem.h>
#include <linux/vmalloc.h>
#include <linux/io.h>
#include <linux/version.h>
#include <linux/string.h>
#include <linux/types.h>

#include "cxl_func.h"
#include "l2_stream.h"

// ---------- Simple file I/O wrappers ----------
static long write_text_simple(const char *path, const char *buf, size_t len)
{
    struct file *f;
    loff_t pos = 0;
    long ret;

    f = filp_open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (IS_ERR(f)) {
        pr_err("l2_stream: open failed: %s\n", path);
        return PTR_ERR(f);
    }
    ret = kernel_write(f, buf, len, &pos);
    filp_close(f, NULL);
    return ret;
}

static long read_exact_simple(const char *path, void *dst, size_t want, loff_t *pos)
{
    struct file *f;
    size_t done = 0;

    f = filp_open(path, O_RDONLY, 0);
    if (IS_ERR(f))
        return PTR_ERR(f);

    while (done < want) {
        long r = kernel_read(f, (char *)dst + done, want - done, pos);
        if (r <= 0) { filp_close(f, NULL); return (done ? (long)done : r); }
        done += (size_t)r;
    }
    filp_close(f, NULL);
    return 0;
}

// ---------- Physically contiguous allocator ----------
static int alloc_contig(size_t bytes, struct page **out_pg, phys_addr_t *out_pa, void **out_va)
{
    unsigned int order;
    struct page *pg;
    void *va;

    if (!bytes || (bytes & (PAGE_SIZE - 1))) return -EINVAL;
    order = get_order(bytes);

    pg = alloc_pages(GFP_KERNEL | __GFP_NOWARN, order);
    if (!pg) return -ENOMEM;

    va = page_address(pg);
    if (!va) { __free_pages(pg, order); return -ENOMEM; }

    *out_pg = pg;
    *out_pa = virt_to_phys(va);
    *out_va = va;
    return 0;
}

static void free_contig(struct page *pg, size_t bytes)
{
    if (pg) __free_pages(pg, get_order(bytes));
}

// ---------- One-batch CSR launch ----------
static int l2_launch_batch(phys_addr_t base_pa, phys_addr_t query_pa,
                           u64 num_vecs, u32 dim_cfg, u64 *cycles_out)
{
    volatile unsigned long long *csr = get_virt_addr();  // maps BAR1 CSR region

    volatile unsigned long long *REG_PAGE_ADDR0  = csr + (0x0008 >> 3);
    volatile unsigned long long *REG_PAGE_ADDR1  = csr + (0x0010 >> 3);
    volatile unsigned long long *REG_DELAY       = csr + (0x0018 >> 3);
    volatile unsigned long long *REG_TEST_CASE   = csr + (0x0020 >> 3);
    volatile unsigned long long *REG_RESP        = csr + (0x0028 >> 3);
    volatile unsigned long long *REG_NUM_REQ     = csr + (0x0060 >> 3);
    volatile unsigned long long *REG_ADDR_RANGE  = csr + (0x0068 >> 3);
    volatile unsigned long long *REG_L2_START    = csr + (0x0070 >> 3);

    *REG_PAGE_ADDR0 = base_pa;
    *REG_PAGE_ADDR1 = query_pa;
    *REG_NUM_REQ    = num_vecs;
    *REG_ADDR_RANGE = dim_cfg;
    *REG_TEST_CASE  = 100ull;
    asm volatile("mfence");

    *REG_L2_START   = 1ull;
    asm volatile("mfence");

    {
        int tries = 0, max_tries = 1000000;
        while (tries++ < max_tries) {
            if ((*REG_RESP) & 0x1ull) break;
            usleep_range(500, 700);
        }
        if (tries >= max_tries) return -ETIMEDOUT;
    }

    *cycles_out = *REG_DELAY;  // muxed to l2_cycles in wrapper for test_case==100
    return 0;
}

// ---------- Public API ----------
int run_l2_streaming_from_file(const char *base_path,
                               const char *query_path,
                               u64 total_vecs, u32 dim,
                               u64 batch_vecs, u32 clk_mhz)
{
    const size_t BYTES_PER_VEC = 512;     // 128 * 4B
    const size_t QUERY_BYTES   = BYTES_PER_VEC;

    struct page *query_page = NULL, *base_pages = NULL;
    void *query_va = NULL, *base_va = NULL;
    phys_addr_t query_pa = 0, base_pa = 0;

    // Allocate a page for the query (512B fits)
    query_page = alloc_page(GFP_KERNEL);
    if (!query_page) return -ENOMEM;
    query_va = page_address(query_page);
    query_pa = virt_to_phys(query_va);

    // Load query
    {
        loff_t qpos = 0;
        int e = read_exact_simple(query_path, query_va, QUERY_BYTES, &qpos);
        if (e) { __free_page(query_page); return e; }
    }

    // Batch setup
    if (batch_vecs == 0 || batch_vecs > total_vecs) batch_vecs = total_vecs;
    {
        size_t batch_bytes = (size_t)batch_vecs * BYTES_PER_VEC;
        if (batch_bytes & (PAGE_SIZE - 1))
            batch_bytes = (batch_bytes + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);

        if (alloc_contig(batch_bytes, &base_pages, &base_pa, &base_va)) {
            __free_page(query_page);
            return -ENOMEM;
        }

        // Stream the base file
        {
            loff_t bpos = 0;
            u64 remain = total_vecs, pass = 0;
            u64 cycles_acc = 0, vecs_acc = 0;

            while (remain) {
                u64 this_vecs  = (remain > batch_vecs) ? batch_vecs : remain;
                size_t this_bs = (size_t)this_vecs * BYTES_PER_VEC;

                memset(base_va, 0, batch_bytes);
                if (read_exact_simple(base_path, base_va, this_bs, &bpos)) {
                    pr_err("l2_stream: base read failed at pass %llu\n", pass);
                    free_contig(base_pages, batch_bytes);
                    __free_page(query_page);
                    return -EIO;
                }

                {
                    u64 cyc = 0;
                    int rc = l2_launch_batch(base_pa, query_pa, this_vecs, dim, &cyc);
                    if (rc) {
                        pr_err("l2_stream: batch failed at pass %llu (rc=%d)\n", pass, rc);
                        free_contig(base_pages, batch_bytes);
                        __free_page(query_page);
                        return rc;
                    }
                    cycles_acc += cyc;
                    vecs_acc   += this_vecs;
                    pr_info("l2_stream: pass=%llu vecs=%llu cyc=%llu acc=%llu\n",
                            pass, this_vecs, cyc, cycles_acc);
                }

                remain -= this_vecs;
                pass++;
            }

            // Summary
            if (vecs_acc) {
                char out[512];
                u64 cpv_x1000 = (cycles_acc * 1000ull) / vecs_acc;
                u64 time_ns   = clk_mhz ? (cycles_acc * 1000ull) / clk_mhz : 0;

                scnprintf(out, sizeof(out),
                          "L2 stream result:\n"
                          "total_vecs=%llu\n"
                          "dim=%u\n"
                          "clk_mhz=%u\n"
                          "cycles_total=%llu\n"
                          "cycles_per_vec=%llu.%03llu\n"
                          "~time_ns=%llu\n",
                          (unsigned long long)vecs_acc,
                          dim,
                          clk_mhz,
                          (unsigned long long)cycles_acc,
                          (unsigned long long)(cpv_x1000/1000ull),
                          (unsigned long long)(cpv_x1000%1000ull),
                          (unsigned long long)time_ns);

                if (write_text_simple("/home/lifan3/cxl_dist_cal/data/l2_stream_result.txt",
                                      out, strlen(out)) < 0)
                    pr_err("l2_stream: failed to write result file\n");
                else
                    pr_info("%s", out);
            }
        }

        free_contig(base_pages, batch_bytes);
    }

    __free_page(query_page);
    return 0;
}
EXPORT_SYMBOL(run_l2_streaming_from_file);
