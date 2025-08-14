/*
 * Copyright (c) 2024-2025, ArtInChip Technology Co., Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Authors:  haidong.pan <haidong.pan@artinchip.com>
 */

#include "custom.h"
#include "battery.h"

screen_t *scr;

static void timer_battery_callback(lv_timer_t *tmr)
{
	int level = check_battery_level();
	switch (level) {
		case 0:
			lv_img_set_src(scr->image_battery, LVGL_IMAGE_PATH(battery_1.png));
			break;
		case 25:
			lv_img_set_src(scr->image_battery, LVGL_IMAGE_PATH(battery_1.png));
			break;
		case 50:
			lv_img_set_src(scr->image_battery, LVGL_IMAGE_PATH(battery_2.png));
			break;
		case 75:
			lv_img_set_src(scr->image_battery, LVGL_IMAGE_PATH(battery_3.png));
			break;
		case 100:
			lv_img_set_src(scr->image_battery, LVGL_IMAGE_PATH(battery_4.png));
			break;
		default:
			lv_img_set_src(scr->image_battery, LVGL_IMAGE_PATH(battery_1.png));
	}

	return;
}

void custom_init(ui_manager_t *ui)
{
    /* Add your codes here */
	scr = screen_get(ui);
	lv_timer_create(timer_battery_callback, 5000, 0);
}

