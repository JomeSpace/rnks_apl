CC=gcc
CFLAGS=-Wall -Wextra -O2

.PHONY: all clean

all: build client server

build:
	mkdir -p build

client: build
	$(CC) $(CFLAGS) src/client.c src/clientSy.c -o build/client

server: build
	$(CC) $(CFLAGS) src/server.c src/serverSy.c -o build/server

clean:
	rm -f build/client build/server
