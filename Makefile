CC = gcc
CFLAGS = -Wall `pkg-config --cflags gtk4`
LDFLAGS = `pkg-config --libs gtk4`

# Fichiers
CLIENT_SRCS = client/main.c client/client_socket.c
SERVER_SRCS = server/main.c server/server_socket.c server/connection_handler.c
COMMON_SRCS = common/utils.c
CRYPTO_SRCS = crypto/crypto.c
DB_SRCS = database/db.c

# Liens
CLIENT_OBJS = $(CLIENT_SRCS) $(COMMON_SRCS)
SERVER_OBJS = $(SERVER_SRCS) $(COMMON_SRCS) $(CRYPTO_SRCS) $(DB_SRCS)

# Noms des binaires
CLIENT_BIN = myDiscord_client.exe
SERVER_BIN = myDiscord_server.exe

all: $(CLIENT_BIN) $(SERVER_BIN)

$(CLIENT_BIN): $(CLIENT_OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

$(SERVER_BIN): $(SERVER_OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS) -lpthread

clean:
	del *.exe

.PHONY: all clean
