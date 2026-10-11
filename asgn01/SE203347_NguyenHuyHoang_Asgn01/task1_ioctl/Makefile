KDIR ?= $(HOME)/embedded_lab1/kernel/linux-5.15
ARCH ?= arm
CROSS_COMPILE ?= arm-linux-gnueabihf-
ccflags-y += -march=armv7-a
obj-m += asgn1_driver.o

.PHONY: all clean
all: module asgn1_test

module:
	$(MAKE) -C $(KDIR) M=$(CURDIR) ARCH=$(ARCH) CROSS_COMPILE=$(CROSS_COMPILE) modules

asgn1_test: asgn1_test.c asgn1_ioctl.h
	$(CROSS_COMPILE)gcc -std=c11 -Wall -Wextra -O2 -static -o $@ asgn1_test.c

clean:
	$(MAKE) -C $(KDIR) M=$(CURDIR) ARCH=$(ARCH) CROSS_COMPILE=$(CROSS_COMPILE) clean
	rm -f asgn1_test
