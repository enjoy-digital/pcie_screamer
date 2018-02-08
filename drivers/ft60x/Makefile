# Makefile for kernel module

KERNEL_VERSION:=$(shell uname -r)
KERNEL_PATH:=/lib/modules/$(KERNEL_VERSION)/build

obj-m = ft60x.o
sdr-objs = ft60x.o

all: ft60x.ko

ft60x.ko: ft60x.c
	make -C $(KERNEL_PATH) M=$(PWD) modules

clean:
	make -C $(KERNEL_PATH) M=$(PWD) clean
	rm -f *~
