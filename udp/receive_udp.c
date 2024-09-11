#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/net.h>
#include <linux/in.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/errno.h>
#include <linux/hrtimer.h>
#include <linux/jiffies.h>
#include <linux/kthread.h>
#include <net/sock.h>
#include <linux/inet.h>
#include <linux/sysfs.h>
#include <linux/sched.h>

// パラメータ
static int debug = 0;
static int stop = 0; // stop パラメータを追加
//static char *data = NULL;
module_param(debug, int, 0644);
MODULE_PARM_DESC(debug, "Debug mode");
//module_param(stop, int, 0644);

#define BUFFER_SIZE (4 * 1024 * 1024 * 1024UL) // 4GB
#ifndef PAGE_SIZE
#define PAGE_SIZE 4096
#endif
#define PORT 8808

// グローバル変数
static char *g_buffer;
static size_t sys_buffer_pos = 0;
static size_t user_buffer_pos = 0;
static struct socket *sock;
static struct hrtimer timer;
static ktime_t kt_periode;
static struct task_struct *recv_thread;
static int use_timer = 0;

// デバッグメッセージ
#define DEBUG_LOG(fmt, ...) \
    if (debug) printk(KERN_INFO fmt, ##__VA_ARGS__)

// タイムアウト処理
static enum hrtimer_restart timer_callback(struct hrtimer *timer) {
    DEBUG_LOG("Timeout reached, no data received for 5 seconds.\n");
    return HRTIMER_NORESTART;
}

// カーネルスレッド内での受信処理
static int receive_udp(void *data) {
    struct sockaddr_in addr;
    struct msghdr msg;
    struct kvec vec;
    char *recv_buffer;
    size_t received_len;
    int ret;

    recv_buffer = vmalloc(PAGE_SIZE);
    if (!recv_buffer) {
        printk(KERN_ERR "Failed to allocate receive buffer.\n");
        return -ENOMEM;
    }

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY); // すべてのインターフェースで受信
    addr.sin_port = htons(PORT);

    ret = kernel_bind(sock, (struct sockaddr *)&addr, sizeof(addr));
    if (ret < 0) {
        printk(KERN_ERR "Failed to bind socket: %d\n", ret);
        vfree(recv_buffer);
        return ret;
    }
    DEBUG_LOG("Socket binded to port %d\n", PORT);

    sys_buffer_pos = 0;
    user_buffer_pos = 0;   
    while (!kthread_should_stop()) {
        // stop パラメータが '1' の場合はカーネルスレッドを停止
        if (stop) {
            DEBUG_LOG("Kernel thread stop is true.\n");
            set_current_state(TASK_INTERRUPTIBLE);
            schedule();
            if (recv_buffer) {
                vfree(recv_buffer);
                recv_buffer = NULL;
            }
            break;
        }

        vec.iov_base = recv_buffer;
        vec.iov_len = PAGE_SIZE;

        memset(&msg, 0, sizeof(msg));
        msg.msg_name = &addr;
        msg.msg_namelen = sizeof(addr);

        ret = kernel_recvmsg(sock, &msg, &vec, 1, PAGE_SIZE, 0);
        if (ret < 0) {
            printk(KERN_ERR "Failed to receive message: %d\n", ret);
            if (recv_buffer) {
                vfree(recv_buffer);
            }
            return ret;
        }
        received_len = ret;

        // 受信したデータをバッファに保存
        if (sys_buffer_pos + received_len <= BUFFER_SIZE) {
            memcpy(g_buffer + sys_buffer_pos, recv_buffer, received_len);
            sys_buffer_pos += received_len;
        } else {
            printk(KERN_ERR "Buffer overflow: %zu + %zu > %ld\n", sys_buffer_pos, received_len, BUFFER_SIZE);
            ret = -ENOMEM;
            break;
        }

        DEBUG_LOG("Received %d bytes from %pI4:%d\n", ret, &addr.sin_addr, ntohs(addr.sin_port));

        if (use_timer) {
            // タイムアウトの再スタート
            hrtimer_start(&timer, kt_periode, HRTIMER_MODE_REL);
        }
    }
    DEBUG_LOG("Kernel thread stopped.\n");
    if (recv_buffer) {
        vfree(recv_buffer);
        recv_buffer = NULL;
        ret = 0;
    }

    return ret;
}

// stop パラメータが変更されたときに呼び出されるコールバック関数
static int param_set_stop(const char *val, const struct kernel_param *kp) {
    int ret = param_set_int(val, kp); // 標準のintパラメータセット関数を呼び出す

    if (ret == 0 && stop == 0 && recv_thread == NULL) {
        // stopが '0' に設定され、スレッドが動作していない場合、再起動
        recv_thread = kthread_run(receive_udp, NULL, "recv_thread");
        if (IS_ERR(recv_thread)) {
            printk(KERN_ERR "Failed to create kernel thread.\n");
            recv_thread = NULL;
            return PTR_ERR(recv_thread);
        }
        printk(KERN_INFO "Kernel thread restarted.\n");
    } else if (stop == 1 && recv_thread != NULL) {
        // stopが '1' に設定された場合、カーネルスレッドを停止
        kthread_stop(recv_thread);
        recv_thread = NULL;
        printk(KERN_INFO "Kernel thread stopped.\n");
    }

    return ret;
}

// sysfsエントリ定義
static const struct kernel_param_ops stop_param_ops = {
    .set = param_set_stop,
    .get = param_get_int,
};
module_param_cb(stop, &stop_param_ops, &stop, 0644);

#ifdef OLD_PARAM_GET_DATA
// sysfsコールバック関数: ユーザーがdataを読み込んだ際にバッファを出力・クリア
static int param_get_data(char *buffer, const struct kernel_param *kp) {
    int len = strlen(recv_buffer);

    if (len == 0) {
        return sprintf(buffer, "Buffer is empty\n");
    } else {
        // バッファの内容を出力
        sprintf(buffer, "Received data: %s\n", recv_buffer);
        // バッファのクリア
        memset(recv_buffer, 0, BUFFER_SIZE);
        return len;
    }
}

// sysfsエントリ定義
static const struct kernel_param_ops data_param_ops = {
    .get = param_get_data,
};
module_param_cb(data, &data_param_ops, &data, 0444);
#endif

// sysfsにデータを書き出すコールバック（動作確認用）
static int set_data_param(const char *buffer, const struct kernel_param *kp)
{
    size_t copied;
    copied = param_set_charp(buffer, kp);
    DEBUG_LOG("set_data_param: Copy %p = %zu\n", kp->name, copied);
    return(copied);
}

// sysfsからデータを読み出すコールバック
static int get_data_param(char *buffer, const struct kernel_param *kp)
{
    size_t to_copy = min(sys_buffer_pos, (size_t)PAGE_SIZE); //end of copy position

    DEBUG_LOG("get_data_param: buffer %p to_copy = %zu user %zu end %zu\n",
        buffer, to_copy, user_buffer_pos, sys_buffer_pos);
    memcpy(buffer, g_buffer + user_buffer_pos, to_copy);
    user_buffer_pos += to_copy;
    
    DEBUG_LOG("get_data_param: Copied %zu bytes\n", to_copy);
    return to_copy;
}

static const struct kernel_param_ops data_ops = {
    .set = set_data_param,
    .get = get_data_param,
};

module_param_cb(data, &data_ops, &g_buffer, 0644);
MODULE_PARM_DESC(data, "Received data buffer");

// 初期化関数
static int __init receive_udp_init(void) {
    int ret;

    printk(KERN_INFO "receive_udp module started\n");
    g_buffer = vmalloc(BUFFER_SIZE);
    if (!g_buffer) {
        printk(KERN_ERR "Failed to allocate global buffer.\n");
        return -ENOMEM;
    }

    ret = sock_create_kern(&init_net, AF_INET, SOCK_DGRAM, 0, &sock);
    if (ret < 0) {
        printk(KERN_ERR "Failed to create socket: %d\n", ret);
        vfree(g_buffer);
        g_buffer = NULL;
        return ret;
    }

    // hrtimer 初期化
    if (use_timer) {
        kt_periode = ktime_set(5, 0); // 5秒のタイムアウト
        hrtimer_init(&timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
        timer.function = timer_callback;
    }
    // 初回カーネルスレッドの起動
    recv_thread = kthread_run(receive_udp, NULL, "recv_thread");
    DEBUG_LOG("Receive thread started.\n");
    if (IS_ERR(recv_thread)) {
        printk(KERN_ERR "Failed to create kernel thread.\n");
        sock_release(sock);
        if (use_timer) {
            hrtimer_cancel(&timer);
        }
        if (g_buffer) {
           vfree(g_buffer);
            g_buffer = NULL;
        }
        return PTR_ERR(recv_thread);
    }

    printk(KERN_INFO "receive_udp module loaded\n");
    return 0;
}

// クリーンアップ関数
static void __exit receive_udp_exit(void) {

    if (use_timer) {
        hrtimer_cancel(&timer);
    }

    if (recv_thread) {
        kthread_stop(recv_thread);
        recv_thread = NULL;
    }
    if (sock) {
        sock_release(sock);
        sock = NULL;
    }
    if (g_buffer) {
        vfree(g_buffer);
        g_buffer = NULL;
    }
    if (g_buffer) {
        vfree(g_buffer);
        g_buffer = NULL;
    }
    printk(KERN_INFO "receive_udp module unloaded\n");
}

module_init(receive_udp_init);
module_exit(receive_udp_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Your Name");
MODULE_DESCRIPTION("UDP Receive Kernel Module with sysfs stop control");
