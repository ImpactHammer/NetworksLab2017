
#include <iostream>
#include <stdio.h>
#include <stdlib.h>

#include <netdb.h>
#include <netinet/in.h>
#include <unistd.h>

#include <string.h>
#include <string>
#include <map>

#include <pthread.h>


/* message  : [header][options][message]
 *
 * [options]: {username}{message length} */

#ifndef uint
typedef unsigned int uint;
#endif

#define HEADER_LEN          8   // length of [header]
#define OPT_USERNAME_LEN   16  // length of {username} in [options]
#define OPT_MSGLEN_LEN      2   // length of {message length} in [options]
#define MSG_MAXLEN          256 // maximum length of [message]
#define MSG_ID_MINVALUE     0   // minimum value of message id
#define MSG_ID_STEP         1   // incremental step of message id
#define MSG_ID_LEN          sizeof(unsigned int)
#define MSG_DATA_MAXLEN     MSG_MAXLEN - MSG_ID_LEN - OPT_USERNAME_LEN - OPT_MSGLEN_LEN

#define PCG_MAXLEN          512 // maximum packege length

#define CMD_AUTHORIZE_ME    "auth"
#define CMD_SEND_MESSAGE    "msg"
#define CMD_BC_MESSAGE      "bcmsg"
#define CMD_DISCONNECT_ME   "disc"
#define CMD_EXIT            "exit"

#define HDR_CLI_AUTHORIZE           "auth_me"
#define HDR_CLI_RETRANSMIT_MESSAGE  "ret_msg"
#define HDR_CLI_BROADCAST_MESSAGE   "ret_bcm"
#define HDR_CLI_DISCONNECT          "disc_me"

#define HDR_SRV_INCOMING_MESSAGE    "inc_msg"
#define HDR_SRV_ERROR               "error_msg"

#define RC_CLIENT_CLOSING   1

#define CLI_PORT 0

typedef std::pair<std::string, void*(*)(void*) > fname_function;

int sockfd;
struct sockaddr addr_server;
struct sockaddr_in cli_addr;

uint msgid_incoming; // current incoming message id
uint msgid_outgoing; // current outgoing message id

char* received_message;

std::map<std::string, void*(*)(void*) > handlers; // key: msg header; value: handler

/* struct containing message string and an address of it's sender */
struct addr_message {
    sockaddr addr_sender;
    char* message;

    addr_message() {
    }

    addr_message(sockaddr addr_sender, char* message) {
        this->addr_sender = addr_sender;
        this->message = message;
    }
};

pthread_t thread_get_header;
pthread_t thread_listen_cmd;

void get_id_from_message(uint* msg_id, char* message) {
    memcpy(msg_id, message, MSG_ID_LEN);
}

void get_header_from_message(char* header, char* message) {
    memcpy(header, message + MSG_ID_LEN, HEADER_LEN);
}

void get_username_from_message(char* username, char* message) {
    memcpy(username, message + MSG_ID_LEN + HEADER_LEN, OPT_USERNAME_LEN);
}

void get_data_from_message(char* data, char* message) {
    memcpy(data, message + MSG_ID_LEN + HEADER_LEN + OPT_USERNAME_LEN, MSG_DATA_MAXLEN);
}

void compile_message(char* message, uint* msg_id, char* header, char* username, char* data) {
    memset(message, 0, PCG_MAXLEN);

    memcpy(message, msg_id, MSG_ID_LEN);
    memcpy(message + MSG_ID_LEN, header, HEADER_LEN);

    if (username != NULL) {
        memcpy(message + MSG_ID_LEN + HEADER_LEN, username, OPT_USERNAME_LEN);
    } else {
        memset(message + MSG_ID_LEN + HEADER_LEN, 0, OPT_USERNAME_LEN);
    }

    if (data != NULL) {
        memcpy(message + MSG_ID_LEN + HEADER_LEN + OPT_USERNAME_LEN, data, strlen(data));
    } else {
        memset(message + MSG_ID_LEN + HEADER_LEN + OPT_USERNAME_LEN, 0,
                PCG_MAXLEN - (MSG_ID_LEN + HEADER_LEN + OPT_USERNAME_LEN));
    }
}

void* incoming_msg(void* arg) {

    char username[OPT_USERNAME_LEN];
    char data[MSG_MAXLEN];

    get_username_from_message(username, received_message);
    get_data_from_message(data, received_message);

    printf("%s > [ %s ]\n\n", username, data);

    return NULL;
}

void* error_msg(void* arg) {
    int sock_serv = *((int*) arg);
    char buffer_msg[MSG_MAXLEN];


    printf("ERROR: [ %s ]\n\n", buffer_msg);

    return NULL;
}

void* authorize_me(void* arg) {

    char header[HEADER_LEN] = HDR_CLI_AUTHORIZE;
    char username[OPT_USERNAME_LEN];
    memset(username, 0, OPT_USERNAME_LEN);

    std::cin.getline(username, OPT_USERNAME_LEN);
    std::cin.clear();

    char message[PCG_MAXLEN];
    memset(message, 0, PCG_MAXLEN);

    compile_message(message, &msgid_outgoing, header, username, NULL);
    sendto(sockfd, message, MSG_ID_LEN + HEADER_LEN + OPT_USERNAME_LEN,
            0, &addr_server, sizeof (addr_server));
    msgid_outgoing += MSG_ID_STEP;

    return NULL;
}

void* disconnect_me(void* arg) {

    pthread_cancel(thread_get_header);
    pthread_join(thread_get_header, NULL);

    shutdown(sockfd, SHUT_RDWR);
    close(sockfd);

    int* rc_disc = new int(RC_CLIENT_CLOSING);

    return (void*) rc_disc;
}

void* close_client(void* arg) {
    printf("\nclosing...\n");

    void* rc = disconnect_me(NULL);

    return rc;
}

void* send_msg(void* arg) {

    char header[HEADER_LEN] = HDR_CLI_RETRANSMIT_MESSAGE;
    char username[OPT_USERNAME_LEN];
    char data[MSG_MAXLEN];
    memset(username, 0, OPT_USERNAME_LEN);
    memset(data, 0, MSG_MAXLEN);
    std::cin.getline(username, OPT_USERNAME_LEN + OPT_MSGLEN_LEN, ' ');
    std::cin.getline(data, MSG_MAXLEN);
    std::cin.clear();

    char message[PCG_MAXLEN];
    memset(message, 0, PCG_MAXLEN);
    compile_message(message, &msgid_outgoing, header, username, data);

    sendto(sockfd, message, MSG_ID_LEN + HEADER_LEN + OPT_USERNAME_LEN + strlen(data),
            0, &addr_server, sizeof (addr_server));
    msgid_outgoing += MSG_ID_STEP;

    return NULL;
}

void* send_broadcast_msg(void* arg) {
    char header[HEADER_LEN] = HDR_CLI_BROADCAST_MESSAGE;
    char data[MSG_MAXLEN];
    memset(data, 0, MSG_MAXLEN);
    std::cin.getline(data, MSG_MAXLEN);
    std::cin.clear();

    char message[PCG_MAXLEN];
    memset(message, 0, PCG_MAXLEN);
    compile_message(message, &msgid_outgoing, header, NULL, data);

    sendto(sockfd, message, MSG_ID_LEN + HEADER_LEN + OPT_USERNAME_LEN + strlen(data),
            0, &addr_server, sizeof (addr_server));
    msgid_outgoing += MSG_ID_STEP;

    return NULL;
}

void* get_header(void* arg) {

    handlers.insert(fname_function(std::string(HDR_SRV_INCOMING_MESSAGE), &incoming_msg)); // incoming message
    handlers.insert(fname_function(std::string(HDR_SRV_ERROR), &error_msg)); // error message

    struct sockaddr addr_server_tmp;
    uint addr_server_tmp_len = sizeof (addr_server_tmp);
    received_message = new char[PCG_MAXLEN];
    uint msg_id;
    char header[HEADER_LEN];

    msgid_incoming = 0;
    msgid_outgoing = 0;

    while (1) {
        memset(received_message, 0, PCG_MAXLEN);
        recvfrom(sockfd, received_message, PCG_MAXLEN, 0, &addr_server_tmp, &addr_server_tmp_len);

        get_id_from_message(&msg_id, received_message);
        get_header_from_message(header, received_message);

        char username[OPT_USERNAME_LEN];
        char data[MSG_MAXLEN];
        get_username_from_message(username, received_message);
        get_data_from_message(data, received_message);

        if (msg_id != msgid_incoming + MSG_ID_STEP) {
            msgid_incoming = msg_id;

            std::cout << "[warning] package loss\n";
        } else {
            msgid_incoming += MSG_ID_STEP;
        }
        if (handlers.find(header) != handlers.end()) {
            handlers[header](NULL);
        }
    }
}

void* listen_cmd(void* arg) {

    std::map < std::string, void*(*)(void*) > handlers_cmd;

    handlers_cmd.insert(fname_function(std::string(CMD_EXIT), &close_client));
    handlers_cmd.insert(fname_function(std::string(CMD_DISCONNECT_ME), &disconnect_me));
    handlers_cmd.insert(fname_function(std::string(CMD_AUTHORIZE_ME), &authorize_me));
    handlers_cmd.insert(fname_function(std::string(CMD_SEND_MESSAGE), &send_msg));
    handlers_cmd.insert(fname_function(std::string(CMD_BC_MESSAGE), &send_broadcast_msg));

    while (1) {

        std::string input;
        std::cin >> input;
        std::cin.get();

        if (handlers_cmd.find(input) != handlers_cmd.end()) {
            void* rc_handlers_cmd = handlers_cmd[input](NULL);

            if (rc_handlers_cmd != NULL) {
                if (*((int*) rc_handlers_cmd) == 1) { // checking exit condition
                    free(rc_handlers_cmd);

                    return NULL;
                }
            }
        }
    }
}

int main(int argc, char *argv[]) {

    int n;

    uint16_t portno;
    struct sockaddr_in serv_addr;
    struct hostent *server;

    if (argc < 3) {
        fprintf(stderr, "usage %s hostname port\n", argv[0]);
        exit(0);
    }

    portno = (uint16_t) atoi(argv[2]);

    sockfd = socket(AF_INET, SOCK_DGRAM, 0);

    if (sockfd < 0) {
        perror("ERROR opening socket");
        exit(1);
    }

    server = gethostbyname(argv[1]);

    if (server == NULL) {
        fprintf(stderr, "ERROR, no such host\n");
        exit(0);
    }

    memset((char *) &serv_addr, 0, sizeof (serv_addr));
    serv_addr.sin_family = AF_INET;
    memcpy((char *) &serv_addr.sin_addr.s_addr, server->h_addr, (size_t) server->h_length);
    serv_addr.sin_port = htons(portno);

    addr_server = *((struct sockaddr*) &serv_addr);

    cli_addr.sin_family = AF_INET;
    cli_addr.sin_addr.s_addr = INADDR_ANY;
    cli_addr.sin_port = htons(CLI_PORT);

    if (bind(sockfd, (struct sockaddr*) &cli_addr, sizeof (cli_addr)) < 0) {
        perror("ERROR on binding");
        exit(1);
    }

    pthread_create(&thread_get_header, NULL, &get_header, NULL);
    pthread_create(&thread_listen_cmd, NULL, &listen_cmd, NULL);

    printf("\nType 'exit' to end the program\n\n");
    pthread_join(thread_listen_cmd, NULL);
    pthread_cancel(thread_get_header);

    shutdown(sockfd, SHUT_RDWR);
    close(sockfd);

    return 0;
}
