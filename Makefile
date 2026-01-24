CC=gcc
CFLAGS=-Wall -Wextra -O2

all: client server

client:
	$(CC) $(CFLAGS) src/client.c src/clientSy.c -o build/client

server:
	$(CC) $(CFLAGS) src/server.c src/serverSy.c -o build/server

clean:
	rm -f build/client build/server