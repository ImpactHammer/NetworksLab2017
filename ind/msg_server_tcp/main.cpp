#include <iostream>
#include <stdio.h>
#include <stdlib.h>

#include <netdb.h>
#include <netinet/in.h>
#include <unistd.h>

#include <string.h>
#include <string>
#include <set>
#include <map>

#include <pthread.h>

#define HEADER_LEN      8   // length of [header]
#define OPT_UNAME_LEN   16  // length of {username} in [options]
#define OPT_MSGLEN_LEN  2   // length of {message length} in [options]
#define MSG_MAXLEN      256 // maximum length of [message]

#define CMD_SEND_MESSAGE        "msg"
#define CMD_BROADCAST_MESSAGE   "bcmsg"
#define CMD_DISCONNECT_CLIENT   "disc"
#define CMD_EXIT                "exit"

#define HDR_CLI_AUTHORIZE           "auth_me"
#define HDR_CLI_RETRANSMIT_MESSAGE  "ret_msg"
#define HDR_CLI_DISCONNECT          "disc_me"

#define HDR_SRV_INCOMING_MESSAGE    "inc_msg"

#define USERNAME_SERVER     "@SERVER"
#define USERNAME_UNKNOWN    "@UNKNOWN"

#define RC_CLIENT_DISCONNECTED  2
#define RC_SERVER_CLOSING       1

typedef std::pair<std::string, void*(*)(void*)> fname_function;

std::map<std::string, void*(*)(void*)> handlers;    // key: msg header; value: handler
std::map<std::string, int> uname_sock;              // key: username  ; value: socket
std::map<int, std::string> sock_uname;              // key: socket    ; value: username
std::map<int, pthread_t*> sock_thread;              // key: socket    ; value: thread

struct sockets {
    int listener;
    std::set<int> accepted;
};

struct sockets sockets;

struct sockaddr_in serv_addr;


int readn(int s, char* buf, int b_remain) {
    int b_rcvd = 0;
    int rc;
    while(b_remain) {
        rc = read(s, buf + b_rcvd, b_remain);
        if (rc <= 0) {
            return rc;
        }
        b_rcvd += rc;
        b_remain -= rc;
    }

    return b_rcvd;
}


void send_msg_by_sock(int socket, char* header, char* uname, char* msg) {

    char buffer_head[HEADER_LEN];
    char buffer_opt[OPT_UNAME_LEN + OPT_MSGLEN_LEN];
    char buffer_msg[MSG_MAXLEN];

    bzero(buffer_head, HEADER_LEN);
    bzero(buffer_opt, OPT_UNAME_LEN + OPT_MSGLEN_LEN);
    bzero(buffer_msg, MSG_MAXLEN);

    bcopy(header, buffer_head, HEADER_LEN);

    short int msg_len;
    int opt_len;
    if (msg != NULL) {
        msg_len = strlen(msg);
        opt_len = OPT_UNAME_LEN + OPT_MSGLEN_LEN;
    } else {
        msg_len = 0;
        opt_len = OPT_UNAME_LEN;
    }

    if (uname != NULL) {
        bcopy(uname, buffer_opt, OPT_UNAME_LEN);
    }

    if (msg_len != 0) {
        bcopy(&msg_len, buffer_opt + OPT_UNAME_LEN, OPT_MSGLEN_LEN);
        bcopy(msg, buffer_msg, msg_len);
    }

    write(socket, buffer_head, HEADER_LEN);
    write(socket, buffer_opt, opt_len);
    if (msg_len != 0) {
        write(socket, buffer_msg, msg_len);
    }
}


void recv_msg_by_sock(int socket, char* buffer_opt, char* buffer_msg) {

    bzero(buffer_opt, OPT_UNAME_LEN + OPT_MSGLEN_LEN);
    if (buffer_msg != NULL) {
        bzero(buffer_msg, MSG_MAXLEN);
    }

    int opt_len;
    if (buffer_msg != NULL) {
        opt_len = OPT_UNAME_LEN + OPT_MSGLEN_LEN;
    } else {
        opt_len = OPT_UNAME_LEN;
    }

    readn(socket, buffer_opt, opt_len);

    short int msg_len;
    bcopy(buffer_opt + OPT_UNAME_LEN, &msg_len, OPT_MSGLEN_LEN);

    if (msg_len != 0) {
        readn(socket, buffer_msg, msg_len);
    }
}


void* send_msg(void* arg) {     // msg to 1 client

    char input_name[OPT_UNAME_LEN];
    char input_msg[MSG_MAXLEN];
    memset(input_name, 0, OPT_UNAME_LEN);
    memset(input_msg, 0, MSG_MAXLEN);
    std::cin.getline(input_name, OPT_UNAME_LEN, ' ');
    std::cin.getline(input_msg, MSG_MAXLEN);
    std::cin.clear();

    if (uname_sock.find(std::string(input_name)) != uname_sock.end()) {
        int sockfd = uname_sock[std::string(input_name)];
        send_msg_by_sock(sockfd, HDR_SRV_INCOMING_MESSAGE, USERNAME_SERVER, input_msg);
    } else {
        std::cout << "user is offline\n";
    }

    return NULL;
}


void* send_msg_bc(void* arg) {  // msg to all clients

    char input_msg[MSG_MAXLEN];
    memset(input_msg, 0, MSG_MAXLEN);
    std::cin.getline(input_msg, MSG_MAXLEN);
    std::cin.clear();

    for (std::set<int>::iterator it = sockets.accepted.begin();
         it != sockets.accepted.end(); it++) {

        int sockfd = *it;

        char header[HEADER_LEN] = HDR_SRV_INCOMING_MESSAGE;
        char uname[] = USERNAME_SERVER;

        send_msg_by_sock(sockfd, header, uname, input_msg);
    }

    return NULL;
}


void* auth_client(void* arg) {

    ssize_t rc;
    int sockfd = *((int*)arg);
    char uname[OPT_UNAME_LEN];

    recv_msg_by_sock(sockfd, uname, NULL);

    std::cout << "user " << uname << " is online\n";

    if (sock_uname.find(sockfd) != sock_uname.end()) {
        uname_sock.erase(sock_uname[sockfd]);
        sock_uname.erase(sockfd);
    }
    if (uname_sock.find(std::string(uname)) != uname_sock.end()) {
        sock_uname.erase(uname_sock[std::string(uname)]);
        uname_sock.erase(std::string(uname));
    }

    uname_sock.insert(std::pair<std::string, int>(std::string(uname), sockfd));
    sock_uname.insert(std::pair<int, std::string>(sockfd, std::string(uname)));

    return NULL;
}


void* ret_msg(void* arg) {

    int rc;
    int sock_cli_sender = *((int*)arg);
    int sock_cli_recver;
    char buffer_opt[OPT_UNAME_LEN + OPT_MSGLEN_LEN];
    char buffer_msg[MSG_MAXLEN];

    recv_msg_by_sock(sock_cli_sender, buffer_opt, buffer_msg);

    char uname[OPT_UNAME_LEN];
    bzero(uname, OPT_UNAME_LEN);
    bcopy(buffer_opt, uname, OPT_UNAME_LEN); // get username from [options]

    if (uname_sock.find(std::string(uname)) != uname_sock.end()) {
        sock_cli_recver = uname_sock[std::string(uname)];

    } else {
        send_msg_by_sock(sock_cli_sender, HDR_SRV_INCOMING_MESSAGE, USERNAME_SERVER, "user is offline");
        return NULL;
    }

    char buffer_head[HEADER_LEN] = HDR_SRV_INCOMING_MESSAGE;

    if (sock_uname.find(sock_cli_sender) != sock_uname.end()) {
        const char* tmp = (sock_uname[sock_cli_sender]).c_str();
        bcopy(tmp, uname, OPT_UNAME_LEN);
    } else {
        char tmp[] = USERNAME_UNKNOWN;
        bcopy(tmp, uname, OPT_UNAME_LEN);
    }

    send_msg_by_sock(sock_cli_recver, buffer_head, uname, buffer_msg);

    return NULL;
}


void* disc_client(void* arg) {

    int sockfd = *((int*)arg);

    std::cout << "disconnected: " << sockfd << std::endl;

    shutdown(sockfd, SHUT_RDWR);
    close(sockfd);

    sockets.accepted.erase(sockfd);
    uname_sock.erase(sock_uname[sockfd]);
    sock_uname.erase(sockfd);

    int* rc = new int(RC_CLIENT_DISCONNECTED);           // client disconnected return code
    return (void*)rc;
}


void* disc_chosen_client(void* arg) {

    char input_name[OPT_UNAME_LEN + OPT_MSGLEN_LEN];
    memset(input_name, 0, OPT_UNAME_LEN + OPT_MSGLEN_LEN);
    std::cin.getline(input_name, OPT_UNAME_LEN + OPT_MSGLEN_LEN);
    std::cin.clear();

    int sockfd = uname_sock[std::string(input_name)];

    std::cout << "disconnecting " << input_name << std::endl;

    shutdown(sockfd, SHUT_RDWR);
    close(sockfd);

    sockets.accepted.erase(sockfd);
    uname_sock.erase(std::string(input_name));
    sock_uname.erase(sockfd);

    pthread_t* thread = sock_thread[sockfd];
    pthread_detach(*thread);
    sock_thread.erase(sockfd);
    free(thread);

    return NULL;
}


void* close_server(void* arg) {
    printf("\nclosing...\n");
    int* rc = new int(RC_SERVER_CLOSING);
    return (void*)rc;
}


void* wait_for_recv(void* arg) {

    int newsockfd = *(int*)arg;

    ssize_t rc;
    char buffer[HEADER_LEN];

    while(1) {

        bzero(buffer, HEADER_LEN);
        rc = readn(newsockfd, buffer, HEADER_LEN);

        if (rc <= 0) {
            std::cout << "client lost connection/n" << std::endl;
            void* rc_disc = disc_client((void*)&newsockfd); // connection lost
            free(rc_disc);
            return NULL;
        }

        std::string header(buffer);
        if (handlers.find(header) != handlers.end()) {
            void* rc_handlers_recv = handlers[header]((void*)&newsockfd);

            if (rc_handlers_recv != NULL) {
                if (*((int*)rc_handlers_recv) == RC_CLIENT_DISCONNECTED) {    // checking client disconnected condition
                    free(rc_handlers_recv);
                    return NULL;
                }
            }
        }
    }

    return NULL;
}


void* monitor (void* arg) {

    handlers.insert(fname_function(std::string(HDR_CLI_AUTHORIZE), &auth_client));         // request for authorization
    handlers.insert(fname_function(std::string(HDR_CLI_RETRANSMIT_MESSAGE), &ret_msg));    // request for msg retransmission
    handlers.insert(fname_function(std::string(HDR_CLI_DISCONNECT), &disc_client));        // request for client disconnecting

    pthread_t* thread_recv = new pthread_t;

    int newsockfd;
    int sockfd = sockets.listener;
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

        sockets.accepted.insert(newsockfd); // add to opened sockets list

        pthread_create(thread_recv, NULL, &wait_for_recv, (void*)&newsockfd);
        sock_thread.insert(std::pair<int, pthread_t*>(newsockfd, thread_recv));
    }
}


void* listen_cmd(void* arg) {

    std::map<std::string, void*(*)(void*)> handlers_cmd;

    handlers_cmd.insert(fname_function(std::string(CMD_EXIT), &close_server));
    handlers_cmd.insert(fname_function(std::string(CMD_SEND_MESSAGE), &send_msg));
    handlers_cmd.insert(fname_function(std::string(CMD_BROADCAST_MESSAGE), &send_msg_bc));
    handlers_cmd.insert(fname_function(std::string(CMD_DISCONNECT_CLIENT), &disc_chosen_client));

    while(1) {
        std::string input;
        std::cin >> input;
        std::cin.get();

        if (handlers_cmd.find(input) != handlers_cmd.end()) {
            void* rc_handlers_cmd = handlers_cmd[input](NULL);

            if (rc_handlers_cmd != NULL) {
                if (*((int*)rc_handlers_cmd) == RC_SERVER_CLOSING) {    // checking exit condition
                    return NULL;
                }
            }
        }

    }
}


int main(int argc, char *argv[]) {

    int sockfd;
    uint16_t portno;
    ssize_t n;

    sockfd = socket(AF_INET, SOCK_STREAM, 0);

    if (sockfd < 0) {
        perror("ERROR opening socket");
        exit(1);
    }

    bzero((char *) &serv_addr, sizeof(serv_addr));
    portno = 5001;

    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = INADDR_ANY;
    serv_addr.sin_port = htons(portno);

    if (bind(sockfd, (struct sockaddr *) &serv_addr, sizeof(serv_addr)) < 0) {
        perror("ERROR on binding");
        exit(1);
    }

    listen(sockfd, 5);

    sockets.listener = sockfd;

    pthread_t thread_monitor;
    pthread_t thread_listen_cmd;
    pthread_create(&thread_monitor, NULL, &monitor, NULL);
    pthread_create(&thread_listen_cmd, NULL, &listen_cmd, NULL);

    printf("\nserver running\n");
    printf("\nType 'exit' to end the program\n\n");
    pthread_join(thread_listen_cmd, NULL);
    pthread_cancel(thread_monitor);

    std::set<int>::iterator it;
    for (it = sockets.accepted.begin(); it != sockets.accepted.end(); it++) {
        shutdown(*it, SHUT_RDWR);
        close(*it);
    }

    shutdown(sockfd, SHUT_RDWR);
    close(sockfd);

    return 0;
}
