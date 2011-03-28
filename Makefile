PATH += :/home/jomat/OpenWrt/backfire/staging_dir/toolchain-armeb_v5te_gcc-4.3.3+cs_uClibc-0.9.30.1_eabi/usr/bin

MAKE = make                                                                                                                                  
CFLAGS = -g -Wall -lpthread
CC = armeb-openwrt-linux-gcc 
STRIP = armeb-openwrt-linux-strip

all: schwesterbot strip

schwesterbot: schwesterbot.c
	$(CC) $(CFLAGS) schwesterbot.c -o schwesterbot

strip: schwesterbot
	$(STRIP) -g schwesterbot
 
clean:
	rm -f *.o schwesterbot

