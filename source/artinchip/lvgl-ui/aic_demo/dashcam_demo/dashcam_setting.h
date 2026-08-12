/*
 * Copyright (C) 2024-2026 ArtInChip Technology Co., Ltd.
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef DASHCAM_SETTING_H
#define DASHCAM_SETTING_H
#ifdef __cplusplus
extern "C" {
#endif
#include "lvgl/lvgl.h"
#include "dashcam_engine.h"

lv_obj_t *dashcam_setting_create(lv_obj_t *parent, lv_obj_t *recorder_obj, const char *path);
void dashcam_setting_show(lv_obj_t *screen);
void dashcam_setting_hide(lv_obj_t *screen);
void dashcam_setting_close_cb(lv_event_t *e);
void dashcam_setting_load_config(void);
struct dashcam_config *dashcam_setting_get_config(void);
void dashcam_setting_update_space(lv_obj_t *screen, int64_t free_mb, int64_t total_mb);

#ifdef __cplusplus
}
#endif
#endif
