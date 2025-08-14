/*
 * Copyright (c) 2024-2025, ArtInChip Technology Co., Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Authors:  haidong.pan <haidong.pan@artinchip.com>
 */

#include "ui_objects.h"
#include "aic_ui.h"
#include "ui_util.h"


void screen_create(ui_manager_t *ui)
{
    screen_t *scr = screen_get(ui);

    if (!ui->auto_del && scr->obj)
        return;

    // Init scr->obj
    scr->obj = lv_obj_create(NULL);
    lv_obj_set_scrollbar_mode(scr->obj, LV_SCROLLBAR_MODE_OFF);

    // Set style of scr->obj
    lv_obj_set_style_bg_color(scr->obj, lv_color_hex(0xfcfcfc), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(scr->obj, 255, LV_PART_MAIN | LV_STATE_DEFAULT);

    // Init scr->image_descrip
    scr->image_descrip = lv_img_create(scr->obj);
    lv_img_set_src(scr->image_descrip, LVGL_IMAGE_PATH(description.png));
    lv_img_set_pivot(scr->image_descrip, 50, 50);
    lv_img_set_angle(scr->image_descrip, 0);
    lv_obj_set_style_img_opa(scr->image_descrip, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_pos(scr->image_descrip, 0, 0);

    // Init scr->image_battery
    scr->image_battery = lv_img_create(scr->obj);
    lv_img_set_src(scr->image_battery, LVGL_IMAGE_PATH(battery_3.png));
    lv_img_set_pivot(scr->image_battery, 50, 50);
    lv_img_set_angle(scr->image_battery, 0);
    lv_obj_set_style_img_opa(scr->image_battery, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_pos(scr->image_battery, 545, 8);


}
