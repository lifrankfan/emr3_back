#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/nvme.h>
#include <linux/virtio.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/delay.h>
#include <linux/random.h>
#include <linux/io.h>
#include <linux/ktime.h>
#include <linux/dax.h>
#include <linux/blkdev.h>
#include <linux/pfn_t.h>
#include <linux/dma-mapping.h>
#include <linux/slab.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/version.h>
#include <linux/string.h>

#include "cxl_func.h"
#include "nvme.h"

void alloc_and_get_phys(struct page **out_page, phys_addr_t *out_phys)
{
    struct page *page = alloc_pages_node(0, GFP_KERNEL, 0);
    if (!page) {
        pr_err("Failed to allocate page\n");
        return;
    }
    void *addr = page_address(page);
    if (!addr) {
        pr_err("Failed to get page address\n");
        __free_pages(page, 0);
        return;
    }
    *out_page = page;
    *out_phys = virt_to_phys(addr);
}

void *get_virt_addr(void)
{
    void *virt_addr = ioremap(FPGA_BAR_1_ADDRESS, 0x1000);
    return (unsigned long long *)virt_addr;
}

static long write_text_to_file(const char *path, const char *buf, size_t len)
{
    struct file *file;
    loff_t pos = 0;
    long ret;

    file = filp_open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (IS_ERR(file)) {
        pr_err("Failed to open output file: %s\n", path);
        return PTR_ERR(file);
    }
    ret = kernel_write(file, buf, len, &pos);
    filp_close(file, NULL);
    return ret;
}

long read_file_into_buffer(const char *path, void *buffer, size_t buffer_size)
{
    struct file *file;
    loff_t pos = 0;
    size_t done = 0;

    file = filp_open(path, O_RDONLY, 0);
    if (IS_ERR(file)) {
        pr_err("Failed to open file: %s\n", path);
        return PTR_ERR(file);
    }

    while (done < buffer_size) {
        long r = kernel_read(file, (char *)buffer + done, buffer_size - done, &pos);
        if (r <= 0) {
            filp_close(file, NULL);
            if (r < 0) {
                pr_err("kernel_read error %ld on %s\n", r, path);
                return r;
            }
            break; /* EOF */
        }
        done += (size_t)r;
    }

    filp_close(file, NULL);
    pr_info("Read %zu bytes from %s\n", done, path);
    return (long)done;
}

void check_db(void __iomem *mapped_base_nvme, unsigned long long *sq_tail)
{
    void *target_address;
    void *acq_address;
    unsigned int value;
    int i;

    mapped_base_nvme = ioremap_uc(PCI_BAR_ADDRESS, 16 * 1024);
    if (!mapped_base_nvme) {
        pr_err("Failed to map PCI BAR memory region.\n");
        return;
    }

    acq_address = (void *)mapped_base_nvme + 0x28;
    target_address = (void *)mapped_base_nvme + 4096;

    for (i = 0; i < 2; i++) {
        value = ioread32(acq_address + 4 * i);
        pr_info("Read acq value %d: 0x%x\n", i, value);
    }

    for (i = 1; i < 17; i++) {
        value = ioread32(target_address + 8 * i);
        pr_info("Read value %d: 0x%x\n", i, value);
        sq_tail[i - 1] = (unsigned long long)value;
        value = ioread32(target_address + 8 * i + 4);
        pr_info("Read value: 0x%x\n", value);
    }
}

/* Local symbol to avoid no-prototype warnings */
static int __maybe_unused check_admin_q(void)
{
    phys_addr_t phys_addr = 0x21684000;
    void *virt_addr;
    unsigned char data[64];

    virt_addr = phys_to_virt(phys_addr);
    if (!virt_addr) {
        pr_err("Failed to get VA for PA 0x%llx\n", (unsigned long long)phys_addr);
        return -ENOMEM;
    }

    memcpy(data, virt_addr + 64 * 10, sizeof(data));
    pr_info("Read 64 bytes from physical address 0x%llx:\n", (unsigned long long)phys_addr);
    memcpy(data, virt_addr + 64 * 11, sizeof(data));
    pr_info("Read 64 bytes from physical address 0x%llx:\n", (unsigned long long)phys_addr);
    memcpy(data, virt_addr + 0, sizeof(data));
    pr_info("Read 64 bytes from physical address 0x%llx:\n", (unsigned long long)phys_addr);

    print_hex_dump(KERN_INFO, "", DUMP_PREFIX_OFFSET, 16, 1, data, sizeof(data), false);
    return 0;
}

void set_cxl(unsigned long long *cq_addresses, unsigned long long *sq_addresses,
             unsigned long long *buffer_addresses, unsigned long long *sq_tail,
             unsigned long long block_offset)
{
    unsigned long long *cxl_func_type = get_virt_addr();
    volatile unsigned long long *cxl_requester_id = cxl_func_type + 3;
    volatile unsigned long long *cxl_block_index  = cxl_func_type + 4;
    volatile unsigned long long *cxl_sq_addr      = cxl_func_type + 26;
    volatile unsigned long long *cxl_cq_addr      = cxl_func_type + 27;
    volatile unsigned long long *cxl_bar_addr     = cxl_func_type + 28;
    volatile unsigned long long *cxl_sq_tail      = cxl_func_type + 29;
    volatile unsigned long long *cxl_cq_head      = cxl_func_type + 30;
    volatile unsigned long long *cxl_csr_init     = cxl_func_type + 32;
    volatile unsigned long long *cxl_host_buffer  = cxl_func_type + 33;
    volatile unsigned long long *cxl_queue_index  = cxl_func_type + 34;
    volatile unsigned long long *cxl_m5_interval  = cxl_func_type + 35;
    volatile unsigned long long *cxl_m5_query_en  = cxl_func_type + 36;
    int i;

    for (i = 0; i < 16; i++) {
        *cxl_queue_index = i;
        *cxl_sq_addr     = sq_addresses[i];
        *cxl_cq_addr     = cq_addresses[i];
        *cxl_cq_head     = sq_tail[i];
        *cxl_sq_tail     = sq_tail[i];
        *cxl_host_buffer = buffer_addresses[i];

        pr_info("qidx=0x%llx sq=0x%llx cq=0x%llx tail=0x%llx head=0x%llx buf=0x%llx\n",
                *cxl_queue_index, *cxl_sq_addr, *cxl_cq_addr, *cxl_sq_tail, *cxl_cq_head, *cxl_host_buffer);
    }

    *cxl_m5_query_en = 0;
    *cxl_bar_addr    = PCI_BAR_ADDRESS;
    *cxl_requester_id = FPGA_BUS_ID;
    *cxl_block_index  = block_offset * 256ull * 1024ull * 1024ull;
    *cxl_m5_interval  = 0;

    *cxl_func_type = 3;
    *cxl_csr_init  = 1;
}

void set_delay(unsigned long long delay_cnt)
{
    unsigned long long *cxl_func_type = get_virt_addr();
    volatile unsigned long long *cxl_delay_cnt = cxl_func_type + 35;
    *cxl_delay_cnt = delay_cnt;
}

void read_m5(void)
{
    unsigned long long *cxl_func_type = get_virt_addr();
    volatile unsigned long long *cxl_m5_rst        = cxl_func_type + 35;
    volatile unsigned long long *cxl_m5_query_en   = cxl_func_type + 36;
    volatile unsigned long long *cxl_m5_hot_page_0 = cxl_func_type + 40;
    volatile unsigned long long *cxl_m5_hot_page_1 = cxl_func_type + 41;
    volatile unsigned long long *cxl_m5_hot_page_2 = cxl_func_type + 42;
    volatile unsigned long long *cxl_m5_hot_page_3 = cxl_func_type + 43;
    volatile unsigned long long *cxl_m5_hot_page_4 = cxl_func_type + 44;

    *cxl_m5_query_en = 1;
    usleep_range(500, 600);
    pr_info("m5_hot_page_0: 0x%llx\n", *cxl_m5_hot_page_0);
    pr_info("m5_hot_page_1: 0x%llx\n", *cxl_m5_hot_page_1);
    pr_info("m5_hot_page_2: 0x%llx\n", *cxl_m5_hot_page_2);
    pr_info("m5_hot_page_3: 0x%llx\n", *cxl_m5_hot_page_3);
    pr_info("m5_hot_page_4: 0x%llx\n", *cxl_m5_hot_page_4);
    usleep_range(500, 600);
    *cxl_m5_rst = 1;
}

void test_multiple_write(uint64_t *ptr, int iter, int test_case)
{
    ktime_t start_i __maybe_unused, end_i __maybe_unused;
    uint64_t total_latency = 0;
    uint64_t single_latency __maybe_unused = 0;
    int i;

    for (i = 0; i < iter; i++) {
        if (test_case == 0) {
            *(ptr + i) = i;
        } else if (test_case == 1) {
            *(ptr + i) = iter - i;
        } else if (test_case == 2) {
            *(ptr + i) = (uint64_t)ptr;
        }
    }
    asm volatile("mfence");
    pr_info("CXL SSD write avg latency: %llu ns\n", total_latency ? (unsigned long long)(total_latency / iter) : 0ull);
}

void test_multiple_read(uint64_t *ptr, int iter, int test_case)
{
    int data, e_data;
    ktime_t start_i __maybe_unused, end_i __maybe_unused;
    uint64_t total_latency = 0;
    uint64_t single_latency __maybe_unused;
    int i;

    for (i = 0; i < iter; i++) {
        data = *(ptr + i);
        if (test_case == 0) {
            e_data = i;
        } else if (test_case == 1) {
            e_data = iter - i;
        } else {
            e_data = (uint64_t)ptr;
        }
        if (e_data != data)
            pr_err("Data mismatch at %d: expected %d got %d\n", i, e_data, data);
        asm volatile("mfence");
    }
    pr_info("CXL SSD read avg latency: %llu ns\n", total_latency ? (unsigned long long)(total_latency / iter) : 0ull);
}

int launch_cxl_cache_write(unsigned long long page_address, unsigned long long buffer_address, unsigned long long iter)
{
    volatile unsigned long long *cxl_func_type = get_virt_addr();
    volatile unsigned long long *cxl_page_addr_0 = cxl_func_type + 1;
    volatile unsigned long long *cxl_test_case   = cxl_func_type + 2;
    volatile unsigned long long *cxl_write_data_0 = cxl_func_type + 14;
    volatile unsigned long long *cxl_write_data_1 = cxl_func_type + 15;
    volatile unsigned long long *cxl_write_data_2 = cxl_func_type + 16;
    volatile unsigned long long *cxl_write_data_3 = cxl_func_type + 17;
    volatile unsigned long long *cxl_write_data_4 = cxl_func_type + 18;
    volatile unsigned long long *cxl_write_data_5 = cxl_func_type + 19;
    volatile unsigned long long *cxl_write_data_6 = cxl_func_type + 20;
    volatile unsigned long long *cxl_write_data_7 = cxl_func_type + 21;

    if (iter == 0) *cxl_write_data_0 = 0x0000000110050002;
    else          *cxl_write_data_0 = 0x0000000110050001;

    *cxl_write_data_1 = 0x0;
    *cxl_write_data_2 = 0x0;
    *cxl_write_data_3 = 0x78787;
    *cxl_write_data_4 = 0x0;
    *cxl_write_data_5 = 0x4008;
    *cxl_write_data_6 = 0x0;
    *cxl_write_data_7 = 0x0;
    asm volatile("mfence");

    *cxl_test_case  = 13;
    *cxl_page_addr_0 = 0x4080000000ull;
    asm volatile("mfence");

    *cxl_func_type = 1;
    asm volatile("mfence");
    usleep_range(500, 600);
    *cxl_func_type = 2;
    asm volatile("mfence");
    usleep_range(500, 600);
    return 0;
}

int launch_cxl_cache_read(unsigned long long page_address)
{
    volatile unsigned long long *cxl_func_type = get_virt_addr();
    volatile unsigned long long *cxl_page_addr_0 = cxl_func_type + 1;
    volatile unsigned long long *cxl_test_case   = cxl_func_type + 2;
    volatile unsigned long long *cxl_addr_handshake __maybe_unused     = cxl_func_type + 3;
    volatile unsigned long long *cxl_data_handshake __maybe_unused     = cxl_func_type + 4;
    volatile unsigned long long *cxl_response_handshake __maybe_unused = cxl_func_type + 5;
    volatile unsigned long long *cxl_read_data_0 = cxl_func_type + 6;
    volatile unsigned long long *cxl_read_data_1 = cxl_func_type + 7;
    volatile unsigned long long *cxl_read_data_2 = cxl_func_type + 8;
    volatile unsigned long long *cxl_read_data_3 = cxl_func_type + 9;
    volatile unsigned long long *cxl_read_data_4 = cxl_func_type + 10;
    volatile unsigned long long *cxl_read_data_5 = cxl_func_type + 11;
    volatile unsigned long long *cxl_read_data_6 = cxl_func_type + 12;
    volatile unsigned long long *cxl_read_data_7 = cxl_func_type + 13;

    pr_info("page_addr: 0x%llx\n", page_address);
    *cxl_test_case  = 4;
    *cxl_page_addr_0 = 0x4080000000ull;
    asm volatile("mfence");

    *cxl_func_type = 1;
    asm volatile("mfence");
    usleep_range(500, 600);

    pr_info("testcase: 0x%llx\n", *cxl_test_case);
    pr_info("read_data_0: 0x%llx\n", *cxl_read_data_0);
    pr_info("read_data_1: 0x%llx\n", *cxl_read_data_1);
    pr_info("read_data_2: 0x%llx\n", *cxl_read_data_2);
    pr_info("read_data_3: 0x%llx\n", *cxl_read_data_3);
    pr_info("read_data_4: 0x%llx\n", *cxl_read_data_4);
    pr_info("read_data_5: 0x%llx\n", *cxl_read_data_5);
    pr_info("read_data_6: 0x%llx\n", *cxl_read_data_6);
    pr_info("read_data_7: 0x%llx\n", *cxl_read_data_7);

    *cxl_func_type = 2;
    return 0;
}

int launch_cxl_io(unsigned long long head_low, unsigned long long head_high, unsigned long long payload)
{
    volatile unsigned long long *cxl_func_type = get_virt_addr();
    volatile unsigned long long *cxl_tx_header_low  = cxl_func_type + 22;
    volatile unsigned long long *cxl_tx_header_high = cxl_func_type + 23;
    volatile unsigned long long *cxl_tx_start       = cxl_func_type + 24;
    volatile unsigned long long *cxl_tx_payload     = cxl_func_type + 25;

    *cxl_tx_header_low  = head_low;
    *cxl_tx_header_high = head_high;
    *cxl_tx_payload     = payload;
    asm volatile("mfence");
    *cxl_tx_start = 1;
    asm volatile("mfence");
    usleep_range(500, 600);
    return 0;
}

void access_pcie_bar(void)
{
    void __iomem *bar;
    u32 val;

    bar = ioremap(FPGA_BAR_0_ADDRESS, PCI_BAR_SIZE);
    if (!bar) {
        pr_err("Failed to ioremap PCIe BAR 0x%llx\n", (unsigned long long)FPGA_BAR_0_ADDRESS);
        return;
    }

    writel(0x12345678, bar + 0x8);
    pr_info("Wrote 0x12345678 to BAR[0x8]\n");

    val = readl(bar + 0x8);
    pr_info("Read from BAR[0x8]: 0x%x\n", val);
    iounmap(bar);
}

void write_pattern_512B_to_pcie_bar(size_t bar_offset)
{
    void __iomem *bar;
    u8 pattern_buf[512];
    int i, j;

    bar = ioremap(FPGA_BAR_0_ADDRESS, PCI_BAR_SIZE);
    if (!bar) {
        pr_err("Failed to ioremap PCIe BAR 0x%llx\n", (unsigned long long)FPGA_BAR_0_ADDRESS);
        return;
    }

    for (i = 511; i >= 0; i--)
        pattern_buf[i] = (u8)(i ^ 0xAA);

    memcpy_toio(bar + bar_offset, pattern_buf, 512);
    pr_info("Wrote 512B pattern to BAR\n");

    for (i = 496; i >= 0; i -= 16) {
        char line[128] = {0};
        int offset = 0;
        offset += snprintf(line + offset, sizeof(line) - offset, "Offset 0x%03X:", i);
        for (j = 0; j < 16; j++)
            offset += snprintf(line + offset, sizeof(line) - offset, " %02X", pattern_buf[i + j]);
        pr_info("%s\n", line);
    }
    iounmap(bar);
}

void verify_pattern_512B_from_pcie_bar(size_t bar_offset)
{
    void __iomem *bar;
    u8 expected __maybe_unused, read_val __maybe_unused;
    int i, j, error_count = 0;
    u8 read_buf[512];

    bar = ioremap(FPGA_BAR_0_ADDRESS, PCI_BAR_SIZE);
    if (!bar) {
        pr_err("Failed to ioremap PCIe BAR 0x%llx\n", (unsigned long long)FPGA_BAR_0_ADDRESS);
        return;
    }

    memcpy_fromio(read_buf, bar + bar_offset, 512);
    pr_info("Verifying and printing 512B from BAR\n");

    for (i = 496; i >= 0; i -= 16) {
        char line[128] = {0};
        int offset = 0;
        offset += snprintf(line + offset, sizeof(line) - offset, "Offset 0x%03X:", i);
        for (j = 15; j >= 0; j--)
            offset += snprintf(line + offset, sizeof(line) - offset, " %02X", read_buf[i + j]);
        pr_info("%s\n", line);
    }

    if (error_count == 0)
        pr_info("All 512B verified correctly from BAR\n");
    else
        pr_err("Verification failed: %d mismatches\n", error_count);

    iounmap(bar);
}

void read_512B_from_phys_buffer(phys_addr_t buffer_phys_addr)
{
    void __iomem *virt_addr;
    u8 data[512];
    int i;

    if (!PAGE_ALIGNED(buffer_phys_addr)) {
        pr_err("Phys addr 0x%llx not page-aligned\n", (unsigned long long)buffer_phys_addr);
        return;
    }

    virt_addr = memremap(buffer_phys_addr, PAGE_SIZE, MEMREMAP_WB);
    if (!virt_addr) {
        pr_err("Failed to map phys addr 0x%llx\n", (unsigned long long)buffer_phys_addr);
        return;
        }

    for (i = 0; i < 512; i++)
        data[i] = readb(virt_addr + i);

    pr_info("First 16 bytes from phys 0x%llx:\n", (unsigned long long)buffer_phys_addr);
    for (i = 0; i < 16; i++)
        pr_cont("%02x ", data[i]);
    pr_cont("\n");

    memunmap(virt_addr);
}

void write_512B_to_phys_buffer(phys_addr_t buffer_phys_addr)
{
    void __iomem *virt_addr;
    int i;
    u8 data[512];

    if (!PAGE_ALIGNED(buffer_phys_addr)) {
        pr_err("Phys addr 0x%llx not page-aligned\n", (unsigned long long)buffer_phys_addr);
        return;
    }

    for (i = 0; i < sizeof(data); i++)
        data[i] = (u8)(i ^ 0xAA);

    virt_addr = memremap(buffer_phys_addr, PAGE_SIZE, MEMREMAP_WB);
    if (!virt_addr) {
        pr_err("Failed to map phys addr 0x%llx\n", (unsigned long long)buffer_phys_addr);
        return;
    }

    for (i = 0; i < 512; i++)
        writeb(data[i], virt_addr + i);

    pr_info("Wrote 512B pattern to phys 0x%llx\n", (unsigned long long)buffer_phys_addr);
    memunmap(virt_addr);
}

void test_fio(unsigned long long cq_addresses, unsigned long long sq_addresses,
              unsigned long long buffer_addresses, unsigned long long tail_head)
{
    unsigned long long *cxl_func_type = get_virt_addr();
    volatile unsigned long long *cxl_sq_addr     = cxl_func_type + 26;
    volatile unsigned long long *cxl_cq_addr     = cxl_func_type + 27;
    volatile unsigned long long *cxl_bar_addr __maybe_unused    = cxl_func_type + 28;
    volatile unsigned long long *cxl_sq_tail     = cxl_func_type + 29;
    volatile unsigned long long *cxl_cq_head     = cxl_func_type + 30;
    volatile unsigned long long *cxl_csr_init __maybe_unused   = cxl_func_type + 32;
    volatile unsigned long long *cxl_host_buffer = cxl_func_type + 33;
    volatile unsigned long long *cxl_queue_index = cxl_func_type + 34;

    *cxl_queue_index = 9;
    *cxl_cq_addr     = cq_addresses;
    *cxl_cq_head     = tail_head;
    *cxl_sq_tail     = 128 * tail_head;
    *cxl_host_buffer = buffer_addresses;
    *cxl_sq_addr     = *cxl_sq_addr + 1;

    pr_info("qidx=0x%llx sq=0x%llx cq=0x%llx tail=0x%llx head=0x%llx buf=0x%llx\n",
            *cxl_queue_index, *cxl_sq_addr, *cxl_cq_addr, *cxl_sq_tail, *cxl_cq_head, *cxl_host_buffer);
}

void set_loopback(unsigned long long addr, unsigned long long test_case, unsigned long long delay_cnt)
{
    unsigned long long *cxl_func_type = get_virt_addr();
    volatile unsigned long long *cxl_m5_interval = cxl_func_type + 35;
    volatile unsigned long long *cxl_m5_query_en __maybe_unused = cxl_func_type + 36;

    *cxl_m5_interval = 2000;
}

/* L2 (single shot); streaming lives in l2_stream.c */
int launch_l2_dist_cal(phys_addr_t base_addr, phys_addr_t query_addr)
{
    volatile unsigned long long *cxl_func_type = get_virt_addr();
    volatile unsigned long long *cxl_page_addr_0   = cxl_func_type + 1;
    volatile unsigned long long *cxl_page_addr_1   = cxl_func_type + 2;
    volatile unsigned long long *cxl_l2_dist_start = cxl_func_type + 14;

    pr_info("L2 dist: base=0x%llx query=0x%llx\n",
            (unsigned long long)base_addr, (unsigned long long)query_addr);

    *cxl_page_addr_0 = base_addr;
    *cxl_page_addr_1 = query_addr;
    asm volatile("mfence");

    *cxl_l2_dist_start = 1;
    asm volatile("mfence");
    return 0;
}

int run_l2_and_dump(phys_addr_t base_addr, phys_addr_t query_addr, u64 num_vecs, u32 dim, u32 clk_mhz)
{
    volatile unsigned long long *csr = get_virt_addr();
    char outbuf[256];
    long wret;
    u64 cycles;
    const char *out_path = "/home/lifan3/cxl_dist_cal/data/l2_result.txt";

    if (!csr) {
        pr_err("CSR ioremap failed\n");
        return -ENODEV;
    }

    volatile unsigned long long *REG_PAGE_ADDR0  = csr + (0x0008 >> 3);
    volatile unsigned long long *REG_PAGE_ADDR1  = csr + (0x0010 >> 3);
    volatile unsigned long long *REG_DELAY       = csr + (0x0018 >> 3);
    volatile unsigned long long *REG_TEST_CASE   = csr + (0x0020 >> 3);
    volatile unsigned long long *REG_RESP        = csr + (0x0028 >> 3);
    volatile unsigned long long *REG_NUM_REQ     = csr + (0x0060 >> 3);
    volatile unsigned long long *REG_ADDR_RANGE  = csr + (0x0068 >> 3);
    volatile unsigned long long *REG_L2_START    = csr + (0x0070 >> 3);

    *REG_PAGE_ADDR0 = base_addr;
    *REG_PAGE_ADDR1 = query_addr;
    *REG_NUM_REQ    = num_vecs;
    *REG_ADDR_RANGE = dim;
    *REG_TEST_CASE  = 100ull;
    asm volatile("mfence");

    *REG_L2_START   = 1ull;
    asm volatile("mfence");

    {
        int tries = 0, max_tries = 1000000;
        while (tries++ < max_tries) {
            if ((*REG_RESP) & 0x1ull) break;
            usleep_range(500, 600);
        }
        if (tries >= max_tries) {
            pr_err("L2 calc timeout\n");
            return -ETIMEDOUT;
        }
    }

    cycles = *REG_DELAY;

    {
        u64 total_bytes = num_vecs * 512ull;
        u64 time_ns = 0;
        u64 gbps_x1000 = 0;
        if (clk_mhz > 0 && cycles > 0) {
            time_ns = (cycles * 1000ull) / (u64)clk_mhz;
            gbps_x1000 = (total_bytes * (u64)clk_mhz) / cycles;
        }
        snprintf(outbuf, sizeof(outbuf),
                 "L2 result:\ncycles=%llu\nclk_mhz=%u\nnum_vecs=%llu\ndim=%u\nbytes=%llu\n~time_ns=%llu\n~GBps(decimal)=%llu.%03llu\n",
                 (unsigned long long)cycles,
                 clk_mhz,
                 (unsigned long long)num_vecs,
                 dim,
                 (unsigned long long)total_bytes,
                 (unsigned long long)time_ns,
                 (unsigned long long)(gbps_x1000 / 1000ull),
                 (unsigned long long)(gbps_x1000 % 1000ull));
    }

    wret = write_text_to_file(out_path, outbuf, strlen(outbuf));
    if (wret < 0)
        pr_err("Failed to write %s (ret=%ld)\n", out_path, wret);
    else
        pr_info("Wrote L2 results to %s\n", out_path);
    return 0;
}
