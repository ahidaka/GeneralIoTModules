# module definition
obj-m += send_udp.o
obj-m += receive_udp.o
APPS = receive_udp_app

#KDIR := /lib/modules/$(shell uname -r)/build
PWD := $(shell pwd)

# default target
all: modules $(APPS)

# modules compilation target
modules:
	$(MAKE) -C $(KDIR) M=$(PWD) modules

# individual module targets (for manual build)
send_udp.ko:
	$(MAKE) -C $(KDIR) M=$(PWD) modules

receive_udp.ko:
	$(MAKE) -C $(KDIR) M=$(PWD) modules

# build target for receive_app
receive_udp_app: receive_udp_app.c
	$(CC) -g -o receive_udp_app receive_udp_app.c

# clean target
clean:
	$(MAKE) -C $(KDIR) M=$(PWD) clean
	rm -f receive_udp_app
