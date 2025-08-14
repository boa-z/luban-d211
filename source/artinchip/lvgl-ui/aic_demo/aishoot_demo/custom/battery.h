/*
 * Copyright (c) 2024-2025, ArtInChip Technology Co., Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Authors:  haidong.pan <haidong.pan@artinchip.com>
 */

#include "artinchip/sample_base.h"

#define GPAI_CHAN_NUM    8
#define MAX_PATH_LEN     128
#define ADC_CHAN		 7

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

int check_battery_level();

