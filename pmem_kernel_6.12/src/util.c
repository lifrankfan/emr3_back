
#include "util.h"





void *alloc_and_get_phys(struct page **out_page, phys_addr_t *out_phys)
{
    struct page *page = alloc_pages_node(1, GFP_KERNEL, 0);
    if (!page) {
        printk(KERN_ERR "Failed to allocate page\n");
        return NULL;
    }

    void *addr = page_address(page);
    if (!addr) {
        printk(KERN_ERR "Failed to get page address\n");
        __free_pages(page, 0);
        return NULL;
    }

    *out_page = page;
    *out_phys = virt_to_phys(addr);
    return addr;
}