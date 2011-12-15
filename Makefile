CC=arm-linux-gnueabi-gcc

all:
	$(CC) writeloader.c -o writeloader -lm

clean:
	rm -f writeloader
