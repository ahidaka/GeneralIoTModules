# module definition
obj-m += send_module.o
obj-m += receive_module.o
APPS = receive_app

KDIR := /lib/modules/$(shell uname -r)/build
PWD := $(shell pwd)

# default target
all: modules $(APPS)

# modules compilation target
modules:
	$(MAKE) -C $(KDIR) M=$(PWD) modules

# individual module targets (for manual build)
send_module.ko:
	$(MAKE) -C $(KDIR) M=$(PWD) modules

receive_module.ko:
	$(MAKE) -C $(KDIR) M=$(PWD) modules

# build target for receive_app
receive_app: receive_app.c
	gcc -o receive_app receive_app.c

# clean target
clean:
	$(MAKE) -C $(KDIR) M=$(PWD) clean
	rm -f receive_app
