#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <fcntl.h>
#include <errno.h>

#define PORT 8808
//#define BUFFER_SIZE (4 * 1024 * 1024 * 1024UL) // 4GB
#define BUFFER_SIZE (32 * 1024UL) // 32KB
#define PAGE_SIZE 4096
#define TIMEOUT 5 // 5 seconds

void error_handling(const char *message) {
    perror(message);
    exit(1);
}

int main() {
    int sock;
    struct sockaddr_in serv_addr, clnt_addr;
    socklen_t clnt_addr_size;
    int total_length = 0;

    char *buffer = (char *)malloc(BUFFER_SIZE);
    if (buffer == NULL) {
        error_handling("malloc() error");
    }

    sock = socket(PF_INET, SOCK_DGRAM, 0);
    if (sock == -1) {
        error_handling("socket() error");
    }

    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    serv_addr.sin_port = htons(PORT);

    if (bind(sock, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) == -1) {
        error_handling("bind() error");
    }

    struct timeval timeout;
    timeout.tv_sec = TIMEOUT;
    timeout.tv_usec = 0;

    if (setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) == -1) {
        error_handling("setsockopt() error");
    }

    while (1) {
        int str_len;
        int received_bytes = 0;
        char receive_buffer[PAGE_SIZE];       
        clnt_addr_size = sizeof(clnt_addr);

        do {
            memset(buffer, 0, PAGE_SIZE);
            str_len = recvfrom(sock, receive_buffer, PAGE_SIZE, 0, 
                                   (struct sockaddr*)&clnt_addr, &clnt_addr_size);
            if (str_len == -1) {
                if (errno == EWOULDBLOCK) {
                    printf("Timeout reached. Writing data to file...\n");
                } else {
                    error_handling("recvfrom() error");
                }
                printf("Receive error %d bytes\n", errno);
                break;
            }
            received_bytes = str_len;
            memcpy(receive_buffer + received_bytes, buffer,  str_len);
            total_length += str_len;
            printf("Received %d/%d bytes\n", received_bytes, total_length);
            if (received_bytes >= BUFFER_SIZE) {
                printf("Buffer full. Writing data to file...\n");
                break;
            }
        } while (received_bytes < BUFFER_SIZE);

        if (str_len == -1 || received_bytes >= BUFFER_SIZE) {

            printf("Now write data to file size = %d\n", total_length);

            int fd = open("/tmp/data", O_WRONLY | O_CREAT | O_TRUNC, 0644);
            if (fd == -1) {
                error_handling("open() error");
            }

            if (write(fd, receive_buffer, total_length) == -1) {
                error_handling("write() error");
            }

            close(fd);


            //memset(buffer, 0, BUFFER_SIZE);
            //total_length = 0;
            //printf("Buffer cleared. Waiting for new data...\n");
        }
    }

    close(sock);
    free(buffer);
    return 0;
}
