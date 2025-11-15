#include "application.h"

int parse_url(const char* ch_url, URL* url) {

    if (strchr(ch_url, '/') == NULL) return -1;

    if(strchr (ch_url, '@') == NULL) {
        sscanf(ch_url, DEFAULT_HOST_REGEX, url->host);
        strcpy(url->user, DEFAULT_USER);
        strcpy(url->pass, DEFAULT_PASS);
    }

    else {
        sscanf(ch_url, SPECIFIC_HOST_REGEX, url->host);
        sscanf(ch_url, USER_REGEX, url->user);
        sscanf(ch_url, PASS_REGEX, url->pass);
    }

    sscanf(ch_url, PATH_REGEX, url->path);
    strcpy(url->file, strrchr(ch_url, '/') + 1);

    struct hostent *h;
    if(strlen(url->host) == 0) return -1;

    if ((h = gethostbyname(url->host)) == NULL) {
        herror("gethostbyname()");
        exit(-1);
    }

    strcpy(url->ip, inet_ntoa(*((struct in_addr *) h->h_addr_list[0])));

    return 0;
}

int create_socket(char* ip, int port) {

    int sockfd;
    struct sockaddr_in server_addr;

    bzero((char *) &server_addr, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = inet_addr(ip);
    server_addr.sin_port = htons(port);

    if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("socket()");
        exit(-1);
    }

    if (connect(sockfd, (struct sockaddr *) &server_addr, sizeof(server_addr)) < 0) {
        perror("connect()");
        exit(-1);
    }

    return sockfd;
}

int read_response(const int socket, char* buffer) {
    
    char byte;
    int index = 0, responseCode;
    ResponseState state = START;
    memset(buffer, 0, 512);

    while (state != END) {
        read(socket, &byte, 1);

        switch (state) {
            case START:
                if (byte == ' ') state = SINGLE_LINE;
                if (byte == '-') state = MULTI_LINE;
                if (byte == '\n') state = END;
                else buffer[index++] = byte;
                break;
            case SINGLE_LINE:
                if(byte == '\n') state = END;
                else buffer[index++] = byte;
                break;
            case MULTI_LINE:
                if(byte == '\n') {
                    memset(buffer, 0, 512);
                    state = START;
                    index = 0;
                }
                else buffer[index++] = byte;
                break;
            case END:
                break;
            default:
                break;
        }
    }

    sscanf(buffer, SV_CODE_REGEX, &responseCode);
    return responseCode;
}

int authentication(const int socket, const char* user, const char* pass) {

    char inputUser[5 + strlen(user) + 1];
    sprintf(inputUser, "USER %s\n", user);

    char inputPass[5 + strlen(pass) + 1];
    sprintf(inputPass, "PASS %s\n", pass);

    char answer[512];

    write(socket, inputUser, strlen(inputUser));
    if(read_response (socket, answer) != SV_PASSWORD_READY) {
        printf("Unknown user '%s'. Abort.\n", user);
        exit(-1);
    }

    write(socket, inputPass, strlen(inputPass));
    return read_response(socket, answer);
}

int enter_passive_mode(const int socket, char* ip, int* port) {

    char answer[512];
    int ip1, ip2, ip3, ip4, port1, port2;

    write(socket, "pasv\n", 5);
    if(read_response(socket, answer) != SV_PASSIVE) return -1;

    sscanf(answer, PASSIVE_REGEX, &ip1, &ip2, &ip3, &ip4, &port1, &port2);
    *port = port1 * 256 + port2;
    sprintf(ip, "%d.%d.%d.%d", ip1, ip2, ip3, ip4);

    return SV_PASSIVE;
}

int request_transfer(const int socket, char* path) {
    
    char inputFile[5 + strlen(path) + 1];
    sprintf(inputFile, "retr %s\n", path);

    char answer[512];

    write(socket, inputFile, strlen(inputFile));
    return read_response(socket, answer);
}

int get_file(const int socketA, const int socketB, char* filename) {
    
    FILE *fd = fopen(filename, "wb");
    if(fd == NULL) {
        perror("Error opening or creating file");
        exit(-1);
    }

    char buffer[512];
    int bytes;

    do {
        bytes = read(socketB, buffer, 512);
        if(fwrite(buffer, bytes, 1, fd) < 0) return -1;
    } while (bytes);

    fclose(fd);

    return read_response(socketA, buffer);
}

int close_connection (const int socketA, const int socketB) {
    
    char answer[512];
    write(socketA, "quit\n", 5);
    if(read_response(socketA, answer) != SV_GOODBYE) return -1;

    if (close(socketA) < 0) return -1;
    if (close(socketB) < 0) return -1;
    return 0;
}

int main(int argc, char* argv[]) {

    if(argc != 2) {
        fprintf(stderr, "Usage: ./application ftp://[<user>:<password>@]<host>/<url-path>\n");
        exit(-1);
    }

    URL url;

    memset(&url, 0, sizeof(url));
    if(parse_url(argv[1], &url) != 0) {
        fprintf(stderr, "Parse error. Usage: ./application ftp://[<user>:<password>@]<host>/<url-path>\n");
        exit(-1);
    }

    printf("Host: %s\n"
           "User: %s\n"
           "Password: %s\n"
           "Path: %s\n"
           "File: %s\n"
           "IP Address: %s\n",
           url.host, url.user, url.pass, url.path, url.file, url.ip
    );

    char answer[512];
    int socketA = create_socket(url.ip, FTP_PORT);
    if(socketA < 0 || read_response(socketA, answer) != SV_WELCOME) {
        fprintf(stderr, "Socket to %s and port %d failed\n", url.ip, FTP_PORT);
        exit(-1);
    }

    if(authentication(socketA, url.user, url.pass) != SV_LOGIN_SUCCESS) {
        fprintf(stderr, "Authentication failed with user %s and password %s\n", url.user, url.pass);
        exit(-1);
    }

    char ip[512];
    int port;

    if(enter_passive_mode(socketA, ip, &port) != SV_PASSIVE) {
        fprintf(stderr, "Couldn't enter passive mode.\n");
        exit(-1);
    }

    int socketB = create_socket(ip, port);
    if(socketB < 0) {
        fprintf(stderr, "Socket to %s and port %d failed\n", ip, port);
        exit(-1);
    }

    if(request_transfer(socketA, url.path) != SV_START_TRANSFER) {
        fprintf(stderr, "Path %s doesn't exist in %s:%d\n", url.path, ip, port);
        exit(-1);
    }

    if(get_file(socketA, socketB, url.file) != SV_TRANSFER_COMPLETE) {
        fprintf(stderr, "Error transfering file %s from %s:%d\n", url.file, ip, port);
        exit(-1);
    }

    if(close_connection(socketA, socketB) != 0) {
        fprintf(stderr, "Error when closing sockets\n");
        exit(-1);
    }
    return 0;
}

