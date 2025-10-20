#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/mm.h>
#include <asm/cacheflush.h>
#include <linux/moduleparam.h>
#include <linux/virtio.h>  // For virt_to_phys
#include "cxl_func.h"
#include "/fast-lab-share/yangz15/emr1_back/env/nvme.h"

static int cxl_set = 1; // Default value
module_param(cxl_set, int, 0644);
MODULE_PARM_DESC(cxl_set, "cxl_set parameter to control behavior (0, 1, or other values)");

static int iter = 1024; // Default value
module_param(iter, int, 0644);
MODULE_PARM_DESC(iter, "cxl_set parameter to control behavior (0, 1, or other values)");

static int delay = 0; // Default value
module_param(delay, int, 0644);
MODULE_PARM_DESC(delay, "cxl_set parameter to control behavior (0, 1, or other values)");

static int test_case = 0; // Default value
module_param(test_case, int, 0644);
MODULE_PARM_DESC(test_case, "cxl_set parameter to control behavior (0, 1, or other values)");

struct page *page_0, *page_1, *page_2, *page_3, *page_4, *page_5, *page_6, *page_7;
phys_addr_t phys_addr_0, phys_addr_1, phys_addr_2, phys_addr_3;
phys_addr_t phys_addr_4, phys_addr_5, phys_addr_6, phys_addr_7;

static struct arona_dax_state* arona_dax_state_var;

static void __iomem *mapped_base_nvme;

// #define LOG_PATH "/proc/nvme_log"
// #define BUF_SIZE 4096

struct nvme_log_entry {
    int type;
    int id;
    unsigned long long addr;
};

// extern struct nvme_log_entry nvme_log[];
// extern int nvme_log_idx;

#define MAX_LOG_ENTRIES 16  // adjust as needed

static int __init my_module_init(void)
{
    // pr_info("ZY: Initializing NVMe SQ reader module...\n");

    int i;

    //allocation
    alloc_and_get_phys(&page_0, &phys_addr_0);
    // alloc_and_get_phys(&page_1, &phys_addr_1);
    // alloc_and_get_phys(&page_2, &phys_addr_2);
    // alloc_and_get_phys(&page_3, &phys_addr_3);
    // alloc_and_get_phys(&page_4, &phys_addr_4);
    // alloc_and_get_phys(&page_5, &phys_addr_5);
    // alloc_and_get_phys(&page_6, &phys_addr_6);
    // alloc_and_get_phys(&page_7, &phys_addr_7);


    switch (cxl_set) {
        case 0: //set cxl device 
            // sq_addrs[0] = 10;
            // set_cxl(cq_addrs, sq_addrs, buffer_addrs, sq_tail, test_case);
            break;
        case 1: //read m5 hot page addr
            // set_loopback(phys_addr_0, test_case, delay);
            // read_m5();
            break;
        case 2:
            launch_cxl_cache_read(phys_addr_0);
            break;
        case 3:
            launch_cxl_cache_write(phys_addr_0, 0, 0);
            launch_cxl_cache_read(phys_addr_0);
            break;
        case 4:
            launch_cxl_io(0x4301000F60000001, 0xFAB1100C00000000, 0); //
            break;
        default: //do nothing
    }
    
    return 0;
}

// Module exit function
static void __exit my_module_exit(void)
{
    printk(KERN_INFO "ZY: Kernel module unloaded.\n");
}

// Register the initialization and exit functions
module_init(my_module_init);
module_exit(my_module_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Your Name");
MODULE_DESCRIPTION("NVMe SQ Reader Kernel Module");
MODULE_VERSION("1.0");
