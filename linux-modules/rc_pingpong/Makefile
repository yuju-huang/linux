obj-m := rc-pingpong.o

rc-pingpong-y := rc_pingpong.o

cflags += -g
EXTRA_CFLAGS += -g

all:
	make -C /lib/modules/$(shell uname -r)/build M=$(shell pwd) modules

clean:
	make -C /lib/modules/$(shell uname -r)/build M=$(shell pwd) clean
