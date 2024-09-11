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
#include <linux/kthread.h>
#include <net/sock.h>
#include <linux/delay.h>

#define PORT 4444
#define BUFFER_SIZE (4 * 1024 * 1024 * 1024UL) // 4GB
#ifndef PAGE_SIZE
#define PAGE_SIZE 4096
#endif
#define RECV_WAIT_TIMEOUT 5 // 5秒間データが途切れると受信完了

static int debug = 0;
module_param(debug, int, 0644);
MODULE_PARM_DESC(debug, "Debug mode");

//static int stop = 0;
//module_param(stop, int, 0644);
//MODULE_PARM_DESC(stop, "Stop flag");

//static char *data = NULL;
// module_param(data, charp, 0644); // これは不要になります
//MODULE_PARM_DESC(data, "Data to receive");

static char *g_buffer;
static size_t sys_buffer_pos = 0;
static size_t user_buffer_pos = 0;
static struct socket *sock;
static struct task_struct *recv_thread;

// 受信処理のメインループ
static int receive_thread(void *arg)
{
    struct sockaddr_in saddr;
    int ret;
    struct msghdr msg;
    struct kvec iov;
    size_t received_len;
    char *temp_buffer;

    if (debug)
        printk(KERN_DEBUG "receive_thread: Started\n");

    // 動的に一時バッファを確保
    temp_buffer = kmalloc(PAGE_SIZE, GFP_KERNEL);
    if (!temp_buffer) {
        printk(KERN_ERR "Failed to allocate temp buffer\n");
        return -ENOMEM;
    }

    // ソケットの作成
    ret = sock_create_kern(&init_net, AF_INET, SOCK_STREAM, IPPROTO_TCP, &sock);
    if (ret < 0) {
        printk(KERN_ERR "Error creating socket: %d\n", ret);
        kfree(temp_buffer);
        return ret;
    }

    // ソケットアドレス構造体の設定
    memset(&saddr, 0, sizeof(saddr));
    saddr.sin_family = AF_INET;
    saddr.sin_addr.s_addr = htonl(INADDR_ANY); // すべてのインターフェースで受信
    saddr.sin_port = htons(PORT); // ポート番号の設定

    // バインド
    ret = sock->ops->bind(sock, (struct sockaddr *)&saddr, sizeof(saddr));
    if (ret < 0) {
        printk(KERN_ERR "Error binding socket: %d\n", ret);
        sock_release(sock);
        kfree(temp_buffer);
        return ret;
    }
    if (debug)
        printk(KERN_DEBUG "receive_thread: bound\n");

    while(!kthread_should_stop()) {
        struct socket *client_sock;

        // リッスン
        ret = sock->ops->listen(sock, 1);
        if (ret < 0) {
            printk(KERN_ERR "Error listening on socket: %d\n", ret);
            //sock_release(sock);
            //kfree(temp_buffer);
            //return ret;
            continue;
        }
        if (debug) {
            printk(KERN_INFO "receive_thread: Listening on port %d\n", PORT);
        }

        // クライアントからの接続を受け入れ
        ret = kernel_accept(sock, &client_sock, 0);
        if (ret < 0) {
            printk(KERN_ERR "Error accepting connection: %d\n", ret);
            //sock_release(sock);
            //kfree(temp_buffer);
            //return ret;
            continue;
        }
        if (debug) {
            printk(KERN_INFO "receive_thread: Client connected\n");
        }
        sys_buffer_pos = 0;
        user_buffer_pos = 0;       

        // データの受信ループ
        while (!kthread_should_stop()) {
            memset(&msg, 0, sizeof(msg));
            iov.iov_base = temp_buffer;
            iov.iov_len = PAGE_SIZE;

            ret = kernel_recvmsg(client_sock, &msg, &iov, 1, PAGE_SIZE, 0);
            if (ret < 0) {
                printk(KERN_ERR "Error receiving data: %d\n", ret);
                //if (client_sock) {
                //    sock_release(client_sock);
                //    client_sock = NULL;
                //}
                break;
            }

            received_len = ret;
            if (debug) {
                printk(KERN_DEBUG "receive_thread: sys:%zu user:%zu Received %zu bytes\n",
                    sys_buffer_pos, sys_buffer_pos, received_len);
            }
            // 受信したデータをバッファに保存
            if (sys_buffer_pos + received_len <= BUFFER_SIZE) {
                memcpy(g_buffer + sys_buffer_pos, temp_buffer, received_len);
                sys_buffer_pos += received_len;
            } else {
                printk(KERN_ERR "Buffer overflow\n");
                if (client_sock) {
                    sock_release(client_sock);
                    client_sock = NULL;
                }
                break;
            }

            if (debug) {
                printk(KERN_INFO "receive_thread: sys:%zu user:%zu memcpied %zu bytes\n",
                sys_buffer_pos, user_buffer_pos, received_len);
            }
#if 0
            // 受信が5秒間途切れたら完了
            if (received_len == 0) {
                msleep(RECV_WAIT_TIMEOUT * 1000);
                printk(KERN_INFO "receive_thread: Receive timeout\n");

                if (client_sock) {
                    sock_release(client_sock);
                    client_sock = NULL;
                    printk(KERN_INFO "receive_thread: sock_released\n");
                }
                break;
            }
#endif
            if (received_len == 0) {
                if (debug)
                    printk(KERN_DEBUG "receive_len == 0\n");

                if (client_sock) {
                    sock_release(client_sock);
                    client_sock = NULL;
                    printk(KERN_INFO "receive_thread: sock_released\n");
                }
                break;
            }

            if (debug) {
                printk(KERN_DEBUG "receive_thread: sys:%zu\n", sys_buffer_pos);
            } 
        }

        if (client_sock) {
            if (debug)
                printk(KERN_DEBUG "Sudden terminate\n");
            sock_release(client_sock);
            client_sock = NULL;
        }
        schedule();
    }
    // ソケットの解放
    if (sock) {
        sock_release(sock);
        sock = NULL;
    }

    // バッファの解放
    if (temp_buffer)
        kfree(temp_buffer);

    if (debug)
        printk(KERN_DEBUG "receive_thread: Stopped\n");
    return 0;
}

// sysfsにデータを書き出すコールバック（動作確認用）
static int set_data_param(const char *buffer, const struct kernel_param *kp)
{
    size_t copied;
    copied = param_set_charp(buffer, kp);
    if (debug) {
        printk(KERN_DEBUG "set_data_param: Copy %p = %zu\n", kp->name, copied);
    }
    return(copied);
}

// sysfsからデータを読み出すコールバック
static int get_data_param(char *buffer, const struct kernel_param *kp)
{
    size_t to_copy = min(sys_buffer_pos, (size_t)PAGE_SIZE); //end of copy position

    if (debug) {
        printk(KERN_DEBUG "get_data_param: buffer %p to_copy = %zu user %zu end %zu\n",
        buffer, to_copy, user_buffer_pos, sys_buffer_pos);
    }
    memcpy(buffer, g_buffer + user_buffer_pos, to_copy);
    user_buffer_pos += to_copy;
    
    if (debug) {
        printk(KERN_DEBUG "get_data_param: Copied %zu bytes\n", to_copy);
    }
    return to_copy;
}

static const struct kernel_param_ops data_ops = {
    .set = set_data_param,
    .get = get_data_param,
};

module_param_cb(data, &data_ops, &g_buffer, 0644);
MODULE_PARM_DESC(data, "Received data buffer");

// sysfsにデータを書き出すコールバック
static int set_stop_param(const char *buffer, const struct kernel_param *kp)
{
    int stop_flag;
    stop_flag = param_set_int(buffer, kp);
    if (debug) {
        printk(KERN_DEBUG "set_stop_param: Copy %p = %d\n", kp->name, stop_flag);
    }
    if (stop_flag == 1) {
        if (debug)
            printk(KERN_DEBUG "set_stop_param: Stop flag set\n");
        if (recv_thread) {
            kthread_stop(recv_thread);
            printk(KERN_INFO "Receive thread stopped\n");
            recv_thread = NULL;
        }
    }
    return 0;
}

#if 0
// sysfsからデータを読み出すコールバック
static int get_stop_param(char *buffer, const struct kernel_param *kp)
{
    size_t to_copy = min(sys_buffer_pos, (size_t)PAGE_SIZE); //end of copy position

    if (debug) {
        printk(KERN_DEBUG "get_data_param: buffer %p to_copy = %zu user %zu end %zu\n",
        buffer, to_copy, user_buffer_pos, sys_buffer_pos);
    }
    memcpy(buffer, g_buffer + user_buffer_pos, to_copy);
    user_buffer_pos += to_copy;
    
    if (debug) {
        printk(KERN_DEBUG "get_data_param: Copied %zu bytes\n", to_copy);
    }
    return to_copy;
}
#endif

static const struct kernel_param_ops stop_ops = {
    .set = set_stop_param,
    //.get = get_stop_param,
};

module_param_cb(stop, &stop_ops, NULL, 0644);
MODULE_PARM_DESC(stop, "Stop flag");

static int __init receive_module_init(void)
{
    printk(KERN_INFO "Receive module loaded\n");

    // バッファの初期化
    g_buffer = vmalloc(BUFFER_SIZE);
    if (!g_buffer) {
        printk(KERN_ERR "Failed to allocate global buffer\n");
        return -ENOMEM;
    }

    // 受信スレッドの開始
    recv_thread = kthread_run(receive_thread, NULL, "receive_thread");
    if (IS_ERR(recv_thread)) {
        vfree(g_buffer);
        return PTR_ERR(recv_thread);
    }

    return 0;
}

static void __exit receive_module_exit(void)
{
    if (debug)
        printk(KERN_DEBUG "Receive module_exit start\n");

    // スレッドの停止
    if (recv_thread) {
        kthread_stop(recv_thread);
        printk(KERN_INFO "Receive thread stopped\n");
    }

    if (debug)
        printk(KERN_DEBUG "Receive thread stopped\n");

    // ソケットの解放
    if (sock) {
        sock_release(sock);
        printk(KERN_INFO "Socket released\n");
        sock = NULL;
    }

    if (debug)
        printk(KERN_DEBUG "Receive socket released\n");

    // バッファの解放
    if (g_buffer) {
        vfree(g_buffer);
        printk(KERN_INFO "Global buffer released\n");
    }

    printk(KERN_INFO "Receive module unloaded\n");
}

module_init(receive_module_init);
module_exit(receive_module_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("User");
MODULE_DESCRIPTION("Simple TCP Receive Module");
