CC=gcc
CFLAGS=-I. -pthread

all: cliente servidor

cliente: cliente.o common.h
	$(CC) -o cliente cliente.o $(CFLAGS)

servidor: servidor.o common.h
	$(CC) -o servidor servidor.o $(CFLAGS)

clean:
	rm -f cliente servidor *.o

%.o: %.c
	$(CC) -c -o $@ $< $(CFLAGS)
