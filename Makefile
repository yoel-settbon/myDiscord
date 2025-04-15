CC = gcc
CFLAGS = -Wall
LDFLAGS = -lws2_32

CLIENT_SRC = client/main.c
SERVER_SRC = server/main.c
CLIENT_BIN = client.exe
SERVER_BIN = server.exe

all: $(CLIENT_BIN) $(SERVER_BIN)

$(CLIENT_BIN): $(CLIENT_SRC)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

$(SERVER_BIN): $(SERVER_SRC)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

clean:
	rm -f *.exe

.PHONY: all clean
