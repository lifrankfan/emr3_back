#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/mm.h>
#include <asm/cacheflush.h>
#include <linux/moduleparam.h>
#include <linux/virtio.h>
#include "cxl_func.h"
#include "l2_stream.h"
#include "nvme.h"

static char *base_path  = "/home/lifan3/emr3_back/pmem_kernel_6.12/data/base.bin";
module_param(base_path, charp, 0644);
MODULE_PARM_DESC(base_path, "Path to base vectors (raw float32, row-major)");

static char *query_path = "/home/lifan3/emr3_back/pmem_kernel_6.12/data/query.bin";
module_param(query_path, charp, 0644);
MODULE_PARM_DESC(query_path, "Path to single query vector (512B)");

static unsigned long long total_vecs_param = 1000000ull;
module_param(total_vecs_param, ullong, 0644);
MODULE_PARM_DESC(total_vecs_param, "Total base vectors");

static unsigned long long batch_vecs_param = 16384ull; // 8 MiB (16384*512B)
module_param(batch_vecs_param, ullong, 0644);
MODULE_PARM_DESC(batch_vecs_param, "Vectors per batch");

static int dim_param = 128;
module_param(dim_param, int, 0644);
MODULE_PARM_DESC(dim_param, "Embedding dimension (SIFT=128)");

// Demo sizes; not used in streaming mode (case 5), kept for other modes
#define BASE_BUFFER_SIZE  (1 * 1024 * 1024)
#define QUERY_BUFFER_SIZE (4 * 1024)

// cxl_set: selects operational path at module init
//   0=cfg,1=hotpage,2=cache_read,3=cache_wr_rd,4=io_pkt,5=l2_streaming
static int cxl_set = 1;
module_param(cxl_set, int, 0644);
MODULE_PARM_DESC(cxl_set, "0=cfg,1=hotpage,2=cache_read,3=cache_wr_rd,4=io_pkt,5=l2_streaming");

// iter: legacy host memory tests (unused in case 5)
static int iter = 1024;
module_param(iter, int, 0644);
MODULE_PARM_DESC(iter, "Iteration count for legacy host memory latency tests");

// delay: legacy hardware delay (unused in case 5)
static int delay = 0;
module_param(delay, int, 0644);
MODULE_PARM_DESC(delay, "Optional hardware delay/count for legacy test cases");

// test_case: legacy micro-op selector (case 5 forces test_case=100 in HW wrapper)
static int test_case = 0;
module_param(test_case, int, 0644);
MODULE_PARM_DESC(test_case, "FPGA micro-op selector (legacy traffic generator)");

// Assumed AXI/CXL clock for cycle->time conversions (does not set HW clock)
static int axi_clk_mhz = 400;
module_param(axi_clk_mhz, int, 0644);
MODULE_PARM_DESC(axi_clk_mhz, "Assumed AXI/CXL clock (MHz) for cycle->time & bandwidth");

// Example pages (used by some legacy paths)
struct page *page_0, *page_1, *page_2, *page_3, *page_4, *page_5, *page_6, *page_7;
phys_addr_t phys_addr_0, phys_addr_1, phys_addr_2, phys_addr_3;
phys_addr_t phys_addr_4, phys_addr_5, phys_addr_6, phys_addr_7;

// Legacy buffers (not used by case 5, safe to keep)
static struct page *base_pages;
static struct page *query_page;

// Silence unused warning in current build
static void __iomem *mapped_base_nvme __maybe_unused;

struct nvme_log_entry {
    int type;
    int id;
    unsigned long long addr;
};

#define MAX_LOG_ENTRIES 16

static int __init my_module_init(void)
{
    // Example allocation (used by some legacy cases)
    alloc_and_get_phys(&page_0, &phys_addr_0);

    switch (cxl_set) {
        case 0: // configure device (placeholder)
            break;
        case 1: // read M5 hot page addr (legacy)
            // set_loopback(phys_addr_0, test_case, delay);
            // read_m5();
            break;
        case 2: // single cache read (legacy)
            launch_cxl_cache_read(phys_addr_0);
            break;
        case 3: // cache write+read (legacy)
            launch_cxl_cache_write(phys_addr_0, 0, 0);
            launch_cxl_cache_read(phys_addr_0);
            break;
        case 4: // raw IO transmit
            launch_cxl_io(0x4301000F60000001ull, 0xFAB1100C00000000ull, 0ull);
            break;
        case 5: { // L2 Distance Streaming
            int rc = run_l2_streaming_from_file(
                base_path, query_path,
                total_vecs_param, (u32)dim_param,
                batch_vecs_param, (u32)axi_clk_mhz);
            if (rc) pr_err("L2 streaming failed rc=%d\n", rc);
            break;
        }
        default:
            break;
    }

    return 0;
}

static void __exit my_module_exit(void)
{
    if (base_pages) {
        __free_pages(base_pages, get_order(BASE_BUFFER_SIZE));
        pr_info("Freed base vector pages\n");
    }
    if (query_page) {
        __free_page(query_page);
        pr_info("Freed query vector page\n");
    }
    pr_info("Kernel module unloaded.\n");
}

module_init(my_module_init);
module_exit(my_module_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Your Name");
MODULE_DESCRIPTION("NVMe/CXL test + L2 streaming benchmark");
MODULE_VERSION("1.0");
