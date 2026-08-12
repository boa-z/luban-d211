/*
 * Copyright (C) 2024-2026 ArtInChip Technology Co., Ltd.
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include <string.h>
#include <time.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <video/artinchip_fb.h>
#include "dashcam_preview.h"
#include "dashcam_setting.h"
#include "lv_aic_recorder.h"

#ifndef LV_SYMBOL_REC
#define LV_SYMBOL_REC "\xEF\x84\x91" /* FontAwesome fa-circle */
#endif

#include "lv_aic_camera.h"
#include "lv_aic_display.h"

static lv_obj_t *g_recorder_obj, *g_camera_obj, *g_rear_view;
static lv_obj_t *g_timer_label, *g_rec_dot, *g_switch_cam, *g_rec_btn;

static void switch_cam_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) == LV_EVENT_CLICKED)
        lv_aic_recorder_swap_cameras(g_recorder_obj);
}

/* --- Frame callback: preview display + recording --- */

static s32 record_frame_cb(void *user_data, struct vin_video_buf *buf, int index, int w, int h,
                            int format)
{
    (void)user_data;

    /* Forward to recorder */
    if (g_recorder_obj) {
        struct aic_recorder_frame rec_frame;
        struct mpp_frame *frame = &rec_frame.mpp_frame;
        struct timespec ts = { 0 };

        memset(&rec_frame, 0, sizeof(rec_frame));
        clock_gettime(CLOCK_MONOTONIC, &ts);
        frame->id = index;
        frame->pts = (u64)(ts.tv_sec * 1000000LL + ts.tv_nsec / 1000LL);
        frame->buf.buf_type = MPP_DMA_BUF_FD;
        frame->buf.size.width = w;
        frame->buf.size.height = h;
        frame->buf.format = format;
        frame->buf.stride[0] = w;
        frame->buf.stride[1] = w;
        for (int i = 0; i < VID_BUF_PLANE_NUM; i++) {
            frame->buf.fd[i] = buf->planes[i].fd;
            rec_frame.vaddr[i] = buf->planes[i].vaddr;
        }
        if (lv_aic_recorder_send_frame(g_recorder_obj, &rec_frame) < 0) {
            /* recorder not ready, return buffer to DVP directly */
            return -1;
        }
    } else {
        return -1;
    }

    return 0;
}

/* recorder buffer release callback — return frame to DVP */
static s32 recorder_buf_release_cb(void *data, s32 event, void *buf)
{
    (void)data;
    if (event != AIC_RECORDER_EVENT_RELEASE_VIDEO_FRAME || !buf || !g_camera_obj)
        return -1;

    struct aic_recorder_frame *frame = (struct aic_recorder_frame *)buf;
    return lv_aic_camera_q_buf(g_camera_obj, frame->mpp_frame.id);
}

/*
 * dashcam_preview_create()  UI layout (1024×600)
 *
 *  ┌──────────────────────────────────────────────────────────┐
 *  │  ●                2026-06-18 14:30:25           [Setting]│
 *  ├──────────────────────────────────────────────────────────┤
 *  │                                                          │
 *  │                                                          │
 *  │                   Camera Preview                         │
 *  │                                                          │
 *  │                                                          │
 *  │                      [Switch]                            │
 *  ├──────────────────────────────────────────────────────────┤
 *  │   Preview               │     Playback                   │
 *  └──────────────────────────────────────────────────────────┘
 */
lv_obj_t *dashcam_preview_create(lv_obj_t *parent, lv_obj_t *recorder_obj)
{
    g_recorder_obj = recorder_obj;

    lv_obj_t *screen = lv_obj_create(parent);
    lv_obj_set_size(screen, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_color(screen, lv_color_hex(0x111111), LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(screen, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_opa(screen, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(screen, 0, 0);
    lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);

    /* alpha: global mode, UI layer 50% transparent → video shows through*/
    {
        int fb = open("/dev/fb0", O_RDWR);
        if (fb >= 0) {
            struct aicfb_alpha_config alpha = {
                .layer_id = AICFB_LAYER_TYPE_UI,
                .enable   = 1,
                .mode     = AICFB_GLOBAL_ALPHA_MODE,
                .value    = 128,
            };
            ioctl(fb, AICFB_UPDATE_ALPHA_CONFIG, &alpha);
            close(fb);
        }
    }

    /* Full-screen camera preview */
    g_camera_obj = lv_aic_camera_create(screen);
    lv_obj_set_pos(g_camera_obj, 0, 32);
    lv_obj_set_size(g_camera_obj, 1024, 522);
    lv_obj_set_style_bg_color(g_camera_obj, lv_color_hex(0x606060), 0);
    lv_obj_set_style_bg_opa(g_camera_obj, LV_OPA_COVER, 0);
    struct dashcam_config *cfg = dashcam_setting_get_config();
    lv_aic_camera_set_format(g_camera_obj, LV_AIC_CAMERA_FORMAT_NV12);
    if (cfg && cfg->video_width > 0)
        lv_aic_camera_set_sensor_fmt(g_camera_obj, cfg->video_width, cfg->video_height);
    lv_aic_camera_open(g_camera_obj);
    lv_aic_camera_set_frame_callback(g_camera_obj, record_frame_cb, NULL);
    lv_aic_camera_start(g_camera_obj);

    /* wire recorder buffer release → QBUF back to DVP */
    lv_aic_recorder_set_buf_callback(g_recorder_obj, recorder_buf_release_cb, NULL);

    /* Recorder overlay (PIP, front/rear camera swap) */
    lv_obj_set_pos(g_recorder_obj, 0, 0);
    lv_obj_set_size(g_recorder_obj, lv_pct(100), lv_pct(85));

    g_rear_view = lv_aic_recorder_add_camera(g_recorder_obj, LV_AIC_RECORDER_CAM_REAR, 200, 112);
    lv_aic_recorder_set_pip_position(g_recorder_obj, LV_AIC_RECORDER_PIP_TOP_RIGHT);

    /* Top left: recording red dot indicator (blinking) */
    g_rec_dot = lv_obj_create(screen);
    lv_obj_set_style_opa(g_rec_dot, LV_OPA_COVER, 0);
    lv_obj_set_size(g_rec_dot, 16, 16);
    lv_obj_set_pos(g_rec_dot, 15, 12);
    lv_obj_set_style_bg_color(g_rec_dot, lv_color_hex(0xFF0000), 0);
    lv_obj_set_style_bg_opa(g_rec_dot, LV_OPA_COVER, 0);
    lv_obj_set_style_border_opa(g_rec_dot, LV_OPA_TRANSP, 0);
    lv_obj_set_style_radius(g_rec_dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_add_flag(g_rec_dot, LV_OBJ_FLAG_HIDDEN);

    /* Top center: date & time */
    g_timer_label = lv_label_create(screen);
    lv_obj_set_style_opa(g_timer_label, LV_OPA_COVER, 0);
    lv_obj_set_style_text_color(g_timer_label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(g_timer_label, &lv_font_montserrat_14, 0);
    lv_label_set_text(g_timer_label, "2026-01-01 00:00:00");
    lv_obj_align(g_timer_label, LV_ALIGN_TOP_MID, 0, 10);

    /* Top right: settings button */
    lv_obj_t *btn_settings = lv_btn_create(screen);
    lv_obj_set_size(btn_settings, 32, 32);
    lv_obj_align(btn_settings, LV_ALIGN_TOP_RIGHT, -10, 8);
    lv_obj_set_style_bg_color(btn_settings, lv_color_hex(0x333355), 0);
    lv_obj_set_style_bg_opa(btn_settings, LV_OPA_70, 0);
    lv_obj_set_style_border_opa(btn_settings, LV_OPA_TRANSP, 0);
    lv_obj_set_style_radius(btn_settings, 6, 0);
    lv_obj_t *stl = lv_label_create(btn_settings);
    lv_label_set_text(stl, LV_SYMBOL_SETTINGS);
    lv_obj_center(stl);

    /* Bottom center: front/rear camera switch */
    g_switch_cam = lv_btn_create(screen);
    lv_obj_set_size(g_switch_cam, 48, 48);
    lv_obj_align(g_switch_cam, LV_ALIGN_BOTTOM_MID, 0, -10);
    lv_obj_set_style_bg_color(g_switch_cam, lv_color_hex(0x667eea), 0);
    lv_obj_set_style_bg_opa(g_switch_cam, LV_OPA_40, 0);
    lv_obj_set_style_border_opa(g_switch_cam, LV_OPA_TRANSP, 0);
    lv_obj_set_style_radius(g_switch_cam, 24, 0);
    lv_obj_add_event_cb(g_switch_cam, switch_cam_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *sw_label = lv_label_create(g_switch_cam);
    lv_label_set_text(sw_label, LV_SYMBOL_REFRESH);
    lv_obj_set_style_text_color(sw_label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(sw_label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_bg_opa(sw_label, LV_OPA_TRANSP, 0);
    lv_obj_center(sw_label);

    /* Bottom right: record button (no text, red=recording, gray=stopped) */
    g_rec_btn = lv_btn_create(screen);
    lv_obj_set_size(g_rec_btn, 36, 36);
    lv_obj_align(g_rec_btn, LV_ALIGN_BOTTOM_RIGHT, -20, -10);
    lv_obj_set_style_bg_color(g_rec_btn, lv_color_hex(0x888888), 0);
    lv_obj_set_style_bg_opa(g_rec_btn, LV_OPA_40, 0);
    lv_obj_set_style_border_opa(g_rec_btn, LV_OPA_TRANSP, 0);
    lv_obj_set_style_radius(g_rec_btn, 18, 0);


    return screen;
}

lv_obj_t *dashcam_preview_get_recorder(lv_obj_t *s)
{
    (void)s;
    return g_recorder_obj;
}

void dashcam_preview_update_time(lv_obj_t *s, const char *str)
{
    (void)s;
    lv_label_set_text(g_timer_label, str);
}

void dashcam_preview_set_recording(lv_obj_t *s, int rec)
{
    (void)s;
    if (rec) {
        lv_obj_clear_flag(g_rec_dot, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_style_bg_color(g_rec_btn, lv_color_hex(0xFF4444), 0);
        lv_obj_set_style_bg_opa(g_rec_btn, LV_OPA_60, 0);
    } else {
        lv_obj_add_flag(g_rec_dot, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_style_bg_color(g_rec_btn, lv_color_hex(0x888888), 0);
        lv_obj_set_style_bg_opa(g_rec_btn, LV_OPA_40, 0);
    }
}

int dashcam_preview_start()
{
    if (!g_camera_obj)
        return -1;

    return lv_aic_camera_display_start(g_camera_obj);
}

int dashcam_preview_stop()
{
    if (!g_camera_obj)
        return -1;

    return lv_aic_camera_display_stop(g_camera_obj);
}
