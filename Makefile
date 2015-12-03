CROSS_COMPILE ?=
KDIR ?=

CC      := $(CROSS_COMPILE)gcc
CFLAGS  := -O2 -W -Wall -I$(KDIR)/usr/include

all:
	$(CC) $(CFLAGS) writeloader.c -o writeloader

clean:
	rm -f writeloader
