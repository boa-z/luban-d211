// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2020-2022 Artinchip Technology Co., Ltd.
 * Authors:  Matteo <duanmt@artinchip.com>
 */

#include "artinchip/sample_base.h"
#include <sys/time.h>
#include <sys/stat.h>
#include <linux/input.h>

/* Global macro and variables */

#define ADC_CHAN_GPAI7		7
#define ADC_CHAN_TSEN3		11
#define ADC_CHAN_RTP_XP		12
#define ADC_CHAN_RTP_XN		13
#define ADC_CHAN_RTP_YP		14
#define ADC_CHAN_RTP_YN		15
#define ADC_CHAN_NUM		16

#define ADC_DM_SRAM_SIZE	512

static const char sopts[] = "d:c:hv";
static const struct option lopts[] = {
	{"device",	required_argument, NULL, 'd'},
	{"channel",	required_argument, NULL, 'c'},
	{"usage",	no_argument, NULL, 'h'},
	{"verbose",	no_argument, NULL, 'v'},
	{0, 0, 0, 0}
};

static int sram_data[ADC_DM_SRAM_SIZE] = {0};
static int rtp_data[4][ADC_DM_SRAM_SIZE] = {{0}};
static char event_dev[32] = "";
static int g_verbose = 0;
static u32 g_err_cnt = 0;

/* Functions */

int usage(char *program)
{
	printf("Compile time: %s\n", __TIME__);
	printf("Usage: %s [options]\n", program);
	printf("\t -d, --device\t\tThe name of event device\n");
	printf("\t -c, --channel\t\tSelect one channel in [0, 15]\n");
	printf("\t -v, --verbose\t\tShow more debug information\n");
	printf("\t -h, --usage \n");
	printf("\n");
	printf("Example: %s -c 0\n", program);
	return 0;
}

int is_dir(char *path)
{
	struct stat st = {0};
	int ret = 0;

	if (!path)
		return 0;

	ret = stat(path, &st);
	if (ret) {
		ERR("Failed to access %s\n", path);
		return 0;
	}

	if (g_verbose)
		DBG("%s mode: %#x\n", path, st.st_mode);
	return st.st_mode & S_IFDIR;
}

int is_exist(char *path)
{
	struct stat st = {0};
	int ret = 0;

	if (!path)
		return 0;

	ret = stat(path, &st);
	if (ret)
		return 0;
	else
		return 1;
}

void change_cwd(char *dir)
{
	char path[128] = "";

	chdir(dir);
	getcwd(path, 128);

	if (g_verbose)
		printf("Current path: %s\n", path);
}

int abs(int val)
{
	if (val < 0)
		return -val;
	else
		return val;
}

int average(int *data, u32 size, int trim)
{
	int sum = 0, i, min = *data, max = 0;

	if (!data || size < 3) {
		ERR("Invalid argument: data %p, size %d\n", data, size);
		return 0;
	}

	for (i = 0; i < size; i++) {
		if (data[i] < min)
			min = data[i];
		if (data[i] > max)
			max = data[i];
		sum += data[i];
	}

	if (trim)
		return (sum - min - max) / (size - 2);
	else
		return sum/size;
}

void hexdump(int *data, u32 size)
{
	int i, j;
	int *pdata = NULL;

	for (i = 0; i < size/8; i++) {
		printf("%2d 0x%03x: ", i, i * 8);
		pdata = &data[i * 8];
		for(j = 0; j < 8; j++)
			printf("%4d ", pdata[j]);

		printf("| %4d | %4d | %2d\n",
		       average(pdata, 8, 0), average(pdata, 8, 1), i);
	}
	printf("\n");
}

int read_int(char *filename)
{
	FILE *f = NULL;
	int ret = 0;
	char buf[16] = "";

	f = fopen(filename, "r");
	if (!f) {
		ERR("Failed to open %s errno: %d[%s]\n",
			filename, errno, strerror(errno));
		return -1;
	}

	ret = fread(buf, 1, 16, f);
	if (ret <= 0) {
		ERR("fread() return %d, errno: %d[%s]\n",
			ret, errno, strerror(errno));
		fclose(f);
		return -1;
	}

	ret = str2int(buf);
	fclose(f);
	return ret;
}

int adc_select_chan(u32 chan)
{
	s32 fd = -1, ret = 0;
	char filename[8] = "dm_chan";
	char ch[4] = "";

	snprintf(ch, 4, "%d", chan);

	fd = open(filename, O_RDWR);
	if (fd < 0) {
		ERR("Failed to open %s errno: %d[%s]\n",
			filename, errno, strerror(errno));
		return fd;
	}

	ret = write(fd, ch, 2);
	if (ret < 0)
		ERR("Failed to write(%d) %s\n", fd, ch);
	else
		ret = 0;

	close(fd);
	return ret;
}

int adc_load_data(int *data, u32 size)
{
	s32 fd = -1, ret = 0;
	char filename[8] = "sram";

	fd = open(filename, O_WRONLY);
	if (fd < 0) {
		ERR("Failed to open %s errno: %d[%s]\n",
			filename, errno, strerror(errno));
		return fd;
	}

	if (size > 0) {
		ret = write(fd, data, size*4);
		if (ret < 0)
			ERR("Failed to write(%d) %d\n", fd, size);
		else
			ret = 0;
	}

	close(fd);
	return ret;
}

void gen_adc_data(u32 *size)
{
	int i;
	int *pdata = sram_data;

	if (g_verbose)
		DBG("Generate %d data ...\n", ADC_DM_SRAM_SIZE);
	for (i = 0; i < ADC_DM_SRAM_SIZE; i++)
		*pdata++ = 1800 + abs(i%256 - 128) - 64;

	*size = ADC_DM_SRAM_SIZE;
	if (g_verbose) {
		DBG("The ADC data, size %d:\n", *size);
		hexdump(sram_data, *size);
	}
}

int check_gpai_data(u32 chan, u32 size)
{
	int i, expect, cur;
	char tmp[80] = "ln -sf /sys/devices/platform/soc/*.gpai/iio:device0 /tmp/gpai";

	if (!is_exist("/tmp/gpai"))
		system(tmp);
	change_cwd("/tmp/gpai");
	sprintf(tmp, "in_voltage%d_raw", chan);

	for (i = 0; i < size/8; i++) {
		expect = average(&sram_data[i*8], 8, 0);
		cur = read_int(tmp);
		if (expect != cur) {
			printf("GPAI%d data %2d is invalid! %d/%d\n",
			       chan, i, cur, expect);
			g_err_cnt++;
		}
		else
			printf("GPAI%d data %2d is OK! %d/%d\n",
			       chan, i, cur, expect);
		/* When GPAI driver work in period mode,
		   the peroid should better be 2 seconds. */
		sleep(1);
	}

	return 0;
}

int check_tsen_data(u32 chan, u32 size)
{
	int i, expect, cur;
	char tmp[64] = "";

	sprintf(tmp, "/sys/class/thermal/thermal_zone%d/temp", chan);

	for (i = 0; i < size/8; i++) {
		expect = average(&sram_data[i*8], 8, 0);
		cur = read_int(tmp);
		if (expect != cur) {
			printf("Tsen%d data %2d is invalid! %d/%d\n",
			       chan, i, cur, expect);
			g_err_cnt++;
		}
		else
			printf("Tsen%d data %2d is OK! %d/%d\n",
			       chan, i, cur, expect);
		/* When tsen driver work in period mode,
		   the peroid should better be 2 seconds. */
		sleep(1);
	}

	return 0;
}

void gen_rtp_data(u32 *size)
{
	int i, j, base;
	int *pdata = NULL;
	char *chan[4] = {"XP", "XN", "YP", "YN"};

	if (g_verbose)
		DBG("Generate %d data ...\n", ADC_DM_SRAM_SIZE);
	for (i = 0; i < 4; i++) {
		base = 1000 + 500 * i;
		pdata = rtp_data[i];
		for (j = 0; j < ADC_DM_SRAM_SIZE; j++)
			*pdata++ = base + abs(j % 512 - 256) - 128;

		if (g_verbose) {
			DBG("The RTP %s data, size %d:\n", chan[i], ADC_DM_SRAM_SIZE);
			hexdump(rtp_data[i], ADC_DM_SRAM_SIZE);
		}
	}

	*size = ADC_DM_SRAM_SIZE;
}

int load_rtp_data(u32 size)
{
	int i;

	for (i = 0; i < 4; i++) {
		if (adc_select_chan(ADC_CHAN_RTP_XP + i))
			return -1;

		if (adc_load_data(rtp_data[i], size))
			return -1;
	}
	return 0;
}

void gen_rtp_press(u32 down)
{
	static int cur_stat = 0;

	if (cur_stat == down)
		return;

	if (down)
		system("echo 1 > rtp_down");
	else
		system("echo 0 > rtp_down");

	cur_stat = down;
	if (g_verbose)
		DBG("Generate a %s event\n", down ? "press-down" : "pop-up");
}

void check_rtp_auto1(u32 cnt, int x, int y)
{
	int expect_x, expect_y;
	int *pdata_xn = rtp_data[1];
	int *pdata_yn = rtp_data[3];

	expect_x = average(&pdata_xn[cnt * 8], 8, 1);
	expect_y = average(&pdata_yn[cnt * 8], 8, 1);

	if ((expect_x != x) || (expect_y != y)) {
		printf("Point %2d is invalid! x %d/%d, y %d/%d\n",
			cnt, x, expect_x, y, expect_y);
		g_err_cnt++;
	}
	else
		printf("Point %2d is OK!\n", cnt);
}

void check_rtp_auto4(u32 cnt, int x, int y, int press)
{
	int expect_x, expect_y, expect_press;
	int *pdata_x, *pdata_y, *pdata_za, *pdata_zb;
	static int second_point = 0;

	if (second_point) {
		pdata_x = &rtp_data[0][(cnt - 1) * 8];
		pdata_y = &rtp_data[2][(cnt - 1) * 8];
		pdata_za = &rtp_data[0][cnt * 8];
		pdata_zb = &rtp_data[3][cnt * 8];
	}
	else {
		pdata_x = &rtp_data[1][cnt * 8];
		pdata_y = &rtp_data[3][cnt * 8];
		pdata_za = &rtp_data[2][(cnt + 1) * 8];
		pdata_zb = &rtp_data[1][(cnt + 1) * 8];
	}

	expect_x = average(pdata_x, 8, 1);
	expect_y = average(pdata_y, 8, 1);
	expect_press = (average(pdata_za, 8, 1) + average(pdata_zb, 8, 1)) / 2;

	if (expect_x != x || expect_y != y || expect_press != press) {
		printf("Point %2d is invalid! x %d/%d, y %d/%d, press %d/%d\n",
			cnt, x, expect_x, y, expect_y, press, expect_press);
		g_err_cnt++;
	}
	else
		printf("Point %2d is OK!\n", cnt);

	second_point = !second_point;
}

int check_rtp_data(u32 size)
{
	int fd = -1, syn_cnt = 0, ret;
	int x = 0, y = 0, press = 0;
	int popup_happened = 0;
	struct input_event event;


	fd = open(event_dev, O_RDONLY);
	if (fd < 0) {
		ERR("Failed to open(%s)\n", event_dev);
		return -1;
	}

	while (1) {
		if (syn_cnt % 32 == 0)
			gen_rtp_press(1);
		else if (syn_cnt % 32 == 31)
			gen_rtp_press(0);

		memset(&event, 0, sizeof(struct input_event));
		ret = read(fd, &event, sizeof(struct input_event));
		if (ret < 0) {
			ERR("Failed to read(%d)\n", fd);
			break;
		}
		if (g_verbose)
			DBG("Type %#x, code %#x, value %d\n",
			    event.type, event.code, event.value);

		if (event.type == EV_SYN) {
			printf("Recv point %2d: x %4d, y %4d, press %4d. ",
				syn_cnt, x, y, press);

			/* Must ignore the EV_SYN event after a POP-UP event */
			if (!popup_happened) {
				if (press)
					check_rtp_auto4(syn_cnt, x, y, press);
				else
					check_rtp_auto1(syn_cnt, x, y);
				syn_cnt++;
			}
			else
				popup_happened = 0;

			if (syn_cnt >= size / 8)
				break;
			else
				continue;
		}
		else if (event.type == EV_ABS) {
			if (event.code == ABS_X)
				x = event.value;
			else if (event.code == ABS_Y)
				y = event.value;
			else if (event.code == ABS_PRESSURE)
				press = event.value;
		}
		else if (event.type == EV_KEY) {
			if (event.value == 1)
				printf("\nDetect press-down ...\n");
			else if (event.value == 0) {
				printf("\nDetect pop-up ...\n");
				popup_happened = 1;
			}
		}
	}

	close(fd);
	return 0;
}

int adc_dm_test(u32 chan)
{
	u32 size = 0;
	int ret = 0;

	if (chan <= ADC_CHAN_GPAI7) {
		if (adc_select_chan(chan))
			return -1;

		gen_adc_data(&size);
		if (adc_load_data(sram_data, size))
			return -1;
		ret = check_gpai_data(chan, size);
	}
	else if (chan <= ADC_CHAN_TSEN3) {
		if (adc_select_chan(chan))
			return -1;

		gen_adc_data(&size);
		if (adc_load_data(sram_data, size))
			return -1;
		ret = check_tsen_data(chan - ADC_CHAN_GPAI7 - 1, size);
	}
	else {
		if (!is_exist(event_dev))
			return -1;

		gen_rtp_data(&size);
		if (load_rtp_data(size))
			return -1;
		ret = check_rtp_data(size);
	}

	if (g_err_cnt)
		printf("\nThere are %d invalid data! Please check it\n",g_err_cnt);
	else
		printf("\nAll data is OK. Test Successfully!\n");

	return ret;
}

int main(int argc, char **argv)
{
	int c, chan = 0;
	char tmp[80] = "ln -sf /sys/devices/platform/soc/*.adcim /tmp/adcim";

	if (argc < 2) {
		usage(argv[0]);
		return -1;
	}

	while ((c = getopt_long(argc, argv, sopts, lopts, NULL)) != -1) {
		switch (c) {
		case 'd':
			strncpy(event_dev, optarg, 32);
			continue;

		case 'c':
			chan = str2int(optarg);
			if ((chan < 0) || (chan >= ADC_CHAN_NUM)) {
				ERR("Invalid channel No.%s\n", optarg);
				return -1;
			}
			printf("You select the channel %d\n", chan);
			continue;
		case 'v':
			g_verbose = 1;
			continue;
		case 'h':
		default:
			return usage(argv[0]);
		}
	}

	if (!is_exist("/tmp/adcim"))
		system(tmp);
	change_cwd("/tmp/adcim");

	return adc_dm_test(chan);
}
