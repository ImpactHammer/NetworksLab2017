#include <stdio.h>
#include <stdlib.h>
#include <iostream>

#include <netdb.h>
#include <netinet/in.h>
#include <unistd.h>

#include <string.h>
#include <map>

#include <pthread.h>

#define HEADER_LENGTH 8
#define OPT_LENGTH 4        // probably will vary
#define IP_LENGTH 4
#define MSG_LENGTH 256      // maybe would be variable

int sockfd;

std::map<std::string, void*(*)(void*)> handlers;  // key: msg header; value: handler

pthread_t thread_wait_for_recv;
pthread_t thread_listen_cmd;

void print_ip(char* ip) {

    for (int i = 0; i < IP_LENGTH; i++) {
        short int part = ip[i];
        printf("%o.", part);
    }
}

int readn(int s, char* buf, int b_remain) {
    int b_rcvd = 0;
    int rc;
    while(b_remain) {
        rc = read(s, buf + b_rcvd, b_remain);
        if (rc < 1) {
            return rc;
        }
        b_rcvd += rc;
        b_remain -= rc;
    }

    return b_rcvd;
}

void* inc_msg(void* arg) {

    int sock_serv = *((int*)arg);
    ssize_t rc;
    char buffer_opt[OPT_LENGTH];
    char buffer_msg[MSG_LENGTH];
    bzero(buffer_opt, OPT_LENGTH);
    bzero(buffer_msg, MSG_LENGTH);

    rc = readn(sock_serv, buffer_opt, OPT_LENGTH);
    rc = readn(sock_serv, buffer_msg, MSG_LENGTH);

    unsigned short int ip_part[4];
    for (int i = 0; i < 4; i++) {
        unsigned char c = buffer_opt[i];
        ip_part[i] = c;
    }
    printf("message from %u.%u.%u.%u : \n%s\n\n", ip_part[0], ip_part[1],
            ip_part[2], ip_part[3], buffer_msg);

    return NULL;
}

void* disc_me(void* arg) {
    int rc;
    char header[HEADER_LENGTH] = "disc_me";
    char* buffer_head = header;

    rc = write(sockfd, buffer_head, HEADER_LENGTH);

    pthread_detach(thread_wait_for_recv);
    shutdown(sockfd, SHUT_RDWR);
    close(sockfd);

    int* rc_disc = new int(1);
    return (void*)rc_disc;
}

void* close_client(void* arg) {
    printf("\nclosing...\n");

    void* rc = disc_me(NULL);
    return rc;
}

void* send_msg(void* arg) {

    int rc;

    char header[HEADER_LENGTH] = "ret_msg";
    char input_ip[32];
    char input_msg[MSG_LENGTH];
    memset(input_ip, 0, 32);
    std::cin >> input_ip;
    std::cin >> input_msg;

    struct hostent *recver;
    recver = gethostbyname(input_ip);

    if (recver == NULL) {
        fprintf(stderr, "ERROR, no such host\n");
        return NULL;
    }

    printf("ip: %d", *((int*)(recver->h_addr)));

    char* ip = (recver->h_addr);

    char* buffer_head = header;
    char* buffer_opt = ip;
    char* buffer_msg = input_msg;
    rc = write(sockfd, buffer_head, HEADER_LENGTH);
    rc = write(sockfd, buffer_opt, OPT_LENGTH);
    rc = write(sockfd, buffer_msg, MSG_LENGTH);

    if (rc < 0) {
        perror("ERROR writing to socket");
        exit(1);
    }

    return NULL;
}

void* wait_for_recv(void* arg) {

    int newsockfd = *(int*)arg;

    ssize_t rc;
    char buffer[HEADER_LENGTH];

    while(1) {

        bzero(buffer, HEADER_LENGTH);
        rc = readn(newsockfd, buffer, HEADER_LENGTH);

        if (rc < 0) {
            perror("ERROR reading from socket");
            exit(1);
        }

        std::string header(buffer);
        if (handlers.find(header) != handlers.end()) {
            handlers[header]((void*)&newsockfd);
        }

    }

    return NULL;

}

void* listen_cmd(void* arg) {

    std::map<std::string, void*(*)(void*)> handlers_cmd;

    handlers_cmd.insert(std::pair<std::string, void*(*)(void*)>(std::string("exit"), &close_client));
    handlers_cmd.insert(std::pair<std::string, void*(*)(void*)>(std::string("disc_me"), &disc_me));
    handlers_cmd.insert(std::pair<std::string, void*(*)(void*)>(std::string("send_msg"), &send_msg));

    while(1) {

        std::string input;
        std::cin >> input;

        if (handlers_cmd.find(input) != handlers_cmd.end()) {
            void* rc_handlers_cmd = handlers_cmd[input](NULL);

            if (rc_handlers_cmd != NULL) {
                if (*((int*)rc_handlers_cmd) == 1) {    // checking exit condition
                    free(rc_handlers_cmd);
                    return NULL;
                }
            }
        }
    }
}

int main(int argc, char *argv[]) {

    handlers.insert(std::pair<std::string, void*(*)(void*)>(std::string("inc_msg"), &inc_msg)); // incoming message

    int n;
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

    bzero((char *) &serv_addr, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    bcopy(server->h_addr, (char *) &serv_addr.sin_addr.s_addr, (size_t) server->h_length);
    serv_addr.sin_port = htons(portno);

    /* Now connect to the server */
    if (connect(sockfd, (struct sockaddr *) &serv_addr, sizeof(serv_addr)) < 0) {
        perror("ERROR connecting");
        exit(1);
    }

    pthread_create(&thread_wait_for_recv, NULL, &wait_for_recv, (void*)(&sockfd));
    pthread_create(&thread_listen_cmd, NULL, &listen_cmd, NULL);

    printf("\nType 'exit' to end the program\n\n");
    pthread_join(thread_listen_cmd, NULL);
    pthread_detach(thread_wait_for_recv);

    /* Closing socket */
    shutdown(sockfd, SHUT_RDWR);
    close(sockfd);

    printf("%s\n", buffer);
    return 0;
}
