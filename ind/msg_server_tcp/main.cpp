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

#define HEADER_LEN          8   // length of [header]
#define OPT_USERNAME_LEN    16  // length of {username} in [options]
#define OPT_MSGLEN_LEN      2   // length of {message length} in [options]
#define MSG_MAXLEN          256 // maximum length of [message]

#define CMD_SEND_MESSAGE        "msg"
#define CMD_BROADCAST_MESSAGE   "bcmsg"
#define CMD_DISCONNECT_CLIENT   "disc"
#define CMD_EXIT                "exit"
#define CMD_LIST_VALID_LOGINS   "lvl"

#define HDR_CLI_AUTHORIZE           "auth_me"
#define HDR_CLI_RETRANSMIT_MESSAGE  "ret_msg"
#define HDR_CLI_BROADCAST_MESSAGE   "ret_bcm"
#define HDR_CLI_DISCONNECT          "disc_me"

#define HDR_SRV_INCOMING_MESSAGE    "inc_msg"
#define HDR_SRV_ERROR               "err_msg"

#define USERNAME_SERVER     "@SERVER"
#define USERNAME_UNKNOWN    "@UNKNOWN"

#define RC_CLIENT_DISCONNECTED  2
#define RC_SERVER_CLOSING       1

#define ERR_USER_IS_OFFLINE     "user is offline"
#define ERR_INVALID_USERNAME    "invalid username"

#define LOGIN_MAXLEN 256

#define ARGC 3

typedef std::pair<std::string, void*(*)(void*)> fname_function;

std::map<std::string, void*(*)(void*)> handlers;    // key: msg header; value: handler
std::map<std::string, int> username_sock;           // key: username  ; value: socket
std::map<int, std::string> sock_username;           // key: socket    ; value: username
std::map<int, pthread_t*> sock_thread;              // key: socket    ; value: thread

struct sockets {
    int listener;
    std::set<int> accepted;
};

struct sockets sockets;

struct sockaddr_in serv_addr;

std::set<std::string> valid_logins;


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


void send_msg_by_sock(int socket, char* header, char* username, char* msg) {

    char buffer_head[HEADER_LEN];
    char buffer_opt[OPT_USERNAME_LEN + OPT_MSGLEN_LEN];
    char buffer_msg[MSG_MAXLEN];

    bzero(buffer_head, HEADER_LEN);
    bzero(buffer_opt, OPT_USERNAME_LEN + OPT_MSGLEN_LEN);
    bzero(buffer_msg, MSG_MAXLEN);

    bcopy(header, buffer_head, HEADER_LEN);


    short int msg_len = 0;
    int opt_len = 0;

    opt_len += (username == NULL) ? 0 : OPT_USERNAME_LEN;
    opt_len += (msg == NULL) ? 0 : OPT_MSGLEN_LEN;
    msg_len += (msg == NULL) ? 0 : strlen(msg);

    if (username != NULL) {
        bcopy(username, buffer_opt, OPT_USERNAME_LEN);
    }

    if (msg_len != 0) {
        if (username != NULL) {
            bcopy(&msg_len, buffer_opt + OPT_USERNAME_LEN, OPT_MSGLEN_LEN);
        } else {
            bcopy(&msg_len, buffer_opt, OPT_MSGLEN_LEN);
        }
        bcopy(msg, buffer_msg, msg_len);
    }

    write(socket, buffer_head, HEADER_LEN);
    write(socket, buffer_opt, opt_len);
    if (msg_len != 0) {
        write(socket, buffer_msg, msg_len);
    }
}


void recv_msg_by_sock(int socket, char* username, char* buffer_msg) {

    char buffer_opt[OPT_USERNAME_LEN + OPT_MSGLEN_LEN];
    bzero(buffer_opt, OPT_USERNAME_LEN + OPT_MSGLEN_LEN);
    if (buffer_msg != NULL) {
        bzero(buffer_msg, MSG_MAXLEN);
    }

    int opt_len = 0;
    short int msg_len = 0;

    opt_len += (username == NULL) ? 0 : OPT_USERNAME_LEN;
    opt_len += (buffer_msg == NULL) ? 0 : OPT_MSGLEN_LEN;
    msg_len += (buffer_msg == NULL) ? 0 : MSG_MAXLEN;

    readn(socket, buffer_opt, opt_len);
    if (username != NULL) {
        bcopy(buffer_opt, username, OPT_USERNAME_LEN);
        bcopy(buffer_opt + OPT_USERNAME_LEN, &msg_len, OPT_MSGLEN_LEN);
    } else {
        bcopy(buffer_opt, &msg_len, OPT_MSGLEN_LEN);
    }

    if (msg_len != 0) {
        readn(socket, buffer_msg, msg_len);
    }
}


void err_msg(int socket, char* msg) {
    send_msg_by_sock(socket, HDR_SRV_ERROR, NULL, msg);
}


void aux_broadcast_msg(char* username, char* msg) {

    char header[HEADER_LEN] = HDR_SRV_INCOMING_MESSAGE;
    for (std::set<int>::iterator it = sockets.accepted.begin();
         it != sockets.accepted.end(); it++) {

        int sockfd = *it;

        send_msg_by_sock(sockfd, header, username, msg);
    }
}


void* send_msg(void* arg) {     // msg to 1 client

    char input_name[OPT_USERNAME_LEN];
    char input_msg[MSG_MAXLEN];
    memset(input_name, 0, OPT_USERNAME_LEN);
    memset(input_msg, 0, MSG_MAXLEN);
    std::cin.getline(input_name, OPT_USERNAME_LEN, ' ');
    std::cin.getline(input_msg, MSG_MAXLEN);
    std::cin.clear();

    if (username_sock.find(std::string(input_name)) != username_sock.end()) {
        int sockfd = username_sock[std::string(input_name)];
        send_msg_by_sock(sockfd, HDR_SRV_INCOMING_MESSAGE, USERNAME_SERVER, input_msg);
    } else {
        std::cout << ERR_USER_IS_OFFLINE << "\n";
    }

    return NULL;
}


void* send_broadcast_msg(void* arg) {  // msg to all clients

    char input_msg[MSG_MAXLEN];
    memset(input_msg, 0, MSG_MAXLEN);
    std::cin.getline(input_msg, MSG_MAXLEN);
    std::cin.clear();
    aux_broadcast_msg(USERNAME_SERVER, input_msg);

    return NULL;
}


void* auth_client(void* arg) {

    ssize_t rc;
    int sockfd = *((int*)arg);
    char username[OPT_USERNAME_LEN];

    recv_msg_by_sock(sockfd, username, NULL);

    std::cout << username << "\n";

    if (valid_logins.find(std::string(username)) == valid_logins.end()) {
        err_msg(sockfd, ERR_INVALID_USERNAME);
        return NULL;
    }

    std::cout << "user " << username << " is online\n";

    if (sock_username.find(sockfd) != sock_username.end()) {
        username_sock.erase(sock_username[sockfd]);
        sock_username.erase(sockfd);
    }
    if (username_sock.find(std::string(username)) != username_sock.end()) {
        sock_username.erase(username_sock[std::string(username)]);
        username_sock.erase(std::string(username));
    }

    username_sock.insert(std::pair<std::string, int>(std::string(username), sockfd));
    sock_username.insert(std::pair<int, std::string>(sockfd, std::string(username)));

    return NULL;
}


void* retransmit_msg(void* arg) {

    int rc;
    int sock_cli_sender = *((int*)arg);
    int sock_cli_recver;
    char buffer_msg[MSG_MAXLEN];
    char username[OPT_USERNAME_LEN];

    recv_msg_by_sock(sock_cli_sender, username, buffer_msg);

    if (username_sock.find(std::string(username)) != username_sock.end()) {
        sock_cli_recver = username_sock[std::string(username)];

    } else {
        err_msg(sock_cli_sender, ERR_USER_IS_OFFLINE);
        return NULL;
    }

    char buffer_head[HEADER_LEN] = HDR_SRV_INCOMING_MESSAGE;

    if (sock_username.find(sock_cli_sender) != sock_username.end()) {
        const char* tmp = (sock_username[sock_cli_sender]).c_str();
        bcopy(tmp, username, OPT_USERNAME_LEN);
    } else {
        char tmp[] = USERNAME_UNKNOWN;
        bcopy(tmp, username, OPT_USERNAME_LEN);
    }

    send_msg_by_sock(sock_cli_recver, buffer_head, username, buffer_msg);

    return NULL;
}

void* retransmit_broadcast_msg(void* arg) {

    int sock_cli_sender = *((int*)arg);
    char* username;

    if (sock_username.find(sock_cli_sender) != sock_username.end()) {
        username = (char*)sock_username[sock_cli_sender].c_str();
    } else {
        username = (char*)USERNAME_UNKNOWN;
    }

    char buffer_msg[MSG_MAXLEN];

    recv_msg_by_sock(sock_cli_sender, NULL, buffer_msg);
    aux_broadcast_msg(username, buffer_msg);
}

void* disconnect_client(void* arg) {

    int sockfd = *((int*)arg);

    std::cout << "disconnected: " << sockfd << std::endl;

    shutdown(sockfd, SHUT_RDWR);
    close(sockfd);

    if (sockets.accepted.find(sockfd) != sockets.accepted.end()) {
        sockets.accepted.erase(sockfd);
    }
    if (username_sock.find(sock_username[sockfd]) != username_sock.end()) {
        username_sock.erase(sock_username[sockfd]);
    }
    if (sock_username.find(sockfd) != sock_username.end()) {
        sock_username.erase(sockfd);
    }

    int* rc = new int(RC_CLIENT_DISCONNECTED);           // client disconnected return code
    return (void*)rc;
}


void* disconnect_chosen_client(void* arg) {

    char input_name[OPT_USERNAME_LEN + OPT_MSGLEN_LEN];
    memset(input_name, 0, OPT_USERNAME_LEN + OPT_MSGLEN_LEN);
    std::cin.getline(input_name, OPT_USERNAME_LEN + OPT_MSGLEN_LEN);
    std::cin.clear();

    if (username_sock.find(std::string(input_name)) == username_sock.end()) {
        std::cout << ERR_USER_IS_OFFLINE << "\n";
        return NULL;
    }

    int sockfd = username_sock[std::string(input_name)];

    std::cout << "disconnecting " << input_name << std::endl;

    shutdown(sockfd, SHUT_RDWR);
    close(sockfd);

    sockets.accepted.erase(sockfd);
    username_sock.erase(std::string(input_name));
    sock_username.erase(sockfd);

    pthread_t* thread = sock_thread[sockfd];
    pthread_cancel(*thread);
    sock_thread.erase(sockfd);
    free(thread);

    return NULL;
}

void* list_valid_logins(void*) {
    std::set<std::string>::iterator it;
    std::cout << "valid logins:\n";
    for (it = valid_logins.begin(); it != valid_logins.end(); it++) {
        std::cout << *it << "\n";
    }
}


void* close_server(void* arg) {
    printf("\nclosing...\n");

    std::map<int, pthread_t*>::iterator it;
    for (it = sock_thread.begin(); it != sock_thread.end(); it++) {
        pthread_t* thread = (*it).second;
        int socket = (*it).first;
        shutdown(socket, SHUT_RDWR);
        close(socket);
        pthread_cancel(*thread);
        sock_thread.erase(socket);
        free(thread);
    }

    int* rc = new int(RC_SERVER_CLOSING);
    return (void*)rc;
}


void* get_header(void* arg) {

    int newsockfd = *(int*)arg;

    ssize_t rc;
    char buffer[HEADER_LEN];

    while(1) {

        bzero(buffer, HEADER_LEN);
        rc = readn(newsockfd, buffer, HEADER_LEN);

        if (rc <= 0) {
            std::cout << "client lost connection\n" << std::endl;
            void* rc_disc = disconnect_client((void*)&newsockfd); // connection lost
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


void* accept_connections (void* arg) {

    handlers.insert(fname_function(std::string(HDR_CLI_AUTHORIZE), &auth_client));         // request for authorization
    handlers.insert(fname_function(std::string(HDR_CLI_RETRANSMIT_MESSAGE), &retransmit_msg));    // request for msg retransmission
    handlers.insert(fname_function(std::string(HDR_CLI_BROADCAST_MESSAGE), &retransmit_broadcast_msg));     // request for broadcast msg retransmission
    handlers.insert(fname_function(std::string(HDR_CLI_DISCONNECT), &disconnect_client));        // request for client disconnecting

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

        pthread_t* thread_recv = new pthread_t;
        pthread_create(thread_recv, NULL, &get_header, (void*)&newsockfd);
        sock_thread.insert(std::pair<int, pthread_t*>(newsockfd, thread_recv));
    }
}


void* listen_cmd(void* arg) {

    std::map<std::string, void*(*)(void*)> handlers_cmd;

    handlers_cmd.insert(fname_function(std::string(CMD_EXIT), &close_server));
    handlers_cmd.insert(fname_function(std::string(CMD_SEND_MESSAGE), &send_msg));
    handlers_cmd.insert(fname_function(std::string(CMD_BROADCAST_MESSAGE), &send_broadcast_msg));
    handlers_cmd.insert(fname_function(std::string(CMD_DISCONNECT_CLIENT), &disconnect_chosen_client));
    handlers_cmd.insert(fname_function(std::string(CMD_LIST_VALID_LOGINS), &list_valid_logins));

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

    if (argc < ARGC) {
        printf("usage: msg_server_tcp <port> <valid_logins_filename>\n");
        exit(1);
    }

    int sockfd;
    uint16_t portno;
    ssize_t n;

    portno = 5001;
    std::cout << portno;

    /* open file containing valid logins */
    FILE * valid_logins_file;
    valid_logins_file = fopen(argv[2],"r");

    if (valid_logins_file == NULL) {
        printf("no such file\n");
        exit(1);
    }

    /* fill vector with valid logins */
    char login[LOGIN_MAXLEN];
    memset(login, 0, LOGIN_MAXLEN);
    while (fscanf(valid_logins_file, "%s", login) != EOF) {
        valid_logins.insert(std::string(login));
        memset(login, 0, LOGIN_MAXLEN);
    }


    sockfd = socket(AF_INET, SOCK_STREAM, 0);

    if (sockfd < 0) {
        perror("ERROR opening socket");
        exit(1);
    }

    bzero((char *) &serv_addr, sizeof(serv_addr));

    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = INADDR_ANY;
    serv_addr.sin_port = htons(portno);

    if (bind(sockfd, (struct sockaddr *) &serv_addr, sizeof(serv_addr)) < 0) {
        perror("ERROR on binding");
        exit(1);
    }

    listen(sockfd, 5);

    sockets.listener = sockfd;

    pthread_t thread_accept_connections;
    pthread_t thread_listen_cmd;
    pthread_create(&thread_accept_connections, NULL, &accept_connections, NULL);
    pthread_create(&thread_listen_cmd, NULL, &listen_cmd, NULL);

    printf("\nserver running\n");
    printf("\nType 'exit' to end the program\n\n");
    pthread_join(thread_listen_cmd, NULL);
    pthread_cancel(thread_accept_connections);

    std::set<int>::iterator it;
    for (it = sockets.accepted.begin(); it != sockets.accepted.end(); it++) {
        shutdown(*it, SHUT_RDWR);
        close(*it);
    }

    shutdown(sockfd, SHUT_RDWR);
    close(sockfd);

    return 0;
}
