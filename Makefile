obj-m += xen-blkback-ljx.o
xen-blkback-ljx-objs := xenbus.o ext3.o blkback-ljx.o boot.o

all:
	make -C /lib/modules/3.3.6-xen-ljx-g6304e82/build M=$(PWD) modules

clean:
	make -C /lib/modules/3.3.6-xen-ljx-g6304e82/build M=$(PWD) clean

tags: *.c *.h
	ctags -R .
	cscope -Rb
