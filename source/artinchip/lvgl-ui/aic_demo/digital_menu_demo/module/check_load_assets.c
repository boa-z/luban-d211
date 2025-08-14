/*
 * Copyright (c) 2023-2025, ArtInChip Technology Co., Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Authors:  Zequan Liang <zequan.liang@artinchip.com>
 */

#include "ui_objects.h"
#include "ui_util.h"

static uint32_t g_start_time;

#if USE_TRY_MTP_MOUNTED
#include <stdio.h>
#include <dirent.h>
#define MTP_MOUNT_PATH "/usr/local/share/lvgl_data"

int is_mtp_mounted(const char *mount_path)
{
    DIR *dir = opendir(mount_path);
    if (!dir) {
        return -1;
    }
    closedir(dir);
    return 0;
}
#endif
static void check_load_assets_cb(lv_timer_t *timer)
{
    ui_manager_t *ui = (ui_manager_t *)timer->user_data;
    uint32_t elapsed = lv_tick_elaps(g_start_time);
#if USE_TRY_MTP_MOUNTED
    if (is_mtp_mounted(MTP_MOUNT_PATH) == 0) {
        progress_screen_set_finish(ui);
        lv_timer_delete(timer);
    }
#endif
    if (elapsed > 20 * 1000) {
#if USE_DOUBLE_SDL_DISP
        progress_screen_set_finish(ui);
#endif
        LV_LOG_ERROR("try get assets timeout");
        lv_timer_delete(timer);
    }
}

void check_load_assets_create(ui_manager_t *ui)
{
    g_start_time = lv_tick_get();
    lv_timer_create(check_load_assets_cb, 50, ui);
}