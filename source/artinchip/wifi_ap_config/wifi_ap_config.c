/*
 * Copyright (C) 2026 ArtInChip Technology Co., Ltd.
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Authors:  hanlei.liu <hanlei.liu@artinchip.com>
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include "wpa_ctrl.h"
#include <poll.h>
#include <signal.h>

#include "wifimanager.h"

#define SOCKET_BUF_SIZE     512
/* Define same to weixin Mini Programs */
#define SOCKET_PORT 		5000

static volatile int g_running = 1;

typedef struct {
    int32_t msg_id;
    char ssid[32];
    char password[32];
}network_ap_t;

typedef struct {
    int sock_fd;
    struct sockaddr_in client_addr;
    socklen_t client_addr_len;
}aic_wifi_sock_t;

typedef enum {
    STA_STATE_CONNECT = 0,
    STA_STATE_CONNECTING,
    STA_STATE_DISCONNECT,
    STA_STATE_CONNECT_FAIL,
    STA_STATE_PROBE_ACK = 4,      /* Probe ack, device received probe from mini program */
    STA_STATE_CRED_RECEIVED = 5,  /* Credentials received, sent before P2P/AP tears down */
} sta_conns_state;

static void cleanup_resources(int sock_fd)
{
    if (sock_fd > 0) {
        close(sock_fd);
    }

    system("killall hostapd 2>/dev/null");
    system("killall udhcpd 2>/dev/null");
}

static void signal_handler(int sig)
{
    g_running = 0;
}

int aicwifi_parse_ap(char *rx_buffer, network_ap_t *nw_ap)
{
    int offset = 0;
    int32_t msg_id;

    memcpy(&msg_id, rx_buffer + offset, sizeof(msg_id));
    nw_ap->msg_id = ntohl(msg_id);
    offset += sizeof(msg_id);

    memset(nw_ap->ssid, 0, sizeof(nw_ap->ssid));
    memcpy(nw_ap->ssid, rx_buffer + offset, sizeof(nw_ap->ssid));
    nw_ap->ssid[sizeof(nw_ap->ssid) - 1] = '\0';
    offset += sizeof(nw_ap->ssid);

    memset(nw_ap->password, 0, sizeof(nw_ap->password));
    memcpy(nw_ap->password, rx_buffer + offset, sizeof(nw_ap->password));
    nw_ap->password[sizeof(nw_ap->password) - 1] = '\0';
    offset += sizeof(nw_ap->password);

    return offset;
}

void aicwifi_socket_send(sta_conns_state state, aic_wifi_sock_t *sock_info)
{
    int offset = 0;
    ssize_t ret = 0;
    char sock_tx_buffer[SOCKET_BUF_SIZE];

    if (sock_info->client_addr_len == 0) {
        return;
    }

    memset(sock_tx_buffer, 0, sizeof(sock_tx_buffer));
    memcpy(sock_tx_buffer + offset, &state, sizeof(state));
    offset += sizeof(state);

    ret = sendto(sock_info->sock_fd, sock_tx_buffer, offset, 0,
                (struct sockaddr *)&sock_info->client_addr, sock_info->client_addr_len);
    if (ret < 0) {
        perror("UDP send failed");
    }
}

/* event loop */
static void aicwifi_event(int sock_fd)
{
    struct pollfd fds[1];
    char sock_rx_buffer[SOCKET_BUF_SIZE];
    struct sockaddr_in client_addr;
    socklen_t addr_len = sizeof(client_addr);
    aic_wifi_sock_t sock_info;

    fds[0].fd = sock_fd;
    fds[0].events = POLLIN;
    fds[0].revents = 0;

    network_ap_t nw_ap;

    while (g_running) {
        int ret = poll(fds, 1, 5000);
        if (ret < 0) {
            break;
        } else if (ret == 0) {
            continue;
        }

        if (fds[0].revents & POLLIN) {
            ssize_t recv_len = recvfrom(sock_fd, sock_rx_buffer, SOCKET_BUF_SIZE, 0,
                                        (struct sockaddr*)&client_addr, &addr_len);
            if (recv_len < 0) {
                perror("recvfrom failed");
                continue;
            } else if (recv_len > 0) {
                if (recv_len >= SOCKET_BUF_SIZE)
                    recv_len = SOCKET_BUF_SIZE - 1;
                sock_rx_buffer[recv_len] = '\0';

                memcpy(&(sock_info.client_addr), &client_addr, sizeof(sock_info.client_addr));
                sock_info.client_addr_len = addr_len;
                sock_info.sock_fd = sock_fd;
            }

            memset(&nw_ap, 0, sizeof(nw_ap));
            aicwifi_parse_ap(sock_rx_buffer, &nw_ap);

            if(nw_ap.ssid[0] != '\0' && nw_ap.password[0] != '\0') {
                wifimanager_connect(nw_ap.ssid, nw_ap.password);
                aicwifi_socket_send(STA_STATE_CRED_RECEIVED, &sock_info);
            } else {
                aicwifi_socket_send(STA_STATE_PROBE_ACK, &sock_info);
            }
        }
        usleep(20000);
    }

}

static void ap_scan_result(char *result)
{
    printf("wifi_ap_config: ap_scan_result results %s\n", result);
}

static void connect_stat_change(wifistate_t stat, wifimanager_disconn_reason_t reason)
{
    printf("wifi_ap_config: connect_stat_change reason = %d \n", reason);
}

int aicwifi_create_socket()
{
    int sock_fd;
    int opt = 1;
    struct sockaddr_in server_addr;

    if ((sock_fd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
        perror("create socket fail");
        return -1;
    }

    if (setsockopt(sock_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt))) {
        perror("setsockopt fail");
        close(sock_fd);
        return -1;
    }

    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(SOCKET_PORT);

    if (bind(sock_fd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        perror("bind socket fail");
        close(sock_fd);
        return -1;
    }
    return sock_fd;
}

int main(int argc, char *argv[])
{
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <interface>\n", argv[0]);
        fprintf(stderr, "Example: %s wlan1\n", argv[0]);
        return -1;
    }
    int sock_fd = -1;
    char ifconfig_cmd[128] = {0};

    wifimanager_cb_t cb = {
        .scan_result_cb = ap_scan_result,
        .stat_change_cb = connect_stat_change,
    };

    if (wifimanager_init(&cb) != 0) {
        wifimanager_deinit();
        fprintf(stderr, "wifimanager_init failed\n");
        return -1;
    }

    if (system("hostapd -d /etc/wifi/hostapd.conf -B") != 0) {
        fprintf(stderr, "hostapd start failed\n");
        return -1;
    }

    snprintf(ifconfig_cmd, sizeof(ifconfig_cmd), "ifconfig %s 192.168.169.1", argv[1]);
    if (system(ifconfig_cmd) != 0) {
        fprintf(stderr, "ifconfig failed\n");
        return -1;
    }

    if (system("udhcpd /etc/wifi/udhcpd.conf") != 0) {
        fprintf(stderr, "udhcpd start failed\n");
        system("killall hostapd 2>/dev/null");
        return -1;
    }
    sock_fd = aicwifi_create_socket();
    if (sock_fd < 0) {
        fprintf(stderr, "socket create failed\n");
        goto clean;
    }

    signal(SIGINT, signal_handler);       // Ctrl+C
    signal(SIGTERM, signal_handler);      // kill
    signal(SIGQUIT, signal_handler);      // Ctrl+

    aicwifi_event(sock_fd);
clean:
    cleanup_resources(sock_fd);
    return 0;
}
