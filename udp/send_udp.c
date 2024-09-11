#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/net.h>
#include <linux/in.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/errno.h>
#include <linux/sysfs.h>
#include <net/sock.h>
#include <linux/inet.h>

// パラメータ
static char *ip = "127.0.0.1";
static int debug = 0;
static char *data = NULL;
module_param(ip, charp, 0644);
module_param(debug, int, 0644);

// BUFFER_SIZE はテスト用に小さくしています
#define BUFFER_SIZE (4096) 
#define PORT 8808

// グローバル変数
static struct socket *sock;
static char *send_buffer;

// デバッグメッセージ
#define DEBUG_LOG(fmt, ...) \
    if (debug) printk(KERN_INFO fmt, ##__VA_ARGS__)

// 送信処理
// 送信処理
static int send_udp(size_t data_len) {
    struct sockaddr_in addr;
    struct msghdr msg;
    struct kvec vec;
    int ret;

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = in_aton(ip);
    addr.sin_port = htons(PORT);

    vec.iov_base = send_buffer;
    vec.iov_len = data_len;  // 実際に送信するデータ長

    memset(&msg, 0, sizeof(msg));
    msg.msg_name = &addr;
    msg.msg_namelen = sizeof(addr);

    ret = kernel_sendmsg(sock, &msg, &vec, 1, data_len);
    if (ret < 0) {
        printk(KERN_ERR "Failed to send message: %d\n", ret);
        return ret;
    }
    DEBUG_LOG("Sent %d bytes to %s:%d\n", ret, ip, PORT);
    return 0;
}

// sysfs コールバック関数
static int param_set_data(const char *val, const struct kernel_param *kp) {
    size_t len = strlen(val);
    
    if (len > BUFFER_SIZE) {
        printk(KERN_ERR "Data exceeds buffer size\n");
        return -EINVAL;
    }

    memset(send_buffer, 0, BUFFER_SIZE);
    memcpy(send_buffer, val, len);

    // 実際のデータ長を使ってUDP送信を行う
    return send_udp(len);
}

// sysfs エントリーの定義
static const struct kernel_param_ops data_param_ops = {
    .set = param_set_data,
    .get = param_get_string,
};
module_param_cb(data, &data_param_ops, &data, 0644);

// 初期化関数
static int __init send_udp_init(void) {
    int ret;

    send_buffer = vmalloc(BUFFER_SIZE);
    if (!send_buffer) {
        printk(KERN_ERR "Failed to allocate send buffer.\n");
        return -ENOMEM;
    }

    ret = sock_create_kern(&init_net, AF_INET, SOCK_DGRAM, 0, &sock);
    if (ret < 0) {
        printk(KERN_ERR "Failed to create socket: %d\n", ret);
        vfree(send_buffer);
        return ret;
    }

    printk(KERN_INFO "send_udp module loaded\n");
    return 0;
}

// クリーンアップ関数
static void __exit send_udp_exit(void) {
    if (sock) {
        sock_release(sock);
    }
    vfree(send_buffer);
    printk(KERN_INFO "send_udp module unloaded\n");
}

module_init(send_udp_init);
module_exit(send_udp_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Your Name");
MODULE_DESCRIPTION("UDP Send Kernel Module with sysfs interface");
