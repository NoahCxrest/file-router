CC=gcc
CFLAGS=-Wall -O3 -std=c99 -I/opt/homebrew/include
LIBS=-lmicrohttpd -lcurl -L/opt/homebrew/lib

all: server

server: main.c
	$(CC) $(CFLAGS) -o server main.c $(LIBS)

clean:
	rm -f server

.PHONY: all clean