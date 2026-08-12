/*
 * Copyright (C) 2024-2026 ArtInChip Technology Co., Ltd.
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef DASHCAM_PLAYBACK_H
#define DASHCAM_PLAYBACK_H
#ifdef __cplusplus
extern "C" {
#endif

#include "lvgl/lvgl.h"

struct dashcam_engine;

lv_obj_t *dashcam_playback_create(lv_obj_t *parent, struct dashcam_engine *engine);
void dashcam_playback_refresh_file_list(lv_obj_t *screen);
void dashcam_playback_filter_by_date(lv_obj_t *screen, int year, int month, int day);
void dashcam_playback_set_setting_screen(lv_obj_t *setting_screen);

#ifdef __cplusplus
}
#endif
#endif
