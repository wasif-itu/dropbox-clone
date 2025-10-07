CC = gcc
CFLAGS = -Wall -Iinclude
SRC_DIR = src
BIN_DIR = bin

SERVER = $(BIN_DIR)/server
CLIENT = $(BIN_DIR)/client

all: $(SERVER) $(CLIENT)

$(BIN_DIR):
	mkdir -p $(BIN_DIR)

$(SERVER): $(SRC_DIR)/server.c | $(BIN_DIR)
	$(CC) $(CFLAGS) -o $(SERVER) $(SRC_DIR)/server.c

$(CLIENT): $(SRC_DIR)/client.c | $(BIN_DIR)
	$(CC) $(CFLAGS) -o $(CLIENT) $(SRC_DIR)/client.c

clean:
	rm -rf $(BIN_DIR)

run-server:
	./bin/server

run-client:
	./bin/client
.PHONY: all clean run-server run-client
