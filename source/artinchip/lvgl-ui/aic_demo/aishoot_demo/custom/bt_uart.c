/*
 * Copyright (c) 2024-2026, ArtInChip Technology Co., Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Authors:  haidong.pan <haidong.pan@artinchip.com>
 */

#ifdef ENABLE_BT_AP2029

#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <signal.h>
#include <termios.h>
#include <errno.h>
#include <getopt.h>
#include <string.h>
#include <sys/time.h>
#include <sys/select.h>

#include "bt_uart.h"

#define UART_BAUNDRATE B115200

static const unsigned char disconnect_msg[] = {0xac,0x69,0x0a,0x00,0x00,0xb7,0x00,0x00,0xd6,0x02};
static const unsigned char connect_msg[] = {0xac,0x69,0x0a,0x00,0x00,0xb7,0x00,0x00,0xd6,0x01};

static const unsigned char shutter_msg[] = {0xac,0x69,0x0a,0x00,0x00,0xbc,0x00,0x00,0xdb,0x01};
static unsigned char rename_header_msg[32] = {0xaa,0x55,0x01,0x0a};

static int uart_fd = -1;

int config_uart(char *dev_name)
{
    struct termios options;
    int ret = 0;

    uart_fd = open(dev_name, O_RDWR | O_NOCTTY | O_NDELAY);
    if (uart_fd == -1) {
        perror("open uart device fail \n");
    }

    ret = fcntl(uart_fd, F_SETFL, 0);
    if (ret < 0)
        printf("fcntl for failed!\n");

    tcgetattr(uart_fd, &options);

    bzero(&options, sizeof(options));

    options.c_cflag |= CLOCAL | CREAD;
    options.c_cflag &= ~CSIZE;

    //baundrate
    cfsetispeed(&options, UART_BAUNDRATE);
    cfsetospeed(&options, UART_BAUNDRATE);

    //nbits
    options.c_cflag |= CS8;

    //nparity
    options.c_cflag &= ~PARENB;

    //nstop
    options.c_cflag &= ~CSTOPB;

    //min and time
    options.c_cc[VTIME] = 1;
    options.c_cc[VMIN] = 1;

    if (tcsetattr(uart_fd, TCSANOW, &options) != 0) {
        perror("tcsetattr faild \n");
        close(uart_fd);
        return -1;
    }

    return 0;
}

int shutter_uart(void)
{
    if (uart_fd < 0) {
        perror("uart not open success \n");
        return -1;
    }
    int len = write(uart_fd, (char *)shutter_msg, sizeof(shutter_msg));
    for (int i = 0; i < len; i++) {
        printf("0x%02x ", shutter_msg[i]);
    }
    if (len < 0) {
        perror("shutter msg send fail \n");
        return -1;
    }
    return len;
}

int rename_bt_uart(char *name)
{
    if (uart_fd < 0) {
        perror("uart not open success \n");
        return -1;
    }

    strcat((char *)rename_header_msg, name);
    int len = write(uart_fd, rename_header_msg, sizeof(rename_header_msg));
    if (len < 0) {
        perror("shutter rename_header_msg send fail \n");
        return -1;
    }
    return len;
}

int recv_uart(char *rcv_buf, int data_len)
{
    int len, fs_sel;
    fd_set fs_read;

    if (uart_fd < 0) {
        perror("uart not open success \n");
        return -1;
    }

    FD_ZERO(&fs_read);
    FD_SET(uart_fd, &fs_read);

    memset(rcv_buf, 0, data_len);
    fs_sel = select(uart_fd + 1, &fs_read, NULL, NULL, NULL);
    if (fs_sel) {
        len = read(uart_fd, rcv_buf, data_len);
        if (len == 10 && memcmp(rcv_buf, (char *)connect_msg, len) == 0) {
            printf("bt connect success!\n");
        }
        if (len == 10 && memcmp(rcv_buf, (char *)disconnect_msg, len) == 0) {
            printf("bt disconnect success!\n");
        }
        return len;
    } else {
        return -1;
    }
}

int bt_uart_connect(char *msg)
{
    if (strcmp(msg, (char *)connect_msg) == 0)
        return 1;
    else
        return 0;
}

#endif
