/*
 * Copyright (C) 2024-2026 ArtInChip Technology Co., Ltd.
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include "lvgl/lvgl.h"
#include "dashcam_ui.h"
#include "dashcam_preview.h"
#include "dashcam_playback.h"
#include "dashcam_setting.h"
#include "dashcam_engine.h"
#include "lv_aic_recorder.h"

//#define STORAGE_PATH "/mnt/sdcard/"
#define STORAGE_PATH "/mnt/udisk/"
//#define ONLY_PLAYBACK

static struct dashcam_engine *g_engine;
static lv_obj_t *g_recorder_obj;

static lv_obj_t *g_preview_screen, *g_playback_screen, *g_setting_screen, *g_tabview;
#ifndef ONLY_PLAYBACK
static lv_timer_t *g_record_timer;
#endif
static int g_is_recording;

static void on_recorder_event(int event, void *data, void *user_data)
{
    (void)data;
    (void)user_data;

    switch (event) {
    case AIC_RECORDER_EVENT_COMPLETE:
        printf("[dashcam] recording complete\n");
        break;
    case AIC_RECORDER_EVENT_NO_SPACE:
        printf("[dashcam] no space\n");
        break;
    default:
        break;
    }
}

#ifndef ONLY_PLAYBACK
static void record_timer_cb(lv_timer_t *tmr)
{
    (void)tmr;

    time_t now = time(NULL);
    struct tm *lt = localtime(&now);
    char tbuf[32];
    strftime(tbuf, sizeof(tbuf), "%Y-%m-%d %H:%M:%S", lt);
    dashcam_preview_update_time(g_preview_screen, tbuf);

    if (!g_is_recording || !g_engine)
        return;
}

static void tab_changed_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_VALUE_CHANGED)
        return;
    int tab = lv_tabview_get_tab_act(g_tabview);
    if (tab == 0) {
        dashcam_preview_start();
    } else {
        dashcam_preview_stop();
        dashcam_playback_refresh_file_list(g_playback_screen);
    }
}
#endif

void dashcam_ui_init(void)
{
    g_is_recording = 1;

    /* 1. Create and init engine */
    g_engine = dashcam_engine_create();

    struct dashcam_config cfg;
    memset(&cfg, 0, sizeof(cfg));
    strncpy(cfg.storage_path, STORAGE_PATH, 255);
    cfg.file_duration = 60;
    cfg.file_num = 0;
    cfg.video_width = 640;
    cfg.video_height = 480;
    cfg.video_bitrate = 4000000;
    cfg.video_framerate = 30;
    cfg.qfactor = 80;

    if (dashcam_engine_init(g_engine, &cfg) != 0) {
        printf("[dashcam] engine init failed\n");
        return;
    }
    dashcam_engine_set_callback(g_engine, on_recorder_event, NULL);
    g_recorder_obj = dashcam_engine_get_recorder(g_engine);

    /* 2. Build UI — root screen fully transparent for video layer underneath */
    lv_obj_set_style_bg_opa(lv_scr_act(), LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_opa(lv_scr_act(), LV_OPA_TRANSP, 0);
    lv_obj_set_style_shadow_opa(lv_scr_act(), LV_OPA_TRANSP, 0);
    lv_obj_set_style_outline_opa(lv_scr_act(), LV_OPA_TRANSP, 0);

    g_tabview = lv_tabview_create(lv_scr_act());
    lv_tabview_set_tab_bar_position(g_tabview, LV_DIR_BOTTOM);
    lv_tabview_set_tab_bar_size(g_tabview, 48);
    lv_obj_set_size(g_tabview, 1024, 600);
    lv_obj_set_pos(g_tabview, 0, 0);
    lv_obj_set_style_bg_opa(g_tabview, LV_OPA_0, 0);
    lv_obj_set_style_border_opa(g_tabview, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(g_tabview, 0, 0);

    /* load config before preview (needs video resolution) */
    dashcam_setting_load_config();

#ifdef ONLY_PLAYBACK
    lv_obj_t *tab_playback = lv_tabview_add_tab(g_tabview, LV_SYMBOL_LIST " Playback");
    lv_obj_set_style_bg_opa(tab_playback, LV_OPA_0, 0);
    lv_obj_set_style_border_opa(tab_playback, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(tab_playback, 0, 0);

    g_playback_screen = dashcam_playback_create(tab_playback, g_engine);
    g_preview_screen = NULL;

    /* create settings screen (overlay on root) */
    g_setting_screen = dashcam_setting_create(lv_scr_act(), g_recorder_obj, STORAGE_PATH);
    dashcam_playback_set_setting_screen(g_setting_screen);

    /* start recording without preview */
    dashcam_engine_start_recording(g_engine);
    dashcam_playback_refresh_file_list(g_playback_screen);
#else
    lv_obj_t *tab_preview = lv_tabview_add_tab(g_tabview, LV_SYMBOL_VIDEO " Preview");
    lv_obj_t *tab_playback = lv_tabview_add_tab(g_tabview, LV_SYMBOL_LIST " Playback");
    lv_obj_set_style_bg_opa(tab_preview, LV_OPA_0, 0);
    lv_obj_set_style_border_opa(tab_preview, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(tab_preview, 0, 0);
    lv_obj_set_style_bg_opa(tab_playback, LV_OPA_0, 0);
    lv_obj_set_style_border_opa(tab_playback, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(tab_playback, 0, 0);

    /* preview screen — pass engine's recorder */
    g_preview_screen = dashcam_preview_create(tab_preview, g_recorder_obj);
    /* playback screen — pass engine for file queries */
    g_playback_screen = dashcam_playback_create(tab_playback, g_engine);

    /* create settings screen (overlay on root) */
    g_setting_screen = dashcam_setting_create(lv_scr_act(), g_recorder_obj, STORAGE_PATH);
    dashcam_playback_set_setting_screen(g_setting_screen);

    lv_obj_add_event_cb(g_tabview, tab_changed_cb, LV_EVENT_ALL, NULL);

    g_record_timer = lv_timer_create(record_timer_cb, 1000, NULL);

    /* 3. Start preview & recording */
    dashcam_preview_start();
    dashcam_engine_start_recording(g_engine);
    dashcam_preview_set_recording(g_preview_screen, 1);
    dashcam_playback_refresh_file_list(g_playback_screen);
#endif
}

void ui_init(void)
{
    dashcam_ui_init();
}
