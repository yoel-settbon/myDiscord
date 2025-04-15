#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <winsock2.h>
#include <ws2tcpip.h>

#pragma comment(lib, "ws2_32.lib")

#define PORT 4242

int main() {
    WSADATA wsa;
    SOCKET sock;
    struct sockaddr_in serv_addr;
    char *message = "Hello depuis le client !";

    // Initialise Winsock
    if (WSAStartup(MAKEWORD(2,2), &wsa) != 0) {
        printf("Échec de WSAStartup. Code erreur : %d\n", WSAGetLastError());
        return 1;
    }

    sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock == INVALID_SOCKET) {
        printf("Erreur socket : %d\n", WSAGetLastError());
        return 1;
    }

    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(PORT);
    serv_addr.sin_addr.s_addr = inet_addr("127.0.0.1");

    if (connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        printf("Connexion échouée\n");
        return 1;
    }

    send(sock, message, strlen(message), 0);
    printf("Message envoyé au serveur.\n");

    closesocket(sock);
    WSACleanup();
    return 0;
}
