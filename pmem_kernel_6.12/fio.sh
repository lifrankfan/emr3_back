#!/bin/bash

ITER=${1:-64}  # Default to 128 if no parameter is passed

for ((i=1; i<=1; i++))
do
    echo "===== Iteration $i ====="
    
    sudo insmod nvme_test.ko cxl_set=11 iter=$ITER test_case=0
    sudo rmmod nvme_test.ko

    sudo insmod nvme_test.ko cxl_set=3 iter=$ITER test_case=1
    sudo rmmod nvme_test.ko

    sudo insmod nvme_test.ko cxl_set=10 iter=$ITER test_case=0
    sudo rmmod nvme_test.ko

    sudo insmod nvme_test.ko cxl_set=2 iter=$ITER test_case=1
    sudo rmmod nvme_test.ko

    sudo dmesg | tail -n 60
    
    # sudo insmod nvme_test.ko cxl_set=6 iter=0 && sudo rmmod nvme_test.ko && sudo dmesg 
done
