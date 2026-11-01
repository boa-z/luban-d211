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

#define EVENT_BUF_SIZE      512
#define SOCKET_BUF_SIZE     512

/* Define same to weixin Mini Programs */
#define SOCKET_PORT         5000
#define P2P_GROUP_PASSWORD  "12345678"

static volatile int g_running = 1;

typedef struct {
    struct wpa_ctrl *p2p_ctrl;           /* P2P device control (e.g., p2p-dev-wlan1) */
    struct wpa_ctrl *p2p_group_ctrl;     /* P2P group control (e.g., p2p-wlan1-0) */
    int sock_fd;                         /* UDP socket FD */
    char device_name[32];                /* Physical interface name (e.g., wlan1) */
    char p2p_device_name[32];            /* P2P device interface name (e.g., p2p-dev-wlan1) */
    char group_ifname[32];               /* P2P group interface name (e.g., p2p-wlan1-0) */
} p2p_context_t;

static p2p_context_t g_p2p_ctx = {0};

typedef struct {
    int32_t msg_id;
    char ssid[32];
    char password[32];
} network_ap_t;

typedef struct {
    int sock_fd;
    struct sockaddr_in client_addr;
    socklen_t client_addr_len;
} aic_wifi_sock_t;

typedef enum {
    STA_STATE_CONNECT = 0,
    STA_STATE_CONNECTING,
    STA_STATE_DISCONNECT,
    STA_STATE_CONNECT_FAIL,
    STA_STATE_PROBE_ACK = 4,      /* Probe ack, device received probe from mini program */
    STA_STATE_CRED_RECEIVED = 5,  /* Credentials received, sent before P2P/AP tears down */
} sta_conns_state;

/* Check if wpa_supplicant is running for the specified interface */
static pid_t is_wpa_supplicant_running(const char *ifname)
{
    FILE *fp;
    char cmd[128];
    char line[256];
    pid_t pid = 0;

    if (!ifname) return 0;

    snprintf(cmd, sizeof(cmd), "ps | grep wpa_supplicant | grep -v grep | grep '%s'", ifname);

    fp = popen(cmd, "r");
    if (fp == NULL) {
        perror("Failed to run ps command");
        return 0;
    }

    if (fgets(line, sizeof(line), fp) != NULL) {
        sscanf(line, "%d", &pid);
    }

    pclose(fp);
    return pid;
}

static void cleanup_resources()
{
    if (g_p2p_ctx.sock_fd > 0) {
        close(g_p2p_ctx.sock_fd);
        g_p2p_ctx.sock_fd = -1;
    }

    if (g_p2p_ctx.p2p_group_ctrl) {
        wpa_ctrl_detach(g_p2p_ctx.p2p_group_ctrl);
        wpa_ctrl_close(g_p2p_ctx.p2p_group_ctrl);
        g_p2p_ctx.p2p_group_ctrl = NULL;
    }
    if (g_p2p_ctx.p2p_ctrl) {
        wpa_ctrl_detach(g_p2p_ctx.p2p_ctrl);
        wpa_ctrl_close(g_p2p_ctx.p2p_ctrl);
        g_p2p_ctx.p2p_ctrl = NULL;
    }

    pid_t pid = is_wpa_supplicant_running(g_p2p_ctx.device_name);
    if (pid > 0) {
        kill(pid, SIGTERM);
    }

    /* Kill udhcpd process */
    system("killall udhcpd 2>/dev/null");
}

static void signal_handler(int sig)
{
    g_running = 0;
}

/*
Insert p2p_disabled=1 before in wpa_supplicant.conf
close p2p when wifimanager run.
*/
static int p2p_disable_in_conf(void)
{
    const char *conf_path = "/etc/wifi/wpa_supplicant.conf";
    const char *tmp_path = "/etc/wifi/wpa_supplicant.conf.tmp";
    const char *disable_line = "p2p_disabled=1\n";
    FILE *fp_in, *fp_out;
    char line[256];
    int found = 0;
    int inserted = 0;

    /* Step 1: Check if p2p_disabled=1 already exists */
    fp_in = fopen(conf_path, "r");
    if (!fp_in) {
        perror("[p2p_config] Failed to open wpa_supplicant.conf");
        return -1;
    }

    while (fgets(line, sizeof(line), fp_in)) {
        if (strstr(line, "p2p_disabled=1")) {
            found = 1;
            break;
        }
    }
    fclose(fp_in);

    if (found) {
        printf("[p2p_config] p2p_disabled=1 already exists, skip\n");
        return 0;
    }

    /* Step 2: Read original and write to temp file */
    fp_in = fopen(conf_path, "r");
    if (!fp_in) {
        perror("[p2p_config] Failed to open wpa_supplicant.conf");
        return -1;
    }

    fp_out = fopen(tmp_path, "w");
    if (!fp_out) {
        perror("[p2p_config] Failed to create temp file");
        fclose(fp_in);
        return -1;
    }

    while (fgets(line, sizeof(line), fp_in)) {
        /* Insert before first network{} block */
        if (!inserted && strstr(line, "network")) {
            fprintf(fp_out, "%s", disable_line);
            inserted = 1;
        }
        fprintf(fp_out, "%s", line);
    }

    /* Step 3: If no network{} found, append at end */
    if (!inserted) {
        fprintf(fp_out, "%s", disable_line);
    }

    fclose(fp_in);
    fclose(fp_out);

    /* Step 4: Atomically replace original file */
    if (rename(tmp_path, conf_path) != 0) {
        perror("[p2p_config] Failed to replace config file");
        return -1;
    }

    printf("[p2p_config] Added p2p_disabled=1 to %s\n", conf_path);
    return 0;
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

/* Connect the wpa_supplicant control interface */
static int p2p_connect_wpa_supplicant(char *if_name)
{
    char wpas_socket_path[64] = {0};

    if (if_name == NULL)
        return -1;

    snprintf(wpas_socket_path, sizeof(wpas_socket_path), "/var/run/wpa_supplicant/%s", if_name);
    g_p2p_ctx.p2p_ctrl = wpa_ctrl_open(wpas_socket_path);
    if (!g_p2p_ctx.p2p_ctrl) {
        perror("[p2p_config]wpa_ctrl_open failed");
        return -1;
    }
    if (wpa_ctrl_attach(g_p2p_ctx.p2p_ctrl) != 0) {
        wpa_ctrl_close(g_p2p_ctx.p2p_ctrl);
        perror("[p2p_config]wpa_ctrl_attach failed");
        return -1;
    }
    return 0;
}

static int p2p_connect_group_interface()
{
    char wpas_group_socket_path[128] = {0};

    snprintf(wpas_group_socket_path, sizeof(wpas_group_socket_path),
            "/var/run/wpa_supplicant/%s", g_p2p_ctx.group_ifname);

    g_p2p_ctx.p2p_group_ctrl = wpa_ctrl_open(wpas_group_socket_path);
    if (!g_p2p_ctx.p2p_group_ctrl) {
        perror("[p2p_config]wpa_ctrl_open group interface failed");
        return -1;
    }

    if (wpa_ctrl_attach(g_p2p_ctx.p2p_group_ctrl) != 0) {
        wpa_ctrl_close(g_p2p_ctx.p2p_group_ctrl);
        g_p2p_ctx.p2p_group_ctrl = NULL;
        perror("[p2p_config]wpa_ctrl_attach group interface failed");
        return -1;
    }

    return 0;
}

/* Send control cmd and check the response */
static int p2p_send_command(const char *cmd, char *reply_buf, size_t *reply_len)
{
    if (cmd == NULL)
        return -1;

    printf("[p2p_config][CMD] %s\n", cmd);

    int ret = wpa_ctrl_request(g_p2p_ctx.p2p_ctrl, cmd, strlen(cmd), reply_buf, reply_len, NULL);
    if (ret < 0) {
        fprintf(stderr, "[p2p_config]Command '%s' failed\n", cmd);
        return -1;
    }

    if (reply_buf == NULL || reply_len == NULL)
        return 0;

    reply_buf[*reply_len] = '\0';
    if (strncmp(reply_buf, "OK\n", 3) != 0) {
        fprintf(stderr, "[p2p_config]Unexpected reply: %s", reply_buf);
        return -1;
    }
    return 0;
}

/* Start P2P listen */
static int p2p_start_listen(int duration)
{
    if (duration < 0)
        return -1;

    char cmd[64] = {0}, reply[EVENT_BUF_SIZE] = {0};
    size_t reply_len = sizeof(reply);

    snprintf(cmd, sizeof(cmd), "P2P_LISTEN %d", duration);
    if (p2p_send_command(cmd, reply, &reply_len) == 0) {
        printf("[p2p_config]P2P listening started\n");
    } else {
        perror("[p2p_config]P2P listen fail");
        return -1;
    }

    return 0;
}

/* add a P2P group */
static int p2p_group_add()
{
    char cmd[64] = {0}, reply[EVENT_BUF_SIZE] = {0};
    size_t reply_len = sizeof(reply);

    snprintf(cmd, sizeof(cmd), "P2P_GROUP_ADD passphrase=%s", P2P_GROUP_PASSWORD);
    if (p2p_send_command(cmd, reply, &reply_len) == 0) {
        printf("[p2p_config]P2P p2p_group_add\n");
    } else {
        perror("[p2p_config]P2P p2p_group_add fail");
        return -1;
    }

    return 0;
}

/* connect device */
static void p2p_connect_device(const char *peer_mac)
{
    char cmd[128] = {0}, reply[EVENT_BUF_SIZE] = {0};
    size_t reply_len = EVENT_BUF_SIZE;

    snprintf(cmd, sizeof(cmd), "P2P_CONNECT %s pbc", peer_mac);
    if (p2p_send_command(cmd, reply, &reply_len) == 0) {
        printf("[p2p_config]Accepted invitation from %s\n", peer_mac);
    }
}

/* dhcpd/dhcpc */
static void p2p_setup_network(const char *ifname, const char *role)
{
    char cmd[128] = {0};
    if (strcmp(role, "GO") == 0) {
        printf("[p2p_config]Acting as Group Owner\n");
        snprintf(cmd, sizeof(cmd), "ifconfig %s 192.168.169.1", ifname);
        system(cmd);
        /* Dynamically generate dhcpd configuration files*/
        char conf_path[64] = {0};
        snprintf(conf_path, sizeof(conf_path), "/etc/wifi/udhcpd.conf");

        FILE *fp = fopen(conf_path, "w");
        if (!fp) {
            perror("[p2p_config]Failed to create DHCP config");
            return;
        }

        fprintf(fp,
            "start        192.168.169.2\n"
            "end         192.168.169.254\n"
            "interface   %s\n"
            "max_leases  86400\n"
            "option subnet  255.255.255.0\n"
            "option router 192.168.169.1\n"
            "option dns    8.8.8.8 8.8.4.4\n",
            ifname);

        fclose(fp);

        memset(cmd, 0, sizeof(cmd));
        snprintf(cmd, sizeof(cmd), "udhcpd %s", conf_path);
        system(cmd);

        usleep(200000);

        unlink(conf_path);
    } else {
        printf("[p2p_config]Acting as Group Client\n");
        snprintf(cmd, sizeof(cmd), "udhcpc -i %s", ifname);
        system(cmd);
    }
    usleep(200000);
}

static int p2p_event_handle(char *payload)
{
    if (payload == NULL)
        return -1;

    if (strstr(payload, "P2P-DEVICE-FOUND")) {
        char mac[18] = {0}, name[64] = {0};
        if (sscanf(payload, "P2P-DEVICE-FOUND %17s p2p_dev_addr=%*s pri_dev_type=%*s name='%63[^']",
            mac, name) >= 1) {
            printf("[p2p_config]Discovered device: MAC=%s Name=%s\n", mac, name);
        }
    } else if (strstr(payload, "P2P-INVITATION-RECEIVED")) {
        char sa_mac[18] = {0}, go_mac[18] = {0};
        if (sscanf(payload, "P2P-INVITATION-RECEIVED sa=%17s go_dev_addr=%17s",
                    sa_mac, go_mac) == 2) {
            printf("[p2p_config]Received invitation  SA=%s GO=%s\n", sa_mac, go_mac);
            p2p_connect_device(sa_mac);
        }
    } else if (strstr(payload, "P2P-GO-NEG-REQUEST")) {
        char mac[18] = {0};
        int dev_passwd_id, go_intent;
        if (sscanf(payload, "P2P-GO-NEG-REQUEST %17s dev_passwd_id=%d go_intent=%d",
            mac, &dev_passwd_id, &go_intent) >= 3) {
            printf("[p2p_config]GO negotiation request %s (passwd_id=%d)\n", mac, dev_passwd_id);
        }
    } else if (strstr(payload, "P2P-GROUP-STARTED")) {
        char ifname[32] = {0}, role[4] = {0}, ssid[33] = {0};
        if (sscanf(payload, "P2P-GROUP-STARTED %31s %3s ssid=\"%32[^\"]",
                    ifname, role, ssid) >= 2) {
            printf("[p2p_config]Group started: Interface=%s Role=%s SSID=%s\n", ifname, role, ssid);
        }
    } else if (strstr(payload, "P2P-PROV-DISC-PBC-REQ")) {
        char mac[18] = {0}, name[64] = {0};
        if (sscanf(payload, "P2P-PROV-DISC-PBC-REQ %17s p2p_dev_addr=%*s %*s name='%63[^']",
            mac, name) >= 1) {
            printf("[p2p_config]PBC Request from %s (%s)\n", mac, name);
            p2p_connect_device(mac);
        }
    } else if (strstr(payload, "AP-STA-CONNECTED")) {
        char sta_mac[18] = {0},  p2p_dev_addr[18] = {0};

        /* Two formats：
           1. AP-STA-CONNECTED d2:16:b4:86:4b:ef
           2. AP-STA-CONNECTED d2:16:b4:86:4b:ef p2p_dev_addr=d2:16:b4:86:cb:ef */
        if (sscanf(payload, "AP-STA-CONNECTED %17s p2p_dev_addr=%17s",
                    sta_mac, p2p_dev_addr) >= 1) {
            if (strlen(p2p_dev_addr) > 0) {
                printf("[p2p_config] P2P Device: %s\n", p2p_dev_addr);
            }

            p2p_setup_network(g_p2p_ctx.group_ifname, "GO");
        }
    }
    return 0;
}

/* event loop */
static void aicwifi_event()
{
    char event[EVENT_BUF_SIZE] = {0};
    size_t event_len;
    struct pollfd fds[3];
    int nfds = 2;
    aic_wifi_sock_t sock_info;
    network_ap_t nw_ap;
    struct sockaddr_in client_addr;
    socklen_t addr_len = sizeof(client_addr);
    char sock_rx_buffer[SOCKET_BUF_SIZE];

    fds[0].fd = wpa_ctrl_get_fd(g_p2p_ctx.p2p_ctrl);
    fds[0].events = POLLIN | POLLPRI;
    fds[0].revents = 0;

    fds[1].fd = g_p2p_ctx.sock_fd;
    fds[1].events = POLLIN;
    fds[1].revents = 0;

    if (g_p2p_ctx.p2p_group_ctrl) {
        fds[2].fd = wpa_ctrl_get_fd(g_p2p_ctx.p2p_group_ctrl);
        fds[2].events = POLLIN;
        fds[2].revents = 0;
        nfds = 3;
    }

    while (g_running) {
        int ret = poll(fds, nfds, 5000);
        if (ret < 0) {
            break;
        } else if (ret == 0) {
            continue;
        }

        if (fds[0].revents & (POLLIN | POLLPRI)) {
            event_len = EVENT_BUF_SIZE - 1;
            if (wpa_ctrl_pending(g_p2p_ctx.p2p_ctrl) > 0 &&
                wpa_ctrl_recv(g_p2p_ctx.p2p_ctrl, event, &event_len) == 0) {
                event[event_len] = '\0';

                /* Processing log priority tags */
                char *payload = event;
                if (*payload == '<') {
                    payload = strchr(payload, '>');
                    if (payload)
                        payload++;
                }

                if (p2p_event_handle(payload) < 0)
                    printf("[p2p_config] g_p2p_ctx.p2p_ctrl payload null");
            }
        }
        if (fds[1].revents & POLLIN) {
            ssize_t recv_len = recvfrom(g_p2p_ctx.sock_fd, sock_rx_buffer, SOCKET_BUF_SIZE, 0,
                (struct sockaddr*)&client_addr, &addr_len);
            if (recv_len < 0) {
                continue;
            } else if (recv_len > 0) {
                if (recv_len >= SOCKET_BUF_SIZE)
                    recv_len = SOCKET_BUF_SIZE - 1;
                sock_rx_buffer[recv_len] = '\0';

                memcpy(&(sock_info.client_addr), &client_addr, sizeof(sock_info.client_addr));
                sock_info.client_addr_len = addr_len;
                sock_info.sock_fd = g_p2p_ctx.sock_fd;
            }

            aicwifi_parse_ap(sock_rx_buffer, &nw_ap);
            if(nw_ap.ssid[0] != '\0' && nw_ap.password[0] != '\0') {
                wifimanager_connect(nw_ap.ssid, nw_ap.password);
                aicwifi_socket_send(STA_STATE_CRED_RECEIVED, &sock_info);
            } else {
                aicwifi_socket_send(STA_STATE_PROBE_ACK, &sock_info);
            }
        }

        if(nfds == 3) {
            if ((fds[2].revents & (POLLIN | POLLPRI))) {
                event_len = EVENT_BUF_SIZE - 1;
                if (wpa_ctrl_pending(g_p2p_ctx.p2p_group_ctrl) > 0 &&
                    wpa_ctrl_recv(g_p2p_ctx.p2p_group_ctrl, event, &event_len) == 0) {
                    event[event_len] = '\0';
                    char *payload = event;
                    if (*payload == '<') {
                        payload = strchr(payload, '>');
                        if (payload) payload++;
                    }
                    if (p2p_event_handle(payload) < 0)
                        printf("[p2p_config] g_p2p_ctx.p2p_group_ctrl payload null");
                }
            }
        }
        usleep(20000);
    }
}

int aicwifi_p2p_init(char *device, int duration)
{
    char cmd[128] = {0};
    int retry = 5;

    strncpy(g_p2p_ctx.device_name, device, sizeof(g_p2p_ctx.device_name) - 1);
    snprintf(g_p2p_ctx.p2p_device_name, sizeof(g_p2p_ctx.p2p_device_name), "p2p-dev-%s", device);
    snprintf(g_p2p_ctx.group_ifname, sizeof(g_p2p_ctx.group_ifname), "p2p-%s-0", device);

    /* start the wpa_supplicant */
    snprintf(cmd, sizeof(cmd), "wpa_supplicant -i%s -Dnl80211 -c/etc/wifi/p2p_supplicant.conf &",
             device);
    system(cmd);
    sleep(2);

    /* connect to wpa_supplicant */
    if(p2p_connect_wpa_supplicant(g_p2p_ctx.p2p_device_name) < 0) {
        return -1;
    }

    if(p2p_start_listen(duration) < 0) {
        wpa_ctrl_detach(g_p2p_ctx.p2p_ctrl);
        wpa_ctrl_close(g_p2p_ctx.p2p_ctrl);
        return -1;
    }
    /*add group, P2P device could find in phone setting*/
    if(p2p_group_add() < 0) {
        wpa_ctrl_detach(g_p2p_ctx.p2p_ctrl);
        wpa_ctrl_close(g_p2p_ctx.p2p_ctrl);
        return -1;
    }

    while(retry-- > 0) {
        sleep(1);
        if(p2p_connect_group_interface() == 0) {
            printf("[p2p_config] Group interface connected successfully\n");
            break;
        }
    }
    if(retry < 0) {
        printf("[p2p_config] Group interface connected fail\n");
        return -1;
    }

    return 0;
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

static void ap_scan_result(char *result)
{
    printf("[p2p_config] ap_scan_result results %s\n", result);
}

static void connect_stat_change(wifistate_t stat, wifimanager_disconn_reason_t reason)
{
    printf("[p2p_config] connect_stat_change stat = %d ", stat);
}

int main(int argc, char *argv[])
{
    if (argc < 3) {
        /*wifi_manager use wlan0, so p2p config use wlan1 */
        fprintf(stderr, "usage:wifi_config <interface name> <listen duration> \n"
                            "eg.wifi_config wlan1 1200\n");
        return -1;
    }

    wifimanager_cb_t cb = {
        .scan_result_cb = ap_scan_result,
        .stat_change_cb = connect_stat_change,
    };
    p2p_disable_in_conf();

    if (wifimanager_init(&cb) != 0) {
        wifimanager_deinit();
        fprintf(stderr, "wifimanager_init failed\n");
        return -1;
    }

    if (aicwifi_p2p_init(argv[1], atoi(argv[2])) < 0)
        goto clean;

    g_p2p_ctx.sock_fd = aicwifi_create_socket();
    if(g_p2p_ctx.sock_fd == -1) {
        fprintf(stderr, " create socket fail\n");
        goto clean;
    }

    signal(SIGINT, signal_handler);       // Ctrl+C
    signal(SIGTERM, signal_handler);      // kill
    signal(SIGQUIT, signal_handler);      // Ctrl+

    aicwifi_event();

clean:
    cleanup_resources();
    return 0;
}
