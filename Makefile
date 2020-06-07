obj-m += ouichefs.o
obj-m += ouichefs_sys.o
obj-m += policy.o

ouichefs-objs := fs.o super.o inode.o file.o dir.o cleaning.o
ouichefs_sys-objs := fs_sys.o super.o inode.o file.o dir.o cleaning.o

KERNELDIR ?= ../../../../KERNEL_SRC/linux-4.19.3
#KERNELDIR ?= ~/linux-4.19.3


all:
	make -C $(KERNELDIR) M=$(PWD) modules

debug:
	make -C $(KERNELDIR) M=$(PWD) ccflags-y+="-DDEBUG -g" modules

clean:
	make -C $(KERNELDIR) M=$(PWD) clean
	rm -rf *~

.PHONY: all clean
