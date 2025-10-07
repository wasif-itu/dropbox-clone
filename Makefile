CC = gcc
CFLAGS = -Wall -Wextra -pthread -Iinclude -g
SRCDIR = src
OBJ = $(SRCDIR)/queue.o $(SRCDIR)/auth.o $(SRCDIR)/storage.o $(SRCDIR)/worker_pool.o $(SRCDIR)/client_pool.o $(SRCDIR)/main.o

all: server client_app

server: $(OBJ)
	$(CC) $(CFLAGS) -o server $(OBJ)

client_app: src/client_app.c
	$(CC) $(CFLAGS) -o client_app src/client_app.c

src/queue.o: src/queue.c include/queue.h
	$(CC) $(CFLAGS) -c src/queue.c -o src/queue.o

src/auth.o: src/auth.c include/auth.h
	$(CC) $(CFLAGS) -c src/auth.c -o src/auth.o

src/storage.o: src/storage.c include/storage.h
	$(CC) $(CFLAGS) -c src/storage.c -o src/storage.o

src/worker_pool.o: src/worker_pool.c include/worker_pool.h include/server_types.h include/storage.h include/queue.h
	$(CC) $(CFLAGS) -c src/worker_pool.c -o src/worker_pool.o

src/client_pool.o: src/client_pool.c include/client_pool.h include/server_types.h include/queue.h include/auth.h include/storage.h
	$(CC) $(CFLAGS) -c src/client_pool.c -o src/client_pool.o

src/main.o: src/main.c include/dropbox.h include/queue.h include/client_pool.h include/worker_pool.h include/auth.h include/storage.h
	$(CC) $(CFLAGS) -c src/main.c -o src/main.o

clean:
	rm -f src/*.o server client_app

.PHONY: all clean
