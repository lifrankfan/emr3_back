# Output module
obj-m := nvme_test_v3.o

# Objects that form the module
nvme_test_v3-objs := \
  src/main.o \
  src/cxl_func.o \
  src/l2_stream.o

# Add our local include/ to the compiler include path
ccflags-y += -I$(src)/include

# Kernel tree
KDIR := /lib/modules/$(shell uname -r)/build

.PHONY: all clean
all:
	$(MAKE) -C $(KDIR) M=$(PWD) modules

clean:
	$(MAKE) -C $(KDIR) M=$(PWD) clean
	@rm -rf out/*
