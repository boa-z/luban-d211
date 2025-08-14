// SPDX-License-Identifier: Apache-2.0
/*
 * Copyright (C) 2025 ArtInChip Technology Co., Ltd.
 * Authors:  Siyao.Li <siyao.li@artinchip.com>
 */
#include <unistd.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <getopt.h>
#include <fcntl.h>
#include "artinchip/sample_base.h"
#include <sys/time.h>
#include <sys/stat.h>
#include <linux/input.h>

#define GPAI_CHAN_NUM    8
#define MAX_PATH_LEN     128

static const struct battery_level {
    int adc_val;
    int level;
} battery_levels[] = {
    {2330, 100},
    {2291, 75},
    {2234, 50},
    {2177, 25},
    {0,    0}
};

static const char sopts[] = "c:h";
static const struct option lopts[] = {
    {"chan",    required_argument, NULL, 'c'},
    {"usage",   no_argument,       NULL, 'h'},
    {NULL,      0,                 NULL, 0}
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
        ERR("Failed to open %s: %s\n", filename, strerror(errno));
        return -1;
    }

    char buf[GPAI_CHAN_NUM] = {0};
    int ret = fread(buf, 1, sizeof(buf) - 1, f);
    fclose(f);

    if (ret <= 0) {
        ERR("fread() returned %d: %s\n", ret, strerror(errno));
        return -1;
    }

    return atoi(buf);
}

static void show_usage(const char *program_name)
{
    printf("\nUsage: %s [options]: \n", program_name);
    printf("\t-c, --chan <channel>\tThe channel for getting battery level (0-%d)\n", GPAI_CHAN_NUM-1);
    printf("\t-h, --usage\t\tShow this help message\n");
    printf("Example: %s -c 7\n", program_name);
}

static int calculate_battery_level(int adc_val)
{
    for (size_t i = 0; i < sizeof(battery_levels)/sizeof(battery_levels[0]); i++) {
        if (adc_val >= battery_levels[i].adc_val) {
            printf("Current ADC value: %d, battery level: %d%%\n", 
                  adc_val, battery_levels[i].level);
            return battery_levels[i].level;
        }
    }
    return 0;
}

int check_battery_level(u32 chan)
{
    char path[MAX_PATH_LEN] = {0};

    if (!file_exists("/tmp/gpai")) {
        system("ln -sf /sys/devices/platform/soc/*.gpai/iio:device0 /tmp/gpai");
    }

    if (!change_working_dir("/tmp/gpai")) {
        ERR("Failed to change to /tmp/gpai directory\n");
        return -1;
    }

    snprintf(path, sizeof(path), "in_voltage%d_raw", chan);
    int adc_val = read_int_from_file(path);

    if (adc_val < 0) {
        return -1;
    }

    return calculate_battery_level(adc_val);
}

int main(int argc, char *argv[])
{
    int opt = 0;
    u32 chan = 0;

    if (argc != 3) {
        show_usage(argv[0]);
        return -1;
    }

    while ((opt = getopt_long(argc, argv, sopts, lopts, NULL)) != -1) {
        switch (opt) {
        case 'c':
            chan = (u32)atoi(optarg);
            if (chan >= GPAI_CHAN_NUM) {
                ERR("Invalid channel number: %s (must be 0-%d)\n",
                    optarg, GPAI_CHAN_NUM-1);
                return -1;
            }
            printf("Selected channel: %u\n", chan);
            check_battery_level(chan);
            break;
        case 'h':
        default:
            show_usage(argv[0]);
            return -1;
        }
    }

    return 0;
}
