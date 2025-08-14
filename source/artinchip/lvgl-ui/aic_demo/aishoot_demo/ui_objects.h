/*
 * Copyright (c) 2024-2025, ArtInChip Technology Co., Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Authors:  haidong.pan <haidong.pan@artinchip.com>
 */

#ifndef _UI_OBJECTS
#define _UI_OBJECTS

#ifdef __cplusplus
extern "C" {
#endif

#include "lvgl.h"
#include "lv_conf_custom.h"

typedef struct {
    lv_obj_t *obj;
    lv_obj_t *image_descrip;
    lv_obj_t *image_battery;
} screen_t;


typedef struct {
    bool auto_del;
    screen_t screen;

} ui_manager_t;

static inline void ui_manager_init(ui_manager_t *ui, bool auto_del)
{
    memset(ui, 0 , sizeof(ui_manager_t));
    ui->auto_del = auto_del;
}

static inline screen_t *screen_get(ui_manager_t *ui)
{
    return &ui->screen;
}


void screen_create(ui_manager_t *ui);



extern ui_manager_t ui_manager;

#ifdef __cplusplus
} /*extern "C"*/
#endif

#endif // _UI_OBJECTS
