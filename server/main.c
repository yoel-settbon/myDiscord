#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <winsock2.h>

#pragma comment(lib, "ws2_32.lib")

#define PORT 4242

int main() {
    WSADATA wsa;
    SOCKET server_fd, client_socket;
    struct sockaddr_in server, client;
    int c;
    char buffer[1024];

    WSAStartup(MAKEWORD(2,2), &wsa);

    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    server.sin_family = AF_INET;
    server.sin_addr.s_addr = INADDR_ANY;
    server.sin_port = htons(PORT);

    bind(server_fd, (struct sockaddr *)&server, sizeof(server));
    listen(server_fd, 3);

    printf("Serveur en écoute sur le port %d...\n", PORT);

    c = sizeof(struct sockaddr_in);
    client_socket = accept(server_fd, (struct sockaddr *)&client, &c);
    recv(client_socket, buffer, sizeof(buffer), 0);

    printf("Message reçu : %s\n", buffer);

    closesocket(client_socket);
    closesocket(server_fd);
    WSACleanup();
    return 0;
}
