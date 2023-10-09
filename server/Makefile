CC=$(CROSS_COMPILE)gcc
CFLAGS=-g -Wall -Werror
LDLIBS=-lm -lpthread -lrt
LDFLAGS=-L/usr/lib64

.PHONY: all
all: default

.PHONY: clean
clean:
	rm -f aesdsocket *.o

.PHONY: default
default: aesdsocket.o
	$(CC) $(CFLAGS) aesdsocket.o $(LDFLAGS) $(LDLIBS) -o aesdsocket

