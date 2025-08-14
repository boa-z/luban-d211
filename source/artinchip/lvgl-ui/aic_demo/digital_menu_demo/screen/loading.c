/*
 * Copyright (c) 2025, ArtInChip Technology Co., Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Authors:  Zequan Liang <zequan.liang@artinchip.com>
 */

#include "ui_objects.h"
#include "ui_util.h"

#define ESTIMATED_LOAD_TIME_MS 8000 // Estimated loading time 8s
#define PROGRESS_UPDATE_MS 50       // Progress update interval 50ms

// Non-linear progress calculation (0~99%)
static uint8_t calculate_progress(uint32_t elapsed_ms)
{
    if (elapsed_ms >= ESTIMATED_LOAD_TIME_MS)
        return 99; // Stay at 99% after reaching estimated time

    // Easing function: slow start → fast middle → slow end
    float t = (float)elapsed_ms / ESTIMATED_LOAD_TIME_MS;
    if (t < 0.5f)
        t = 2 * t * t;
    else
        t = -1 + (4 - 2 * t) * t;

    return (uint8_t)(t * 99);
}

// Timer callback: update progress
static void update_progress(lv_timer_t *timer)
{
    ui_manager_t *ui = (ui_manager_t *)timer->user_data;
    progress_screen_t *scr = progress_screen_get(ui);
    uint32_t elapsed = lv_tick_elaps(scr->start_time);

    if (scr->is_loading_complete)
    {
        lv_label_set_text(scr->progress_label, "100%");
        lv_bar_set_value(scr->progress_bar, 100, LV_ANIM_OFF);
        lv_timer_pause(scr->timer); // Stop timer
        // Page transition can be added here
#if USE_DOUBLE_SDL_DISP
        lv_disp_set_default(lv_display_get_next(NULL));
#endif
        lv_screen_load_anim(price_tags_screen_2_get(ui)->obj, LV_SCR_LOAD_ANIM_NONE, 0, 500, 0);
        return;
    }

    if (elapsed > ESTIMATED_LOAD_TIME_MS * 2)
    {
        lv_label_set_text(scr->progress_label, "Load failed");
        lv_spinner_set_anim_params(scr->spinner, 0, 0);
        
        // lv_obj_set_style_bg_color(scr->container, lv_palette_lighten(LV_PALETTE_RED, 1), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_text_color(scr->progress_label, lv_palette_main(LV_PALETTE_RED), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_bg_color(scr->progress_bar,  lv_palette_main(LV_PALETTE_RED), LV_PART_INDICATOR | LV_STATE_DEFAULT);
        lv_timer_pause(scr->timer);
        return;
    }

    uint8_t progress = calculate_progress(elapsed);
    lv_label_set_text_fmt(scr->progress_label, "Loading assets: %d%%", progress);
    lv_bar_set_value(scr->progress_bar, progress, LV_ANIM_OFF);
}

static void progress_screen_cb(lv_event_t *e)
{
    lv_event_code_t code = (lv_event_code_t)lv_event_get_code(e);
    ui_manager_t *ui = (ui_manager_t *)lv_event_get_user_data(e);
    progress_screen_t *scr = progress_screen_get(ui);

    if (code == LV_EVENT_SCREEN_UNLOAD_START) {
        lv_timer_delete(scr->timer);
    }
    if (code == LV_EVENT_DELETE) {;}
    if (code == LV_EVENT_SCREEN_LOADED) {
        scr->start_time = lv_tick_get();
        scr->is_loading_complete = false;
        lv_timer_resume(scr->timer);
    }
}

void progress_screen_create(ui_manager_t *ui)
{
    progress_screen_t *scr = progress_screen_get(ui);

    if (!ui->auto_del && scr->obj)
        return;
    
    scr->obj = lv_obj_create(NULL);
    lv_obj_set_scrollbar_mode(scr->obj, LV_SCROLLBAR_MODE_OFF);

    // Set style of scr->obj
    lv_obj_set_style_bg_color(scr->obj, lv_color_hex(0xfcfcfc), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(scr->obj, 255, LV_PART_MAIN | LV_STATE_DEFAULT);

    // Init scr->container
    scr->container = lv_obj_create(scr->obj);
    lv_obj_center(scr->container);
    lv_obj_set_size(scr->container, 700, 173);
    lv_obj_set_scrollbar_mode(scr->container, LV_SCROLLBAR_MODE_OFF);

    // Set style of scr->container
    lv_obj_set_style_bg_color(scr->container, lv_color_hex(0xe7f7ff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(scr->container, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(scr->container, 20, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_top(scr->container, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_right(scr->container, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_bottom(scr->container, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_left(scr->container, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    // Init scr->title_label
    scr->title_label = lv_label_create(scr->container);
    lv_label_set_text(scr->title_label, "Digital menu board");
    lv_label_set_long_mode(scr->title_label, LV_LABEL_LONG_WRAP);
    lv_obj_align(scr->title_label, LV_ALIGN_TOP_MID, 0, 10);
    lv_obj_set_size(scr->title_label, 160, 16);

    // Init scr->progress_bar
    scr->progress_bar = lv_bar_create(scr->container);
    lv_obj_set_style_anim_time(scr->progress_bar, 0, 0);
    lv_bar_set_value(scr->progress_bar, 50, LV_ANIM_OFF);
    lv_obj_align(scr->progress_bar, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_size(scr->progress_bar, 657, 20);

    // Init scr->progress_label
    scr->progress_label = lv_label_create(scr->container);
    lv_label_set_text(scr->progress_label, "Loading assets: 0%");
    lv_label_set_long_mode(scr->progress_label, LV_LABEL_LONG_WRAP);
    lv_obj_align(scr->progress_label, LV_ALIGN_LEFT_MID, 60, -28);
    lv_obj_set_size(scr->progress_label, 188, 20);

    // Init scr->spinner
#if LVGL_VERSION_MAJOR == 8
    scr->spinner = lv_spinner_create(scr->container, 1000, 60);
#else
    scr->spinner = lv_spinner_create(scr->container);
    lv_spinner_set_anim_params(scr->spinner, 1000, 60);
#endif
    lv_obj_align(scr->spinner, LV_ALIGN_LEFT_MID, 25, -30);
    lv_obj_set_size(scr->spinner, 26, 25);

    // Set style of scr->spinner
    lv_obj_set_style_arc_width(scr->spinner, 4, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_arc_width(scr->spinner, 4, LV_PART_INDICATOR | LV_STATE_DEFAULT);

    scr->timer = lv_timer_create(update_progress, PROGRESS_UPDATE_MS, ui);
    lv_timer_pause(scr->timer);

    lv_obj_add_event_cb(scr->obj, progress_screen_cb, LV_EVENT_ALL, ui);
}

void progress_screen_set_finish(ui_manager_t *ui)
{
    progress_screen_t *scr = progress_screen_get(ui);
    scr->is_loading_complete = true;
}
