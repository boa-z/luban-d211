/*
 * Copyright (C) 2024-2026 ArtInChip Technology Co., Ltd.
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef DASHCAM_PREVIEW_H
#define DASHCAM_PREVIEW_H
#ifdef __cplusplus
extern "C" {
#endif
#include "lvgl/lvgl.h"
lv_obj_t *dashcam_preview_create(lv_obj_t *parent, lv_obj_t *recorder_obj);
lv_obj_t *dashcam_preview_get_recorder(lv_obj_t *screen);
int dashcam_preview_start();
int dashcam_preview_stop();
void dashcam_preview_update_time(lv_obj_t *screen, const char *str);
void dashcam_preview_set_recording(lv_obj_t *screen, int recording);
#ifdef __cplusplus
}
#endif
#endif
