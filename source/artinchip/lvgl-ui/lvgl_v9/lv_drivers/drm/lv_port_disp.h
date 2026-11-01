/*
 * Copyright (C) 2025-2026, ArtInChip Technology Co., Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Authors:  Huahui Mai <huahui.mai@artinchip.com>
 */

#ifndef LV_PORT_DISP_H
#define LV_PORT_DISP_H

#ifdef __cplusplus
extern "C" {
#endif

#include <xf86drm.h>
#include <xf86drmMode.h>
#include <drm_fourcc.h>

#include "lvgl.h"

#ifndef DISP_SHOW_FPS
#define DISP_SHOW_FPS 1
#endif

/**********************
 *      TYPEDEFS
 **********************/
typedef struct {
    uint32_t handle;
    uint32_t pitch;
    uint32_t offset;
    uint32_t phy_addr;
    unsigned long int size;
    uint8_t * map;
    uint32_t id;
    int fd;
} drm_buffer_t;

typedef struct {
    int fd;

    uint32_t conn_id, enc_id, crtc_id, plane_id, crtc_idx;
    uint32_t width, height;
    uint32_t mmWidth, mmHeight;
    uint32_t fourcc;
    drmModeModeInfo mode;
    uint32_t blob_id;
    drmModeCrtc *saved_crtc;
    drmModeAtomicReq *req;
    drmEventContext drm_event_ctx;
    drmModePlane *plane;
    drmModeCrtc *crtc;
    drmModeConnector *conn;
    uint32_t count_plane_props;
    uint32_t count_crtc_props;
    uint32_t count_conn_props;
    drmModePropertyPtr plane_props[128];
    drmModePropertyPtr crtc_props[128];
    drmModePropertyPtr conn_props[128];
    drm_buffer_t drm_bufs[2]; /*DUMB buffers*/
} drm_dev_t;

typedef struct {
    drm_dev_t drm_dev;

    drm_buffer_t *draw_buf;
    uint32_t buf_id;

    lv_thread_t thread;
    lv_thread_sync_t sync;
    lv_thread_sync_t sync_notify;
    bool exit_status;
    bool sync_ready;
    bool flush_act;

    bool rotate_en;
    int rotate_degree;

    int fps;
} aic_disp_t;

void lv_port_disp_init(void);
void lv_img_cache_set_size(uint16_t max_num);

static inline int fbdev_draw_fps()
{
#if DISP_SHOW_FPS
    lv_display_t *disp = lv_display_get_default();
    aic_disp_t *aic_disp = (aic_disp_t *)lv_display_get_user_data(disp);
    return aic_disp->fps;
#else
    return 0;
#endif
}

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /*LV_PORT_DISP_H*/
