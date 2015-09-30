ifeq ($(KERNELDIR),)
	KERNELDIR ?= /lib/modules/$(shell uname -r)/build
endif

PWD := $(shell pwd)
obj-m := simple_blinker.o

default: 
	$(MAKE) -C $(KERNELDIR) M=$(PWD) modules
clean:
	$(MAKE) -C $(KERNELDIR) M=$(PWD) clean
