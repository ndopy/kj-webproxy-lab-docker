CC = gcc
CFLAGS = -O2 -Wall -I .
LIB = -lpthread

all: echoclient echoserveri

echoclient: echo-client.c csapp.o
	$(CC) $(CFLAGS) -o echoclient echo-client.c csapp.o $(LIB)

echoserveri: echo-server.c csapp.o
	$(CC) $(CFLAGS) -o echoserveri echo-server.c csapp.o $(LIB)

csapp.o: csapp.c
	$(CC) $(CFLAGS) -c csapp.c

clean:
	rm -f *.o echoclient echoserveri csapp *~