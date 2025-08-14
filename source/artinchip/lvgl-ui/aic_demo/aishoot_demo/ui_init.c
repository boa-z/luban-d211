/*
 * Copyright (c) 2024-2025, ArtInChip Technology Co., Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Authors:  haidong.pan <haidong.pan@artinchip.com>
 */

#include "ui_init.h"
#include "ui_util.h"
#include "aic_ui.h"
#include "custom/custom.h"

ui_manager_t ui_manager;


void ui_init(void)
{

    // auto delete screen
    ui_manager_init(&ui_manager, true);

    screen_create(&ui_manager);
    lv_scr_load(screen_get(&ui_manager)->obj);

    // custom code
    custom_init(&ui_manager);
}
