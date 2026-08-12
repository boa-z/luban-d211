/*
 * Copyright (C) 2023-2026 ArtInChip Technology Co., Ltd.
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Authors:  dwj <weijie.ding@artinchip.com>
 */

#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <netinet/in.h>
#include <net/if.h>
#include <sys/ioctl.h>
#include <arpa/inet.h>
#include <sys/socket.h>

#include <sys/time.h>
#include <pthread.h>
#include <linux/rpmsg_aic.h>
#include <artinchip/sample_base.h>

#define MAX_REMOTE_NUMBER               3
#define GMAC_PARA_SIZE		        3

/* SESS wakeup control register */
#define PRCM_SESS_WAKEUP_ADDR	0x88000100
/* SESS address configuration value */
#define PRCM_SESS_ADDRESS	0x7f000080

struct mbox_info {
	int fd;
	char *name;
};

struct eth_info {
	char mac[6];
	char mac_reserved[2];
	char ipv4[4];
	char ipv4_reserved[4];
};

/* Open a device file to be needed. */
static int device_open(char *_fname, int _flag)
{
	s32 fd = -1;

	fd = open(_fname, _flag);
	if (fd < 0) {
		ERR("Failed to open %s errno: %d[%s]\n",
		    _fname, errno, strerror(errno));
		exit(0);
	}
	return fd;
}

static int get_eth_info(const char *eth_name, struct eth_info *info)
{
	struct ifreq ifr;
	int sock = -1;

	sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
	if (sock == -1) {
		return -1;
	}

	memset(&ifr, 0, sizeof(ifr));
	strncpy(ifr.ifr_name, eth_name, IFNAMSIZ - 1);

	if (ioctl(sock, SIOCGIFFLAGS, &ifr) < 0) {
		close(sock);
		ERR("Interface %s: Can't get IF status.\n", eth_name);
		return -1;
	}

	if (!(ifr.ifr_ifru.ifru_flags & IFF_RUNNING)) {
		system("ifconfig eth0 up");
		sleep(1);
	}

	if (ioctl(sock, SIOCGIFHWADDR, &ifr) == -1) {
		close(sock);
		return -1;
	}
	memcpy(info->mac, ifr.ifr_hwaddr.sa_data, sizeof(info->mac));

	if (ioctl(sock, SIOCGIFADDR, &ifr) == -1) {
		close(sock);
		if (errno == 19)
			ERR("Interface %s: No such device.\n", eth_name);
		if (errno == 99)
			ERR("Interface %s: No IPv4 address assigned.\n", eth_name);
		return -1;
	}
	memcpy(info->ipv4, &((struct sockaddr_in *)&ifr.ifr_addr)->sin_addr, sizeof(info->ipv4));

	close(sock);

	return 0;
}

int read_rpmsg_name(const char *path, char *buffer, size_t size)
{
	int fd;
	ssize_t len;

	fd = open(path, O_RDONLY);
	if (fd < 0) {
		ERR("Failed to open %s errno: %d[%s]\n", path, errno, strerror(errno));
		return -1;
	}

	len = read(fd, buffer, size - 1);
	if (len < 0) {
		ERR("Failed to read %s errno: %d[%s]\n", path, errno, strerror(errno));
		close(fd);
		return -1;
	}

	buffer[len] = '\0';

	close(fd);
	return 0;
}

static const char sopts[] = "csh";
static const struct option lopts[] = {
	{"coldboot",	no_argument, NULL, 'c'},
	{"secure",		no_argument, NULL, 's'},
	{"usage",		no_argument, NULL, 'h'},
	{0, 0, 0, 0}
};

int usage(char *program)
{
	printf("Usage: %s [options]\n", program);
	printf("\t -s, --secure	\tTest secure sleep/wakeup flow\n");
	printf("\t -c, --coldboot \tTest coldboot flow\n");
	printf("\t -h, --help \n");
	printf("\t 	\t\tDefault is non-secure mode\n");
	return 0;
}

static int write_prcm_sess_addr(void)
{
	int fd;
	void *mmap_addr = NULL;
	volatile unsigned int *reg_ptr;
	unsigned long page_size = sysconf(_SC_PAGESIZE);
	unsigned long aligned_addr = PRCM_SESS_WAKEUP_ADDR & ~(page_size - 1);
	unsigned long offset = PRCM_SESS_WAKEUP_ADDR - aligned_addr;

	fd = open("/dev/mem", O_RDWR | O_SYNC);
	if (fd < 0) {
		printf("[PRCM ERROR] Failed to open /dev/mem: %s\n", strerror(errno));
		return -1;
	}

	mmap_addr = mmap(NULL, page_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, aligned_addr);
	if (mmap_addr == MAP_FAILED) {
		printf("[PRCM ERROR] Failed to mmap 0x%08lX: %s\n", aligned_addr, strerror(errno));
		close(fd);
		return -1;
	}

	/* Get register address */
	reg_ptr = (volatile unsigned int *)((unsigned char *)mmap_addr + offset);

	/* Write register */
	*reg_ptr = PRCM_SESS_ADDRESS;

	/* Cache sync: ensure other cores can see the latest value */
	msync((void *)reg_ptr, sizeof(unsigned int), MS_SYNC);

	/* Memory barrier */
	__sync_synchronize();

	/* Release resources */
	munmap(mmap_addr, page_size);
	close(fd);

	return 0;
}

int main(int argc, char **argv)
{
	struct mbox_info mbox_dev[MAX_REMOTE_NUMBER] = {{0}};
	struct aic_rpmsg *msg = NULL;
	bool coldboot_mode = false;
	bool secure_mode = false;
	void *msg_new = NULL;
	char name[20] = "";
	int ret = 0, i, c;

	if (argc > 1) {
		while ((c = getopt_long(argc, argv, sopts, lopts, NULL)) != -1) {
			switch (c) {
			case 'c':
				coldboot_mode = true;
				printf("Run coldboot mode of SPSS & SCSS\n");
				break;
			case 's':
				secure_mode = true;
				printf("Run PM in Secure mode\n");
				break;
			case 'h':
				return usage(argv[0]);
			default:
				break;
			}
		}
	}

	if (read_rpmsg_name("/sys/class/rpmsg/rpmsg0/name", name, sizeof(name)) < 0)
		return -1;

	memset(mbox_dev, 0, sizeof(mbox_dev));

	if (!strncmp(name, "rpmsg-spss", 10)) {
		mbox_dev[0].name = "/dev/rpmsg0";
		mbox_dev[1].name = "/dev/rpmsg1";
	} else {
		mbox_dev[0].name = "/dev/rpmsg1";
		mbox_dev[1].name = "/dev/rpmsg0";
	}

	mbox_dev[2].name = "/dev/mbox0";

	for (i = 0; i < MAX_REMOTE_NUMBER; i++) {
		mbox_dev[i].fd = device_open(mbox_dev[i].name, O_RDWR);
	}

	/* check idle */
	msg = malloc(sizeof(struct aic_rpmsg));
	if (!msg) {
		ret = -1;
		goto __exit_close;
	}
	msg->cmd = RPMSG_CMD_IS_IDLE;
	msg->seq = 0;
	msg->len = 0;

	c = 1;
	printf("Step %d. Confirm SPSS/SCSS/SESS is idle ...\n", c++);
	for (i = 0; i < MAX_REMOTE_NUMBER - 1; i++) {
		ret = write(mbox_dev[i].fd, msg, sizeof(struct aic_rpmsg));
		if (ret < 0) {
			ERR("Failed to send msg RPMSG_CMD_IS_IDLE to %s\n",
			    mbox_dev[i].name);
			goto __exit_eth;
		}
	}

	if (secure_mode) {
		ret = write(mbox_dev[2].fd, msg, sizeof(struct aic_rpmsg));
		if (ret < 0) {
			ERR("Failed to send msg RPMSG_CMD_IS_IDLE to %s\n",
			    mbox_dev[2].name);
			goto __exit_eth;
		}
	}

	for (i = 0; i < MAX_REMOTE_NUMBER - 1; i++) {
		ret = read(mbox_dev[i].fd, msg, sizeof(struct aic_rpmsg));
		if (ret < 0) {
			ERR("Failed to read msg from %s!\n", mbox_dev[i].name);
			goto __exit_eth;
		} else if (msg->cmd != RPMSG_CMD_ACK) {
			ERR("Comunication error %s!\n", mbox_dev[i].name);
						ret = -1;
			goto __exit_eth;
		}
	}

	if (secure_mode) {
		ret = read(mbox_dev[2].fd, msg, sizeof(struct aic_rpmsg));
		if (ret < 0) {
			ERR("Failed to read msg from %s!\n", mbox_dev[2].name);
			goto __exit_eth;
		} else if (msg->cmd != RPMSG_CMD_ACK) {
			ERR("Comunication error %s!\n", mbox_dev[2].name);
						ret = -1;
			goto __exit_eth;
		}
	}

	printf("Step %d. Pass GMAC parameters to SPSS ...\n", c++);
	struct eth_info *eth_info;
	eth_info = malloc(sizeof(struct eth_info));
	if (!eth_info) {
		ret = -1;
		goto __exit_eth;
	}
	memset(eth_info, 0, sizeof(struct eth_info));
	get_eth_info("eth0", eth_info);
	DBG("get mac info MAC addr: %x:%x:%x:%x:%x:%x, IPv4:%d.%d.%d.%d\n",
		 eth_info->mac[0], eth_info->mac[1], eth_info->mac[2], eth_info->mac[3],
		 eth_info->mac[4], eth_info->mac[5], eth_info->ipv4[0], eth_info->ipv4[1],
		 eth_info->ipv4[2], eth_info->ipv4[3]);
	int *info = (int *)eth_info;

	msg_new = realloc(msg, sizeof(struct aic_rpmsg) + sizeof(unsigned int) * GMAC_PARA_SIZE);
	if (!msg_new) {
		ret = -1;
		goto __exit;
	}

	msg = (struct aic_rpmsg *)msg_new;
	msg->cmd = RPMSG_CMD_GMAC_PARAMS;
	msg->seq = 0;
	msg->len = GMAC_PARA_SIZE;
	msg->data[0] = info[0]; //MAC addr low 6 bytes
	msg->data[1] = info[1]; //MAC addr high 2 bytes
	msg->data[2] = info[2]; //IP addr

	ret = write(mbox_dev[0].fd, msg, AIC_RPMSG_REAL_SIZE(GMAC_PARA_SIZE));
	if (ret < 0) {
		ERR("Failed to send msg RPMSG_CMD_GMAC_PARAMS to %s\n",
		    mbox_dev[0].name);
		goto __exit;
	}

	ret = read(mbox_dev[0].fd, msg, sizeof(struct aic_rpmsg));
	if (ret < 0) {
		ERR("Failed to read msg from %s!\n", mbox_dev[0].name);
		goto __exit;
	} else if (msg->cmd != RPMSG_CMD_ACK) {
		ERR("Comunication GMAC_PARAMS error %s!\n", mbox_dev[0].name);
				ret = -1;
		goto __exit;
	}

	printf("Step %d. Notify SPSS/SCSS/SESS to standby ...\n", c++);
	msg->cmd = RPMSG_CMD_REQ_STANDBY;
	msg->seq = 0;
	msg->len = 0;

	ret = write_prcm_sess_addr();
	if (ret < 0) {
		ERR("Failed to write PRCM_SESS_ADDRESS\n");
		goto __exit;
	}

	for (i = 0; i < MAX_REMOTE_NUMBER; i++) {
		ret = write(mbox_dev[i].fd, msg, sizeof(struct aic_rpmsg));
		if (ret < 0) {
			ERR("Failed to send msg REQ_STANDBY to %s\n",
			    mbox_dev[i].name);
			goto __exit;
		}
	}

	for (i = 0; i < MAX_REMOTE_NUMBER - 1; i++) {
		ret = read(mbox_dev[i].fd, msg, sizeof(struct aic_rpmsg));
		if (ret < 0) {
			ERR("Failed to read msg from %s!\n", mbox_dev[i].name);
			goto __exit;
		} else if (msg->cmd != RPMSG_CMD_ACK) {
			ERR("Comunication error %s!\n", mbox_dev[i].name);
			ret = -1;
			goto __exit;
		}
	}

	if (secure_mode) {
		ret = read(mbox_dev[2].fd, msg, sizeof(struct aic_rpmsg));
		if (ret < 0) {
			ERR("Failed to read msg from %s!\n", mbox_dev[2].name);
			goto __exit;
		} else if (msg->cmd != RPMSG_CMD_ACK) {
			ERR("Comunication error %s!\n", mbox_dev[2].name);
			ret = -1;
			goto __exit;
		}
	}

__exit:
	free(eth_info);

__exit_eth:
	free(msg);

__exit_close:
	/* close fd */
	for (i = 0; i < MAX_REMOTE_NUMBER; i++) {
		if (mbox_dev[i].fd > 0)
			close(mbox_dev[i].fd);
	}

	sleep(1);

	if (ret >= 0) {
		printf("\nStep %d. CSYS enter suspend ...\n", c++);
		system("echo mem > /sys/power/state");
		printf("\nStep %d. CSYS resumed successfully!\n", c++);
		ret = 0;
	} else {
		printf("CSYS failed to enter suspend!\n");
		return ret;
	}

	if (coldboot_mode) {
		printf("Step %d. Notify SPSS & SCSS to create link\n", c++);
		system("echo 1 > /sys/class/remoteproc/remoteproc0/kick");
		system("echo 1 > /sys/class/remoteproc/remoteproc1/kick");
	}

	return ret;
}
