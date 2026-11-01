/*
 * Copyright (c) 2024-2026, ArtInChip Technology Co., Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Authors:  haidong.pan <haidong.pan@artinchip.com>
 */

#include <unistd.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <getopt.h>
#include <fcntl.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <linux/input.h>
#include <linux/gpio.h>
#include <time.h>
#include <stdarg.h>
#include "battery.h"

#define GPIO_PATH "/sys/class/gpio"
#define MAX_BUF 64

#define GPIO_PF_DEVICE "/dev/gpiochip5"
#define GPIO_PIN_DET  0
#define POLL_INTERVAL_MS 500

#define DEBUG 0
#if DEBUG
void time_printf() {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    struct tm *tm = localtime(&tv.tv_sec);

    printf("[%04d-%02d-%02d %02d:%02d:%02d.%03ld] ",
           tm->tm_year+1900, tm->tm_mon+1, tm->tm_mday,
           tm->tm_hour, tm->tm_min, tm->tm_sec,
           tv.tv_usec/1000);
}
#endif

static const struct battery_level battery_levels[] = {
    {2810, 100},
    {2700, 75},
    {2570, 50},
    {2400, 25},
    {2350, 1},
    {0, 0}
};

bool file_exists(const char *path)
{
    struct stat st;
    return (path && stat(path, &st) == 0);
}

bool change_working_dir(const char *dir)
{
    char path[MAX_PATH_LEN] = {0};

    if (chdir(dir) != 0)
        return false;

    return (getcwd(path, sizeof(path)) != NULL);
}

int read_int_from_file(const char *filename)
{
    FILE *f = fopen(filename, "r");
    if (!f) {
        printf("Failed to open %s\n", filename);
        return -1;
    }

    char buf[GPAI_CHAN_NUM] = {0};
    int ret = fread(buf, 1, sizeof(buf) - 1, f);
    fclose(f);

    if (ret <= 0) {
        printf("fread() returned %d\n", ret);
        return -1;
    }

    return atoi(buf);
}

static int calculate_battery_level(int adc_val)
{
    for (size_t i = 0; i < sizeof(battery_levels)/sizeof(battery_levels[0]); i++) {
        if (adc_val >= battery_levels[i].adc_val) {
#if DEBUG
            time_printf();
#endif
            printf(" Current ADC value: %d, battery level: %d%%\n",
                  adc_val, battery_levels[i].level);
            return battery_levels[i].level;
        }
    }
    return 0;
}

int check_battery_level()
{
    char path[MAX_PATH_LEN] = {0};

    if (!file_exists("/tmp/gpai")) {
        system("ln -sf /sys/devices/platform/soc/*.gpai/iio:device0 /tmp/gpai");
    }

    if (!change_working_dir("/tmp/gpai")) {
        printf("Failed to change to /tmp/gpai directory\n");
        return -1;
    }

    snprintf(path, sizeof(path), "in_voltage%d_raw", ADC_CHAN);
    int adc_val = read_int_from_file(path);

    if (adc_val < 0) {
        return -1;
    }

    return calculate_battery_level(adc_val);
}

int gpio_export(unsigned int gpio)
{
    int fd = -1, len = 0;
    char buf[MAX_BUF] = {0};

    fd = open(GPIO_PATH "/export", O_WRONLY);
    if (fd < 0) {
        perror("gpio/export");
        return fd;
    }

    len = snprintf(buf, sizeof(buf), "%d", gpio);
    if (write(fd, buf, len) < 0) {
        perror("Failed to write to gpio/export");
        close(fd);
        return -1;
    }
    close(fd);

    return 0;
}

int gpio_set_dir(unsigned int gpio, const char *dir)
{
    int fd = -1, ret = -1;
    char buf[MAX_BUF];

    snprintf(buf, sizeof(buf), GPIO_PATH "/gpio%d/direction", gpio);

    fd = open(buf, O_WRONLY);
    if (fd < 0) {
        perror("gpio/direction");
        return fd;
    }

    ret = write(fd, dir, strlen(dir)+1);
    if (ret < 0) {
        printf("Failed to write %s to %s\n", dir, buf);
        close(fd);
        return -1;
    }

    close(fd);
    return 0;
}

int gpio_get_value(unsigned int gpio, int *value)
{
    int fd = -1, ret = -1;
    char buf[MAX_BUF] = {0};
    char ch = '0';

    snprintf(buf, sizeof(buf), GPIO_PATH "/gpio%d/value", gpio);

    fd = open(buf, O_RDONLY);
    if (fd < 0) {
        perror("open gpio/value fail");
        return fd;
    }

    ret = read(fd, &ch, 1);
    if (ret != 1) {
        perror("read gpio/value fail");
        close(fd);
        return -1;
    }

    *value = (ch != '0') ? 1 : 0;

    close(fd);
    return 0;
}

int gpio_set_value(unsigned int gpio, int value)
{
    int fd = -1;
    char buf[MAX_BUF] = {0};
    const char *val_str = (value == 0) ? "0" : "1";

    snprintf(buf, sizeof(buf), GPIO_PATH "/gpio%d/value", gpio);

    fd = open(buf, O_WRONLY);
    if (fd < 0) {
        perror("Failed to open gpio/value");
        return -1;
    }

    if (write(fd, val_str, 1) != 1) {
        perror("Failed to write to gpio/value");
        close(fd);
        return -1;
    }

    close(fd);
    return 0;
}

int gpio_det_get()
{
    int fd = -1;
    int ret = -1;
    struct gpio_v2_line_request req = {0};
    struct gpio_v2_line_values data = {0};

    fd = open(GPIO_PF_DEVICE, O_RDONLY);
    if (fd < 0) {
        perror("gpio dev open fail");
        return -1;
    }

    memset(&req, 0, sizeof(req));
    req.offsets[0] = GPIO_PIN_DET;
    req.config.flags = GPIO_V2_LINE_FLAG_INPUT | GPIO_V2_LINE_FLAG_BIAS_PULL_UP;
    req.num_lines = 1;

    strncpy(req.consumer, "gpio_input_pullup", sizeof(req.consumer) - 1);
    req.consumer[sizeof(req.consumer) - 1] = '\0';

    ret = ioctl(fd, GPIO_V2_GET_LINE_IOCTL, &req);
    if (ret < 0) {
        perror("request req fail");
        close(fd);
        return -1;
    }

    data.mask = 1;
    ret = ioctl(req.fd, GPIO_V2_LINE_GET_VALUES_IOCTL, &data);
    if (ret < 0) {
        perror("gpio get value fail");
        close(fd);
        return -1;
    }

    printf("gpio value: %d\n", (int)data.bits & 1);

    close(req.fd);
    close(fd);

    return (int)data.bits & 1;
}
