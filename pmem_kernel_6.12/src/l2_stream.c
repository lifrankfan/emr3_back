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
#include <asm/barrier.h>

#include "cxl_func.h"
#include "l2_stream.h"
#include "nvme.h"

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
static int alloc_contig(size_t bytes, int nid, struct page **out_pg, phys_addr_t *out_pa, void **out_va)
{
    unsigned int order;
    struct page *pg;
    void *va;

    if (!bytes || (bytes & (PAGE_SIZE - 1))) return -EINVAL;
    order = get_order(bytes);

    if (nid == NUMA_NO_NODE)
        pg = alloc_pages(GFP_KERNEL | __GFP_NOWARN, order);
    else
        pg = alloc_pages_node(nid, GFP_KERNEL | __GFP_NOWARN, order);

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
static int l2_launch_batch(phys_addr_t device_base_pa,
                           phys_addr_t query_pa,
                           u64         num_vecs,
                           u32         dim_cfg,
                           u64        *cycles_out)
{
    void __iomem *csr = (void __iomem *)get_virt_addr();
    /* Offsets in bytes */
    const u32 OFF_FUNC_TYPE  = 0x00;
    const u32 OFF_PAGE_ADDR0 = 0x08;
    const u32 OFF_PAGE_ADDR1 = 0x10;
    const u32 OFF_DELAY      = 0x18;
    const u32 OFF_TEST_CASE  = 0x20;
    const u32 OFF_RESP       = 0x28;
    const u32 OFF_NUM_REQ    = 0x60;
    const u32 OFF_ADDR_RANGE = 0x68;
    const u32 OFF_L2_START   = 0x70;
    const u32 OFF_BAR_ADDR   = 0xE0;
    const u32 OFF_CSR_INIT   = 0x100;

    const int max_tries = 20000;
    int       tries;

    /* Use 32-bit writes. For 64-bit values, write lo/hi if needed, 
       but here most regs seem to be fine with lower 32-bit or are just scratchpad?
       The original code wrote 64-bit.
       If the device is 64-bit register but 32-bit bus, we need 2 writes.
       However, let's assume standard AXI-Lite which is 32-bit.
       If regs are 64-bit wide in hardware design, they usually take 2 cycles.
       Let's try writing lower 32 first.
    */
    
    /* Helper to write 64-bit as two 32-bit */
    #define WRITE64(val, offset) do { \
        writel((u32)(val), csr + offset); \
        writel((u32)((val) >> 32), csr + offset + 4); \
    } while (0)

    #define READ64(offset) ( \
        (u64)readl(csr + offset) | ((u64)readl(csr + offset + 4) << 32) \
    )

    // *REG_CSR_INIT   = 1ull; 
    writel(1, csr + OFF_CSR_INIT);
    writel(0, csr + OFF_CSR_INIT + 4); 
    mb();

    // *REG_FUNC_TYPE  = 3ull; 
    writel(3, csr + OFF_FUNC_TYPE);
    writel(0, csr + OFF_FUNC_TYPE + 4);
    mb();

    /* Program batch parameters */
    WRITE64(device_base_pa, OFF_PAGE_ADDR0);
    WRITE64(query_pa, OFF_PAGE_ADDR1);
    WRITE64(num_vecs, OFF_NUM_REQ);
    WRITE64(dim_cfg, OFF_ADDR_RANGE);
    WRITE64(FPGA_BUS_ID, OFF_DELAY);
    WRITE64(FPGA_BAR_0_ADDRESS, OFF_BAR_ADDR); 
    mb();

    // *REG_TEST_CASE = 100ull;
    WRITE64(100ull, OFF_TEST_CASE);
    mb();

    // *REG_L2_START = 0ull;
    WRITE64(0ull, OFF_L2_START);
    mb();

    // Start
    WRITE64(1ull, OFF_L2_START);
    mb();

    for (tries = 0; tries < max_tries; ++tries) { 
        u64 resp = READ64(OFF_RESP); // Or just 32-bit read?
        
        if (resp & 0x1ull)
            break;

        usleep_range(500, 700);
    }

    if (tries == max_tries) {
        iounmap(csr);
        return -ETIMEDOUT;
    }

    *cycles_out = READ64(OFF_DELAY);
    u64 l2_res = READ64(OFF_RESP) >> 1;
    
    pr_info("l2_stream: Batch done. Cycles=%llu, Last L2 Result=%llu\n", 
            (unsigned long long)*cycles_out, (unsigned long long)l2_res);

    WRITE64(0ull, OFF_L2_START);
    mb();

    iounmap(csr);
    return 0;
}


#include <asm/fpu/api.h>

// ---------- Helpers ----------
// Q16.16 conversion: val * 65536.0
// We implement this using integer arithmetic to avoid "SSE register" errors in kernel build.
// IEEE 754 float32: [31: sign] [30-23: exponent] [22-0: mantissa]
// Bias 127.
static void convert_float_to_fixed_stream(float *src_float, int *dst, size_t count)
{
    u32 *src = (u32 *)src_float; // Treat as bits
    int i;
    
    // We don't strictly need kernel_fpu_begin/end if we don't use FPU instructions.
    // But we'll leave them just in case compiler optimizes something strangely, 
    // though purely bitwise logic shouldn't touch XMM.
    // Actually, removing them is safer if we are purely integer.
    
    for (i = 0; i < count; i++) {
        u32 val = src[i];
        u32 sign = (val >> 31) & 1;
        int exp  = ((val >> 23) & 0xFF) - 127;
        u32 man  = (val & 0x7FFFFF) | 0x800000; // Add implicit 1
        
        // We want result = float * 2^16
        // float = 1.mantissa * 2^exp
        // result = 1.mantissa * 2^(exp + 16)
        
        int shift = exp + 16 - 23; // 23 is mantissa bits
        // result = man << shift (if shift > 0)
        
        int res;
        if (exp == -127) { // Zero / Denormal (treat as 0)
             res = 0;
        } else if (shift >= 0) {
             // Check overflow? SIFT1M is mostly small integers in range 0-255.
             // 255 * 65536 = 16M, fits in 32 bit.
             res = man << shift;
        } else {
             res = man >> (-shift);
        }
        
        if (sign) res = -res;
        dst[i] = res;
    }
}

// ---------- Public API ----------
int run_l2_streaming_from_file(const char *base_path,
                               const char *query_path,
                               u64 total_vecs, u32 dim,
                               u64 batch_vecs, u32 clk_mhz,
                               int cxl_nid, u64 cxl_base)
{
    const size_t BYTES_PER_VEC = 512;     // 128 * 4B
    const size_t QUERY_BYTES   = BYTES_PER_VEC;

    struct page *query_page = NULL, *base_pages = NULL;
    void *query_va = NULL, *base_va = NULL;
    phys_addr_t query_pa = 0, cpu_base_pa = 0;
    
    // Staging buffers for conversion (allocated in standard Kernel memory)
    // We use a separate staging buffer to Load -> Convert -> Write to CXL
    struct page *staging_page = NULL;
    void *staging_va = NULL;
    struct page *base_staging_pages = NULL;
    void *base_staging_va = NULL;

    int ret = 0;

    // 1. Setup Query Vector
    // Allocate final destination in CXL memory
    if (alloc_contig(PAGE_SIZE, cxl_nid, &query_page, &cpu_base_pa, &query_va)) {
        pr_err("l2_stream: Failed to allocate query page in CXL node %d\n", cxl_nid);
        return -ENOMEM;
    }
    query_pa = cpu_base_pa;
    
    // Calculate Query Device Physical Address (DPA)
    phys_addr_t query_device_pa = query_pa;
    if (cxl_nid != NUMA_NO_NODE && cxl_base != 0) {
        if (query_pa >= cxl_base) {
            query_device_pa = query_pa - cxl_base;
        } else {
            pr_warn("l2_stream: Query PA %llx < cxl_base %llx\n", 
                    (unsigned long long)query_pa, (unsigned long long)cxl_base);
        }
    }

    // Allocate small staging buffer for query (1 page)
    staging_page = alloc_page(GFP_KERNEL);
    if (!staging_page) {
        free_contig(query_page, PAGE_SIZE);
        return -ENOMEM;
    }
    staging_va = page_address(staging_page);

    // Load and Convert Query
    {
        loff_t qpos = 0;
        if (read_exact_simple(query_path, staging_va, QUERY_BYTES, &qpos)) {
            pr_err("l2_stream: Failed to read query file\n");
            __free_page(staging_page);
            free_contig(query_page, PAGE_SIZE);
            return -EIO;
        }
        
        // Convert in place in staging
        // We assume input is Float32 (4 bytes), Output is Q16.16 (4 bytes)
        // So size matches.
        convert_float_to_fixed_stream((float *)staging_va, (int *)staging_va, dim); // dim should be 128
        
        // Copy to CXL
        memcpy(query_va, staging_va, QUERY_BYTES);
    }
    __free_page(staging_page); // Done with query staging


    // 2. Setup Base Vectors (Batch mode)
    if (batch_vecs == 0 || batch_vecs > total_vecs) batch_vecs = total_vecs;
    
    size_t batch_bytes = (size_t)batch_vecs * BYTES_PER_VEC;
    // Round up to page size for allocation
    size_t alloc_bytes = batch_bytes;
    if (alloc_bytes & (PAGE_SIZE - 1))
        alloc_bytes = (alloc_bytes + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);

    // Allocate Staging for Batch (Standard RAM)
    if (alloc_contig(alloc_bytes, NUMA_NO_NODE, &base_staging_pages, &cpu_base_pa, &base_staging_va)) {
        pr_err("l2_stream: Failed to alloc staging buffer for batch\n");
        free_contig(query_page, PAGE_SIZE);
        return -ENOMEM;
    }

    // Allocate Destination for Batch (CXL RAM)
    if (alloc_contig(alloc_bytes, cxl_nid, &base_pages, &cpu_base_pa, &base_va)) {
        pr_err("l2_stream: Failed to alloc CXL buffer for batch\n");
        free_contig(base_staging_pages, alloc_bytes);
        free_contig(query_page, PAGE_SIZE);
        return -ENOMEM;
    }

    // Calculate Batch Device Physical Address
    phys_addr_t device_pa = cpu_base_pa;
    if (cxl_nid != NUMA_NO_NODE && cxl_base != 0) {
        if (cpu_base_pa >= cxl_base) {
            device_pa = cpu_base_pa - cxl_base;
        } else {
             pr_warn("l2_stream: Batch PA %llx < cxl_base %llx\n", 
                     (unsigned long long)cpu_base_pa, (unsigned long long)cxl_base);
        }
    }

    // Stream Loop
    {
        loff_t bpos = 0;
        u64 remain = total_vecs, pass = 0;
        u64 cycles_acc = 0, vecs_acc = 0;

        while (remain) {
            u64 this_vecs  = (remain > batch_vecs) ? batch_vecs : remain;
            size_t this_bs = (size_t)this_vecs * BYTES_PER_VEC;

            // Read to Staging
            if (read_exact_simple(base_path, base_staging_va, this_bs, &bpos)) {
                pr_err("l2_stream: base read failed at pass %llu\n", pass);
                ret = -EIO;
                break;
            }

            // Convert in Staging
            // Total floats = this_vecs * dim
            convert_float_to_fixed_stream((float *)base_staging_va, (int *)base_staging_va, this_vecs * dim);

            // Copy to CXL
            // Note: memcpy_toio could be used if mapped uncached, but system-ram CXL is usually cached.
            // Using standard memcpy.
            memcpy(base_va, base_staging_va, this_bs); 

            // Flush caches if CXL memory is not coherent with device (assumed coherent or device snoops)
            // But explicit flush might be safer if unsure? 
            // For CXL Type 2 / Type 3, it should remain coherent. 
            // We'll leave it as memcpy for now.

            {
                u64 cyc = 0;
                int rc = l2_launch_batch(device_pa, query_device_pa, this_vecs, dim, &cyc);
                if (rc) {
                    pr_err("l2_stream: batch failed at pass %llu (rc=%d)\n", pass, rc);
                    ret = rc;
                    break;
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
        if (!ret && vecs_acc) {
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

            if (write_text_simple("/fast-lab-share/lifan3/emr3_back/pmem_kernel_6.12/data/l2_stream_result.txt",
                                  out, strlen(out)) < 0)
                pr_err("l2_stream: failed to write result file\n");
            else
                pr_info("%s", out);
        }
    }

    // Cleanup
    free_contig(base_staging_pages, alloc_bytes);
    free_contig(base_pages, alloc_bytes);
    free_contig(query_page, PAGE_SIZE);
    
    return ret;
}
EXPORT_SYMBOL(run_l2_streaming_from_file);
