CROSS_COMPILE ?=
KDIR ?=

CC      := $(CROSS_COMPILE)gcc
CFLAGS  := -O2 -W -Wall -I$(KDIR)/usr/include
LIBS    := -lm

all:
	$(CC) $(CFLAGS) writeloader.c -o writeloader $(LIBS)

clean:
	rm -f writeloader
