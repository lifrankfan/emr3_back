#!/bin/bash
source /fast-lab-share/yangz15/gnr2_back/env/pci_var.sh

echo "0000:${SSD_PCI_ID}" | sudo tee /sys/bus/pci/drivers/nvme/unbind
sudo setpci -s ${FPGA_PCI_ID} COMMAND=0x02
sudo insmod /fast-lab-share/lifan3/emr3_back/linux-6.12.40/drivers/nvme/host/my_nvme/my_nvme.ko
sudo rmmod nd_pmem
sudo insmod /research/lifan3/host_mig/CXL-SSD/pmem_kernel_6.12/nvme_test.ko cxl_set=0
sudo rmmod /research/lifan3/host_mig/CXL-SSD/pmem_kernel_6.12/nvme_test.ko
sudo insmod /fast-lab-share/lifan3/emr3_back/linux-6.5.10/drivers/nvdimm/nd_pmem.ko