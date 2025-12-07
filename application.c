#include "application.h"

int parse_url(const char* ch_url, URL* url) {
    if (!ch_url || strlen(ch_url) == 0) return -1;

    const char* p = ch_url;
    if (strncmp(p, "ftp://", 6) == 0) p += 6;

    const char* at_ptr = strchr(p, '@');
    const char* slash_ptr = strchr(p, '/');

    if (!slash_ptr) return -1;

    strcpy(url->user, DEFAULT_USER);
    strcpy(url->pass, DEFAULT_PASS);

    if (at_ptr && at_ptr < slash_ptr) {
        const char* colon_ptr = strchr(p, ':');
        if (colon_ptr && colon_ptr < at_ptr) {
            strncpy(url->user, p, colon_ptr - p);
            url->user[colon_ptr - p] = '\0';
            strncpy(url->pass, colon_ptr + 1, at_ptr - colon_ptr - 1);
            url->pass[at_ptr - colon_ptr - 1] = '\0';
        } else {

            strncpy(url->user, p, at_ptr - p);
            url->user[at_ptr - p] = '\0';
        }
        p = at_ptr + 1;
    }

    strncpy(url->host, p, slash_ptr - p);
    url->host[slash_ptr - p] = '\0';

    strcpy(url->path, slash_ptr + 1);

    const char* last_slash = strrchr(url->path, '/');
    if (last_slash)
        strcpy(url->file, last_slash + 1);
    else
        strcpy(url->file, url->path);

    struct hostent* h;
    if (strlen(url->host) == 0) return -1;

    if ((h = gethostbyname(url->host)) == NULL) {
        herror("gethostbyname()");
        return -1;
    }

    strcpy(url->ip, inet_ntoa(*((struct in_addr*)h->h_addr_list[0])));

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
    char line[512];
    int responseCode = 0;

    while (1) {
        int byte = 0;
        int idx = 0;
        char c;
        memset(line, 0, sizeof(line));

        while ((byte = read(socket, &c, 1)) > 0) {
            if (c == '\r') continue;
            if (c == '\n') break;
            if (idx < sizeof(line) - 1) line[idx++] = c;
        }
        line[idx] = '\0';

        if (byte <= 0) {
            printf("[DEBUG] Socket closed or read error\n");
            return -1;
        }

        strcpy(buffer, line);
        printf("[DEBUG] Full response: '%s'\n", buffer);

        if (sscanf(line, "%d", &responseCode) != 1) {
            responseCode = -1;
        }

        if (strlen(line) >= 4 && line[3] == '-') {
            int code = responseCode;
            do {
                idx = 0;
                memset(line, 0, sizeof(line));
                while ((byte = read(socket, &c, 1)) > 0) {
                    if (c == '\r') continue;
                    if (c == '\n') break;
                    if (idx < sizeof(line) - 1) line[idx++] = c;
                }
                line[idx] = '\0';
                strcpy(buffer, line);
                printf("[DEBUG] Full response: '%s'\n", buffer);
            } while (!(strlen(line) >= 4 && line[3] == ' ' && atoi(line) == code));
        }

        break;
    }

    return responseCode;
}


int authentication(const int socket, const char* user, const char* pass) {

    char inputUser[512 + 5 + 2];
    snprintf(inputUser, sizeof(inputUser), "USER %s\r\n", user);

    char inputPass[512 + 5 + 2];
    sprintf(inputPass, "PASS %s\r\n", pass);

    char answer[512];

    write(socket, inputUser, strlen(inputUser));

    if(read_response(socket, answer) != SV_PASSWORD_READY) {
        printf("Unknown user '%s'. Abort.\n", user);
        exit(-1);
    }
    write(socket, inputPass, strlen(inputPass));
    
    return read_response(socket, answer);
}

int enter_passive_mode(const int socket, char* ip, int* port) {

    char answer[512];
    int ip1, ip2, ip3, ip4, port1, port2;

    write(socket, "PASV\r\n", 6);
    if(read_response(socket, answer) != SV_PASSIVE) return -1;

    sscanf(answer, PASSIVE_REGEX, &ip1, &ip2, &ip3, &ip4, &port1, &port2);
    *port = port1 * 256 + port2;
    sprintf(ip, "%d.%d.%d.%d", ip1, ip2, ip3, ip4);

    return SV_PASSIVE;
}

int request_transfer(const int socket, char* path) {
    
    char inputFile[5 + strlen(path) + 1];
    sprintf(inputFile, "retr %s\r\n", path);

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

    char buffer[4096];
    int bytes;

    while ((bytes = read(socketB, buffer, sizeof(buffer))) > 0) {
        if (fwrite(buffer, 1, bytes, fd) != bytes) {
            perror("fwrite");
            fclose(fd);
            return -1;
        }
    }

    if (bytes < 0) {
        perror("read");
        fclose(fd);
        return -1;
    }

    fclose(fd);
    if (close(socketB) < 0) return -1;

    char answer[512];
    int resp = read_response(socketA, answer);
    return resp;
}


int close_connection (const int socketA) {
    
    char answer[512];
    write(socketA, "QUIT\r\n", 6);
    if(read_response(socketA, answer) != SV_GOODBYE) return -1;

    if (close(socketA) < 0) return -1;
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

    printf("Host: '%s'\n"
           "User: %s\n"
           "Password: %s\n"
           "Path: %s\n"
           "File: %s\n"
           "IP Address: %s\n",
           url.host, url.user, url.pass, url.path, url.file, url.ip
    );

    char answer[512];
    int socketA = create_socket(url.ip, FTP_PORT);
    printf("[DEBUG] Connected to %s:%d\n", url.ip, FTP_PORT);

    if(socketA < 0 || read_response(socketA, answer) != SV_WELCOME) {
        fprintf(stderr, "Socket to %s and port %d failed\n", url.ip, FTP_PORT);
        exit(-1);
    }

    if(authentication(socketA, url.user, url.pass) != SV_LOGIN_SUCCESS) {
        fprintf(stderr, "Authentication failed with user %s and password %s\n", url.user, url.pass);
        exit(-1);
    }
    printf("[DEBUG] Authenticated as %s\n", url.user);


    char ip[512];
    int port;

    if(enter_passive_mode(socketA, ip, &port) != SV_PASSIVE) {
        fprintf(stderr, "Couldn't enter passive mode.\n");
        exit(-1);
    }
    printf("[DEBUG] Passive mode IP: %s Port: %d\n", ip, port);


    int socketB = create_socket(ip, port);
    if(socketB < 0) {
        fprintf(stderr, "Socket to %s and port %d failed\n", ip, port);
        exit(-1);
    }

    int resp = request_transfer(socketA, url.path);

    if (resp != SV_START_TRANSFER && resp != SV_START_TRANSFER_CONNECTED) {
        fprintf(stderr, "Path %s doesn't exist in %s:%d\n", url.path, ip, port);
        exit(-1);
    }

    if(get_file(socketA, socketB, url.file) != SV_TRANSFER_COMPLETE) {
        fprintf(stderr, "Error transfering file %s from %s:%d\n", url.file, ip, port);
        exit(-1);
    }

    if(close_connection(socketA) != 0) {
        fprintf(stderr, "Error when closing sockets\n");
        exit(-1);
    }

    return 0;
}

