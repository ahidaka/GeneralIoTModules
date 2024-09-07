#
# module definition
obj-m += send_module.o
obj-m += receive_module.o

KDIR := /lib/modules/$(shell uname -r)/build
PWD := $(shell pwd)

# default target
all:
	$(MAKE) -C $(KDIR) M=$(PWD) modules

# individual module targets
send_module.ko:
	$(MAKE) -C $(KDIR) M=$(PWD) send_module.o

receive_module.ko:
	$(MAKE) -C $(KDIR) M=$(PWD) receive_module.o

# build target of receive_app
receive_app: receive_app.c
	gcc -o receive_app receive_app.c

# clean target
clean:
	$(MAKE) -C $(KDIR) M=$(PWD) clean
	rm -f receive_app
