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

static int cxl_nid = 1;
module_param(cxl_nid, int, 0644);
MODULE_PARM_DESC(cxl_nid, "NUMA node ID for CXL memory (default: 1)");

static unsigned long long cxl_base = 0x8080000000ull;
module_param(cxl_base, ullong, 0644);
MODULE_PARM_DESC(cxl_base, "Base physical address of CXL memory window (for DPA calculation)");

// Example pages (used by some legacy paths)
struct page *page_0, *page_1, *page_2, *page_3, *page_4, *page_5, *page_6, *page_7;
phys_addr_t phys_addr_0, phys_addr_1, phys_addr_2, phys_addr_3;
phys_addr_t phys_addr_4, phys_addr_5, phys_addr_6, phys_addr_7;

// Legacy buffers (not used by case 5, safe to keep)
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
