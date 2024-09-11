#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/socket.h>
#include <linux/net.h>
#include <linux/in.h>
#include <linux/string.h>
#include <linux/vmalloc.h>
#include <linux/sysfs.h>
#include <linux/kobject.h>
#include <net/sock.h>
#include <linux/delay.h>
#include <linux/inet.h>

#define PORT 4444
#define BUFFER_SIZE (4 * 1024 * 1024 * 1024UL) // 4GB

static char *ip = "127.0.0.1";
module_param(ip, charp, 0644);
MODULE_PARM_DESC(ip, "Destination IP address");

static int debug = 0;
module_param(debug, int, 0644);
MODULE_PARM_DESC(debug, "Debug mode");

static char *data = NULL;
// module_param(data, charp, 0644); // これは不要になります
MODULE_PARM_DESC(data, "Data to send");

static char *buffer;
static struct socket *sock;

static int send_data(void)
{
    struct sockaddr_in saddr;
    int ret;
    struct msghdr msg;
    struct kvec iov;
    size_t data_len;
    unsigned char ipv4[4];

    // バッファのデータ長を計算
    data_len = strnlen(data, BUFFER_SIZE);
    if (data_len == 0) {
        printk(KERN_ERR "No data to send\n");
        return -EINVAL;
    }

    // IPアドレスの文字列をバイナリ形式に変換
    ret = in4_pton(ip, -1, ipv4, -1, NULL);
    if (ret == 0) {
        printk(KERN_ERR "Invalid IP address: %s\n", ip);
        return -EINVAL;
    }

    // ソケットの作成
    ret = sock_create_kern(&init_net, AF_INET, SOCK_STREAM, IPPROTO_TCP, &sock);
    if (ret < 0) {
        printk(KERN_ERR "Error creating socket: %d\n", ret);
        return ret;
    }

    // ソケットアドレス構造体の設定
    memset(&saddr, 0, sizeof(saddr));
    saddr.sin_family = AF_INET;
    memcpy(&saddr.sin_addr.s_addr, ipv4, sizeof(ipv4)); // IPアドレスの設定
    saddr.sin_port = htons(PORT); // ポート番号の設定

    // 接続
    ret = sock->ops->connect(sock, (struct sockaddr *)&saddr, sizeof(saddr), 0);
    if (ret < 0) {
        printk(KERN_ERR "Error connecting socket: %d\n", ret);
        sock_release(sock);
        return ret;
    }

    if (debug) {
        printk(KERN_INFO "Connected to %s:%d\n", ip, PORT);
    }

    // データの送信
    memset(&msg, 0, sizeof(msg));
    iov.iov_base = data;
    iov.iov_len = data_len;

    ret = kernel_sendmsg(sock, &msg, &iov, 1, data_len);
    if (ret < 0) {
        printk(KERN_ERR "Error sending data: %d\n", ret);
    } else if (debug) {
        printk(KERN_INFO "Sent %zu bytes of data\n", data_len);
    }

    // ソケットを閉じる
    sock_release(sock);
    return ret;
}

static int set_data_param(const char *val, const struct kernel_param *kp)
{
    int ret;

    // データ設定
    ret = param_set_charp(val, kp);
    if (ret < 0)
        return ret;

    // 送信処理の開始
    ret = send_data();
    if (ret < 0) {
        printk(KERN_ERR "Failed to send data\n");
    }
    return ret;
}

static const struct kernel_param_ops data_ops = {
    .set = set_data_param,
    .get = param_get_charp,
};

module_param_cb(data, &data_ops, &data, 0644);
MODULE_PARM_DESC(data, "Data parameter");

static int __init send_module_init(void)
{
    printk(KERN_INFO "Send module loaded\n");

    // バッファの初期化
    buffer = vmalloc(BUFFER_SIZE);
    if (!buffer) {
        printk(KERN_ERR "Failed to allocate buffer\n");
        return -ENOMEM;
    }

    return 0;
}

static void __exit send_module_exit(void)
{
    printk(KERN_INFO "Send module unloaded\n");

    // バッファの解放
    if (buffer) {
        vfree(buffer);
    }
}

module_init(send_module_init);
module_exit(send_module_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("User");
MODULE_DESCRIPTION("Simple TCP Send Module");
