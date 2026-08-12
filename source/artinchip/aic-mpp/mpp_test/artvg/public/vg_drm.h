/*
 * Copyright (c) 2026, ArtInChip Technology Co., Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Authors:  ZeQuan Liang <zequan.liang@artinchip.com>
 */

#ifndef ARTVG_DRM_TEST_H
#define ARTVG_DRM_TEST_H

#ifdef __cplusplus
extern "C" {
#endif

#include <xf86drm.h>
#include <xf86drmMode.h>
#include <drm_fourcc.h>
#include <stdio.h>
#include <stdarg.h>
#include <pthread.h>
#include <video/mpp_types.h>
#include <linux/dma-buf.h>

/**********************
 *  LOG LEVELS
 **********************/
#define VG_LOG_LEVEL_ERROR   0
#define VG_LOG_LEVEL_WARN    1
#define VG_LOG_LEVEL_INFO    2
#define VG_LOG_LEVEL_DEBUG   3
#define VG_LOG_LEVEL_TRACE   4

/* Default log level */
#ifndef VG_LOG_LEVEL
#define VG_LOG_LEVEL VG_LOG_LEVEL_WARN
#endif

/**********************
 *  LOG MACROS
 **********************/
#define VG_LOG_ERROR(fmt, ...) do { if(VG_LOG_LEVEL >= VG_LOG_LEVEL_ERROR) fprintf(stderr, "[VG ERROR] " fmt "\n", ##__VA_ARGS__); } while(0)
#define VG_LOG_WARN(fmt, ...) do { if(VG_LOG_LEVEL >= VG_LOG_LEVEL_WARN) fprintf(stderr, "[VG WARN] " fmt "\n", ##__VA_ARGS__); } while(0)
#define VG_LOG_INFO(fmt, ...) do { if(VG_LOG_LEVEL >= VG_LOG_LEVEL_INFO) fprintf(stderr, "[VG INFO] " fmt "\n", ##__VA_ARGS__); } while(0)
#define VG_LOG_DEBUG(fmt, ...) do { if(VG_LOG_LEVEL >= VG_LOG_LEVEL_DEBUG) fprintf(stderr, "[VG DEBUG] " fmt "\n", ##__VA_ARGS__); } while(0)
#define VG_LOG_TRACE(fmt, ...) do { if(VG_LOG_LEVEL >= VG_LOG_LEVEL_TRACE) fprintf(stderr, "[VG TRACE] " fmt "\n", ##__VA_ARGS__); } while(0)

typedef struct {
    uint32_t handle;
    uint32_t pitch;
    uint32_t offset;
    unsigned long int size;
    uint8_t * map;
    uint32_t id;
    int fd;
    uint32_t fourcc;    /* Pixel format (Four Character Code) */
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

int drm_device_open(drm_dev_t * drm_dev, const char * file, int64_t connector_id);
int drm_device_close(drm_dev_t * drm_dev);
int drm_wait_vsync(drm_dev_t * drm_dev);
int drm_buffer_flush(drm_dev_t *drm_dev, int idx);

int drm_buffer_to_mpp_buffer(drm_dev_t * drm_dev, int idx, struct mpp_buf *buf);
void *drm_framebuffer_get_map(drm_dev_t * drm_dev, int idx);

int dmabuf_sync(int fd, int flags);
#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /*ARTVG_DRM_TEST_H*/
