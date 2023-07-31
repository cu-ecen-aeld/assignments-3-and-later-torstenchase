CC=$(CROSS_COMPILE)gcc
CFLAGS=-I.

default: writer.o
	$(CC) -o writer writer.o

clean:
	rm -f writer *.o