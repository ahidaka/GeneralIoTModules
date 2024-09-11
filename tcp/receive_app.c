#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <fcntl.h>

#define PORT 4444
#define BUFFER_SIZE 4096
#define OUTPUT_FILE "/tmp/data"

int main() {
    int server_sock, client_sock;
    struct sockaddr_in server_addr, client_addr;
    socklen_t client_addr_len = sizeof(client_addr);
    char buffer[BUFFER_SIZE];
    ssize_t received_len;
    int output_fd;
    ssize_t total_received = 0;

    // ソケットの作成
    server_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (server_sock < 0) {
        perror("Error creating socket");
        return 1;
    }

    // ソケットアドレスの設定
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(PORT);

    // ソケットのバインド
    if (bind(server_sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("Error binding socket");
        close(server_sock);
        return 1;
    }

    while (1) {
        // 接続待ち状態にする
        if (listen(server_sock, 1) < 0) {
            perror("Error listening on socket");
            close(server_sock);
            return 1;
        }

        printf("Waiting for a connection on port %d...\n", PORT);

        // クライアントからの接続を受け入れる
        client_sock = accept(server_sock, (struct sockaddr *)&client_addr, &client_addr_len);
        if (client_sock < 0) {
            perror("Error accepting connection");
            close(server_sock);
            return 1;
        }

        printf("Client connected.\n");

        // 出力ファイルのオープン
        output_fd = open(OUTPUT_FILE, O_WRONLY | O_CREAT | O_TRUNC, 0666);
        if (output_fd < 0) {
            perror("Error opening output file");
            close(client_sock);
            close(server_sock);
            return 1;
        }

        // データの受信ループ
        while ((received_len = recv(client_sock, buffer, BUFFER_SIZE, 0)) > 0) {
            printf("Received %zd bytes\n", received_len);
            total_received += received_len;

            // データをファイルに書き込む
            if (write(output_fd, buffer, received_len) != received_len) {
                perror("Error writing to file");
                break;
            }
        }

        if (received_len < 0) {
            perror("Error receiving data");
        } else {
            printf("Connection closed by client. Total received: %zd bytes\n", total_received);
        }

        // ソケットとファイルを閉じる
        close(client_sock);
        close(output_fd);
        printf("End of loop\n");
    }

    close(server_sock);

    return 0;
}
