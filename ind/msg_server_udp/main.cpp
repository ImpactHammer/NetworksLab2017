#include <iostream>
#include <stdio.h>
#include <stdlib.h>
#include <winsock2.h>
#include <windows.h>

#include <string.h>
#include <set>
#include <map>

#define HEADER_LEN      8   // length of [header]
#define OPT_USERNAME_LEN   16  // length of {username} in [options]
#define OPT_MSGLEN_LEN  2   // length of {message length} in [options]
#define MSG_MAXLEN      256 // maximum length of [message]
#define MSG_ID_MINVALUE 0   // minimum value of message id
#define MSG_ID_STEP     1   // incremental step of message id
#define MSG_ID_LEN      sizeof(unsigned int)
#define MSG_DATA_MAXLEN MSG_MAXLEN - MSG_ID_LEN - OPT_USERNAME_LEN - OPT_MSGLEN_LEN

#define PCG_MAXLEN      512 // maximum packege length

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
#define ERR_PACKAGE_LOSS        "package loss"

#define LOGIN_MAXLEN 256

#define ARGC 3

typedef unsigned int uint;

typedef std::pair<std::string, void*(*)(void*)> fname_function;
typedef std::pair<struct sockaddr, uint> addr_msgid_pair;
typedef std::pair<std::string, sockaddr> username_addr_pair;
typedef std::pair<sockaddr, std::string> addr_username_pair;
typedef std::pair<sockaddr, uint> addr_msgid_incoming_pair;
typedef std::pair<sockaddr, uint> addr_msgid_outgoing_pair;


std::map<std::string, void*(*)(void*)> handlers;        // key: msg header ; value: handler
std::map<std::string, sockaddr> username_addr;                  // key: username   ; value: address
std::map<sockaddr, std::string> addr_username;                  // key: address    ; value: username
std::map<struct sockaddr, uint> addr_msgid_incoming;    // key: client addr; value: message id
std::map<struct sockaddr, uint> addr_msgid_outgoing;    // key: client addr; value: message id


/* struct containing message string and an address of it's sender */
struct addr_message {
    sockaddr addr_sender;
    char* message;

    addr_message() {}

    addr_message(sockaddr addr_sender, char* message) {
        this->addr_sender = addr_sender;
        this->message = message;
    }
};


struct sockets {
    int listener;
    std::set<int> accepted;
};


struct sockets sockets;

struct sockaddr_in serv_addr;

std::set<std::string> valid_logins;

DWORD thread_get_header;
DWORD thread_listen_cmd;

HANDLE h_get_header;
HANDLE h_listen_cmd;

/* to make struct sockadr usable as a key in hash table */
bool operator <(const struct sockaddr addr1, const struct sockaddr addr2) {
    if (addr1.sa_family < addr2.sa_family) {
        return true;
    } else if (addr1.sa_family > addr2.sa_family) {
        return false;
    } else {
        for (int i = 0; i < 14; i = i + 1) {
            if ((addr1.sa_data)[i] < (addr2.sa_data)[i]) {
                return true;
            } else if ((addr1.sa_data)[i] > (addr2.sa_data)[i]) {
                return false;
            }
        }
    }
    return false;
}

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

void err_msg(sockaddr addr_recver, char* data) {

    char message[PCG_MAXLEN];
    uint msg_id = addr_msgid_outgoing[addr_recver];
    char header[HEADER_LEN] = HDR_SRV_ERROR;
    compile_message(message, &msg_id, header, NULL, data);

    sendto(sockets.listener, message, MSG_ID_LEN + HEADER_LEN + OPT_USERNAME_LEN + strlen(data),
           0, &addr_recver, sizeof(addr_recver));
    addr_msgid_outgoing[addr_recver] += MSG_ID_STEP;
}


void* send_msg(void* arg) {     // msg to 1 client

    char username[OPT_USERNAME_LEN];
    char data[MSG_MAXLEN];
    memset(username, 0, OPT_USERNAME_LEN);
    memset(data, 0, MSG_MAXLEN);
    std::cin.getline(username, OPT_USERNAME_LEN, ' ');
    std::cin.getline(data, MSG_MAXLEN);
    std::cin.clear();

    if (username_addr.find(std::string(username)) != username_addr.end()) {
        char message[PCG_MAXLEN];
        sockaddr addr_recver = username_addr[std::string(username)];
        uint msg_id = addr_msgid_outgoing[addr_recver];
        char header[HEADER_LEN] = HDR_SRV_INCOMING_MESSAGE;
        compile_message(message, &msg_id, header, USERNAME_SERVER, data);

        sendto(sockets.listener, message, MSG_ID_LEN + HEADER_LEN + OPT_USERNAME_LEN + strlen(data),
               0, &addr_recver, sizeof(addr_recver));
        addr_msgid_outgoing[addr_recver] += MSG_ID_STEP;
    } else {
        std::cout << ERR_USER_IS_OFFLINE << "\n";
    }

    return NULL;
}


void* send_broadcast_msg(void* arg) {  // msg to all clients

    char data[MSG_MAXLEN];
    memset(data, 0, MSG_MAXLEN);
    std::cin.getline(data, MSG_MAXLEN);
    std::cin.clear();

    char message[PCG_MAXLEN];
    char header[HEADER_LEN] = HDR_SRV_INCOMING_MESSAGE;

    std::map<addr_msgid_outgoing_pair::first_type, addr_msgid_outgoing_pair::second_type>::iterator it;
    for (it = addr_msgid_outgoing.begin(); it != addr_msgid_outgoing.end(); it++) {
        sockaddr addr_recver = (*it).first;
        uint msg_id_outgoing = (*it).second;
        compile_message(message, &msg_id_outgoing, header, USERNAME_SERVER, data);

        sendto(sockets.listener, message, MSG_ID_LEN + HEADER_LEN + OPT_USERNAME_LEN + strlen(data),
               0, &addr_recver, sizeof(addr_recver));
        addr_msgid_outgoing[addr_recver] += MSG_ID_STEP;
    }

    return NULL;
}


void* auth_client(void* arg) {

    struct addr_message addr_message = *((struct addr_message*)arg);
    sockaddr addr_sender = addr_message.addr_sender;
    char* message = addr_message.message;

    char username_sender[OPT_USERNAME_LEN];
    get_username_from_message(username_sender, message);

    if (valid_logins.find(std::string(username_sender)) == valid_logins.end()) {
        err_msg(addr_sender, ERR_INVALID_USERNAME);
        return NULL;
    }

    std::cout << "user " << username_sender << " is online\n";

    if (addr_username.find(addr_sender) != addr_username.end()) {
        username_addr.erase(addr_username[addr_sender]);
        addr_username.erase(addr_sender);
    }
    if (username_addr.find(std::string(username_sender)) != username_addr.end()) {
        addr_username.erase(username_addr[std::string(username_sender)]);
        username_addr.erase(std::string(username_sender));
    }

    username_addr.insert(username_addr_pair(std::string(username_sender), addr_sender));
    addr_username.insert(addr_username_pair(addr_sender, std::string(username_sender)));

    return NULL;
}


void* retransmit_msg(void* arg) {

    struct addr_message addr_message_inst = *((struct addr_message*)arg);
    sockaddr addr_sender = addr_message_inst.addr_sender;
    char* message = addr_message_inst.message;

    char username_recver[OPT_USERNAME_LEN];
    memset(username_recver, 0, OPT_USERNAME_LEN);

    get_username_from_message(username_recver, message);

    sockaddr addr_recver;
    if (username_addr.find(std::string(username_recver)) != username_addr.end()) {
        addr_recver = username_addr[std::string(username_recver)];
    } else {
        err_msg(addr_sender, ERR_USER_IS_OFFLINE);
        return NULL;
    }

    uint msg_id = addr_msgid_outgoing[addr_recver];
    char header[HEADER_LEN] = HDR_SRV_INCOMING_MESSAGE;
    char username_sender[OPT_USERNAME_LEN];
    memset(username_sender, 0, OPT_USERNAME_LEN);
    char data[MSG_MAXLEN];
    memset(data, 0, MSG_MAXLEN);

    std::string username_sender_string;
    if (addr_username.find(addr_sender) != addr_username.end()) {
        username_sender_string = addr_username[addr_sender];
    } else {
        username_sender_string = USERNAME_UNKNOWN;
    }

    memcpy(username_sender, username_sender_string.c_str(), username_sender_string.length());
    get_data_from_message(data, message);
    compile_message(message, &msg_id, header, username_sender, data);

    sendto(sockets.listener, message, MSG_ID_LEN + HEADER_LEN + OPT_USERNAME_LEN + strlen(data),
           0, &addr_recver, sizeof(addr_recver));
    addr_msgid_outgoing[addr_recver] += MSG_ID_STEP;

    return NULL;
}

void* retransmit_broadcast_msg(void* arg) {

    struct addr_message addr_message_inst = *((struct addr_message*)arg);
    sockaddr addr_sender = addr_message_inst.addr_sender;
    char* message = addr_message_inst.message;

    char header[HEADER_LEN] = HDR_SRV_INCOMING_MESSAGE;
    char username_sender[OPT_USERNAME_LEN];
    memset(username_sender, 0, OPT_USERNAME_LEN);
    char data[MSG_MAXLEN];
    memset(data, 0, MSG_MAXLEN);

    std::string username_sender_string;
    if (addr_username.find(addr_sender) != addr_username.end()) {
        username_sender_string = addr_username[addr_sender];
    } else {
        username_sender_string = USERNAME_UNKNOWN;
    }

    memcpy(username_sender, username_sender_string.c_str(), username_sender_string.length());
    get_data_from_message(data, message);

    std::map<addr_msgid_outgoing_pair::first_type, addr_msgid_outgoing_pair::second_type>::iterator it;
    for (it = addr_msgid_outgoing.begin(); it != addr_msgid_outgoing.end(); it++) {
        sockaddr addr_recver = (*it).first;
        uint msg_id_outgoing = (*it).second;
        compile_message(message, &msg_id_outgoing, header, username_sender, data);

        sendto(sockets.listener, message, MSG_ID_LEN + HEADER_LEN + OPT_USERNAME_LEN + strlen(data),
               0, &addr_recver, sizeof(addr_recver));
        addr_msgid_outgoing[addr_recver] += MSG_ID_STEP;
    }
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

    int* rc = new int(RC_SERVER_CLOSING);
    return (void*)rc;
}

void package_loss (sockaddr recver_addr) {
    printf("ERROR: [ %s ]\n\n", ERR_PACKAGE_LOSS);
    err_msg(recver_addr, ERR_PACKAGE_LOSS);
}


DWORD WINAPI get_header (void* arg) {

    handlers.insert(fname_function(std::string(HDR_CLI_AUTHORIZE), &auth_client));         // request for authorization
    handlers.insert(fname_function(std::string(HDR_CLI_RETRANSMIT_MESSAGE), &retransmit_msg));    // request for msg retransmission
    handlers.insert(fname_function(std::string(HDR_CLI_BROADCAST_MESSAGE), &retransmit_broadcast_msg));     // request for broadcast msg retransmission

    int newsockfd;
    int sockfd = sockets.listener;
    int cli_addr_len;
    struct sockaddr cli_addr;
    cli_addr_len = sizeof(cli_addr);

    char buffer[PCG_MAXLEN];
    unsigned int msg_id;
    char header[HEADER_LEN];
    struct addr_message addr_message;

    while (1) {

        memset(buffer, 0, PCG_MAXLEN);
        recvfrom(sockets.listener, buffer, PCG_MAXLEN, 0, &cli_addr, &cli_addr_len);

        get_id_from_message(&msg_id, buffer);

        if (addr_msgid_incoming.find(cli_addr) == addr_msgid_incoming.end()) {

            if (msg_id != MSG_ID_MINVALUE) {
                addr_msgid_incoming.insert(addr_msgid_incoming_pair(cli_addr, msg_id));

                std::cout << "[warning] package loss\n";

            } else {
                addr_msgid_incoming.insert(addr_msgid_incoming_pair(cli_addr, MSG_ID_MINVALUE));
            }
            addr_msgid_outgoing.insert(addr_msgid_incoming_pair(cli_addr, MSG_ID_MINVALUE));
        } else {
            uint curr_id = addr_msgid_incoming[cli_addr];
            if (msg_id != curr_id + MSG_ID_STEP) {
                addr_msgid_incoming[cli_addr] = msg_id;

                std::cout << "[warning] package loss\n";
            } else {
                addr_msgid_incoming[cli_addr] += MSG_ID_STEP;
            }
        }
        get_header_from_message(header, buffer);

        if (handlers.find(header) != handlers.end()) {
            addr_message.addr_sender = cli_addr;
            addr_message.message = buffer;
            handlers[header]((void*)&addr_message);
        }
    }
}


DWORD WINAPI listen_cmd(void* arg) {

    std::map<std::string, void*(*)(void*)> handlers_cmd;

    handlers_cmd.insert(fname_function(std::string(CMD_EXIT), &close_server));
    handlers_cmd.insert(fname_function(std::string(CMD_SEND_MESSAGE), &send_msg));
    handlers_cmd.insert(fname_function(std::string(CMD_BROADCAST_MESSAGE), &send_broadcast_msg));
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
    
    WSADATA wsaData;
    int n = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (n != 0) {
        printf("WSAStartup failed: %d\n", n);
        return 1;
    }
    
    int sockfd;
    uint16_t portno;

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

    sockfd = socket(AF_INET, SOCK_DGRAM, 0);

    if (sockfd < 0) {
        perror("ERROR opening socket");
        exit(1);
    }

    memset((char *) &serv_addr, 0, sizeof(serv_addr));

    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = INADDR_ANY;
    serv_addr.sin_port = htons(portno);

    if (bind(sockfd, (struct sockaddr *) &serv_addr, sizeof(serv_addr)) < 0) {
        perror("ERROR on binding");
        exit(1);
    }

    sockets.listener = sockfd;
    bind(sockets.listener, (struct sockaddr*)&serv_addr, sizeof(serv_addr));

    h_get_header = CreateThread(
            NULL, NULL, &get_header, (void*) (&sockfd), NULL, &thread_get_header);
    h_listen_cmd = CreateThread(
            NULL, NULL, &listen_cmd, (void*) (&sockfd), NULL, &thread_listen_cmd);

    printf("\nserver running\n");
    printf("\nType 'exit' to end the program\n\n");
    WaitForSingleObject(h_listen_cmd, INFINITE);
    TerminateThread(h_get_header, NULL);
    WaitForSingleObject(h_get_header, INFINITE);

    std::set<int>::iterator it;
    for (it = sockets.accepted.begin(); it != sockets.accepted.end(); it++) {
        shutdown(*it, SD_BOTH);
        closesocket(*it);
    }

    shutdown(sockfd, SD_BOTH);
    closesocket(sockfd);
    WSACleanup();

    return 0;
}
