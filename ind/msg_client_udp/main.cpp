
#include <iostream>
#include <stdio.h>
#include <stdlib.h>
#include <winsock2.h>
#include <windows.h>

#include <string.h>
#include <map>


/* message  : [header][options][message]
 *
 * [options]: {username}{message length} */


#define HEADER_LEN      8   // length of [header]
#define OPT_UNAME_LEN   16  // length of {username} in [options]
#define OPT_MSGLEN_LEN  2   // length of {message length} in [options]
#define MSG_MAXLEN      256 // maximum length of [message]

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
#define HDR_SRV_ERROR               "err_msg"

#define RC_CLIENT_CLOSING   1

typedef std::pair<std::string, void*(*)(void*)> fname_function;

int sockfd;

std::map<std::string, void*(*)(void*)> handlers;  // key: msg header; value: handler

DWORD thread_wait_for_recv;
DWORD thread_listen_cmd;

HANDLE h_wait_for_recv;
HANDLE h_listen_cmd;

int readn(int s, char* buf, int b_remain) {
    int b_rcvd = 0;
    int rc;
    while(b_remain) {
        rc = recv(s, buf + b_rcvd, b_remain, 0);
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

    memset(buffer_head, 0, HEADER_LEN);
    memset(buffer_opt, 0, OPT_UNAME_LEN + OPT_MSGLEN_LEN);
    memset(buffer_msg, 0, MSG_MAXLEN);

    memcpy(buffer_head, header, HEADER_LEN);


    short int msg_len = 0;
    int opt_len = 0;

    opt_len += (uname == NULL) ? 0 : OPT_UNAME_LEN;
    opt_len += (msg == NULL) ? 0 : OPT_MSGLEN_LEN;
    msg_len += (msg == NULL) ? 0 : strlen(msg);

    if (uname != NULL) {
        memcpy(buffer_opt, uname, OPT_UNAME_LEN);
    }

    if (msg_len != 0) {
        if (uname != NULL) {
            memcpy(buffer_opt + OPT_UNAME_LEN, &msg_len, OPT_MSGLEN_LEN);
        } else {
            memcpy(buffer_opt, &msg_len, OPT_MSGLEN_LEN);
        }
        memcpy(buffer_msg, msg, msg_len);
    }

    send(socket, buffer_head, HEADER_LEN, 0);
    send(socket, buffer_opt, opt_len, 0);
    if (msg_len != 0) {
        send(socket, buffer_msg, msg_len, 0);
    }
}


void recv_msg_by_sock(int socket, char* uname, char* buffer_msg) {

    char buffer_opt[OPT_UNAME_LEN + OPT_MSGLEN_LEN];
    memset(buffer_opt, 0, OPT_UNAME_LEN + OPT_MSGLEN_LEN);
    if (buffer_msg != NULL) {
        memset(buffer_msg, 0, MSG_MAXLEN);
    }

    int opt_len = 0;
    short int msg_len = 0;

    opt_len += (uname == NULL) ? 0 : OPT_UNAME_LEN;
    opt_len += (buffer_msg == NULL) ? 0 : OPT_MSGLEN_LEN;
    msg_len += (buffer_msg == NULL) ? 0 : MSG_MAXLEN;

    readn(socket, buffer_opt, opt_len);
    if (uname != NULL) {
        memcpy(uname, buffer_opt, OPT_UNAME_LEN);
        memcpy(&msg_len, buffer_opt + OPT_UNAME_LEN, OPT_MSGLEN_LEN);
    } else {
        memcpy(&msg_len, buffer_opt, OPT_MSGLEN_LEN);
    }

    if (msg_len != 0) {
        readn(socket, buffer_msg, msg_len);
    }
}

void* inc_msg(void* arg) {

    int sock_serv = *((int*)arg);
    char buffer_opt[OPT_UNAME_LEN + OPT_MSGLEN_LEN];
    char buffer_msg[MSG_MAXLEN];
    char uname[OPT_UNAME_LEN];

    recv_msg_by_sock(sock_serv, uname, buffer_msg);


    printf("%s > [ %s ]\n\n", uname, buffer_msg);

    return NULL;
}

void* err_msg(void* arg) {
    int sock_serv = *((int*)arg);
    char buffer_msg[MSG_MAXLEN];

    recv_msg_by_sock(sock_serv, NULL, buffer_msg);

    printf("ERROR: [ %s ]\n\n", buffer_msg);

    return NULL;
}

void* auth_me(void* arg) {
    int rc;

    char header[HEADER_LEN] = HDR_CLI_AUTHORIZE;
    char input_name[OPT_UNAME_LEN];
    memset(input_name, 0, OPT_UNAME_LEN);

    std::cin.getline(input_name, OPT_UNAME_LEN);
    std::cin.clear();

    send_msg_by_sock(sockfd, header, input_name, NULL);

    return NULL;
}

void* disc_me(void* arg) {
    int rc;
    char header[HEADER_LEN] = HDR_CLI_DISCONNECT;

    TerminateThread(h_wait_for_recv, NULL);
    WaitForSingleObject (h_wait_for_recv, INFINITE);
    send_msg_by_sock(sockfd, header, NULL, NULL);

    shutdown(sockfd, SD_BOTH);
    closesocket(sockfd);

    int* rc_disc = new int(RC_CLIENT_CLOSING);
    return (void*)rc_disc;
}

void* close_client(void* arg) {
    printf("\nclosing...\n");

    void* rc = disc_me(NULL);
    return rc;
}

void* send_msg(void* arg) {
    
    char buffer_head[HEADER_LEN] = HDR_CLI_RETRANSMIT_MESSAGE;
    char input_name[OPT_UNAME_LEN];
    char input_msg[MSG_MAXLEN];
    memset(input_name, 0, OPT_UNAME_LEN);
    memset(input_msg, 0, MSG_MAXLEN);
    std::cin.getline(input_name, OPT_UNAME_LEN + OPT_MSGLEN_LEN, ' ');
    std::cin.getline(input_msg, MSG_MAXLEN);
    std::cin.clear();

    send_msg_by_sock(sockfd, buffer_head, input_name, input_msg);

    return NULL;
}

void* bc_msg(void* arg) {
    char buffer_head[HEADER_LEN] = HDR_CLI_BROADCAST_MESSAGE;
    char input_msg[MSG_MAXLEN];
    memset(input_msg, 0, MSG_MAXLEN);
    std::cin.getline(input_msg, MSG_MAXLEN);
    std::cin.clear();

    send_msg_by_sock(sockfd, buffer_head, NULL, input_msg);

    return NULL;
}

DWORD WINAPI wait_for_recv(CONST LPVOID arg) {

    int newsockfd = *(int*)arg;

    ssize_t rc;
    char buffer[HEADER_LEN];

    while(1) {

        memset(buffer, 0, HEADER_LEN);
        rc = readn(newsockfd, buffer, HEADER_LEN);

        if (rc <= 0) {
            shutdown(sockfd, SD_BOTH);
            closesocket(sockfd);
            std::cout << "connection lost\n";

            TerminateThread(h_listen_cmd, NULL);
            CloseHandle(overlapped.hEvent);
            CloseHandle(h_listen_cmd);
            WaitForSingleObject (h_listen_cmd, INFINITE);

            ExitThread(NULL);
        }

        std::string header(buffer);
        if (handlers.find(header) != handlers.end()) {
            handlers[header]((void*)&newsockfd);
        }
    }

    return NULL;
}

DWORD WINAPI listen_cmd(CONST LPVOID arg) {

    std::map<std::string, void*(*)(void*)> handlers_cmd;

    handlers_cmd.insert(fname_function(std::string(CMD_EXIT), &close_client));
    handlers_cmd.insert(fname_function(std::string(CMD_DISCONNECT_ME), &disc_me));
    handlers_cmd.insert(fname_function(std::string(CMD_AUTHORIZE_ME), &auth_me));
    handlers_cmd.insert(fname_function(std::string(CMD_SEND_MESSAGE), &send_msg));
    handlers_cmd.insert(fname_function(std::string(CMD_BC_MESSAGE), &bc_msg));

    while(1) {

        std::string input;
        std::cin >> input;
        std::cin.get();

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
    
    int n;
    WSADATA wsaData;
    n = WSAStartup(MAKEWORD(2,2), &wsaData);
    if (n != 0) {
        printf("WSAStartup failed: %d\n", n);
        return 1;
    }

    handlers.insert(fname_function(std::string(HDR_SRV_INCOMING_MESSAGE), &inc_msg)); // incoming message
    handlers.insert(fname_function(std::string(HDR_SRV_ERROR), &err_msg));            // error message

    uint16_t portno;
    struct sockaddr_in serv_addr;
    struct hostent *server;

    if (argc < 3) {
        fprintf(stderr, "usage %s hostname port\n", argv[0]);
        exit(0);
    }

    portno = (uint16_t) atoi(argv[2]);

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

    memset((char *) &serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    memcpy((char *) &serv_addr.sin_addr.s_addr, server->h_addr, (size_t) server->h_length);
    serv_addr.sin_port = htons(portno);

    if (connect(sockfd, (struct sockaddr *) &serv_addr, sizeof(serv_addr)) < 0) {
        perror("ERROR connecting");
        exit(1);
    }

    h_wait_for_recv = CreateThread(
            NULL, NULL, &wait_for_recv, (void*)(&sockfd), NULL, &thread_wait_for_recv);
    h_listen_cmd = CreateThread(
            NULL, NULL, &listen_cmd, (void*)(&sockfd), NULL, &thread_listen_cmd);

    printf("\nType 'exit' to end the program\n\n");
    WaitForSingleObject (h_listen_cmd, INFINITE);
    TerminateThread(h_wait_for_recv, NULL);
    WaitForSingleObject (h_wait_for_recv, INFINITE);

    shutdown(sockfd, SD_BOTH);
    closesocket(sockfd);
    WSACleanup();

    return 0;
}
