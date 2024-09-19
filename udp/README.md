# Linux kernel loadble module base udp data send and receive

## 準備

送信側、受信側をロードします。

```sh
# insmod send_udp.ko
# insmod receive_udp.ko
```

## 送信

必要であれば送信先IPアドレスを設定して、データを送信します。
省略時の送信先IPアドレスは、"127.0.0.1" です。

```sh
# echo "192.168.1.1" > /sys/modules/send_udp/parameters/ip
# echo "This is test data." > /sys/modules/send_udp/parameters/data
```

## 受信

```sh
# cat /sys/modules/receive_udp/parameters/data
```
