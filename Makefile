obj-m += gpio-driver.o

kernel_dir = /lib/modules/$(shell uname -r)/build

all:
	make -C $(kernel_dir) M=$(shell pwd) modules

clean:
	make -C $(kernel_dir) M=$(shell pwd) clean