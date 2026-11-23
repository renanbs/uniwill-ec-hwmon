obj-m := uniwill_ec_hwmon.o

KDIR ?= /lib/modules/$(shell uname -r)/build

all:
	$(MAKE) -C $(KDIR) M=$(CURDIR) modules

clean:
	$(MAKE) -C $(KDIR) M=$(CURDIR) clean

install: all
	install -Dm644 uniwill_ec_hwmon.ko \
		/usr/lib/modules/$(shell uname -r)/updates/uniwill_ec_hwmon.ko
	depmod -a

uninstall:
	rm -f /usr/lib/modules/$(shell uname -r)/updates/uniwill_ec_hwmon.ko
	depmod -a

.PHONY: all clean install uninstall
