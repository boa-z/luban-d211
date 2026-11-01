/*
 * Copyright (C) 2024-2026 ArtInChip Technology Co., Ltd.
 * SPDX-License-Identifier: Apache-2.0
 *
 * Dashcam settings screen — SD card info/format, date/time, video, audio, save/restore.
 * Layout per requirement.md §5.
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <stdbool.h>
#include <sys/stat.h>
#include <sys/statfs.h>
#include <unistd.h>
#include "dashcam_setting.h"
#include "dashcam_engine.h"
#include "lv_aic_recorder.h"

#define DASHCAM_MAX_PATH  256
#define RADIO_W  100
#define RADIO_H  34
#define CONFIG_FILE      "/usr/local/share/lvgl_data/dashcam_config.json"

/* ---- globals ---- */
static lv_obj_t *g_setting_screen;
static lv_obj_t *g_recorder_obj;
static struct dashcam_config g_cfg;
static struct dashcam_config g_default_cfg;

/* SD card info labels */
static lv_obj_t *g_space_total_label;
static lv_obj_t *g_space_used_label;
static lv_obj_t *g_space_free_label;
static lv_obj_t *g_space_status_label;

/* date / time */
static lv_obj_t *g_cur_time_label;
static lv_obj_t *g_date_ta;
static lv_obj_t *g_time_ta;

/* radio group objects */
static lv_obj_t *g_loop_btns[3];   /* 1min / 3min / 5min */
static lv_obj_t *g_sens_btns[3];   /* Low / Medium / High */
static int g_loop_duration = 180;   /* default 3 min */
static int g_sensitivity = 1;       /* 0=Low, 1=Medium, 2=High */

/* audio toggle */
static lv_obj_t *g_audio_sw;

/* ---- styles ---- */
static lv_style_t sty_card;
static lv_style_t sty_radio_on, sty_radio_off;
static lv_style_t sty_btn_prim, sty_btn_danger, sty_btn_sec;
static lv_style_t sty_ta;
static lv_style_t sty_title;
static lv_obj_t *g_format_popup;

static const char *g_loop_labels[] = { "1 min", "3 min", "5 min" };
static const char *g_sens_labels[] = { "Low", "Medium", "High" };

static void apply_radio_style(lv_obj_t *btn, bool on)
{
    if (on) {
        lv_obj_add_style(btn, &sty_radio_on, 0);
        lv_obj_remove_style(btn, &sty_radio_off, 0);
    } else {
        lv_obj_add_style(btn, &sty_radio_off, 0);
        lv_obj_remove_style(btn, &sty_radio_on, 0);
    }
}

static void update_loop_radio_ui(void)
{
    /* map duration to index: 60→0, 180→1, 300→2 */
    int idx = (g_loop_duration == 60) ? 0 : (g_loop_duration == 300) ? 2 : 1;
    for (int i = 0; i < 3; i++)
        apply_radio_style(g_loop_btns[i], i == idx);
}

static void update_sens_radio_ui(void)
{
    for (int i = 0; i < 3; i++)
        apply_radio_style(g_sens_btns[i], i == g_sensitivity);
}

static lv_obj_t *make_section_label(lv_obj_t *parent, const char *icon, const char *text)
{
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_set_size(row, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(row, 0, 0);
    lv_obj_set_style_margin_top(row, 10, 0);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t *ico = lv_label_create(row);
    lv_label_set_text(ico, icon);
    lv_obj_set_style_text_color(ico, lv_color_hex(0x667eea), 0);
    lv_obj_set_style_text_font(ico, &lv_font_montserrat_14, 0);

    lv_obj_t *l = lv_label_create(row);
    lv_label_set_text(l, text);
    lv_obj_set_style_text_color(l, lv_color_hex(0x8888CC), 0);
    lv_obj_set_style_text_font(l, &lv_font_montserrat_14, 0);
    lv_obj_set_style_margin_left(l, 6, 0);

    return row;
}

static lv_obj_t *make_label(lv_obj_t *parent, const char *text)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_label_set_text(l, text);
    lv_obj_set_style_text_color(l, lv_color_hex(0xBBBBBB), 0);
    lv_obj_set_style_text_font(l, &lv_font_montserrat_14, 0);
    return l;
}

static lv_obj_t *make_info_cell(lv_obj_t *parent, const char *label, const char *value)
{
    /* column: label on top, value below, flex_grow=1 so cells spread evenly */
    lv_obj_t *col = lv_obj_create(parent);
    lv_obj_set_size(col, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(col, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_opa(col, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(col, 0, 0);
    lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(col, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_flex_grow(col, 1);

    lv_obj_t *lbl = lv_label_create(col);
    lv_label_set_text(lbl, label);
    lv_obj_set_style_text_color(lbl, lv_color_hex(0x888888), 0);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_14, 0);

    lv_obj_t *val = lv_label_create(col);
    lv_label_set_text(val, value);
    lv_obj_set_style_text_color(val, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(val, &lv_font_montserrat_14, 0);
    lv_obj_set_style_margin_top(val, 2, 0);

    return val;
}

static lv_obj_t *make_radio_group(lv_obj_t *parent, const char **labels, int count,
                                  lv_obj_t **out_btns)
{
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_set_size(row, lv_pct(100), 44);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(row, 0, 0);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    for (int i = 0; i < count; i++) {
        lv_obj_t *btn = lv_btn_create(row);
        lv_obj_set_size(btn, RADIO_W, RADIO_H);
        lv_obj_set_style_radius(btn, 4, 0);
        lv_obj_set_style_border_width(btn, 1, 0);
        lv_obj_set_style_shadow_opa(btn, LV_OPA_TRANSP, 0);
        lv_obj_set_style_outline_opa(btn, LV_OPA_TRANSP, 0);

        lv_obj_t *lbl = lv_label_create(btn);
        lv_label_set_text(lbl, labels[i]);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_14, 0);
        lv_obj_center(lbl);

        out_btns[i] = btn;
    }
    return row;
}

/* ---- SD card space reader ---- */
static void refresh_storage_info(void)
{
    int64_t total_mb = 0, free_mb = 0, used_mb = 0;
    const char *status = "OK";
    char buf[64] = {0};
    int pct = 0;

    const char *path = g_cfg.storage_path;
    if (!path || !path[0]) path = "/mnt/sdcard";

    struct statfs st;
    if (statfs(path, &st) == 0) {
        uint64_t total = (uint64_t)st.f_bsize * st.f_blocks;
        uint64_t free  = (uint64_t)st.f_bsize * st.f_bfree;
        total_mb = (int64_t)(total / (1024 * 1024));
        free_mb  = (int64_t)(free  / (1024 * 1024));
        used_mb  = total_mb - free_mb;
        if (total_mb > 0) pct = (int)(used_mb * 100 / total_mb);
    } else {
        status = "No SD Card";
    }

    snprintf(buf, sizeof(buf), "%lld MB", (long long)total_mb);
    lv_label_set_text(g_space_total_label, buf);
    snprintf(buf, sizeof(buf), "%lld MB (%d%%)", (long long)used_mb, pct);
    lv_label_set_text(g_space_used_label, buf);
    snprintf(buf, sizeof(buf), "%lld MB", (long long)free_mb);
    lv_label_set_text(g_space_free_label, buf);
    lv_label_set_text(g_space_status_label, status);

}

/* ---- format SD card ---- */
static int do_format_sd(const char *mnt_path)
{
    char fstype[32] = {0};
    char line[256] = {0};
    char cmd[384] = {0};
    char dev[64] = {0};
    int ret = -1;
    FILE *fp;

    /* find block device and fs type from mount point */
    fp = fopen("/proc/mounts", "r");
    if (!fp) {
        LV_LOG_ERROR("format: device not found for %s", mnt_path);
        return -1;
    }
    while (fgets(line, sizeof(line), fp)) {
        char *s = strtok(line, " \t");
        if (!s) continue;
        strncpy(dev, s, sizeof(dev) - 1);
        s = strtok(NULL, " \t"); /* mount point */
        if (!s) { dev[0] = '\0'; continue; }
        size_t plen = strlen(mnt_path);
        if (plen > 0 && mnt_path[plen - 1] == '/') plen--;
        if (strncmp(s, mnt_path, plen) != 0 || s[plen] != '\0')
            { dev[0] = '\0'; continue; }
        s = strtok(NULL, " \t"); /* fstype */
        if (s) strncpy(fstype, s, sizeof(fstype) - 1);
        break;
    }
    fclose(fp);

    if (!dev[0]) {
        LV_LOG_ERROR("format: device not found for %s", mnt_path);
        return -1;
    }

    /* unmount */
    snprintf(cmd, sizeof(cmd), "umount %s 2>/dev/null", mnt_path);
    system(cmd);

    /* format: use same fs type, default FAT32 */
    if (strncmp(fstype, "ext4", 4) == 0) {
        snprintf(cmd, sizeof(cmd), "mkfs.ext4 -F %s 2>/dev/null", dev);
    } else {
        snprintf(cmd, sizeof(cmd), "mkfs.vfat %s 2>/dev/null", dev);
    }
    ret = system(cmd);

    /* remount */
    snprintf(cmd, sizeof(cmd), "mount %s %s 2>/dev/null", dev, mnt_path);
    system(cmd);

    LV_LOG_USER("format %s (%s, %s) ret=%d", mnt_path, dev, fstype, ret);

    return ret;
}

/* ---- JSON config save / load ---- */
static int json_get_int(const char *buf, const char *key, int def)
{
    char search[64];
    snprintf(search, sizeof(search), "\"%s\"", key);
    const char *p = strstr(buf, search);
    if (!p) return def;
    p = strchr(p, ':');
    if (!p) return def;
    return atoi(p + 1);
}

static void json_set_int(FILE *fp, const char *key, int val, int comma)
{
    fprintf(fp, "    \"%s\": %d%s\n", key, val, comma ? "," : "");
}

static void json_set_bool(FILE *fp, const char *key, bool val, int comma)
{
    fprintf(fp, "    \"%s\": %s%s\n", key, val ? "true" : "false", comma ? "," : "");
}

static void save_config(void)
{
    char path[280];
    snprintf(path, sizeof(path), CONFIG_FILE);
    FILE *fp = fopen(path, "w");
    if (!fp) {
        LV_LOG_ERROR("save_config: cannot open %s", path);
        return;
    }
    fprintf(fp, "{\n");
    json_set_int(fp, "video_width", g_cfg.video_width, 1);
    json_set_int(fp, "video_height", g_cfg.video_height, 1);
    json_set_int(fp, "loop_duration", g_loop_duration, 1);
    json_set_int(fp, "sensitivity", g_sensitivity, 1);
    json_set_bool(fp, "audio_enabled", lv_obj_has_state(g_audio_sw, LV_STATE_CHECKED), 0);
    fprintf(fp, "}\n");
    fclose(fp);
    LV_LOG_USER("config saved to %s", path);
}

void dashcam_setting_load_config(void)
{
    char path[280] = {0};
    snprintf(path, sizeof(path), CONFIG_FILE);
    FILE *fp = fopen(path, "r");
    if (!fp) {
        LV_LOG_USER("load_config: no config file, using defaults");
        return;
    }
    fseek(fp, 0, SEEK_END);
    long sz = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    if (sz <= 0 || sz > 4096) {
        fclose(fp);
        return;
    }
    char *buf = malloc(sz + 1);
    if (!buf) {
        fclose(fp);
        return;
    }
    fread(buf, 1, sz, fp);
    buf[sz] = '\0';
    fclose(fp);

    g_cfg.video_width  = json_get_int(buf, "video_width", 640);
    g_cfg.video_height = json_get_int(buf, "video_height", 480);
    g_loop_duration = json_get_int(buf, "loop_duration", 180);
    g_sensitivity   = json_get_int(buf, "sensitivity", 1);
    bool audio_on   = json_get_int(buf, "audio_enabled", 1);
    if (g_audio_sw) {
        if (audio_on)
            lv_obj_add_state(g_audio_sw, LV_STATE_CHECKED);
        else
            lv_obj_remove_state(g_audio_sw, LV_STATE_CHECKED);
    }

    g_cfg.file_duration = g_loop_duration;
    g_cfg.qfactor = (g_sensitivity == 0) ? 60 : (g_sensitivity == 2) ? 95 : 80;

    LV_LOG_INFO("config loaded: width=%d, height=%d, dur=%d, sens=%d, audio=%d",
                g_cfg.video_width, g_cfg.video_height, g_loop_duration, g_sensitivity, audio_on);
    free(buf);
}

static void format_confirm_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code != LV_EVENT_CLICKED) return;

    if (do_format_sd(g_cfg.storage_path) != 0)
        return;

    if (g_format_popup) {
        lv_obj_del(g_format_popup);
        g_format_popup = NULL;
    }
    sync();
    system("reboot");
}

static void format_cancel_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code != LV_EVENT_CLICKED) return;

    if (g_format_popup) {
        lv_obj_del(g_format_popup);
        g_format_popup = NULL;
    }
}

static void format_sd_btn_cb(lv_event_t *e)
{
    (void)e;
    /* create confirmation popup */
    g_format_popup = lv_obj_create(lv_layer_top());
    lv_obj_set_size(g_format_popup, 360, 160);
    lv_obj_set_style_bg_color(g_format_popup, lv_color_hex(0x1a1a2e), 0);
    lv_obj_set_style_bg_opa(g_format_popup, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(g_format_popup, 8, 0);
    lv_obj_set_style_border_color(g_format_popup, lv_color_hex(0x333355), 0);
    lv_obj_set_style_border_width(g_format_popup, 2, 0);
    lv_obj_set_style_shadow_opa(g_format_popup, LV_OPA_70, 0);
    lv_obj_set_style_shadow_width(g_format_popup, 20, 0);
    lv_obj_center(g_format_popup);

    lv_obj_t *msg = lv_label_create(g_format_popup);
    lv_label_set_text(msg, "Format SD Card?\nAll data will be erased.");
    lv_obj_set_style_text_color(msg, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(msg, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_align(msg, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(msg, LV_ALIGN_TOP_MID, 0, 20);

    lv_obj_t *btn_ok = lv_btn_create(g_format_popup);
    lv_obj_set_size(btn_ok, 120, 36);
    lv_obj_align(btn_ok, LV_ALIGN_BOTTOM_LEFT, 40, -20);
    lv_obj_set_style_bg_color(btn_ok, lv_color_hex(0xDC2626), 0);
    lv_obj_set_style_bg_opa(btn_ok, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(btn_ok, 6, 0);
    lv_obj_set_style_shadow_opa(btn_ok, LV_OPA_TRANSP, 0);
    lv_obj_set_style_outline_opa(btn_ok, LV_OPA_TRANSP, 0);
    lv_obj_add_event_cb(btn_ok, format_confirm_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *ok_lbl = lv_label_create(btn_ok);
    lv_label_set_text(ok_lbl, "Format");
    lv_obj_center(ok_lbl);

    lv_obj_t *btn_cancel = lv_btn_create(g_format_popup);
    lv_obj_set_size(btn_cancel, 120, 36);
    lv_obj_align(btn_cancel, LV_ALIGN_BOTTOM_RIGHT, -40, -20);
    lv_obj_set_style_bg_color(btn_cancel, lv_color_hex(0x444455), 0);
    lv_obj_set_style_bg_opa(btn_cancel, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(btn_cancel, 6, 0);
    lv_obj_set_style_shadow_opa(btn_cancel, LV_OPA_TRANSP, 0);
    lv_obj_set_style_outline_opa(btn_cancel, LV_OPA_TRANSP, 0);
    lv_obj_add_event_cb(btn_cancel, format_cancel_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *cancel_lbl = lv_label_create(btn_cancel);
    lv_label_set_text(cancel_lbl, "Cancel");
    lv_obj_center(cancel_lbl);
}

/* ---- date / time ---- */
static void sync_time_cb(lv_event_t *e)
{
    (void)e;
    time_t now = time(NULL);
    struct tm *lt = localtime(&now);

    char dbuf[36] = {0}, tbuf[16] = {0};
    snprintf(dbuf, sizeof(dbuf), "%04d/%02d/%02d", lt->tm_year + 1900, lt->tm_mon + 1, lt->tm_mday);
    snprintf(tbuf, sizeof(tbuf), "%02d:%02d:%02d", lt->tm_hour, lt->tm_min, lt->tm_sec);

    lv_textarea_set_text(g_date_ta, dbuf);
    lv_textarea_set_text(g_time_ta, tbuf);

    char full[48] = {0};
    snprintf(full, sizeof(full), "%04d-%02d-%02d %02d:%02d:%02d",
             lt->tm_year + 1900, lt->tm_mon + 1, lt->tm_mday,
             lt->tm_hour, lt->tm_min, lt->tm_sec);
    lv_label_set_text(g_cur_time_label, full);
}

/* ---- loop recording radio ---- */
static void loop_radio_cb(lv_event_t *e)
{
    lv_obj_t *btn = lv_event_get_target(e);
    int idx = 0;
    for (int i = 0; i < 3; i++) {
        if (g_loop_btns[i] == btn) { idx = i; break; }
    }
    int dur_map[] = { 60, 180, 300 };
    g_loop_duration = dur_map[idx];
    update_loop_radio_ui();
}

/* ---- sensitivity radio ---- */
static void sens_radio_cb(lv_event_t *e)
{
    lv_obj_t *btn = lv_event_get_target(e);
    int idx = 0;
    for (int i = 0; i < 3; i++) {
        if (g_sens_btns[i] == btn) { idx = i; break; }
    }
    g_sensitivity = idx;
    update_sens_radio_ui();
}

/* ---- audio toggle ---- */
static void audio_sw_cb(lv_event_t *e)
{
    (void)e;
    bool on = lv_obj_has_state(g_audio_sw, LV_STATE_CHECKED);
    LV_LOG_USER("Audio recording: %s", on ? "ON" : "OFF");
    /* TODO: apply to recorder */
}

/* ---- restore / save ---- */
static void restore_defaults_cb(lv_event_t *e)
{
    (void)e;
    memcpy(&g_cfg, &g_default_cfg, sizeof(g_cfg));

    g_loop_duration = 180;
    g_sensitivity = 1;
    update_loop_radio_ui();
    update_sens_radio_ui();

    /* reset date/time to current */
    sync_time_cb(NULL);

    if (g_audio_sw)
        lv_obj_add_state(g_audio_sw, LV_STATE_CHECKED);  /* default ON */

    refresh_storage_info();
    save_config();
    LV_LOG_USER("Settings restored to defaults");
}

static void save_settings_cb(lv_event_t *e)
{
    (void)e;

    /* apply config to recorder */
    if (g_cfg.file_duration != g_loop_duration) {
        if (!g_recorder_obj) {
            LV_LOG_ERROR("g_recorder_obj is null.");
            return;
        }
        lv_aic_recorder_set_cmd(g_recorder_obj, LV_AIC_RECORDER_CMD_SET_DUARTION,
                                &g_loop_duration);
        g_cfg.file_duration = g_loop_duration;
    }

    save_config();
    LV_LOG_USER("Settings saved");

    /* hide the setting screen */
    dashcam_setting_hide(g_setting_screen);
}


lv_obj_t *dashcam_setting_create(lv_obj_t *parent, lv_obj_t *recorder_obj, const char *path)
{
    g_recorder_obj = recorder_obj;

    /* init default config — kept as g_default_cfg for restore */
    memset(&g_cfg, 0, sizeof(g_cfg));
    if (path)
        strncpy(g_cfg.storage_path, path, DASHCAM_MAX_PATH - 1);
    else
        strncpy(g_cfg.storage_path, "/mnt/sdcard", DASHCAM_MAX_PATH - 1);
    g_cfg.file_duration = 180;   /* 3 min */
    g_cfg.file_num = 0;
    g_cfg.video_width = 640;
    g_cfg.video_height = 480;
    g_cfg.video_bitrate = 4000000;
    g_cfg.video_framerate = 30;
    g_cfg.qfactor = 80;
    memcpy(&g_default_cfg, &g_cfg, sizeof(g_cfg));

    g_loop_duration = 180;
    g_sensitivity = 1;  /* Medium */

    /* ---- init styles ---- */
    lv_style_init(&sty_card);
    lv_style_set_bg_color(&sty_card, lv_color_hex(0x222233));
    lv_style_set_bg_opa(&sty_card, LV_OPA_COVER);
    lv_style_set_radius(&sty_card, 8);
    lv_style_set_border_opa(&sty_card, LV_OPA_TRANSP);
    lv_style_set_pad_all(&sty_card, 8);

    lv_style_init(&sty_radio_on);
    lv_style_set_bg_color(&sty_radio_on, lv_color_hex(0x8b5cf6));
    lv_style_set_bg_opa(&sty_radio_on, LV_OPA_COVER);
    lv_style_set_border_color(&sty_radio_on, lv_color_hex(0x8b5cf6));
    lv_style_set_text_color(&sty_radio_on, lv_color_hex(0xFFFFFF));

    lv_style_init(&sty_radio_off);
    lv_style_set_bg_color(&sty_radio_off, lv_color_hex(0x2a2a2a));
    lv_style_set_bg_opa(&sty_radio_off, LV_OPA_COVER);
    lv_style_set_border_color(&sty_radio_off, lv_color_hex(0x333333));
    lv_style_set_text_color(&sty_radio_off, lv_color_hex(0x888888));

    lv_style_init(&sty_btn_prim);
    lv_style_set_bg_color(&sty_btn_prim, lv_color_hex(0x667eea));
    lv_style_set_bg_opa(&sty_btn_prim, LV_OPA_COVER);
    lv_style_set_border_opa(&sty_btn_prim, LV_OPA_TRANSP);
    lv_style_set_radius(&sty_btn_prim, 6);
    lv_style_set_shadow_opa(&sty_btn_prim, LV_OPA_70);
    lv_style_set_shadow_width(&sty_btn_prim, 8);
    lv_style_set_shadow_color(&sty_btn_prim, lv_color_hex(0x667eea));
    lv_style_set_text_color(&sty_btn_prim, lv_color_hex(0xFFFFFF));

    lv_style_init(&sty_btn_danger);
    lv_style_set_bg_color(&sty_btn_danger, lv_color_hex(0xef4444));
    lv_style_set_bg_opa(&sty_btn_danger, LV_OPA_COVER);
    lv_style_set_border_opa(&sty_btn_danger, LV_OPA_TRANSP);
    lv_style_set_radius(&sty_btn_danger, 6);
    lv_style_set_shadow_opa(&sty_btn_danger, LV_OPA_50);
    lv_style_set_shadow_width(&sty_btn_danger, 6);
    lv_style_set_text_color(&sty_btn_danger, lv_color_hex(0xFFFFFF));

    lv_style_init(&sty_btn_sec);
    lv_style_set_bg_color(&sty_btn_sec, lv_color_hex(0x444455));
    lv_style_set_bg_opa(&sty_btn_sec, LV_OPA_COVER);
    lv_style_set_border_opa(&sty_btn_sec, LV_OPA_TRANSP);
    lv_style_set_radius(&sty_btn_sec, 6);
    lv_style_set_text_color(&sty_btn_sec, lv_color_hex(0xFFFFFF));

    lv_style_init(&sty_ta);
    lv_style_set_bg_color(&sty_ta, lv_color_hex(0x1a1a2e));
    lv_style_set_bg_opa(&sty_ta, LV_OPA_COVER);
    lv_style_set_border_color(&sty_ta, lv_color_hex(0x555588));
    lv_style_set_border_width(&sty_ta, 1);
    lv_style_set_radius(&sty_ta, 6);
    lv_style_set_text_color(&sty_ta, lv_color_hex(0xFFFFFF));
    lv_style_set_pad_all(&sty_ta, 6);

    lv_style_init(&sty_title);
    lv_style_set_text_color(&sty_title, lv_color_hex(0xFFFFFF));

    /* ---- screen ---- */
    g_setting_screen = lv_obj_create(parent);
    lv_obj_set_size(g_setting_screen, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_color(g_setting_screen, lv_color_hex(0x111122), 0);
    lv_obj_set_style_bg_opa(g_setting_screen, LV_OPA_COVER, 0);
    lv_obj_set_style_border_opa(g_setting_screen, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(g_setting_screen, 0, 0);
    lv_obj_clear_flag(g_setting_screen, LV_OBJ_FLAG_SCROLLABLE);

    /* ---- title bar ---- */
    lv_obj_t *bar = lv_obj_create(g_setting_screen);
    lv_obj_set_size(bar, lv_pct(100), 44);
    lv_obj_set_style_bg_opa(bar, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_opa(bar, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(bar, 8, 0);
    lv_obj_set_flex_flow(bar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(bar, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t *t = lv_label_create(bar);
    lv_label_set_text(t, "Settings");
    lv_obj_add_style(t, &sty_title, 0);
    lv_obj_set_style_text_font(t, &lv_font_montserrat_14, 0);

    lv_obj_t *btn_close = lv_btn_create(bar);
    lv_obj_set_size(btn_close, 36, 36);
    lv_obj_add_style(btn_close, &sty_btn_sec, 0);
    lv_obj_add_event_cb(btn_close, dashcam_setting_close_cb, LV_EVENT_CLICKED, g_setting_screen);
    lv_obj_t *cl = lv_label_create(btn_close);
    lv_label_set_text(cl, LV_SYMBOL_CLOSE);
    lv_obj_center(cl);

    /* ---- scrollable content ---- */
    lv_obj_t *cont = lv_obj_create(g_setting_screen);
    lv_obj_set_size(cont, lv_pct(100), lv_pct(90));
    lv_obj_set_pos(cont, 0, 50);
    lv_obj_set_style_bg_opa(cont, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_opa(cont, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(cont, 12, 0);
    lv_obj_set_flex_flow(cont, LV_FLEX_FLOW_COLUMN);

    /* SD Card Settings */
    make_section_label(cont, LV_SYMBOL_SD_CARD, "SD Card");

    /* storage info card — grid layout: 3 items per row */
    lv_obj_t *card = lv_obj_create(cont);
    lv_obj_set_size(card, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_add_style(card, &sty_card, 0);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(card, 12, 0);
    lv_obj_set_style_pad_row(card, 8, 0);

    /* row 1: Total | Used | Free | Status */
    lv_obj_t *r1 = lv_obj_create(card);
    lv_obj_set_size(r1, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(r1, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_opa(r1, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(r1, 0, 0);
    lv_obj_set_flex_flow(r1, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(r1, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    g_space_total_label = make_info_cell(r1, "Total", "--");
    g_space_used_label  = make_info_cell(r1, "Used", "--");
    g_space_free_label  = make_info_cell(r1, "Free", "--");
    g_space_status_label = make_info_cell(r1, "Status", "--");

    /* Format SD Card button */
    lv_obj_t *btn_fmt = lv_btn_create(card);
    lv_obj_set_size(btn_fmt, 200, 36);
    lv_obj_add_style(btn_fmt, &sty_btn_danger, 0);
    lv_obj_add_event_cb(btn_fmt, format_sd_btn_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *fmt_lbl = lv_label_create(btn_fmt);
    lv_label_set_text(fmt_lbl, "Format SD Card");
    lv_obj_set_style_text_font(fmt_lbl, &lv_font_montserrat_14, 0);
    lv_obj_center(fmt_lbl);

    /* Loop recording duration */
    make_section_label(cont, LV_SYMBOL_LOOP, "Loop Recording");
    make_radio_group(cont, g_loop_labels, 3, g_loop_btns);
    for (int i = 0; i < 3; i++)
        lv_obj_add_event_cb(g_loop_btns[i], loop_radio_cb, LV_EVENT_CLICKED, NULL);
    update_loop_radio_ui();

    /* Date & Time */
    make_section_label(cont, LV_SYMBOL_REFRESH, "Date & Time");

    lv_obj_t *dt_card = lv_obj_create(cont);
    lv_obj_set_size(dt_card, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_add_style(dt_card, &sty_card, 0);
    lv_obj_set_flex_flow(dt_card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(dt_card, 12, 0);
    lv_obj_set_style_pad_row(dt_card, 8, 0);

    /* current time — large */
    g_cur_time_label = lv_label_create(dt_card);
    {
        time_t now = time(NULL);
        struct tm *lt = localtime(&now);
        char cbuf[48];
        snprintf(cbuf, sizeof(cbuf), "%04d-%02d-%02d %02d:%02d:%02d",
                 lt->tm_year + 1900, lt->tm_mon + 1, lt->tm_mday,
                 lt->tm_hour, lt->tm_min, lt->tm_sec);
        lv_label_set_text(g_cur_time_label, cbuf);
    }
    lv_obj_set_style_text_color(g_cur_time_label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(g_cur_time_label, &lv_font_montserrat_14, 0);

    /* date + time + sync row side by side */
    lv_obj_t *dt_row = lv_obj_create(dt_card);
    lv_obj_set_size(dt_row, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(dt_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_opa(dt_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(dt_row, 0, 0);
    lv_obj_set_flex_flow(dt_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(dt_row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    /* date input */
    make_label(dt_row, "Date");
    g_date_ta = lv_textarea_create(dt_row);
    lv_textarea_set_one_line(g_date_ta, true);
    lv_textarea_set_max_length(g_date_ta, 10);
    lv_obj_set_size(g_date_ta, 130, 34);
    lv_obj_add_style(g_date_ta, &sty_ta, 0);
    lv_obj_set_style_margin_left(g_date_ta, 6, 0);
    {
        time_t now = time(NULL);
        struct tm *lt = localtime(&now);
        char dbuf[36];
        snprintf(dbuf, sizeof(dbuf), "%04d/%02d/%02d", lt->tm_year + 1900, lt->tm_mon + 1, lt->tm_mday);
        lv_textarea_set_text(g_date_ta, dbuf);
    }

    /* time input */
    make_label(dt_row, "Time");
    lv_obj_set_style_margin_left(lv_obj_get_child(dt_row, lv_obj_get_child_cnt(dt_row) - 1), 16, 0);
    g_time_ta = lv_textarea_create(dt_row);
    lv_textarea_set_one_line(g_time_ta, true);
    lv_textarea_set_max_length(g_time_ta, 8);
    lv_obj_set_size(g_time_ta, 100, 34);
    lv_obj_add_style(g_time_ta, &sty_ta, 0);
    lv_obj_set_style_margin_left(g_time_ta, 6, 0);
    {
        time_t now = time(NULL);
        struct tm *lt = localtime(&now);
        char tbuf[16];
        snprintf(tbuf, sizeof(tbuf), "%02d:%02d:%02d", lt->tm_hour, lt->tm_min, lt->tm_sec);
        lv_textarea_set_text(g_time_ta, tbuf);
    }

    /* Sync Time button — inline, blue background */
    lv_obj_t *btn_sync = lv_btn_create(dt_row);
    lv_obj_set_size(btn_sync, 100, 34);
    lv_obj_set_style_margin_left(btn_sync, 16, 0);
    lv_obj_add_style(btn_sync, &sty_btn_prim, 0);
    lv_obj_add_event_cb(btn_sync, sync_time_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *sync_lbl = lv_label_create(btn_sync);
    lv_label_set_text(sync_lbl, "Sync");
    lv_obj_set_style_text_font(sync_lbl, &lv_font_montserrat_14, 0);
    lv_obj_center(sync_lbl);

    /* Video Settings */

    make_section_label(cont, LV_SYMBOL_VIDEO, "Video Settings");

    lv_obj_t *v_card = lv_obj_create(cont);
    lv_obj_set_size(v_card, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_add_style(v_card, &sty_card, 0);
    lv_obj_set_flex_flow(v_card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(v_card, 12, 0);
    lv_obj_set_style_pad_row(v_card, 6, 0);

    make_label(v_card, "Collision Sensitivity");
    make_radio_group(v_card, g_sens_labels, 3, g_sens_btns);
    for (int i = 0; i < 3; i++)
        lv_obj_add_event_cb(g_sens_btns[i], sens_radio_cb, LV_EVENT_CLICKED, NULL);
    update_sens_radio_ui();

    /* Audio Settings */

    make_section_label(cont, LV_SYMBOL_AUDIO, "Audio Settings");

    lv_obj_t *a_card = lv_obj_create(cont);
    lv_obj_set_size(a_card, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_add_style(a_card, &sty_card, 0);
    lv_obj_set_flex_flow(a_card, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_all(a_card, 12, 0);
    lv_obj_set_flex_align(a_card, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    make_label(a_card, "Recording");
    g_audio_sw = lv_switch_create(a_card);
    lv_obj_set_style_margin_left(g_audio_sw, 12, 0);
    lv_obj_add_state(g_audio_sw, LV_STATE_CHECKED);
    lv_obj_set_style_bg_color(g_audio_sw, lv_color_hex(0x8b5cf6), LV_STATE_CHECKED);
    lv_obj_set_style_bg_color(g_audio_sw, lv_color_hex(0x555555), 0);
    lv_obj_add_event_cb(g_audio_sw, audio_sw_cb, LV_EVENT_VALUE_CHANGED, NULL);

    /* Bottom Actions */

    lv_obj_t *action_row = lv_obj_create(cont);
    lv_obj_set_size(action_row, lv_pct(100), 56);
    lv_obj_set_style_bg_opa(action_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_opa(action_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(action_row, 0, 0);
    lv_obj_set_style_margin_top(action_row, 16, 0);
    lv_obj_set_flex_flow(action_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(action_row, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    /* Restore Defaults */
    lv_obj_t *btn_restore = lv_btn_create(action_row);
    lv_obj_set_size(btn_restore, 180, 42);
    lv_obj_add_style(btn_restore, &sty_btn_sec, 0);
    lv_obj_add_event_cb(btn_restore, restore_defaults_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *rl = lv_label_create(btn_restore);
    lv_label_set_text(rl, "Restore Defaults");
    lv_obj_set_style_text_font(rl, &lv_font_montserrat_14, 0);
    lv_obj_center(rl);

    /* Save */
    lv_obj_t *btn_save = lv_btn_create(action_row);
    lv_obj_set_size(btn_save, 180, 42);
    lv_obj_add_style(btn_save, &sty_btn_prim, 0);
    lv_obj_add_event_cb(btn_save, save_settings_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *sl = lv_label_create(btn_save);
    lv_label_set_text(sl, "Save");
    lv_obj_set_style_text_font(sl, &lv_font_montserrat_14, 0);
    lv_obj_center(sl);

    /* ---- final ---- */
    lv_obj_add_flag(g_setting_screen, LV_OBJ_FLAG_HIDDEN);

    /* load real storage info */
    refresh_storage_info();

    /* restore saved settings (overrides defaults) */
    dashcam_setting_load_config();
    update_loop_radio_ui();
    update_sens_radio_ui();

    return g_setting_screen;
}

void dashcam_setting_show(lv_obj_t *s)
{
    if (!s) s = g_setting_screen;
    if (s) {
        refresh_storage_info();
        lv_obj_clear_flag(s, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(s);
    }
}

void dashcam_setting_hide(lv_obj_t *s)
{
    if (!s) s = g_setting_screen;
    if (s) lv_obj_add_flag(s, LV_OBJ_FLAG_HIDDEN);
}

void dashcam_setting_close_cb(lv_event_t *e)
{
    lv_obj_t *s = lv_event_get_user_data(e);
    dashcam_setting_hide(s);
}

struct dashcam_config *dashcam_setting_get_config(void)
{
    g_cfg.file_duration = g_loop_duration;
    return &g_cfg;
}

void dashcam_setting_update_space(lv_obj_t *s, int64_t free_mb, int64_t total_mb)
{
    (void)s;
    (void)free_mb;
    (void)total_mb;
    refresh_storage_info();
}
