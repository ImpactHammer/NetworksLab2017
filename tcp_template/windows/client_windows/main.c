#include <stdio.h>
#include <stdlib.h>
#include <winsock2.h>
#include <unistd.h>
#include <stdint.h>
#include <string.h>

int readn(SOCKET s, char* buf, int b_remain, int flags) {
    int b_rcvd = 0;
    int rc;
    while(b_remain) {
        rc = recv(s, buf + b_rcvd, b_remain, flags);
        if (rc < 1) {
            return rc;
        }
        b_rcvd += rc;
        b_remain -= rc;
    }
    
    return b_rcvd;
}

int main(int argc, char *argv[]) {
    int n;
    WSADATA wsaData;
    n = WSAStartup(MAKEWORD(2,2), &wsaData);
    if (n != 0) {
        printf("WSAStartup failed: %d\n", n);
        return 1;
    }
    
    int sockfd;
    uint16_t portno;
    struct sockaddr_in serv_addr;
    struct hostent *server;

    char buffer[256];

    if (argc < 3) {
        fprintf(stderr, "usage %s hostname port\n", argv[0]);
        exit(0);
    }

    portno = (uint16_t) atoi(argv[2]);

    /* Create a socket point */
    sockfd = socket(AF_INET, SOCK_STREAM, 0);

    if (sockfd < 0) {
        perror("ERROR opening socket");
        exit(1);
    }

    server = gethostbyname(argv[1]);

    if (server == NULL) {
        fprintf(stderr, "ERROR, no such host\n");
        exit(0);
    }
    
    memset((char*) &serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    memcpy((char *) &serv_addr.sin_addr.s_addr, server->h_addr, (size_t) server->h_length);
    serv_addr.sin_port = htons(portno);

    /* Now connect to the server */
    if (connect(sockfd, (struct sockaddr *) &serv_addr, sizeof(serv_addr)) < 0) {
        perror("ERROR connecting");
        exit(1);
    }

    /* Now ask for a message from the user, this message
       * will be read by server
    */

    printf("Please enter the message: ");
    memset(buffer, 0, 256);
    fgets(buffer, 255, stdin);

    /* Send message to the server */
    n = send(sockfd, buffer, strlen(buffer), 0);

    if (n < 0) {
        perror("ERROR writing to socket");
        exit(1);
    }

    /* Now read server response */
    memset(buffer, 0, 256);
    // n = recv(sockfd, buffer, 255, 0);
    n = readn(sockfd, buffer, 18, 0);

    if (n < 0) {
        perror("ERROR reading from socket");
        exit(1);
    }

    printf("%s\n", buffer);
    
    /* Closing socket */
    n = shutdown(sockfd, SD_BOTH);
    if (n == SOCKET_ERROR) {
        printf("shutdown failed: %d\n", WSAGetLastError());
        closesocket(sockfd);
        WSACleanup();
        return 1;
    }
    closesocket(sockfd);
    WSACleanup();
    
    return 0;
}
