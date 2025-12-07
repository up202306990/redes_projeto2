#include <stdio.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <netdb.h>

#define FTP_PORT 21

#define SV_WELCOME                  220
#define SV_PASSWORD_READY           331
#define SV_LOGIN_SUCCESS            230
#define SV_PASSIVE                  227
#define SV_START_TRANSFER           150
#define SV_START_TRANSFER_CONNECTED 125
#define SV_TRANSFER_COMPLETE        226
#define SV_GOODBYE                  221

#define SV_CODE_REGEX           "%d"
#define PASSIVE_REGEX           "%*[^(](%d,%d,%d,%d,%d,%d)%*[^\n]"

#define DEFAULT_USER            "anonymous"
#define DEFAULT_PASS            "anonymous"

typedef struct {
    char host[512];
    char user[17];
    char pass[17];
    char path[512];
    char file[512];
    char ip[512];
} URL;


int parse_url(const char* ch_url, URL* url);

int create_socket(char* ip, int port);

int read_response(const int socket, char* outbuf);

int authentication(const int socket, const char* user, const char* pass);

int enter_passive_mode(const int socket, char* ip, int* port);

int request_transfer(const int socket, char* path);

int get_file(const int socketA, const int socketB, char* filename);

int close_connection (const int socketA);
