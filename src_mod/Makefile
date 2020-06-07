obj-m += policy.o

KERNELDIR ?= ../../../../../KERNEL_SRC/linux-4.19.3
#KERNELDIR ?= ~/linux-4.19.3

all:
	make -C $(KERNELDIR) M=$(PWD) modules

debug:
	make -C $(KERNELDIR) M=$(PWD) ccflags-y+="-DDEBUG -g" modules

clean:
	make -C $(KERNELDIR) M=$(PWD) clean
	rm -rf *~

.PHONY: all clean
