/*
 * Copyright (c) 2024-2026, ArtInChip Technology Co., Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Authors:  haidong.pan <haidong.pan@artinchip.com>
 */

#ifndef _BATTERY_H
#define _BATTERY_H

#define GPAI_CHAN_NUM    8
#define MAX_PATH_LEN     128
#define ADC_CHAN         7

struct battery_level {
    int adc_val;
    int level;
};

int check_battery_level();
int gpio_export(unsigned int gpio);
int gpio_set_dir(unsigned int gpio, const char *dir);
int gpio_get_value(unsigned int gpio, int *value);
int gpio_set_value(unsigned int gpio, int value);
int gpio_det_get(void);

#endif

