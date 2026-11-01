/*
 * Copyright (C) 2022-2025 ArtInChip Technology Co., Ltd.
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Authors:  lv.wu <lv.wu@artinchip.com>
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>

#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/un.h>
#include <netinet/in.h>
#include <sys/wait.h>
#include <stdint.h>
#include <sys/prctl.h>
#include <pthread.h>
#include <unistd.h>
#include "wifimanager.h"

#define LIST_NETWORK_MAX 4096
#define USER_SOCKET	"/var/run/user_cb_socket"

struct cb_info {
	int evt;
	int buff_length;
};

static pthread_t gp_user_callback;
static int g_server_fd = -1;
static int g_cli_fd = -1;

static const char *wifistate2string(wifistate_t state)
{
	switch (state) {
	case WIFI_STATE_GOT_IP:
		return "WIFI_STATE_GOT_IP";
	case WIFI_STATE_CONNECTING:
		return "WIFI_STATE_CONNECTING";
	case WIFI_STATE_DHCPC_REQUEST:
		return "WIFI_STATE_DHCPC_REQUEST";
	case WIFI_STATE_DISCONNECTED:
		return "WIFI_STATE_DISCONNECTED";
	case WIFI_STATE_CONNECTED:
		return "WIFI_STATE_CONNECTED";
	default:
		return "WIFI_STATE_ERROR";
	}
}

static const char *disconn_reason2string(wifimanager_disconn_reason_t reason)
{
	switch (reason) {
	case AUTO_DISCONNECT:
		return "wpa auto disconnect";
	case ACTIVE_DISCONNECT:
		return "active disconnect";
	case KEYMT_NO_SUPPORT:
		return "keymt is not supported";
	case CMD_OR_PARAMS_ERROR:
		return "wpas command error";
	case IS_CONNECTTING:
		return "wifi is still connecting";
	case CONNECT_TIMEOUT:
		return "connect timeout";
	case REQUEST_IP_TIMEOUT:
		return "request ip address timeout";
	case WPA_TERMINATING:
		return "wpa_supplicant is closed";
	case AP_ASSOC_REJECT:
		return "AP assoc reject";
	case NETWORK_NOT_FOUND:
		return "can't search such ssid";
	case PASSWORD_INCORRECT:
		return "incorrect password";
	default:
		return "other reason";
	}
}

/* This callback is running in wifimanager daemon process,
 * So you can't process other logic in this callback, such as UI display...
 *
 * It is recommended to use inter-process communication (IPC) to send the
 * results of the WIFIMANAGER DAEMON to the process that needs to execute them.
 */
static void print_scan_result(char *result)
{
	int fd;
	struct sockaddr_un saddr = { .sun_family = AF_UNIX };
	struct cb_info info;

	snprintf(saddr.sun_path, sizeof(saddr.sun_path) - 1, USER_SOCKET);

	if ((fd = socket(PF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0)) == -1) {
		printf("client: socket alloc error\n");
		return;
	}

	if (connect(fd, (struct sockaddr *)(&saddr), sizeof(saddr)) == -1) {
		printf("client: connect error\n");
		close(fd);
		return;
	}

	info.evt = 0;
	info.buff_length = strlen(result);
	send(fd, &info, sizeof(struct cb_info), 0);
	send(fd, result, strlen(result), 0);
	close(fd);
}

/* This callback is running in wifimanager daemon process,
 * So you can't process other logic in this callback, such as UI display...
 *
 * It is recommended to use inter-process communication (IPC) to send the
 * results of the WIFIMANAGER DAEMON to the process that needs to execute them.
 */
static void print_stat_change(wifistate_t stat, wifimanager_disconn_reason_t reason)
{
	int fd;
	struct sockaddr_un saddr = { .sun_family = AF_UNIX };
	struct cb_info info;
	int buff[2];

	snprintf(saddr.sun_path, sizeof(saddr.sun_path) - 1, USER_SOCKET);

	if ((fd = socket(PF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0)) == -1)
		printf("client: socket alloc error\n");
		return;

	if (connect(fd, (struct sockaddr *)(&saddr), sizeof(saddr)) == -1) {
		printf("client: connect error\n");
		close(fd);
		return;
	}

	info.evt = 1;
	info.buff_length = sizeof(int) * 2;

	buff[0] = stat;
	buff[1] = reason;

	send(fd, &info, sizeof(struct cb_info),0);
	send(fd, buff, sizeof(int) * 2,0);
	close(fd);
}

static void *user_callback_thread(void *arg)
{
	struct sockaddr_un saddr = { .sun_family = AF_UNIX };
	snprintf(saddr.sun_path, sizeof(saddr.sun_path) - 1, USER_SOCKET);
	int header[2];
	void *buff;

	g_server_fd = socket(PF_UNIX, SOCK_SEQPACKET, 0);
	if (g_server_fd == -1) {
		printf("server: socket alloc error\n");
		return NULL;
	}
	if (bind(g_server_fd, (struct sockaddr *)(&saddr), sizeof(saddr)) == -1) {
		printf("server: bind error\n");
		goto fail;
	}

	if (chmod(saddr.sun_path, 0660) == -1) {
		printf("server: chmod error\n");
		goto fail;
	}

	if (listen(g_server_fd, 2) == -1) {
		printf("server: listen error\n");
		goto fail;
	}

	while (1) {
		int len;
		g_cli_fd = accept(g_server_fd, NULL, NULL);
		len = recv(g_cli_fd, header, sizeof(int) * 2, 0);
		if (len <= 0) {
			close(g_cli_fd);
			g_cli_fd = -1;
			continue;
		}

		buff = malloc(header[1] + 1);
		if (len <= 0) {
			close(g_cli_fd);
			g_cli_fd = -1;
			continue;
		}
		len = recv(g_cli_fd, buff, header[1], 0);
		if (len <= 0) {
			free(buff);
			close(g_cli_fd);
			g_cli_fd = -1;
			continue;
		}

		if (header[0] == 0) {
			printf("\n-------------------------scan result-------------------------\n");
			printf("%s\n", buff);
			printf("---------------------------------------------------------------\n");
		} else if (header[0] == 1) {
			printf("\n-------------------connection change result------------------\n");
			printf("%s\n", wifistate2string(((int *)buff)[0]));
			if (((int *)buff)[0] == WIFI_STATE_DISCONNECTED)
				printf("disconnect reason: %s\n", disconn_reason2string(((int *)buff)[1]));
			printf("---------------------------------------------------------------\n");
		}

		free(buff);
		close(g_cli_fd);
		g_cli_fd = -1;
	}
fail:
	close(g_server_fd);
	g_server_fd = -1;
	pthread_exit(NULL);
}

/* release socket resource */
static void loop_stop(int sig)
{
	struct sigaction sigact = { .sa_handler = SIG_DFL };

	sigaction(sig, &sigact, NULL);
	pthread_cancel(gp_user_callback);
	pthread_join(gp_user_callback, NULL);

	if (g_server_fd != -1)
		close(g_server_fd);
	if (g_cli_fd != -1)
		close(g_cli_fd);

	unlink(USER_SOCKET);
}

static int wait_loop()
{
	pid_t pid;

	if((pid = fork()) < 0)
		return 0;
	else if(pid)
		return 0;

	pthread_create(&gp_user_callback, NULL, user_callback_thread, NULL);

	struct sigaction sigact = { .sa_handler = SIG_IGN };
	sigaction(SIGPIPE, &sigact, NULL);

	/* free ctl resource */
	sigact.sa_handler = loop_stop;
	sigaction(SIGTERM, &sigact, NULL);
	sigaction(SIGINT, &sigact, NULL);

	while(1) {
		sleep(1);
	}

	return 0;
}

int main(int argc, char *argv[])
{
	int opt;
	const char *opts = "c:hslriod";
	char list_net_results[LIST_NETWORK_MAX];
	wifi_status_t status = {0};

	int ret = -1;

	struct option longopts[] = {
		{"connect", required_argument, NULL, 'c'},
		{"scan", no_argument, NULL, 's'},
		{"list_network", no_argument, NULL, 'l'},
		{"remove_network", no_argument, NULL, 'r'},
		{"status", no_argument, NULL, 'i'},
		{"help", no_argument, NULL, 'h'},
		{"open", no_argument, NULL, 'o'},
		{"close", no_argument, NULL, 'd'},
		{ 0, 0, 0, 0 },
	};

	wifimanager_cb_t cb = {
		.scan_result_cb = print_scan_result,
		.stat_change_cb = print_stat_change,
	};

	if(argc == 1)
		goto usage;

	while ((opt = getopt_long(argc, argv, opts, longopts, NULL)) != -1) {
		switch(opt) {
			case 'c':
				if(argc > 5 || argc < 3) {
					printf("  -c, --connect\t\tconnect AP, -c <ssid> [password]\n");
				} else if (argc == 3) {
					/* Open */
					printf("connecting open wifi [ssid:%s] \n", argv[2]);
					wifimanager_connect(argv[2], NULL);
				} else {
					printf("connecting ssid:%s passward:%s\n", argv[optind-1], argv[optind]);
					wifimanager_connect(argv[optind-1], argv[optind]);
				}
				return EXIT_SUCCESS;

			case 'h':
usage:
				printf("  -h, --help\t\tprint this help\n");
				printf("  -c, --connect\t\tconnect AP, -c <ssid> [password]\n");
				printf("  -s, --scan\t\tscan AP\n");
				printf("  -l, --list\tlist all networks\n");
				printf("  -i, --status\t\tget wifi status information\n");
				printf("  -r, --remove\tremove network in config, -r <ssid>\n");
				printf("  -o, --open\topen WifiManager\n");
				printf("  -d, --close\tclose WifiManager\n");
				return EXIT_SUCCESS;

			case 's':
				ret = wifimanager_scan();
				return EXIT_SUCCESS;

			case 'l':
				ret = wifimanager_list_networks(list_net_results, LIST_NETWORK_MAX);
				if(ret != -1) {
					printf("%s\n", list_net_results);
				}
				return EXIT_SUCCESS;

			case 'i':
				ret = wifimanager_get_status(&status);
				if(ret >= 0) {
					printf("wifi state:   %s\n", wifistate2string(status.state));
					if((status.state == WIFI_STATE_GOT_IP) ||
					   (status.state == WIFI_STATE_CONNECTED) ||
					   (status.state == WIFI_STATE_DHCPC_REQUEST)) {
						printf("ssid:%s\n", status.ssid);
						printf("bssid:%s\n", status.bssid);
						printf("key mgmt: %s\n", status.key_mgmt);
						printf("current frequency: %d GHz\n", status.freq);
						printf("ip address: %s\n", status.ip_address);
						printf("mac address: %s\n", status.mac_address);

						printf("rssi: %d\n", status.rssi);
						printf("link speed: %d\n", status.link_speed);
						printf("noise: %d\n", status.noise);
					}

				}
				return EXIT_SUCCESS;

			case 'r':
				if(argc < 2) {
					printf("  -r, --remove_net\tremove network in config, -r <ssid>\n");
					return EXIT_SUCCESS;
				}
				printf("removing ssid:%s\n", argv[optind]);
				wifimanager_remove_networks(argv[optind], strlen(argv[optind]));
				return EXIT_SUCCESS;

			case 'o':
				if (wifimanager_init(&cb)) {
					printf("wifimanager init failed\n");
					return -1;
				}

				printf("wifimanager init ok\n");

				/* create a child process, in case user_callback_thread don't be closed
				 * just for this test. if your farther process won't exit, ignore it
				 */
				wait_loop();

				return EXIT_SUCCESS;

			case 'd':
				wifimanager_deinit();
				printf("wifimanager deinit ok\n");
				return EXIT_SUCCESS;

			default:
				printf("Try '%s -h' for more information.\n", argv[0]);
				return EXIT_FAILURE;
		}
		if (optind == argc)
			goto usage;
	}
}
