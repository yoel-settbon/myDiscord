#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <process.h>

#define PORT 8080
#define MAX_CLIENTS 10
#define MAX_MSG_LEN 1024

SOCKET clients[MAX_CLIENTS] = {0};

void broadcast_message(const char *msg, SOCKET sender_sock) {
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (clients[i] != 0 && clients[i] != sender_sock) {
            send(clients[i], msg, strlen(msg), 0);
        }
    }
}

unsigned __stdcall handle_client(void *arg) {
    SOCKET sock = *(SOCKET*)arg;
    char buffer[MAX_MSG_LEN];
    
    while (1) {
        int len = recv(sock, buffer, MAX_MSG_LEN, 0);
        if (len <= 0) break;
        
        buffer[len] = '\0';
        printf("Message reçu: %s\n", buffer);
        broadcast_message(buffer, sock);
    }
    
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (clients[i] == sock) {
            clients[i] = 0;
            break;
        }
    }
    
    closesocket(sock);
    return 0;
}

int main() {
    WSADATA wsa;
    SOCKET server_fd, new_socket;
    struct sockaddr_in address;
    int addrlen = sizeof(address);
    
    if (WSAStartup(MAKEWORD(2,2), &wsa) != 0) {
        printf("WSAStartup failed: %d\n", WSAGetLastError());
        return 1;
    }
    
    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == INVALID_SOCKET) {
        printf("socket failed: %d\n", WSAGetLastError());
        return 1;
    }
    
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(PORT);
    
    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) == SOCKET_ERROR) {
        printf("bind failed: %d\n", WSAGetLastError());
        closesocket(server_fd);
        return 1;
    }
    
    if (listen(server_fd, 3) == SOCKET_ERROR) {
        printf("listen failed: %d\n", WSAGetLastError());
        closesocket(server_fd);
        return 1;
    }
    
    printf("Serveur démarré sur le port %d\n", PORT);
    
    while (1) {
        if ((new_socket = accept(server_fd, (struct sockaddr *)&address, &addrlen)) == INVALID_SOCKET) {
            printf("accept failed: %d\n", WSAGetLastError());
            continue;
        }
        
        for (int i = 0; i < MAX_CLIENTS; i++) {
            if (clients[i] == 0) {
                clients[i] = new_socket;
                _beginthreadex(NULL, 0, handle_client, &new_socket, 0, NULL);
                printf("Nouveau client connecté\n");
                break;
            }
        }
    }
    
    closesocket(server_fd);
    WSACleanup();
    return 0;
}