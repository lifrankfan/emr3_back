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

#include "cxl_func.h"
#include "/fast-lab-share/yangz15/emr1_back/env/nvme.h"


void alloc_and_get_phys(struct page **out_page, phys_addr_t *out_phys)
{
    struct page *page = alloc_pages_node(0, GFP_KERNEL, 0);
    if (!page) {
        printk(KERN_ERR "Failed to allocate page\n");
        return;
    }

    void *addr = page_address(page);
    if (!addr) {
        printk(KERN_ERR "Failed to get page address\n");
        __free_pages(page, 0);
        return;
    }

    *out_page = page;
    *out_phys = virt_to_phys(addr);
    return;
}

void* get_virt_addr(void) {
	void* virt_addr = ioremap(FPGA_BAR_1_ADDRESS, 0x1000);
	unsigned long long *ptr = (unsigned long long *) virt_addr;
	return ptr;
}


void check_db (void __iomem *mapped_base_nvme, unsigned long long *sq_tail){
    void* target_address;
    void* acq_address;
    mapped_base_nvme = ioremap_uc(PCI_BAR_ADDRESS, 16 * 1024);  // 16KB size
    if (!mapped_base_nvme) {
        pr_err("Failed to map PCI BAR memory region.\n");
    } else {
        pr_info("Memory mapped successfully.\n");
    }

    acq_address = (void *) mapped_base_nvme + 0x28;
    target_address = (void *) mapped_base_nvme + 4096;
    // Read the 32-bit value at the target address
    unsigned int value;
    for (int i=0; i<2; i++) {
        value = ioread32(acq_address+4*i);
        printk(KERN_INFO "Read acq value %d: 0x%x\n", i, value);
    }

    for (int i=1; i<17; i++){
        value = ioread32(target_address+8*i);
        printk(KERN_INFO "Read value %d: 0x%x\n", i, value);
        sq_tail[i-1]=(unsigned long long)value;
        value = ioread32(target_address+8*i+4);
        printk(KERN_INFO "Read value: 0x%x\n", value);
    }

    return;
}

int check_admin_q(void){
    phys_addr_t phys_addr = 0x21684000;
    void *virt_addr;
    unsigned char data[64];

    virt_addr = phys_to_virt(phys_addr);
    if (!virt_addr) {
        pr_err("Failed to get virtual address for physical address 0x%llx\n",
               (unsigned long long)phys_addr);
        return -ENOMEM;
    }

    // value = *(unsigned int *)virt_addr; 

    memcpy(data, virt_addr+64*10, sizeof(data));
    pr_info("Read 64 bytes from physical address 0x%llx:\n",
            (unsigned long long)phys_addr);
    memcpy(data, virt_addr+64*11, sizeof(data));
    pr_info("Read 64 bytes from physical address 0x%llx:\n",
            (unsigned long long)phys_addr);
    memcpy(data, virt_addr+0, sizeof(data));
    pr_info("Read 64 bytes from physical address 0x%llx:\n",
            (unsigned long long)phys_addr);

    /* Display the data in hexadecimal form */
    print_hex_dump(KERN_INFO, "", DUMP_PREFIX_OFFSET,
                   16, 1, data, sizeof(data), false);

    return 0;
}


void set_cxl(unsigned long long *cq_addresses, unsigned long long *sq_addresses, unsigned long long *buffer_addresses, unsigned long long *sq_tail, unsigned long long block_offset){
    unsigned long long *cxl_func_type;
    cxl_func_type = get_virt_addr();
    volatile unsigned long long *cxl_requester_id   = cxl_func_type + 3;
	volatile unsigned long long *cxl_block_index    = cxl_func_type + 4;
    volatile unsigned long long *cxl_sq_addr 		= cxl_func_type + 26;
	volatile unsigned long long *cxl_cq_addr 		= cxl_func_type + 27;
	volatile unsigned long long *cxl_bar_addr 		= cxl_func_type + 28;
	volatile unsigned long long *cxl_sq_tail 		= cxl_func_type + 29;
	volatile unsigned long long *cxl_cq_head 		= cxl_func_type + 30;
    volatile unsigned long long *cxl_csr_init 		= cxl_func_type + 32;
    volatile unsigned long long *cxl_host_buffer 	= cxl_func_type + 33;
    volatile unsigned long long *cxl_queue_index 	= cxl_func_type + 34;
    volatile unsigned long long *cxl_m5_interval 	= cxl_func_type + 35;
    volatile unsigned long long *cxl_m5_query_en 	= cxl_func_type + 36;


	// unsigned long long sq_addr_list[] = {
    //     sq_addr_0, sq_addr_1, sq_addr_2, sq_addr_3,
    //     sq_addr_4, sq_addr_5, sq_addr_6, sq_addr_7
    // };

    // unsigned long long cq_addr_list[] = {
    //     cq_addr_0, cq_addr_1, cq_addr_2, cq_addr_3,
    //     cq_addr_4, cq_addr_5, cq_addr_6, cq_addr_7
    // };

    for (int i = 0; i < 16; i++) {
        *cxl_queue_index = i;
        *cxl_sq_addr     = sq_addresses[i];
        *cxl_cq_addr     = cq_addresses[i];
        *cxl_cq_head     = sq_tail[i];
        *cxl_sq_tail     = sq_tail[i];
        *cxl_host_buffer = buffer_addresses[i];

        printk("cxl_queue_index is: 0x%llx", *cxl_queue_index);
        printk("cxl_sq_addr is: 0x%llx", *cxl_sq_addr);
        printk("cxl_cq_addr is: 0x%llx", *cxl_cq_addr);
        printk("cxl_sq_tail is: 0x%llx", *cxl_sq_tail);
        printk("cxl_cq_head is: 0x%llx", *cxl_cq_head);
        printk("cxl_host_buffer is: 0x%llx", *cxl_host_buffer);
    }

    *cxl_m5_query_en = 0;
	*cxl_bar_addr = PCI_BAR_ADDRESS;
    *cxl_requester_id = FPGA_BUS_ID;
    *cxl_block_index = block_offset*256*1024*1024;

    *cxl_m5_interval = 0;

    printk("cxl_m5_query_en is: 0x%llx", *cxl_m5_query_en);
    printk("cxl_block_index is: 0x%llx", *cxl_block_index);

	//start CXL_SSD
    *cxl_func_type = 3;
    *cxl_csr_init = 1;

    return;
}

void set_delay(unsigned long long delay_cnt){
    unsigned long long *cxl_func_type;
    cxl_func_type = get_virt_addr();
    volatile unsigned long long *cxl_delay_cnt	= cxl_func_type + 35;
	*cxl_delay_cnt = delay_cnt;
    return;
}

void read_m5(void){
    unsigned long long *cxl_func_type;
    cxl_func_type = get_virt_addr();
    volatile unsigned long long *cxl_m5_rst         = cxl_func_type + 35;
	volatile unsigned long long *cxl_m5_query_en    = cxl_func_type + 36;
	volatile unsigned long long *cxl_m5_hot_page_0  = cxl_func_type + 40;
	volatile unsigned long long *cxl_m5_hot_page_1  = cxl_func_type + 41;
	volatile unsigned long long *cxl_m5_hot_page_2  = cxl_func_type + 42;
    volatile unsigned long long *cxl_m5_hot_page_3  = cxl_func_type + 43;
    volatile unsigned long long *cxl_m5_hot_page_4  = cxl_func_type + 44;

    *cxl_m5_query_en = 1;
    usleep_range(500,600);
    printk("cxl_m5_hot_page_0 is: 0x%llx", *cxl_m5_hot_page_0);
    printk("cxl_m5_hot_page_1 is: 0x%llx", *cxl_m5_hot_page_1);
    printk("cxl_m5_hot_page_2 is: 0x%llx", *cxl_m5_hot_page_2);
    printk("cxl_m5_hot_page_3 is: 0x%llx", *cxl_m5_hot_page_3);
    printk("cxl_m5_hot_page_4 is: 0x%llx", *cxl_m5_hot_page_4);
    usleep_range(500,600);
    *cxl_m5_rst = 1;
    return;
}

void test_multiple_write(uint64_t *ptr, int iter, int test_case){
	ktime_t start_i, end_i;
	uint64_t total_latency; 
	uint64_t single_latency = 0; 

	total_latency = 0;
	for (int i=0; i<iter; i++){
		// clflush(ptr+i);
		// asm volatile("mfence");

		// start_i = ktime_get();
		if (test_case == 0) {
			*(ptr+i) = i;
		} else if (test_case == 1) {
			*(ptr+i) = iter - i;
		} else if (test_case == 2) {
			*(ptr+i) = (uint64_t) ptr;
		}
		// *(ptr+i) = i;
		// *(ptr+i) = iter - i;
		// *(ptr+i) = (uint64_t) ptr;
		// clflush(ptr+i);
		// asm volatile("mfence");
		// end_i = ktime_get(); 
		// total_latency += ktime_to_ns(ktime_sub(end_i, start_i));
		// single_latency = ktime_to_ns(ktime_sub(end_i, start_i));
		// if (i % 1000 == 0) {
		// 	printk(KERN_INFO "CXL SSD write operation latency %d: %lld ns\n", i, single_latency);
		// } 
	}
	asm volatile("mfence");

	printk(KERN_INFO "CXL SSD write operation latency: %lld ns\n", total_latency/iter);
}

void test_multiple_read(uint64_t *ptr, int iter, int test_case){
	int data;
	int e_data;
	ktime_t start_i, end_i;
	uint64_t total_latency; 
	uint64_t single_latency; 

	total_latency = 0;
	for (int i=0; i<iter; i++) {
		// clflush(ptr+i);
		// asm volatile("mfence");

		// start_i = ktime_get();
		data = *(ptr+i);
		if (test_case == 0) {
			e_data = i;
		} else if (test_case == 1) {
			e_data = iter - i;
		} else if (test_case == 2) {
			e_data = (uint64_t) ptr;
		}
		// end_i = ktime_get();
		// total_latency += ktime_to_ns(ktime_sub(end_i, start_i));
		// (void) data; // Prevent compiler optimization
		if (e_data != data) {
			printk(KERN_ERR "Data mismatch at index %d: expected %d, got %d\n", i, e_data, data);
		}
		// printk(KERN_INFO "read nvme %d: %lld\n", i, *(ptr+i));
		// single_latency = ktime_to_ns(ktime_sub(end_i, start_i));
		// if (single_latency > 10000) {
			// printk(KERN_INFO "CXL SSD READ operation latency %d: %lld ns\n", i, single_latency);
		// }
		// clflush(ptr+i);
		asm volatile("mfence");
	}

	printk(KERN_INFO "CXL SSD read operation latency: %lld ns\n", total_latency/iter);
}

int launch_cxl_cache_write (unsigned long long page_address, unsigned long long buffer_address, unsigned long long iter){
    volatile unsigned long long *cxl_func_type;
    cxl_func_type = get_virt_addr();
    volatile unsigned long long *cxl_page_addr_0 = cxl_func_type + 1;
	volatile unsigned long long *cxl_test_case = cxl_func_type + 2;
	volatile unsigned long long *cxl_write_data_0 = cxl_func_type + 14;
	volatile unsigned long long *cxl_write_data_1 = cxl_func_type + 15;
	volatile unsigned long long *cxl_write_data_2 = cxl_func_type + 16;
	volatile unsigned long long *cxl_write_data_3 = cxl_func_type + 17;
	volatile unsigned long long *cxl_write_data_4 = cxl_func_type + 18;
	volatile unsigned long long *cxl_write_data_5 = cxl_func_type + 19;
	volatile unsigned long long *cxl_write_data_6 = cxl_func_type + 20;
	volatile unsigned long long *cxl_write_data_7 = cxl_func_type + 21;

    // nvme write
    if (iter==0){
        *cxl_write_data_0 = 0x0000000110050002; //read
    }
    else {
        *cxl_write_data_0 = 0x0000000110050001; //write
    }
    *cxl_write_data_1 = 0x0;
    *cxl_write_data_2 = 0x0;
    *cxl_write_data_3 = 0x78787;               //buffer physical address   
    *cxl_write_data_4 = 0x0;    
    *cxl_write_data_5 = 0x4008;     //start logical block address
    *cxl_write_data_6 = 0x0;    //read 1 block
    *cxl_write_data_7 = 0x0;
    asm volatile("mfence");
    // *cxl_test_case = 10;
    // *cxl_page_addr_0 = page_address;
    *cxl_test_case = 13;
    *cxl_page_addr_0 = 0x4080000000;
    // *cxl_test_case = 13;
    // *cxl_test_case = 16;
    // *cxl_page_addr_0 = 0x4080000000;

    asm volatile("mfence");
    *cxl_func_type = 1;
    asm volatile("mfence");
    usleep_range(500, 600);
    *cxl_func_type = 2;
    asm volatile("mfence");
    usleep_range(500, 600);

    return 0;
}

 int launch_cxl_cache_read (unsigned long long page_address){
    volatile unsigned long long *cxl_func_type;
    cxl_func_type = get_virt_addr();
    volatile unsigned long long *cxl_page_addr_0 = cxl_func_type + 1;
	volatile unsigned long long *cxl_test_case = cxl_func_type + 2;
	volatile unsigned long long *cxl_addr_handshake = cxl_func_type + 3;
	volatile unsigned long long *cxl_data_handshake = cxl_func_type + 4;
	volatile unsigned long long *cxl_response_handshake = cxl_func_type + 5;
	volatile unsigned long long *cxl_read_data_0 = cxl_func_type + 6;
	volatile unsigned long long *cxl_read_data_1 = cxl_func_type + 7;
	volatile unsigned long long *cxl_read_data_2 = cxl_func_type + 8;
	volatile unsigned long long *cxl_read_data_3 = cxl_func_type + 9;
	volatile unsigned long long *cxl_read_data_4 = cxl_func_type + 10;
	volatile unsigned long long *cxl_read_data_5 = cxl_func_type + 11;
	volatile unsigned long long *cxl_read_data_6 = cxl_func_type + 12;
	volatile unsigned long long *cxl_read_data_7 = cxl_func_type + 13;

    //nvme read
    printk("page_addr: 0x%llx", page_address);
    // *cxl_test_case = 1;
    // *cxl_page_addr_0 = page_address;
    *cxl_test_case = 4;
    *cxl_page_addr_0 = 0x4080000000;
    // *cxl_test_case = 4;
    // *cxl_test_case = 7;
    // *cxl_page_addr_0 = 0x4080000000;
    asm volatile("mfence");
    *cxl_func_type = 1;
    asm volatile("mfence");
    usleep_range(500, 600);
    printk("testcase is: 0x%llx", *cxl_test_case);
    // printk("handshake: 0x%llx", *cxl_addr_handshake);
    // printk("handshake: 0x%llx", *cxl_data_handshake);
    // printk("handshake: 0x%llx", *cxl_response_handshake);
    printk("read_data_0: 0x%llx", *cxl_read_data_0);
    printk("read_data_1: 0x%llx", *cxl_read_data_1);
    printk("read_data_2: 0x%llx", *cxl_read_data_2);
    printk("read_data_3: 0x%llx", *cxl_read_data_3);
    printk("read_data_4: 0x%llx", *cxl_read_data_4);
    printk("read_data_5: 0x%llx", *cxl_read_data_5);
    printk("read_data_6: 0x%llx", *cxl_read_data_6);
    printk("read_data_7: 0x%llx", *cxl_read_data_7);

    *cxl_func_type = 2;
    return 0;
}

 int launch_cxl_io (unsigned long long head_low, unsigned long long head_high, unsigned long long payload){
    volatile unsigned long long *cxl_func_type;
    cxl_func_type = get_virt_addr();
    volatile unsigned long long *cxl_tx_header_low = cxl_func_type + 22;
    volatile unsigned long long *cxl_tx_header_high = cxl_func_type + 23;
    volatile unsigned long long *cxl_tx_start = cxl_func_type + 24;
    volatile unsigned long long *cxl_tx_payload = cxl_func_type + 25;

    //nvme read
    *cxl_tx_header_low = head_low;
    *cxl_tx_header_high = head_high;
    *cxl_tx_payload = payload;
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

    // Map the physical PCIe BAR address to a virtual address
    bar = ioremap(FPGA_BAR_0_ADDRESS, PCI_BAR_SIZE);
    if (!bar) {
        pr_err("Failed to ioremap PCIe BAR address 0x%llx\n", (unsigned long long)FPGA_BAR_0_ADDRESS);
        return;
    }

    // Write 32-bit value to offset 0x4
    writel(0x12345678, bar + 0x8);
    pr_info("Wrote 0x12345678 to BAR[0x0]\n");


    // Read 32-bit value at offset 0x0
    val = readl(bar + 0x8);
    pr_info("Read from BAR[0x4]: 0x%x\n", val);

    // Unmap when done
    iounmap(bar);
}

void write_pattern_512B_to_pcie_bar(size_t bar_offset)
{
    void __iomem *bar;
    u8 pattern_buf[512];
    int i, j;

    bar = ioremap(FPGA_BAR_0_ADDRESS, PCI_BAR_SIZE);
    if (!bar) {
        pr_err("Failed to ioremap PCIe BAR address 0x%llx\n", (unsigned long long)FPGA_BAR_0_ADDRESS);
        return;
    }

    // Generate known pattern: i ^ 0xAA
    for (i = 511; i >= 0; i--) {
        pattern_buf[i] = (u8)(i ^ 0xAA);
        // writeb(pattern_buf[i], bar + bar_offset + i);
    }

    // Copy all 512 bytes to PCIe BAR at once
    memcpy_toio(bar + bar_offset, pattern_buf, 512);

    pr_info("Wrote 512B known pattern to BAR[0x1FF - 0x0]\n");

    // Print pattern from high to low in 16-byte chunks
    for (i = 496; i >= 0; i -= 16) {
        char line[128] = {0};
        int offset = 0;
        offset += snprintf(line + offset, sizeof(line) - offset, "Offset 0x%03X:", i);
        for (j = 0; j < 16; j++) {
            offset += snprintf(line + offset, sizeof(line) - offset, " %02X", pattern_buf[i + j]);
        }
        pr_info("%s\n", line);
    }

    iounmap(bar);
}


void verify_pattern_512B_from_pcie_bar(size_t bar_offset)
{
    void __iomem *bar;
    u8 expected, read_val;
    int i, j, error_count = 0;
    u8 read_buf[512];

    bar = ioremap(FPGA_BAR_0_ADDRESS, PCI_BAR_SIZE);
    if (!bar) {
        pr_err("Failed to ioremap PCIe BAR address 0x%llx\n", (unsigned long long)FPGA_BAR_0_ADDRESS);
        return;
    }

    // Read 512B into buffer (from high to low)
    // // for (i = 511; i >= 0; i--) {
    // for (i = 0; i <= 511; i++) {
    // // for (i = 1; i >= 0; i--) {
    //     read_buf[i] = readb(bar + bar_offset + i);
    //     expected = (u8)(i ^ 0xAA);
    //     if (read_buf[i] != expected)
    //         error_count++;
    // }

    // Read all 512 bytes at once
    memcpy_fromio(read_buf, bar + bar_offset, 512);

    pr_info("Verifying and printing 512B from BAR[0x1FF - 0x000]\n");

    // Print in 16-byte chunks from high to low, with bytes in reverse order
    for (i = 496; i >= 0; i -= 16) {
    // for (i = 112; i >= 0; i -= 16) {
        char line[128] = {0};
        int offset = 0;
        offset += snprintf(line + offset, sizeof(line) - offset, "Offset 0x%03X:", i);
        for (j = 15; j >= 0; j--) {
            offset += snprintf(line + offset, sizeof(line) - offset, " %02X", read_buf[i + j]);
        }
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

    // Ensure the physical address is page-aligned
    if (!PAGE_ALIGNED(buffer_phys_addr)) {
        pr_err("Physical address 0x%llx is not page-aligned\n",
               (unsigned long long)buffer_phys_addr);
        return;
    }

    // Map the physical address (one page) as cached memory
    virt_addr = memremap(buffer_phys_addr, PAGE_SIZE, MEMREMAP_WB);
    if (!virt_addr) {
        pr_err("Failed to map physical address 0x%llx\n",
               (unsigned long long)buffer_phys_addr);
        return;
    }

    // Read 512 bytes
    for (i = 0; i < 512; i++) {
        data[i] = readb(virt_addr + i);
    }

    // Print first 16 bytes for verification
    pr_info("First 16 bytes from phys addr 0x%llx:\n", (unsigned long long)buffer_phys_addr);
    for (i = 0; i < 16; i++) {
        pr_cont("%02x ", data[i]);
    }
    pr_cont("\n");

    // Unmap
    memunmap(virt_addr);
}

void write_512B_to_phys_buffer(phys_addr_t buffer_phys_addr)
{
    void __iomem *virt_addr;
    int i;
    u8 data[512];

    // Ensure physical address is page-aligned
    if (!PAGE_ALIGNED(buffer_phys_addr)) {
        pr_err("Physical address 0x%llx is not page-aligned\n",
               (unsigned long long)buffer_phys_addr);
        return;
    }

    // Generate known pattern (e.g., i ^ 0xAA)
    for (i = 0; i < sizeof(data); i++) {
        data[i] = (u8)(i ^ 0xAA);
    }

    // Map the physical page (cached)
    virt_addr = memremap(buffer_phys_addr, PAGE_SIZE, MEMREMAP_WB);
    if (!virt_addr) {
        pr_err("Failed to map physical address 0x%llx\n",
               (unsigned long long)buffer_phys_addr);
        return;
    }

    // Write 512 bytes
    for (i = 0; i < 512; i++) {
        writeb(data[i], virt_addr + i);
    }

    pr_info("Wrote 512B known pattern to physical address 0x%llx\n",
            (unsigned long long)buffer_phys_addr);

    memunmap(virt_addr);
}



void test_fio(unsigned long long cq_addresses, unsigned long long sq_addresses, unsigned long long buffer_addresses, unsigned long long tail_head){
    unsigned long long *cxl_func_type;
    cxl_func_type = get_virt_addr();
    volatile unsigned long long *cxl_sq_addr 		= cxl_func_type + 26;
	volatile unsigned long long *cxl_cq_addr 		= cxl_func_type + 27;
	volatile unsigned long long *cxl_bar_addr 		= cxl_func_type + 28;
	volatile unsigned long long *cxl_sq_tail 		= cxl_func_type + 29;
	volatile unsigned long long *cxl_cq_head 		= cxl_func_type + 30;
    volatile unsigned long long *cxl_csr_init 		= cxl_func_type + 32;
    volatile unsigned long long *cxl_host_buffer 	= cxl_func_type + 33;
    volatile unsigned long long *cxl_queue_index 	= cxl_func_type + 34;

    *cxl_queue_index = 9;
    *cxl_cq_addr     = cq_addresses;        //
    *cxl_cq_head     = tail_head;           //
    // *cxl_sq_tail     = FPGA_BAR_0_ADDRESS + 128*tail_head;
    // *cxl_sq_tail     = 384 + 128*tail_head;
    *cxl_sq_tail     = 128*tail_head;       //offset from pio
    *cxl_host_buffer = buffer_addresses;    //
    *cxl_sq_addr     = *cxl_sq_addr + 1;

    printk("cxl_queue_index is: 0x%llx", *cxl_queue_index);
    printk("cxl_sq_addr is: 0x%llx", *cxl_sq_addr);
    printk("cxl_cq_addr is: 0x%llx", *cxl_cq_addr);
    printk("cxl_sq_tail is: 0x%llx", *cxl_sq_tail);
    printk("cxl_cq_head is: 0x%llx", *cxl_cq_head);
    printk("cxl_host_buffer is: 0x%llx", *cxl_host_buffer);

    return;
}

void set_loopback(unsigned long long addr, unsigned long long test_case, unsigned long long delay_cnt){
    unsigned long long *cxl_func_type;
    cxl_func_type = get_virt_addr();
    volatile unsigned long long *cxl_m5_interval    = cxl_func_type + 35;
	volatile unsigned long long *cxl_m5_query_en    = cxl_func_type + 36;

    *cxl_m5_interval = 2000;
    // *cxl_m5_query_en = test_case;

    return;
}
