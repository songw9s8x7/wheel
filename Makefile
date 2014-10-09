KVERS = $(shell uname -r)

obj-m += wheel.o

build: kernel_modules
kernel_modules:
	make -C /home/frank/workdir/new8974/8974/out/wd8/target/product/msm8974/obj/KERNEL_OBJ M=$(CURDIR) ARCH=arm CROSS_COMPILE=arm-eabi- modules
clean:
	make -C /lib/modules/$(KVERS)/build M=$(CURDIR) clean
