obj-m += xen-blkback-ljx.o
xen-blkback-ljx-objs := xenbus.o blkback-ljx.o cache.o
CFLAGS_blkback-ljx.o := -DDEBUG
CFLAGS_cache.o := -DDEBUG
CFLAGS_xenbus.o := -DDEBUG

all:
	make -C /lib/modules/3.3.6-xen-ljx-g4d4e3e5/build M=$(PWD) modules

clean:
	make -C /lib/modules/3.3.6-xen-ljx-g4d4e3e5/build M=$(PWD) clean

tags: *.c *.h
	ctags -R .
	cscope -Rb
