CC=$(CROSS_COMPILE)gcc
CFLAGS=-Wall
LDLIBS=-lm
LDFLAGS=-L/usr/lib64

.PHONY: all
all: default

.PHONY: clean
clean:
	rm -f writer *.o

.PHONY: default
default: writer.o
	$(CC) $(CFLAGS) writer.o $(LDFLAGS) $(LDLIBS) -o writer

