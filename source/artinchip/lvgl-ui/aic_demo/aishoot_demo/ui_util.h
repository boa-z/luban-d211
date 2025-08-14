/*
 * Copyright (c) 2024-2025, ArtInChip Technology Co., Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Authors:  haidong.pan <haidong.pan@artinchip.com>
 */

#ifndef _UI_UTIL_H
#define _UI_UTIL_H

#ifdef __cplusplus
extern "C" {
#endif

#include "lvgl.h"

bool screen_is_loading(lv_obj_t *scr);

void ui_style_init(lv_style_t * style);

lv_font_t *ui_font_init(char *path, int size);

#ifdef __cplusplus
} /*extern "C"*/
#endif

#endif // _UI_UTIL_H
