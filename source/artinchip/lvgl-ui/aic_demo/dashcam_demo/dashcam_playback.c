/*
 * Copyright (C) 2024-2026 ArtInChip Technology Co., Ltd.
 * SPDX-License-Identifier: Apache-2.0
 *
 * Dashcam playback screen — browse & play recorded videos.
 * Layout: top bar + video area (left) + file list (right) + control bar (bottom)
 */
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include "dashcam_playback.h"
#include "dashcam_setting.h"
#include "dashcam_engine.h"
#include "lvgl/lvgl.h"
#if LVGL_VERSION_MAJOR == 9
#include "lv_aic_player.h"
#include "aic_player.h"
#endif

#define PLAYBACK_FILE_MAX  256
#define PLAYBACK_PATH_LEN  256

enum { PS_STOPPED, PS_PLAYING, PS_PAUSED };
enum { LIST_VIDEO, LIST_EVT, LIST_PIC };

static struct dashcam_engine *g_engine;
static lv_obj_t *g_player_obj, *g_play_btn_label, *g_file_list;
static char g_cur_file[256];
static lv_obj_t *g_progress_bar, *g_progress_knob;
static lv_timer_t *g_progress_timer;
static int g_play_state;
static s64 g_total_duration;
static bool g_total_fetched;
static bool g_seek_dragging;
static struct aic_recorder_record g_records[PLAYBACK_FILE_MAX];
static struct aic_recorder_picture g_pictures[PLAYBACK_FILE_MAX];
static int g_record_count;
static int g_list_mode = LIST_VIDEO;
static lv_obj_t *g_btn_video, *g_btn_evt, *g_btn_pic;
static lv_obj_t *g_pic_view;

static lv_style_t g_style_ctrl_btn, g_style_list_item, g_style_thumb;
static lv_style_t g_style_ts, g_style_dur, g_style_active, g_style_cam_toggle;
static lv_obj_t *g_calendar, *g_date_label, *g_cal_hdr, *g_arr_label;
static int g_sel_year, g_sel_month, g_sel_day;
static lv_obj_t *g_setting_screen;

/* ---- helpers ---- */

static int parse_filename_time(const char *filename, int *y, int *m, int *d,
                               int *hh, int *mm, int *ss)
{
    /* filename format: YYYYMMDD_HHMMSS.xxx */
    const char *base = strrchr(filename, '/');
    base = base ? base + 1 : filename;
    if (strlen(base) < 15) return -1;

    char buf[8] = {0};
    snprintf(buf, 5, "%s", base);      *y  = atoi(buf);
    snprintf(buf, 3, "%s", base + 4);  *m  = atoi(buf);
    snprintf(buf, 3, "%s", base + 6);  *d  = atoi(buf);
    snprintf(buf, 3, "%s", base + 9);  *hh = atoi(buf);
    snprintf(buf, 3, "%s", base + 11); *mm = atoi(buf);
    snprintf(buf, 3, "%s", base + 13); *ss = atoi(buf);
    return 0;
}

static void parse_timestamp(const char *filename, char *out, int out_len)
{
    int y, m, d, hh, mm, ss;
    if (parse_filename_time(filename, &y, &m, &d, &hh, &mm, &ss) == 0) {
        snprintf(out, out_len, "%02d:%02d:%02d", hh, mm, ss);
    } else {
        const char *base = strrchr(filename, '/');
        base = base ? base + 1 : filename;
        snprintf(out, out_len, "%.*s", out_len - 1, base);
    }
}

static void fmt_duration(int seconds, char *buf, int len)
{
    int m = seconds / 60, s = seconds % 60;
    snprintf(buf, len, "%d:%02d", m, s);
}

static void fmt_size(int64_t bytes, char *buf, int len)
{
    if (bytes > 1024 * 1024)
        snprintf(buf, len, "%lld MB", (long long)(bytes / (1024 * 1024)));
    else if (bytes > 1024)
        snprintf(buf, len, "%lld KB", (long long)(bytes / 1024));
    else
        snprintf(buf, len, "%lld B", (long long)bytes);
}

/* ---- date picker ---- */

static void calendar_event_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code != LV_EVENT_VALUE_CHANGED) {
        return;
    }

    lv_calendar_date_t d;
    lv_calendar_get_pressed_date(g_calendar, &d);
    if (d.day) { /* only on date click, not dropdown nav */
        char buf[32];
        snprintf(buf, sizeof(buf), "%04d-%02d-%02d", d.year, d.month, d.day);
        lv_label_set_text(g_date_label, buf);
        g_sel_year = d.year;
        g_sel_month = d.month;
        g_sel_day = d.day;
        lv_calendar_set_today_date(g_calendar, d.year, d.month, d.day);
        lv_dropdown_set_selected(lv_obj_get_child(g_cal_hdr, 0), 2035 - d.year);
        lv_dropdown_set_selected(lv_obj_get_child(g_cal_hdr, 1), d.month - 1);
        dashcam_playback_filter_by_date(NULL, d.year, d.month, d.day);
        LV_LOG_INFO("year:%d, mon:%d, data:%d.", d.year, d.month, d.day);
    }
}

static void date_btn_cb(lv_event_t *e)
{
    (void)e;
    if (lv_obj_has_flag(g_calendar, LV_OBJ_FLAG_HIDDEN)) {
        lv_obj_clear_flag(g_calendar, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text(g_arr_label, LV_SYMBOL_UP);
    } else {
        lv_obj_add_flag(g_calendar, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text(g_arr_label, LV_SYMBOL_DOWN);
    }
}

/* ---- list mode toggle (Video / Evt / Pic) ---- */

static void update_toggle_styles(void)
{
    lv_obj_set_style_bg_color(g_btn_video, lv_color_hex(g_list_mode == LIST_VIDEO ? 0x8b5cf6 : 0x2a2a2a), 0);
    lv_obj_set_style_bg_color(g_btn_evt,   lv_color_hex(g_list_mode == LIST_EVT   ? 0x8b5cf6 : 0x2a2a2a), 0);
    lv_obj_set_style_bg_color(g_btn_pic,   lv_color_hex(g_list_mode == LIST_PIC   ? 0x8b5cf6 : 0x2a2a2a), 0);
}

static void toggle_list_mode(int mode)
{
    if (g_list_mode == mode) return;
    g_list_mode = mode;
    g_cur_file[0] = '\0';
    update_toggle_styles();

    if (mode == LIST_PIC) {
        lv_aic_player_set_cmd(g_player_obj, LV_AIC_PLAYER_CMD_STOP, NULL);
        if (g_progress_timer)
            lv_timer_pause(g_progress_timer);
        lv_obj_add_flag(g_player_obj, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(g_pic_view, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_clear_flag(g_player_obj, LV_OBJ_FLAG_HIDDEN);
        lv_image_set_src(g_pic_view, NULL);
        lv_obj_add_flag(g_pic_view, LV_OBJ_FLAG_HIDDEN);
    }

    dashcam_playback_refresh_file_list(NULL);
}

static void btn_video_cb(lv_event_t *e) { (void)e; toggle_list_mode(LIST_VIDEO); }
static void btn_evt_cb(lv_event_t *e)   { (void)e; toggle_list_mode(LIST_EVT); }
static void btn_pic_cb(lv_event_t *e)   { (void)e; toggle_list_mode(LIST_PIC); }

/* ---- playback control ---- */

static void knob_move_to(int pct)
{
    if (!g_progress_knob) return;
    int bar_w = lv_obj_get_width(g_progress_bar);
    int bar_h = lv_obj_get_height(g_progress_bar);
    int knob_sz = lv_obj_get_width(g_progress_knob);
    int x = lv_obj_get_x(g_progress_bar) + (bar_w * pct / 100) - knob_sz / 2;
    int y = lv_obj_get_y(g_progress_bar) + (bar_h - knob_sz) / 2;
    lv_obj_set_pos(g_progress_knob, x, y);
}

static void progress_timer_cb(lv_timer_t *t)
{
    (void)t;
    u64 play_time = 0;
    lv_aic_player_set_cmd(g_player_obj, LV_AIC_PLAYER_CMD_GET_PLAY_TIME, &play_time);

    if (!g_total_fetched) {
        struct av_media_info info = {0};
        lv_aic_player_set_cmd(g_player_obj, LV_AIC_PLAYER_CMD_GET_MEDIA_INFO, &info);
        g_total_duration = info.duration;
        g_total_fetched = true;
    }

    if (g_total_duration > 0) {
        int pct = (int)(play_time * 100 / g_total_duration);
        if (pct > 100) pct = 100;
        lv_bar_set_value(g_progress_bar, pct, LV_ANIM_OFF);
        knob_move_to(pct);
    }
}

static void knob_seek_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    lv_indev_t *indev = lv_indev_get_act();
    int bar_x = lv_obj_get_x(g_progress_bar);
    int bar_w = lv_obj_get_width(g_progress_bar);

    if (code == LV_EVENT_PRESSING) {
        g_seek_dragging = true;
        if (g_progress_timer)
            lv_timer_pause(g_progress_timer);
    }
    if (g_seek_dragging && (code == LV_EVENT_PRESSING || code == LV_EVENT_PRESSED) && indev) {
        lv_point_t p;
        lv_indev_get_point(indev, &p);
        int pct = (p.x - bar_x) * 100 / bar_w;
        if (pct < 0) pct = 0;
        if (pct > 100) pct = 100;
        lv_bar_set_value(g_progress_bar, pct, LV_ANIM_OFF);
        knob_move_to(pct);
    }
    if (code == LV_EVENT_RELEASED && g_seek_dragging) {
        g_seek_dragging = false;
        if (g_total_duration > 0) {
            int val = lv_bar_get_value(g_progress_bar);
            u64 seek_us = (u64)((s64)val * g_total_duration / 100);
            lv_aic_player_set_cmd(g_player_obj, LV_AIC_PLAYER_CMD_SET_PLAY_TIME, &seek_us);
        }
        if (g_progress_timer)
            lv_timer_resume(g_progress_timer);
    }
}

static void play_file(const char *path)
{
    if (!g_player_obj || !path) return;
    lv_aic_player_set_src(g_player_obj, path);
    lv_aic_player_set_cmd(g_player_obj, LV_AIC_PLAYER_CMD_START, NULL);
    g_play_state = PS_PLAYING;
    lv_label_set_text(g_play_btn_label, LV_SYMBOL_PAUSE " Pause");
    const char *fn = strrchr(path, '/');
    fn = fn ? fn + 1 : path;

    snprintf(g_cur_file, sizeof(g_cur_file), "%s", fn);
    lv_bar_set_value(g_progress_bar, 0, LV_ANIM_OFF);
    g_total_fetched = false;
    if (!g_progress_timer)
        g_progress_timer = lv_timer_create(progress_timer_cb, 100, NULL);
    lv_timer_resume(g_progress_timer);
}

/* ---- file list ---- */

static void file_btn_cb(lv_event_t *e)
{
    lv_obj_t *btn = lv_event_get_target(e);
    int idx = (int)(intptr_t)lv_obj_get_user_data(btn);
    if (idx >= g_record_count) return;

    /* highlight active item */
    for (int i = 0; i < lv_obj_get_child_cnt(g_file_list); i++) {
        lv_obj_t *child = lv_obj_get_child(g_file_list, i);
        lv_obj_remove_style(child, &g_style_active, 0);
    }
    lv_obj_add_style(btn, &g_style_active, 0);

    if (g_list_mode == LIST_PIC) {
        char path[256] = {0};
        snprintf(path, sizeof(path), "L:%s", g_pictures[idx].file_path);
        lv_obj_move_foreground(g_pic_view);
        lv_image_set_src(g_pic_view, path);
        return;
    }

    play_file(g_records[idx].file_path);
}

static void rebuild_file_list_ui(void)
{
    lv_obj_clean(g_file_list);
    if (g_record_count == 0) {
        lv_obj_t *h = lv_label_create(g_file_list);
        lv_obj_set_style_text_color(h, lv_color_hex(0x666666), 0);
        lv_label_set_text(h, "No files");
        lv_obj_center(h);
        return;
    }

    for (int i = 0; i < g_record_count; i++) {
        lv_obj_t *row = lv_obj_create(g_file_list);
        lv_obj_set_size(row, lv_pct(100), 68);
        lv_obj_add_style(row, &g_style_list_item, 0);
        lv_obj_set_style_pad_all(row, 4, 0);
        lv_obj_set_style_margin_top(row, -2, 0);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                              LV_FLEX_ALIGN_CENTER);
        lv_obj_set_user_data(row, (void *)(intptr_t)i);
        lv_obj_add_event_cb(row, file_btn_cb, LV_EVENT_CLICKED, NULL);

        /* thumbnail */
        lv_obj_t *thumb = lv_obj_create(row);
        lv_obj_set_size(thumb, 88, 56);
        lv_obj_add_style(thumb, &g_style_thumb, 0);
        lv_obj_set_style_border_opa(thumb, LV_OPA_TRANSP, 0);
        lv_obj_t *ti = lv_label_create(thumb);
        lv_obj_set_style_text_font(ti, &lv_font_montserrat_14, 0);
        lv_obj_center(ti);

        /* info column */
        lv_obj_t *col = lv_obj_create(row);
        lv_obj_set_size(col, lv_pct(40), 56);
        lv_obj_set_style_bg_opa(col, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_opa(col, LV_OPA_TRANSP, 0);
        lv_obj_set_style_pad_all(col, 0, 0);
        lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);

        if (g_list_mode == LIST_PIC) {
            struct aic_recorder_picture *p = &g_pictures[i];
            lv_label_set_text(ti, LV_SYMBOL_IMAGE);

            /* extract HH:MM:SS from filename (local time, not UTC created_at) */
            char ts[16] = "--";
            int y, m, d, hh, mm, ss;
            if (parse_filename_time(p->file_path, &y, &m, &d, &hh, &mm, &ss) == 0)
                snprintf(ts, sizeof(ts), "%02d:%02d:%02d", hh, mm, ss);
            lv_obj_t *l1 = lv_label_create(col);
            lv_label_set_text(l1, ts);
            lv_obj_add_style(l1, &g_style_ts, 0);
            lv_obj_set_style_text_font(l1, &lv_font_montserrat_14, 0);

            char info[64];
            fmt_size(p->file_size, info, sizeof(info));
            lv_obj_t *l2 = lv_label_create(col);
            lv_label_set_text(l2, info);
            lv_obj_add_style(l2, &g_style_dur, 0);
            lv_obj_set_style_text_font(l2, &lv_font_montserrat_14, 0);
        } else if (g_list_mode == LIST_EVT) {
            struct aic_recorder_record *r = &g_records[i];
            lv_label_set_text(ti, LV_SYMBOL_WARNING);

            int y, m, d, hh, mm, ss;
            if (parse_filename_time(r->file_path, &y, &m, &d, &hh, &mm, &ss) == 0) {
                char buf[32];
                /* line 1: YYYY-MM-DD */
                snprintf(buf, sizeof(buf), "%04d-%02d-%02d", y, m, d);
                lv_obj_t *l1 = lv_label_create(col);
                lv_label_set_text(l1, buf);
                lv_obj_add_style(l1, &g_style_ts, 0);
                lv_obj_set_style_text_font(l1, &lv_font_montserrat_14, 0);
                /* line 2: HH:MM:SS + duration */
                char dur[16];
                fmt_duration(r->duration, dur, sizeof(dur));
                snprintf(buf, sizeof(buf), "%02d:%02d:%02d  %s", hh, mm, ss, dur);
                lv_obj_t *l2 = lv_label_create(col);
                lv_label_set_text(l2, buf);
                lv_obj_add_style(l2, &g_style_ts, 0);
                lv_obj_set_style_text_font(l2, &lv_font_montserrat_14, 0);
            }
        } else {
            struct aic_recorder_record *r = &g_records[i];
            lv_label_set_text(ti, LV_SYMBOL_VIDEO);

            char ts[32];
            parse_timestamp(r->file_path, ts, sizeof(ts));
            lv_obj_t *tsl = lv_label_create(col);
            lv_label_set_text(tsl, ts);
            lv_obj_add_style(tsl, &g_style_ts, 0);
            lv_obj_set_style_text_font(tsl, &lv_font_montserrat_14, 0);

            char dur[16];
            fmt_duration(r->duration, dur, sizeof(dur));
            lv_obj_t *dl = lv_label_create(col);
            lv_label_set_text(dl, dur);
            lv_obj_add_style(dl, &g_style_dur, 0);
            lv_obj_set_style_text_font(dl, &lv_font_montserrat_14, 0);
        }
    }
}

static int do_refresh(void)
{
    if (!g_engine) return 0;

    int count;
    if (g_list_mode == LIST_PIC) {
        count = dashcam_engine_get_picture_by_date(g_engine, g_sel_year, g_sel_month,
                                                    g_sel_day, g_pictures, PLAYBACK_FILE_MAX);
        g_record_count = count;
    } else if (g_list_mode == LIST_EVT) {
        count = dashcam_engine_get_locked_list(g_engine, g_records, PLAYBACK_FILE_MAX);
        g_record_count = count;
    } else {
        count = dashcam_engine_get_record_by_date(g_engine, g_sel_year, g_sel_month,
                                                   g_sel_day, g_records, PLAYBACK_FILE_MAX);
        int kept = 0;
        for (int i = 0; i < count; i++) {
            if (access(g_records[i].file_path, F_OK) == 0)
                g_records[kept++] = g_records[i];
        }
        g_record_count = kept;
    }

    LV_LOG_INFO("filter: mode:%d, %04d-%02d-%02d, count:%d",
                g_list_mode, g_sel_year, g_sel_month, g_sel_day, g_record_count);
    return g_record_count;
}

void dashcam_playback_refresh_file_list(lv_obj_t *screen)
{
    (void)screen;
    do_refresh();
    rebuild_file_list_ui();
}

void dashcam_playback_filter_by_date(lv_obj_t *screen, int year, int month, int day)
{
    (void)screen;
    g_sel_year = year;
    g_sel_month = month;
    g_sel_day = day;
    do_refresh();
    rebuild_file_list_ui();
}

/* ---- control buttons ---- */

static void btn_play_pause_cb(lv_event_t *e)
{
    (void)e;
    if (!g_player_obj) return;
    if (g_play_state == PS_STOPPED && g_record_count > 0) {
        play_file(g_records[0].file_path);
    } else if (g_play_state == PS_PLAYING) {
        lv_aic_player_set_cmd(g_player_obj, LV_AIC_PLAYER_CMD_PAUSE, NULL);
        g_play_state = PS_PAUSED;
        lv_label_set_text(g_play_btn_label, LV_SYMBOL_PLAY " Play");
    } else if (g_play_state == PS_PAUSED) {
        lv_aic_player_set_cmd(g_player_obj, LV_AIC_PLAYER_CMD_RESUME, NULL);
        g_play_state = PS_PLAYING;
        lv_label_set_text(g_play_btn_label, LV_SYMBOL_PAUSE " Pause");
    }
}

static void btn_stop_cb(lv_event_t *e)
{
    (void)e;
    if (!g_player_obj) return;
    lv_aic_player_set_cmd(g_player_obj, LV_AIC_PLAYER_CMD_STOP, NULL);
    g_play_state = PS_STOPPED;
    lv_label_set_text(g_play_btn_label, LV_SYMBOL_PLAY " Play");
    lv_bar_set_value(g_progress_bar, 0, LV_ANIM_OFF);
    if (g_progress_timer)
        lv_timer_pause(g_progress_timer);
}

static void btn_prev_cb(lv_event_t *e)
{
    (void)e;
    if (!g_record_count) return;
    for (int i = 0; i < g_record_count; i++) {
        if (strstr(g_records[i].file_path, g_cur_file)) {
            int prev = (i - 1 + g_record_count) % g_record_count;
            play_file(g_records[prev].file_path);
            return;
        }
    }
    play_file(g_records[0].file_path);
}

static void btn_next_cb(lv_event_t *e)
{
    (void)e;
    if (!g_record_count) return;
    for (int i = 0; i < g_record_count; i++) {
        if (strstr(g_records[i].file_path, g_cur_file)) {
            int next = (i + 1) % g_record_count;
            play_file(g_records[next].file_path);
            return;
        }
    }
    play_file(g_records[0].file_path);
}

static void btn_delete_cb(lv_event_t *e)
{
    (void)e;
    if (!g_record_count || !g_engine) return;
    for (int i = 0; i < g_record_count; i++) {
        const char *fn = strrchr(g_records[i].file_path, '/');
        fn = fn ? fn + 1 : g_records[i].file_path;
        if (strncmp(fn, g_cur_file, sizeof(g_cur_file)) == 0) {
            lv_aic_player_set_cmd(g_player_obj, LV_AIC_PLAYER_CMD_STOP, NULL);
            g_play_state = PS_STOPPED;
            lv_label_set_text(g_play_btn_label, LV_SYMBOL_PLAY " Play");
            remove(g_records[i].file_path);
            g_cur_file[0] = '\0';
            dashcam_playback_refresh_file_list(NULL);
            return;
        }
    }
}

static void btn_settings_cb(lv_event_t *e)
{
    (void)e;
    if (g_setting_screen)
        dashcam_setting_show(g_setting_screen);
}

void dashcam_playback_set_setting_screen(lv_obj_t *setting_screen)
{
    g_setting_screen = setting_screen;
}

/* ---- create UI ---- */

lv_obj_t *dashcam_playback_create(lv_obj_t *parent, struct dashcam_engine *engine)
{
    g_engine = engine;

    lv_obj_t *screen = lv_obj_create(parent);
    lv_obj_set_size(screen, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_opa(screen, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_opa(screen, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(screen, 0, 0);
    lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);

    /* ---- styles ---- */
    lv_style_init(&g_style_ctrl_btn);
    lv_style_set_border_opa(&g_style_ctrl_btn, LV_OPA_TRANSP);
    lv_style_set_shadow_opa(&g_style_ctrl_btn, LV_OPA_TRANSP);
    lv_style_set_outline_opa(&g_style_ctrl_btn, LV_OPA_TRANSP);
    lv_style_set_radius(&g_style_ctrl_btn, 6);
    lv_style_set_bg_color(&g_style_ctrl_btn, lv_color_hex(0x444444));
    lv_style_set_bg_opa(&g_style_ctrl_btn, LV_OPA_COVER);
    lv_style_set_text_color(&g_style_ctrl_btn, lv_color_hex(0xFFFFFF));

    lv_style_init(&g_style_list_item);
    lv_style_set_border_opa(&g_style_list_item, LV_OPA_TRANSP);
    lv_style_set_shadow_opa(&g_style_list_item, LV_OPA_TRANSP);
    lv_style_set_outline_opa(&g_style_list_item, LV_OPA_TRANSP);
    lv_style_set_bg_opa(&g_style_list_item, LV_OPA_TRANSP);
    lv_style_set_radius(&g_style_list_item, 4);
    lv_style_set_pad_all(&g_style_list_item, 4);

    lv_style_init(&g_style_active);
    lv_style_set_border_opa(&g_style_active, LV_OPA_TRANSP);
    lv_style_set_shadow_opa(&g_style_active, LV_OPA_TRANSP);
    lv_style_set_outline_opa(&g_style_active, LV_OPA_TRANSP);
    lv_style_set_border_color(&g_style_active, lv_color_hex(0x8b5cf6));
    lv_style_set_border_width(&g_style_active, 2);
    lv_style_set_border_opa(&g_style_active, LV_OPA_COVER);

    lv_style_init(&g_style_thumb);
    lv_style_set_border_opa(&g_style_thumb, LV_OPA_TRANSP);
    lv_style_set_shadow_opa(&g_style_thumb, LV_OPA_TRANSP);
    lv_style_set_outline_opa(&g_style_thumb, LV_OPA_TRANSP);
    lv_style_set_bg_color(&g_style_thumb, lv_color_hex(0x2a2a3a));
    lv_style_set_bg_opa(&g_style_thumb, LV_OPA_COVER);
    lv_style_set_radius(&g_style_thumb, 4);
    lv_style_set_text_color(&g_style_thumb, lv_color_hex(0x666688));

    lv_style_init(&g_style_ts);
    lv_style_set_border_opa(&g_style_ts, LV_OPA_TRANSP);
    lv_style_set_shadow_opa(&g_style_ts, LV_OPA_TRANSP);
    lv_style_set_outline_opa(&g_style_ts, LV_OPA_TRANSP);
    lv_style_set_text_color(&g_style_ts, lv_color_hex(0xDDDDDD));

    lv_style_init(&g_style_dur);
    lv_style_set_border_opa(&g_style_dur, LV_OPA_TRANSP);
    lv_style_set_shadow_opa(&g_style_dur, LV_OPA_TRANSP);
    lv_style_set_outline_opa(&g_style_dur, LV_OPA_TRANSP);
    lv_style_set_text_color(&g_style_dur, lv_color_hex(0x888888));

    lv_style_init(&g_style_cam_toggle);
    lv_style_set_radius(&g_style_cam_toggle, 13);
    lv_style_set_bg_opa(&g_style_cam_toggle, LV_OPA_COVER);
    lv_style_set_border_opa(&g_style_cam_toggle, LV_OPA_TRANSP);
    lv_style_set_shadow_opa(&g_style_cam_toggle, LV_OPA_TRANSP);
    lv_style_set_outline_opa(&g_style_cam_toggle, LV_OPA_TRANSP);

    /* ---- top bar ---- */
    /* Date button (top left) */
    lv_obj_t *date_btn = lv_btn_create(screen);
    lv_obj_set_size(date_btn, 180, 30);
    lv_obj_align(date_btn, LV_ALIGN_TOP_LEFT, 10, 6);
    lv_obj_set_style_bg_color(date_btn, lv_color_hex(0x1a1a1a), 0);
    lv_obj_set_style_bg_opa(date_btn, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(date_btn, 6, 0);
    lv_obj_set_style_border_opa(date_btn, LV_OPA_TRANSP, 0);
    lv_obj_set_style_shadow_opa(date_btn, LV_OPA_TRANSP, 0);
    lv_obj_set_style_outline_opa(date_btn, LV_OPA_TRANSP, 0);
    g_date_label = lv_label_create(date_btn);
    char ds[32];
    time_t tn = time(NULL);
    struct tm *lt = localtime(&tn);
    strftime(ds, sizeof(ds), "%Y-%m-%d", lt);
    lv_label_set_text(g_date_label, ds);
    lv_obj_set_style_text_color(g_date_label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(g_date_label, &lv_font_montserrat_14, 0);
    lv_obj_align(g_date_label, LV_ALIGN_LEFT_MID, 26, 0);
    /* calendar icon */
    lv_obj_t *cal_icon = lv_label_create(date_btn);
    lv_label_set_text(cal_icon, LV_SYMBOL_LIST);
    lv_obj_set_style_text_color(cal_icon, lv_color_hex(0xaaaaaa), 0);
    lv_obj_set_style_text_font(cal_icon, &lv_font_montserrat_14, 0);
    lv_obj_align(cal_icon, LV_ALIGN_LEFT_MID, 6, 0);
    lv_obj_add_event_cb(date_btn, date_btn_cb, LV_EVENT_CLICKED, NULL);
    /* dropdown arrow */
    g_arr_label = lv_label_create(date_btn);
    lv_label_set_text(g_arr_label, LV_SYMBOL_DOWN);
    lv_obj_set_style_text_color(g_arr_label, lv_color_hex(0xaaaaaa), 0);
    lv_obj_set_style_text_font(g_arr_label, &lv_font_montserrat_14, 0);
    lv_obj_align(g_arr_label, LV_ALIGN_RIGHT_MID, -6, 0);

    /* Calendar popup */
    g_calendar = lv_calendar_create(screen);
    lv_obj_set_style_text_color(g_calendar, lv_color_hex(0x333333), LV_PART_MAIN);
    lv_obj_set_size(g_calendar, 360, 360);
    lv_obj_align_to(g_calendar, date_btn, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 4);
    lv_obj_set_style_bg_color(g_calendar, lv_color_hex(0xe8f5e9), 0);
    lv_obj_set_style_bg_opa(g_calendar, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(g_calendar, 6, 0);
    lv_obj_set_style_pad_all(g_calendar, 4, 0);
    lv_obj_add_flag(g_calendar, LV_OBJ_FLAG_HIDDEN);
    g_cal_hdr = lv_calendar_header_dropdown_create(g_calendar);
    lv_calendar_header_dropdown_set_year_list(g_calendar,
        "2035\n2034\n2033\n2032\n2031\n2030\n2029\n2028\n"
        "2027\n2026\n2025\n2024\n2023\n2022\n2021\n2020");
    time_t tn2 = time(NULL);
    struct tm *lt2 = localtime(&tn2);
    int this_year = lt2->tm_year + 1900;
    int this_mon  = lt2->tm_mon + 1;
    int this_day  = lt2->tm_mday;
    g_sel_year = this_year;
    g_sel_month = this_mon;
    g_sel_day = this_day;
    lv_calendar_set_today_date(g_calendar, this_year, this_mon, this_day);
    lv_calendar_set_showed_date(g_calendar, this_year, this_mon);
    /* sync dropdowns to current year/month */
    lv_obj_t *year_dd = lv_obj_get_child(g_cal_hdr, 0);
    lv_obj_t *mon_dd  = lv_obj_get_child(g_cal_hdr, 1);
    if (year_dd) lv_dropdown_set_selected(year_dd, 2035 - this_year);
    if (mon_dd)  lv_dropdown_set_selected(mon_dd, this_mon - 1);
    lv_obj_add_event_cb(g_calendar, calendar_event_cb, LV_EVENT_ALL, NULL);

    /* Video / Evt / Pic toggle */
    lv_obj_t *cam_toggle = lv_obj_create(screen);
    lv_obj_set_size(cam_toggle, 266, 32);
    lv_obj_align(cam_toggle, LV_ALIGN_TOP_MID, 0, 6);
    lv_obj_set_style_bg_color(cam_toggle, lv_color_hex(0x1a1a1a), 0);
    lv_obj_set_style_bg_opa(cam_toggle, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(cam_toggle, 14, 0);
    lv_obj_set_style_border_opa(cam_toggle, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(cam_toggle, 0, 0);
    lv_obj_clear_flag(cam_toggle, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(cam_toggle, LV_FLEX_FLOW_ROW);

    g_btn_video = lv_btn_create(cam_toggle);
    lv_obj_set_size(g_btn_video, 80, 28);
    lv_obj_add_style(g_btn_video, &g_style_cam_toggle, 0);
    lv_obj_set_style_bg_color(g_btn_video, lv_color_hex(0x8b5cf6), 0);
    lv_obj_add_event_cb(g_btn_video, btn_video_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *vl = lv_label_create(g_btn_video);
    lv_label_set_text(vl, LV_SYMBOL_VIDEO " Video");
    lv_obj_set_style_text_font(vl, &lv_font_montserrat_14, 0);
    lv_obj_center(vl);

    g_btn_evt = lv_btn_create(cam_toggle);
    lv_obj_set_size(g_btn_evt, 80, 28);
    lv_obj_add_style(g_btn_evt, &g_style_cam_toggle, 0);
    lv_obj_set_style_bg_color(g_btn_evt, lv_color_hex(0x2a2a2a), 0);
    lv_obj_add_event_cb(g_btn_evt, btn_evt_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *cl = lv_label_create(g_btn_evt);
    lv_label_set_text(cl, LV_SYMBOL_WARNING " Evt");
    lv_obj_set_style_text_font(cl, &lv_font_montserrat_14, 0);
    lv_obj_center(cl);

    g_btn_pic = lv_btn_create(cam_toggle);
    lv_obj_set_size(g_btn_pic, 80, 28);
    lv_obj_add_style(g_btn_pic, &g_style_cam_toggle, 0);
    lv_obj_set_style_bg_color(g_btn_pic, lv_color_hex(0x2a2a2a), 0);
    lv_obj_add_event_cb(g_btn_pic, btn_pic_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *pl = lv_label_create(g_btn_pic);
    lv_label_set_text(pl, LV_SYMBOL_IMAGE " Pic");
    lv_obj_set_style_text_font(pl, &lv_font_montserrat_14, 0);
    lv_obj_center(pl);

    /* Setting button (top right) */
    lv_obj_t *btn_settings = lv_btn_create(screen);
    lv_obj_set_size(btn_settings, 32, 32);
    lv_obj_align(btn_settings, LV_ALIGN_TOP_RIGHT, -10, 5);
    lv_obj_add_style(btn_settings, &g_style_ctrl_btn, 0);
    lv_obj_add_event_cb(btn_settings, btn_settings_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *stl = lv_label_create(btn_settings);
    lv_label_set_text(stl, LV_SYMBOL_SETTINGS);
    lv_obj_center(stl);


    /* Delete button (top right) */
    lv_obj_t *btn_del = lv_btn_create(screen);
    lv_obj_set_size(btn_del, 70, 32);
    lv_obj_align(btn_del, LV_ALIGN_TOP_RIGHT, -50, 5);
    lv_obj_add_style(btn_del, &g_style_ctrl_btn, 0);
    lv_obj_add_event_cb(btn_del, btn_delete_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *dl = lv_label_create(btn_del);
    lv_label_set_text(dl, LV_SYMBOL_TRASH);
    lv_obj_center(dl);

    /* ---- video area (left) ---- */
    g_player_obj = lv_aic_player_create(screen);
    lv_obj_set_pos(g_player_obj, 5, 42);
    lv_obj_set_size(g_player_obj, 800, 480);
    lv_aic_player_set_width(g_player_obj, 800);
    lv_aic_player_set_height(g_player_obj, 480);
    lv_aic_player_set_draw_layer(g_player_obj, LV_AIC_PLAYER_LAYER_VIDEO);
    lv_aic_player_set_auto_restart(g_player_obj, false);

    /* picture image view — constrained to video area */
    g_pic_view = lv_image_create(screen);
    lv_obj_set_pos(g_pic_view, 5, 42);
    lv_obj_set_size(g_pic_view, 800, 480);
    lv_image_set_inner_align(g_pic_view, LV_IMAGE_ALIGN_STRETCH);
    lv_obj_set_style_bg_color(g_pic_view, lv_color_hex(0x000000), 0);

    /* Progress bar — bottom, just above control bar */
    g_progress_bar = lv_bar_create(screen);
    lv_obj_set_size(g_progress_bar, 800, 5);
    lv_obj_set_pos(g_progress_bar, 5, 498);
    lv_obj_set_style_bg_color(g_progress_bar, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(g_progress_bar, LV_OPA_20, 0);
    lv_bar_set_range(g_progress_bar, 0, 100);
    lv_bar_set_value(g_progress_bar, 0, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(g_progress_bar, lv_color_hex(0xef4444), LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(g_progress_bar, LV_OPA_20, LV_PART_INDICATOR);
    lv_obj_add_event_cb(g_progress_bar, knob_seek_cb, LV_EVENT_ALL, NULL);

    /* independent red circle knob (LV_PART_KNOB not rendering) */
    g_progress_knob = lv_obj_create(screen);
    lv_obj_set_size(g_progress_knob, 16, 16);
    lv_obj_set_pos(g_progress_knob, 5 - 8, 498 - 4);
    lv_obj_set_style_bg_color(g_progress_knob, lv_color_hex(0xef4444), 0);
    lv_obj_set_style_bg_opa(g_progress_knob, LV_OPA_COVER, 0);
    lv_obj_set_style_border_opa(g_progress_knob, LV_OPA_TRANSP, 0);
    lv_obj_set_style_radius(g_progress_knob, LV_RADIUS_CIRCLE, 0);
    lv_obj_add_flag(g_progress_knob, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(g_progress_knob, knob_seek_cb, LV_EVENT_ALL, NULL);
    lv_obj_move_foreground(g_progress_knob);

    /* ---- file list panel (right) ---- */
    lv_obj_t *list_panel = lv_obj_create(screen);
    lv_obj_set_size(list_panel, 200, 480);
    lv_obj_set_pos(list_panel, 824, 42);
    lv_obj_set_style_bg_color(list_panel, lv_color_hex(0x1a1a1a), 0);
    lv_obj_set_style_bg_opa(list_panel, LV_OPA_COVER, 0);
    lv_obj_set_style_border_opa(list_panel, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(list_panel, 6, 0);
    lv_obj_clear_flag(list_panel, LV_OBJ_FLAG_SCROLLABLE);

    /* Front/Rear camera toggle */
    lv_obj_t *cam_switch = lv_obj_create(list_panel);
    lv_obj_set_size(cam_switch, lv_pct(100), 26);
    lv_obj_set_style_bg_opa(cam_switch, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_opa(cam_switch, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(cam_switch, 0, 0);
    lv_obj_set_flex_flow(cam_switch, LV_FLEX_FLOW_ROW);

    lv_obj_t *btn_front = lv_btn_create(cam_switch);
    lv_obj_set_size(btn_front, 66, 26);
    lv_obj_set_style_bg_color(btn_front, lv_color_hex(0x8b5cf6), 0);
    lv_obj_set_style_radius(btn_front, 13, 0);
    lv_obj_set_style_border_opa(btn_front, LV_OPA_TRANSP, 0);
    lv_obj_t *fl = lv_label_create(btn_front);
    lv_label_set_text(fl, "Front");
    lv_obj_set_style_text_font(fl, &lv_font_montserrat_14, 0);
    lv_obj_center(fl);

    lv_obj_t *btn_rear = lv_btn_create(cam_switch);
    lv_obj_set_size(btn_rear, 66, 26);
    lv_obj_set_style_bg_color(btn_rear, lv_color_hex(0x2a2a2a), 0);
    lv_obj_set_style_radius(btn_rear, 13, 0);
    lv_obj_set_style_border_opa(btn_rear, LV_OPA_TRANSP, 0);
    lv_obj_t *rl = lv_label_create(btn_rear);
    lv_label_set_text(rl, "Rear");
    lv_obj_set_style_text_font(rl, &lv_font_montserrat_14, 0);
    lv_obj_center(rl);


    g_file_list = lv_obj_create(list_panel);
    lv_obj_set_size(g_file_list, lv_pct(100), lv_pct(85));
    lv_obj_set_pos(g_file_list, 0, 28);
    lv_obj_set_style_bg_opa(g_file_list, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_opa(g_file_list, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(g_file_list, 2, 0);
    lv_obj_set_style_pad_row(g_file_list, 0, 0);
    lv_obj_set_flex_flow(g_file_list, LV_FLEX_FLOW_COLUMN);

    g_play_state = PS_STOPPED;

    /* ---- bottom control bar ---- */
    lv_obj_t *ctrl_bar = lv_obj_create(screen);
    lv_obj_set_size(ctrl_bar, lv_pct(100), 50);
    lv_obj_set_pos(ctrl_bar, 0, 510);
    lv_obj_set_style_bg_color(ctrl_bar, lv_color_hex(0x1a1a1a), 0);
    lv_obj_set_style_bg_opa(ctrl_bar, LV_OPA_COVER, 0);
    lv_obj_set_style_border_opa(ctrl_bar, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(ctrl_bar, 0, 0);
    lv_obj_clear_flag(ctrl_bar, LV_OBJ_FLAG_SCROLLABLE);

    /* Prev */
    lv_obj_t *bp = lv_btn_create(ctrl_bar);
    lv_obj_set_size(bp, 60, 38);
    lv_obj_align(bp, LV_ALIGN_LEFT_MID, 240, 0);
    lv_obj_add_style(bp, &g_style_ctrl_btn, 0);
    lv_obj_add_event_cb(bp, btn_prev_cb, LV_EVENT_CLICKED, NULL);
    pl = lv_label_create(bp);
    lv_label_set_text(pl, LV_SYMBOL_PREV);
    lv_obj_center(pl);

    /* Play / Pause */
    lv_obj_t *bplay = lv_btn_create(ctrl_bar);
    lv_obj_set_size(bplay, 120, 42);
    lv_obj_align(bplay, LV_ALIGN_CENTER, -50, 0);
    lv_obj_add_style(bplay, &g_style_ctrl_btn, 0);
    lv_obj_add_event_cb(bplay, btn_play_pause_cb, LV_EVENT_CLICKED, NULL);
    g_play_btn_label = lv_label_create(bplay);
    lv_label_set_text(g_play_btn_label, LV_SYMBOL_PLAY " Play");
    lv_obj_center(g_play_btn_label);

    /* Stop */
    lv_obj_t *bs = lv_btn_create(ctrl_bar);
    lv_obj_set_size(bs, 60, 38);
    lv_obj_align(bs, LV_ALIGN_CENTER, 70, 0);
    lv_obj_add_style(bs, &g_style_ctrl_btn, 0);
    lv_obj_add_event_cb(bs, btn_stop_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *sl = lv_label_create(bs);
    lv_label_set_text(sl, LV_SYMBOL_STOP);
    lv_obj_center(sl);

    /* Next */
    lv_obj_t *bn = lv_btn_create(ctrl_bar);
    lv_obj_set_size(bn, 60, 38);
    lv_obj_align(bn, LV_ALIGN_RIGHT_MID, -240, 0);
    lv_obj_add_style(bn, &g_style_ctrl_btn, 0);
    lv_obj_add_event_cb(bn, btn_next_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *nl = lv_label_create(bn);
    lv_label_set_text(nl, LV_SYMBOL_NEXT);
    lv_obj_center(nl);

    /* keep top bar + calendar on top */
    lv_obj_move_foreground(date_btn);
    lv_obj_move_foreground(cam_toggle);
    lv_obj_move_foreground(g_calendar);

    return screen;
}
