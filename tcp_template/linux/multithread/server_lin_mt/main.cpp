#include <stdio.h>
#include <stdlib.h>

#include <netdb.h>
#include <netinet/in.h>
#include <unistd.h>

#include <string.h>
#include <set>

#include <pthread.h>

struct sockets {
    int listener;
    std::set<int> accepted;
};

void* serve(void* arg) {

    int newsockfd = *(int*)arg;
    ssize_t n;
    char buffer[256];
    bzero(buffer, 256);
    n = read(newsockfd, buffer, 255); // recv on Windows

    if (n < 0) {
        perror("ERROR reading from socket");
        exit(1);
    }

    printf("Here is the message: %s\n", buffer);

    /* Write a response to the client */
    n = write(newsockfd, "I got your message", 18); // send on Windows

    if (n < 0) {
        perror("ERROR writing to socket");
        exit(1);
    }

    /* Closing socket */
    shutdown(newsockfd, SHUT_RDWR);
    close(newsockfd);

    return NULL;
}

void* monitor (void* arg) {
    pthread_t thread;
    int newsockfd;
    struct sockets* sockets = (struct sockets*)arg;
    int sockfd = sockets->listener;
    unsigned int clilen;
    struct sockaddr_in cli_addr;
    clilen = sizeof(cli_addr);

    while (1) {
        /* Accept actual connection from the client */
        newsockfd = accept(sockfd, (struct sockaddr *) &cli_addr, &clilen);

        if (newsockfd < 0) {
            perror("ERROR on accept");
            exit(1);
        }

        sockets->accepted.insert(newsockfd);
        pthread_create(&thread, NULL, &serve, (void*)&newsockfd);
    }
}

void* close_server(void* arg) {
    while(1) {
        char input[5];
        memset(input, 0, 5);
        fgets(input, 6, stdin);
        if (!strcmp(input, "exit\n")) {
            printf("\nclosing...\n");
            return NULL;
        }
    }
}

int main(int argc, char *argv[]) {
    int sockfd;
    uint16_t portno;
    struct sockaddr_in serv_addr;
    ssize_t n;

    /* First call to socket() function */
    sockfd = socket(AF_INET, SOCK_STREAM, 0);

    if (sockfd < 0) {
        perror("ERROR opening socket");
        exit(1);
    }

    /* Initialize socket structure */
    bzero((char *) &serv_addr, sizeof(serv_addr));
    portno = 5001;

    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = INADDR_ANY;
    serv_addr.sin_port = htons(portno);

    /* Now bind the host address using bind() call.*/
    if (bind(sockfd, (struct sockaddr *) &serv_addr, sizeof(serv_addr)) < 0) {
        perror("ERROR on binding");
        exit(1);
    }

    /* Now start listening for the clients, here process will
       * go in sleep mode and will wait for the incoming connection
    */

    listen(sockfd, 5);

    struct sockets sockets;
    sockets.listener = sockfd;


    pthread_t thread_monitor;
    pthread_t thread_close_server;
    pthread_create(&thread_monitor, NULL, &monitor, (void*)&sockets);
    pthread_create(&thread_close_server, NULL, &close_server, NULL);

    printf("\nType 'exit' to end the program\n\n");
    pthread_join(thread_close_server, NULL);
    pthread_detach(thread_monitor);

    std::set<int>::iterator it;
    for (it = sockets.accepted.begin(); it != sockets.accepted.end(); it++) {
        shutdown(*it, SHUT_RDWR);
        close(*it);
    }

    /* Closing socket */
    shutdown(sockfd, SHUT_RDWR);
    close(sockfd);

    return 0;
}
