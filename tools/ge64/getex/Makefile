# GoldenEye's texture decompressor (image.c from the n64decomp/007 decomp, lightly adapted) as a host
# tool. It keeps pointers in 32-bit integers like the N64 code, so it is built as a 32-bit program.
CC ?= gcc
CFLAGS := -m32 -std=gnu89 -w

getex: main.o image.o
	$(CC) -m32 main.o image.o -o $@

image.o: image.c shim.h
	$(CC) $(CFLAGS) -c image.c -o $@

main.o: main.c shim.h
	$(CC) $(CFLAGS) -c main.c -o $@

clean:
	rm -f getex *.o
