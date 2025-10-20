#ifndef FUNCTION_H
#define FUNCTION_H

#include <linux/types.h>
#include <linux/dax.h>


//pmem function
struct arona_dax_state
{
	// Hot items
	void *va;
	ulong size;
	ulong pfn;
	long pgcnt;

	// Cold items
	char *devname;
	struct block_device *bdev;
	struct dax_device *daxdev;
	u64 offset;
};

//Setup function
void *get_virt_addr(void);
void alloc_and_get_phys(struct page **out_page, phys_addr_t *out_phys);
void access_pcie_bar(void);

void verify_pattern_512B_from_pcie_bar(size_t bar_offset);
void write_pattern_512B_to_pcie_bar(size_t bar_offset);

void read_512B_from_phys_buffer(phys_addr_t buffer_phys_addr);
void write_512B_to_phys_buffer(phys_addr_t buffer_phys_addr);

void test_fio(unsigned long long cq_addresses, unsigned long long sq_addresses, unsigned long long buffer_addresses, unsigned long long tail_head);

//PMEM function
struct arona_dax_state *arona_dax_start(const char *dev, ulong align_order);
void arona_dax_stop(struct arona_dax_state *state);
void test_multiple_write(uint64_t *ptr, int iter, int test_case);
void test_multiple_read(uint64_t *ptr, int iter, int test_case);


//NVMe functions
void check_db (void __iomem *mapped_base_nvme, unsigned long long *sq_tail);

//CXL functions 
void set_cxl(unsigned long long *cq_addresses, unsigned long long *sq_addresses, unsigned long long *buffer_addresses, unsigned long long *sq_tail, unsigned long long block_offset);
void set_delay(unsigned long long delay_cnt);
void read_m5(void);

int launch_cxl_io (unsigned long long head_low, unsigned long long head_high, unsigned long long payload);
int launch_cxl_cache_read (unsigned long long page_address);
int launch_cxl_cache_write (unsigned long long page_address, unsigned long long buffer_address, unsigned long long iter);

//loopback test
void set_loopback(unsigned long long addr, unsigned long long test_case, unsigned long long delay_cnt);


#endif // FUNCTION_H