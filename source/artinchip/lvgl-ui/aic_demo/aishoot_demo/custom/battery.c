/*
 * Copyright (c) 2024-2025, ArtInChip Technology Co., Ltd
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
#include "artinchip/sample_base.h"
#include <sys/time.h>
#include <sys/stat.h>
#include <linux/input.h>

#include "battery.h"

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

int check_battery_level()
{
    char path[MAX_PATH_LEN] = {0};

    if (!file_exists("/tmp/gpai")) {
        system("ln -sf /sys/devices/platform/soc/*.gpai/iio:device0 /tmp/gpai");
    }

    if (!change_working_dir("/tmp/gpai")) {
        ERR("Failed to change to /tmp/gpai directory\n");
        return -1;
    }

    snprintf(path, sizeof(path), "in_voltage%d_raw", ADC_CHAN);
    int adc_val = read_int_from_file(path);

    if (adc_val < 0) {
        return -1;
    }

    return calculate_battery_level(adc_val);
}

