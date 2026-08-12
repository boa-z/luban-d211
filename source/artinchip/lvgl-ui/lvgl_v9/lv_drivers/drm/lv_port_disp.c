/*
 * Copyright (C) 2025-2026, ArtInChip Technology Co., Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Authors:  Huahui Mai <huahui.mai@artinchip.com>
 */

/*********************
 *      INCLUDES
 *********************/

#include "lvgl.h"
#include "lv_port_disp.h"

#include "lv_ge2d/lv_draw_ge2d.h"
#include "lv_mpp_dec/lv_mpp_dec.h"

#include <linux/dma-buf.h>

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdint.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <unistd.h>

/*********************
 *      DEFINES
 *********************/
#if LV_COLOR_DEPTH == 32
    #define DRM_FOURCC DRM_FORMAT_ARGB8888
#elif LV_COLOR_DEPTH == 24
    #define DRM_FOURCC DRM_FORMAT_RGB888
#elif LV_COLOR_DEPTH == 16
    #define DRM_FOURCC DRM_FORMAT_RGB565
#else
    #error LV_COLOR_DEPTH not supported
#endif

/**********************
 *  STATIC PROTOTYPES
 **********************/
static uint32_t get_plane_property_id(drm_dev_t * drm_dev, const char * name);
static uint32_t get_crtc_property_id(drm_dev_t * drm_dev, const char * name);
static uint32_t get_conn_property_id(drm_dev_t * drm_dev, const char * name);
static void page_flip_handler(int fd, unsigned int sequence, unsigned int tv_sec,
                              unsigned int tv_usec, void * user_data);
static int drm_get_plane_props(drm_dev_t * drm_dev);
static int drm_get_crtc_props(drm_dev_t * drm_dev);
static int drm_get_conn_props(drm_dev_t * drm_dev);
static int drm_add_plane_property(drm_dev_t * drm_dev, const char * name, uint64_t value);
static int drm_add_crtc_property(drm_dev_t * drm_dev, const char * name, uint64_t value);
static int drm_add_conn_property(drm_dev_t * drm_dev, const char * name, uint64_t value);
static int drm_atomic_set_plane(aic_disp_t * aic_disp, drm_dev_t * drm_dev, drm_buffer_t * buf);
static int find_plane(drm_dev_t * drm_dev, unsigned int fourcc, uint32_t * plane_id,
                      uint32_t crtc_id, uint32_t crtc_idx);
static int drm_find_connector(drm_dev_t * drm_dev, int64_t connector_id);
static int drm_open(const char * path);
static int drm_setup(drm_dev_t * drm_dev, const char * device_path,
                     int64_t connector_id, unsigned int fourcc);
static int drm_allocate_dumb(drm_dev_t * drm_dev, drm_buffer_t * buf);
static int drm_setup_buffers(drm_dev_t * drm_dev);
static void drm_flush_wait(lv_display_t * drm_dev);
static void drm_flush(lv_display_t * disp, const lv_area_t * area, uint8_t * px_map);

/**********************
 *  STATIC VARIABLES
 **********************/

/**********************
 *      MACROS
 **********************/
#ifndef DIV_ROUND_UP
    #define DIV_ROUND_UP(n, d) (((n) + (d) - 1) / (d))
#endif

#define NS_PER_SEC      1000000000

/**********************
 *   GLOBAL FUNCTIONS
 **********************/
#if DISP_SHOW_FPS == 1
static double get_time_gap(struct timespec *start, struct timespec *end)
{
    double diff;

    if (end->tv_nsec < start->tv_nsec) {
        diff = (double)(NS_PER_SEC + end->tv_nsec - start->tv_nsec) / NS_PER_SEC;
        diff += end->tv_sec - 1 - start->tv_sec;
    } else {
        diff = (double)(end->tv_nsec - start->tv_nsec) / NS_PER_SEC;
        diff += end->tv_sec - start->tv_sec;
    }

    return diff;
}

static int cal_fps(double gap, int cnt)
{
    return (int)(cnt / gap);
}
#endif

static void display_cal_frame_rate(aic_disp_t *aic_disp)
{
#if DISP_SHOW_FPS == 1
    static int start_cal = 0;
    static int frame_cnt = 0;
    static struct timespec start, end;
    double interval = 0.5;
    double gap = 0;

    if (start_cal == 0) {
        start_cal = 1;
        clock_gettime(CLOCK_MONOTONIC, &start);
    }

    clock_gettime(CLOCK_MONOTONIC, &end);
    gap = get_time_gap(&start, &end);
    if (gap >= interval) {
        aic_disp->fps = cal_fps(gap, frame_cnt);
        frame_cnt = 0;
        start_cal = 0;
    } else {
        frame_cnt++;
    }
#else
    (void)aic_disp;
#endif
}

static lv_color_format_t lv_display_fmt(int drm_fourcc)
{
    lv_color_format_t fmt = LV_COLOR_FORMAT_ARGB8888;
    switch(drm_fourcc) {
    case DRM_FORMAT_ARGB8888:
        fmt = LV_COLOR_FORMAT_ARGB8888;
        break;
    case DRM_FORMAT_RGB888:
        fmt = LV_COLOR_FORMAT_RGB888;
        break;
    case DRM_FORMAT_RGB565:
        fmt = LV_COLOR_FORMAT_RGB565;
        break;
    default:
        LV_LOG_ERROR("unsupported pixel bits:%d", drm_fourcc);
        break;
    }
    return fmt;
}

static int lv_create_draw_buf(aic_disp_t *aic_disp)
{
    drm_dev_t * drm_dev = &aic_disp->drm_dev;
    int ret;

    aic_disp->draw_buf = lv_malloc_zeroed(sizeof(drm_buffer_t));
    if (aic_disp->draw_buf == NULL) {
        LV_LOG_ERROR("malloc drm_buffer_t buf fail");
        return -1;
    }

    ret = drm_allocate_dumb(drm_dev, aic_disp->draw_buf);
    if (ret)
        return ret;

    LV_LOG_TRACE("create draw buf");

    return 0;
}

static int lv_linux_drm_set_file(aic_disp_t * aic_disp, const char * file, int64_t connector_id)
{
    drm_dev_t * drm_dev = &aic_disp->drm_dev;
    void *buf1, *buf2;
    int ret;

    ret = drm_setup(drm_dev, file, connector_id, DRM_FOURCC);
    if (ret) {
        close(drm_dev->fd);
        drm_dev->fd = -1;
        return -1;
    }

    ret = drm_setup_buffers(drm_dev);
    if (ret) {
        LV_LOG_ERROR("DRM buffer allocation failed");
        close(drm_dev->fd);
        drm_dev->fd = -1;
        goto err;
    }
    LV_LOG_TRACE("DRM subsystem and buffer mapped successfully");

    if (aic_disp->rotate_en) {
        ret = lv_create_draw_buf(aic_disp);
        if (ret) {
            LV_LOG_ERROR("create draw buffer failed");
            close(drm_dev->fd);
            drm_dev->fd = -1;
            goto err;
        }
        aic_disp->buf_id = 0;
    }

    int32_t hor_res = drm_dev->width;
    int32_t ver_res = drm_dev->height;
    int32_t width = drm_dev->mmWidth;

    lv_display_t * disp = lv_display_create(hor_res, ver_res);
    if (disp == NULL) {
        close(drm_dev->fd);
        goto err;
    }

    lv_color_format_t cf = lv_display_fmt(DRM_FOURCC);
    lv_display_set_color_format(disp, cf);

    size_t buf_size = LV_MIN(drm_dev->drm_bufs[1].size, drm_dev->drm_bufs[0].size);

    if (aic_disp->rotate_en) {
        buf1 = aic_disp->draw_buf->map;
        buf2 = NULL;
    } else {
        buf1 = drm_dev->drm_bufs[1].map;
        buf2 = drm_dev->drm_bufs[0].map;
    }

    lv_display_set_buffers(disp, buf1, buf2, buf_size, LV_DISPLAY_RENDER_MODE_DIRECT);
    lv_display_set_resolution(disp, hor_res, ver_res);
    lv_display_set_rotation(disp, aic_disp->rotate_degree);

    lv_display_set_driver_data(disp, drm_dev);
    lv_display_set_user_data(disp, aic_disp);
    lv_display_set_flush_wait_cb(disp, drm_flush_wait);
    lv_display_set_flush_cb(disp, drm_flush);

    if (width)
        lv_display_set_dpi(disp, DIV_ROUND_UP(hor_res * 25400, width * 1000));

    LV_LOG_TRACE("Resolution is set to %" LV_PRId32 "x%" LV_PRId32 " at %" LV_PRId32 "dpi",
                hor_res, ver_res, lv_display_get_dpi(disp));
    return 0;
err:
    close(drm_dev->fd);
    drm_dev->fd = -1;
    return -1;
}

/**********************
 *   STATIC FUNCTIONS
 **********************/

static uint32_t get_plane_property_id(drm_dev_t * drm_dev, const char * name)
{
    uint32_t i;

    LV_LOG_TRACE("Find plane property: %s", name);

    for (i = 0; i < drm_dev->count_plane_props; ++i)
        if (!lv_strcmp(drm_dev->plane_props[i]->name, name))
            return drm_dev->plane_props[i]->prop_id;

    LV_LOG_TRACE("Unknown plane property: %s", name);

    return 0;
}

static uint32_t get_crtc_property_id(drm_dev_t * drm_dev, const char * name)
{
    uint32_t i;

    LV_LOG_TRACE("Find crtc property: %s", name);

    for (i = 0; i < drm_dev->count_crtc_props; ++i)
        if (!lv_strcmp(drm_dev->crtc_props[i]->name, name))
            return drm_dev->crtc_props[i]->prop_id;

    LV_LOG_TRACE("Unknown crtc property: %s", name);

    return 0;
}

static uint32_t get_conn_property_id(drm_dev_t * drm_dev, const char * name)
{
    uint32_t i;

    LV_LOG_TRACE("Find conn property: %s", name);

    for (i = 0; i < drm_dev->count_conn_props; ++i)
        if (!lv_strcmp(drm_dev->conn_props[i]->name, name))
            return drm_dev->conn_props[i]->prop_id;

    LV_LOG_TRACE("Unknown conn property: %s", name);

    return 0;
}

static void page_flip_handler(int fd, unsigned int sequence,
                              unsigned int tv_sec, unsigned int tv_usec,
                              void * user_data)
{
    LV_UNUSED(fd);
    LV_UNUSED(sequence);
    LV_UNUSED(tv_sec);
    LV_UNUSED(tv_usec);
    LV_LOG_TRACE("flip");

    aic_disp_t * aic_disp = user_data;
    drm_dev_t * drm_dev = &aic_disp->drm_dev;

    if (drm_dev->req) {
        drmModeAtomicFree(drm_dev->req);
        drm_dev->req = NULL;
    }
}

static int drm_get_plane_props(drm_dev_t * drm_dev)
{
    uint32_t i;

    drmModeObjectPropertiesPtr props = drmModeObjectGetProperties(drm_dev->fd, drm_dev->plane_id, DRM_MODE_OBJECT_PLANE);
    if (!props) {
        LV_LOG_ERROR("drmModeObjectGetProperties failed");
        return -1;
    }
    LV_LOG_TRACE("Found %u plane props", props->count_props);
    drm_dev->count_plane_props = props->count_props;
    for (i = 0; i < props->count_props; i++) {
        drm_dev->plane_props[i] = drmModeGetProperty(drm_dev->fd, props->props[i]);
        LV_LOG_TRACE("Added plane prop %u:%s", drm_dev->plane_props[i]->prop_id, drm_dev->plane_props[i]->name);
    }
    drmModeFreeObjectProperties(props);

    return 0;
}

static int drm_get_crtc_props(drm_dev_t * drm_dev)
{
    uint32_t i;

    drmModeObjectPropertiesPtr props = drmModeObjectGetProperties(drm_dev->fd, drm_dev->crtc_id, DRM_MODE_OBJECT_CRTC);
    if (!props) {
        LV_LOG_ERROR("drmModeObjectGetProperties failed");
        return -1;
    }
    LV_LOG_TRACE("Found %u crtc props", props->count_props);
    drm_dev->count_crtc_props = props->count_props;
    for (i = 0; i < props->count_props; i++) {
        drm_dev->crtc_props[i] = drmModeGetProperty(drm_dev->fd, props->props[i]);
        LV_LOG_TRACE("Added crtc prop %u:%s", drm_dev->crtc_props[i]->prop_id, drm_dev->crtc_props[i]->name);
    }
    drmModeFreeObjectProperties(props);

    return 0;
}

static int drm_get_conn_props(drm_dev_t * drm_dev)
{
    uint32_t i;

    drmModeObjectPropertiesPtr props = drmModeObjectGetProperties(drm_dev->fd, drm_dev->conn_id, DRM_MODE_OBJECT_CONNECTOR);
    if (!props) {
        LV_LOG_ERROR("drmModeObjectGetProperties failed");
        return -1;
    }
    LV_LOG_TRACE("Found %u connector props", props->count_props);
    drm_dev->count_conn_props = props->count_props;
    for (i = 0; i < props->count_props; i++) {
        drm_dev->conn_props[i] = drmModeGetProperty(drm_dev->fd, props->props[i]);
        LV_LOG_TRACE("Added connector prop %u:%s", drm_dev->conn_props[i]->prop_id, drm_dev->conn_props[i]->name);
    }
    drmModeFreeObjectProperties(props);

    return 0;
}

static int drm_add_plane_property(drm_dev_t * drm_dev, const char * name, uint64_t value)
{
    int ret;
    uint32_t prop_id = get_plane_property_id(drm_dev, name);

    if (!prop_id) {
        LV_LOG_ERROR("Couldn't find plane prop %s", name);
        return -1;
    }

    ret = drmModeAtomicAddProperty(drm_dev->req, drm_dev->plane_id, get_plane_property_id(drm_dev, name), value);
    if (ret < 0) {
        LV_LOG_ERROR("drmModeAtomicAddProperty (%s:%" PRIu64 ") failed: %d", name, value, ret);
        return ret;
    }

    return 0;
}

static int drm_add_crtc_property(drm_dev_t * drm_dev, const char * name, uint64_t value)
{
    int ret;
    uint32_t prop_id = get_crtc_property_id(drm_dev, name);

    if (!prop_id) {
        LV_LOG_ERROR("Couldn't find crtc prop %s", name);
        return -1;
    }

    ret = drmModeAtomicAddProperty(drm_dev->req, drm_dev->crtc_id, get_crtc_property_id(drm_dev, name), value);
    if (ret < 0) {
        LV_LOG_ERROR("drmModeAtomicAddProperty (%s:%" PRIu64 ") failed: %d", name, value, ret);
        return ret;
    }

    return 0;
}

static int drm_add_conn_property(drm_dev_t * drm_dev, const char * name, uint64_t value)
{
    int ret;
    uint32_t prop_id = get_conn_property_id(drm_dev, name);

    if (!prop_id) {
        LV_LOG_ERROR("Couldn't find conn prop %s", name);
        return -1;
    }

    ret = drmModeAtomicAddProperty(drm_dev->req, drm_dev->conn_id, get_conn_property_id(drm_dev, name), value);
    if (ret < 0) {
        LV_LOG_ERROR("drmModeAtomicAddProperty (%s:%" PRIu64 ") failed: %d", name, value, ret);
        return ret;
    }

    return 0;
}

static int drm_atomic_set_plane(aic_disp_t * aic_disp, drm_dev_t * drm_dev, drm_buffer_t * buf)
{
    int ret;
    static int first = 1;
    uint32_t flags = DRM_MODE_PAGE_FLIP_EVENT | DRM_MODE_ATOMIC_NONBLOCK;

    drm_dev->req = drmModeAtomicAlloc();

    /* On first Atomic commit, do a modeset */
    if (first) {
        drm_add_conn_property(drm_dev, "CRTC_ID", drm_dev->crtc_id);

        drm_add_crtc_property(drm_dev, "MODE_ID", drm_dev->blob_id);
        drm_add_crtc_property(drm_dev, "ACTIVE", 1);

        flags |= DRM_MODE_ATOMIC_ALLOW_MODESET;

        first = 0;
    }

    drm_add_plane_property(drm_dev, "FB_ID", buf->id);
    drm_add_plane_property(drm_dev, "CRTC_ID", drm_dev->crtc_id);
    drm_add_plane_property(drm_dev, "SRC_X", 0);
    drm_add_plane_property(drm_dev, "SRC_Y", 0);
    drm_add_plane_property(drm_dev, "SRC_W", drm_dev->width << 16);
    drm_add_plane_property(drm_dev, "SRC_H", drm_dev->height << 16);
    drm_add_plane_property(drm_dev, "CRTC_X", 0);
    drm_add_plane_property(drm_dev, "CRTC_Y", 0);
    drm_add_plane_property(drm_dev, "CRTC_W", drm_dev->width);
    drm_add_plane_property(drm_dev, "CRTC_H", drm_dev->height);

    ret = drmModeAtomicCommit(drm_dev->fd, drm_dev->req, flags, aic_disp);
    if (ret) {
        LV_LOG_ERROR("drmModeAtomicCommit failed: %s (%d)", strerror(errno), errno);
        drmModeAtomicFree(drm_dev->req);
        return ret;
    }

    return 0;
}

static int find_plane(drm_dev_t * drm_dev, unsigned int fourcc, uint32_t * plane_id,
                      uint32_t crtc_id, uint32_t crtc_idx)
{
    LV_UNUSED(crtc_id);
    drmModePlaneResPtr planes;
    drmModePlanePtr plane;
    drmModePlanePtr primary_plane = NULL;
    unsigned int i, j;
    int ret = 0;

    planes = drmModeGetPlaneResources(drm_dev->fd);
    if (!planes) {
        LV_LOG_ERROR("drmModeGetPlaneResources failed");
        return -1;
    }

    LV_LOG_TRACE("drm: found planes %u", planes->count_planes);

    for (i = 0; i < planes->count_planes; ++i) {
        plane = drmModeGetPlane(drm_dev->fd, planes->planes[i]);
        if (!plane) {
            LV_LOG_ERROR("drmModeGetPlane failed: %s", strerror(errno));
            break;
        }

        if (plane->possible_crtcs & (1 << crtc_idx)) {
            drmModeObjectProperties *props = drmModeObjectGetProperties(drm_dev->fd, plane->plane_id, DRM_MODE_OBJECT_PLANE);
            if (!props) {
                LV_LOG_ERROR("Failed to get plane properties");
                drmModeFreePlane(plane);
                continue;
            }

            for (j = 0; j < props->count_props; j++) {
                drmModePropertyRes *prop = drmModeGetProperty(drm_dev->fd, props->props[j]);
                if (!prop)
                    continue;

                if (strcmp(prop->name, "type") == 0) {
                    if (props->prop_values[j] == DRM_PLANE_TYPE_PRIMARY) {
                        primary_plane = plane;
                        drmModeFreeProperty(prop);
                        break;
                    }
                }
                drmModeFreeProperty(prop);
            }

            drmModeFreeObjectProperties(props);

            if (primary_plane)
                break;
        }
    }

    if (primary_plane) {
        *plane_id = primary_plane->plane_id;
        drmModeFreePlane(plane);

        LV_LOG_TRACE("found plane %d", *plane_id);
    } else {
        LV_LOG_ERROR("not found primary plane");
        ret = -1;
    }

    drmModeFreePlaneResources(planes);

    return ret;
}

static int drm_find_connector(drm_dev_t * drm_dev, int64_t connector_id)
{
    drmModeConnector * conn = NULL;
    drmModeEncoder * enc = NULL;
    drmModeRes * res;
    int i;

    if ((res = drmModeGetResources(drm_dev->fd)) == NULL) {
        LV_LOG_ERROR("drmModeGetResources() failed");
        return -1;
    }

    if (res->count_crtcs <= 0) {
        LV_LOG_ERROR("no Crtcs");
        goto free_res;
    }

    /* find all available connectors */
    for (i = 0; i < res->count_connectors; i++) {
        conn = drmModeGetConnector(drm_dev->fd, res->connectors[i]);
        if (!conn)
            continue;

        if (connector_id >= 0 && conn->connector_id != connector_id) {
            drmModeFreeConnector(conn);
            continue;
        }

        if (conn->connection == DRM_MODE_CONNECTED)
            LV_LOG_TRACE("drm: connector %d: connected", conn->connector_id);
        else if (conn->connection == DRM_MODE_DISCONNECTED)
            LV_LOG_TRACE("drm: connector %d: disconnected", conn->connector_id);
        else if (conn->connection == DRM_MODE_UNKNOWNCONNECTION)
            LV_LOG_TRACE("drm: connector %d: unknownconnection", conn->connector_id);
        else
            LV_LOG_TRACE("drm: connector %d: unknown", conn->connector_id);

        if (conn->connection == DRM_MODE_CONNECTED && conn->count_modes > 0)
            break;

        drmModeFreeConnector(conn);
        conn = NULL;
    };

    if (!conn) {
        LV_LOG_ERROR("suitable connector not found");
        goto free_res;
    }

    drm_dev->conn_id = conn->connector_id;
    LV_LOG_TRACE("conn_id: %d", drm_dev->conn_id);
    drm_dev->mmWidth = conn->mmWidth;
    drm_dev->mmHeight = conn->mmHeight;

    lv_memcpy(&drm_dev->mode, &conn->modes[0], sizeof(drmModeModeInfo));

    if (drmModeCreatePropertyBlob(drm_dev->fd, &drm_dev->mode, sizeof(drm_dev->mode),
                                 &drm_dev->blob_id)) {
        LV_LOG_ERROR("error creating mode blob");
        goto free_res;
    }

    drm_dev->width = conn->modes[0].hdisplay;
    drm_dev->height = conn->modes[0].vdisplay;

    for (i = 0 ; i < res->count_encoders; i++) {
        enc = drmModeGetEncoder(drm_dev->fd, res->encoders[i]);
        if (!enc)
            continue;

        LV_LOG_TRACE("enc%d enc_id %d conn enc_id %d", i, enc->encoder_id, conn->encoder_id);

        if (enc->encoder_id == conn->encoder_id)
            break;

        drmModeFreeEncoder(enc);
        enc = NULL;
    }

    if (enc) {
        drm_dev->enc_id = enc->encoder_id;
        LV_LOG_TRACE("enc_id: %d", drm_dev->enc_id);
        drm_dev->crtc_id = enc->crtc_id;
        LV_LOG_TRACE("crtc_id: %d", drm_dev->crtc_id);
        drmModeFreeEncoder(enc);
    } else {
        /* Encoder hasn't been associated yet, look it up */
        for (i = 0; i < conn->count_encoders; i++) {
            int crtc, crtc_id = -1;

            enc = drmModeGetEncoder(drm_dev->fd, conn->encoders[i]);
            if (!enc)
                continue;

            for (crtc = 0 ; crtc < res->count_crtcs; crtc++) {
                uint32_t crtc_mask = 1 << crtc;

                crtc_id = res->crtcs[crtc];

                LV_LOG_TRACE("enc_id %d crtc%d id %d mask %x possible %x",
                             enc->encoder_id, crtc, crtc_id, crtc_mask,
                             enc->possible_crtcs);

                if (enc->possible_crtcs & crtc_mask)
                    break;
            }

            if (crtc_id > 0) {
                drm_dev->enc_id = enc->encoder_id;
                LV_LOG_TRACE("enc_id: %d", drm_dev->enc_id);
                drm_dev->crtc_id = crtc_id;
                LV_LOG_TRACE("crtc_id: %d", drm_dev->crtc_id);
                break;
            }

            drmModeFreeEncoder(enc);
            enc = NULL;
        }

        if (!enc) {
            LV_LOG_ERROR("suitable encoder not found");
            goto free_res;
        }

        drmModeFreeEncoder(enc);
    }

    drm_dev->crtc_idx = UINT32_MAX;

    for (i = 0; i < res->count_crtcs; ++i) {
        if (drm_dev->crtc_id == res->crtcs[i]) {
            drm_dev->crtc_idx = i;
            break;
        }
    }

    if (drm_dev->crtc_idx == UINT32_MAX) {
        LV_LOG_ERROR("drm: CRTC not found");
        goto free_res;
    }

    LV_LOG_TRACE("crtc_idx: %d", drm_dev->crtc_idx);

    return 0;

free_res:
    drmModeFreeResources(res);

    return -1;
}

static int drm_open(const char * path)
{
    int fd, flags;
    uint64_t has_dumb;
    int ret;

    fd = open(path, O_RDWR);
    if (fd < 0) {
        LV_LOG_ERROR("cannot open \"%s\"", path);
        return -1;
    }

    /* set FD_CLOEXEC flag */
    if ((flags = fcntl(fd, F_GETFD)) < 0 ||
       fcntl(fd, F_SETFD, flags | FD_CLOEXEC) < 0) {
        LV_LOG_ERROR("fcntl FD_CLOEXEC failed");
        goto err;
    }

    /* check capability */
    ret = drmGetCap(fd, DRM_CAP_DUMB_BUFFER, &has_dumb);
    if (ret < 0 || has_dumb == 0) {
        LV_LOG_ERROR("drmGetCap DRM_CAP_DUMB_BUFFER failed or \"%s\" doesn't have dumb buffer",
                     path);
        goto err;
    }

    return fd;
err:
    close(fd);
    return -1;
}

static int drm_setup(drm_dev_t * drm_dev, const char * device_path,
                     int64_t connector_id, unsigned int fourcc)
{
    int ret;

    drm_dev->fd = drm_open(device_path);
    if (drm_dev->fd < 0)
    return -1;

    ret = drmSetClientCap(drm_dev->fd, DRM_CLIENT_CAP_ATOMIC, 1);
    if (ret) {
        LV_LOG_ERROR("No atomic modesetting support: %s", strerror(errno));
        goto err;
    }

    ret = drm_find_connector(drm_dev, connector_id);
    if (ret) {
        LV_LOG_ERROR("available drm devices not found");
        goto err;
    }

    ret = find_plane(drm_dev, fourcc, &drm_dev->plane_id, drm_dev->crtc_id, drm_dev->crtc_idx);
    if (ret) {
        LV_LOG_ERROR("Cannot find plane");
        goto err;
    }

    drm_dev->plane = drmModeGetPlane(drm_dev->fd, drm_dev->plane_id);
    if (!drm_dev->plane) {
        LV_LOG_ERROR("Cannot get plane");
        goto err;
    }

    drm_dev->crtc = drmModeGetCrtc(drm_dev->fd, drm_dev->crtc_id);
    if (!drm_dev->crtc) {
        LV_LOG_ERROR("Cannot get crtc");
        goto err;
    }

    drm_dev->conn = drmModeGetConnector(drm_dev->fd, drm_dev->conn_id);
    if (!drm_dev->conn) {
        LV_LOG_ERROR("Cannot get connector");
        goto err;
    }

    ret = drm_get_plane_props(drm_dev);
    if (ret) {
        LV_LOG_ERROR("Cannot get plane props");
        goto err;
    }

    ret = drm_get_crtc_props(drm_dev);
    if (ret) {
        LV_LOG_ERROR("Cannot get crtc props");
        goto err;
    }

    ret = drm_get_conn_props(drm_dev);
    if (ret) {
        LV_LOG_ERROR("Cannot get connector props");
        goto err;
    }

    drm_dev->drm_event_ctx.version = DRM_EVENT_CONTEXT_VERSION;
    drm_dev->drm_event_ctx.page_flip_handler = page_flip_handler;
    drm_dev->fourcc = fourcc;

    LV_LOG_INFO("drm: Found plane_id: %u connector_id: %d crtc_id: %d",
            drm_dev->plane_id, drm_dev->conn_id, drm_dev->crtc_id);

    LV_LOG_INFO("drm: %dx%d (%dmm X% dmm) pixel format %c%c%c%c",
            drm_dev->width, drm_dev->height, drm_dev->mmWidth, drm_dev->mmHeight,
            (fourcc >> 0) & 0xff, (fourcc >> 8) & 0xff, (fourcc >> 16) & 0xff, (fourcc >> 24) & 0xff);

    return 0;

err:
    close(drm_dev->fd);
    return -1;
}

static int drm_allocate_dumb(drm_dev_t * drm_dev, drm_buffer_t * buf)
{
    struct drm_mode_create_dumb creq;
    struct drm_mode_map_dumb mreq;
    uint32_t handles[4] = {0}, pitches[4] = {0}, offsets[4] = {0};
    int ret;

    /* create dumb buffer */
    lv_memzero(&creq, sizeof(creq));
    creq.width = drm_dev->width;
    creq.height = drm_dev->height;
    creq.bpp = LV_COLOR_DEPTH;
    ret = drmIoctl(drm_dev->fd, DRM_IOCTL_MODE_CREATE_DUMB, &creq);
    if (ret < 0) {
        LV_LOG_ERROR("DRM_IOCTL_MODE_CREATE_DUMB fail");
        return -1;
    }

    buf->handle = creq.handle;
    buf->pitch = creq.pitch;
    buf->size = creq.size;

    /* prepare buffer for memory mapping */
    lv_memzero(&mreq, sizeof(mreq));
    mreq.handle = creq.handle;
    ret = drmIoctl(drm_dev->fd, DRM_IOCTL_MODE_MAP_DUMB, &mreq);
    if (ret) {
        LV_LOG_ERROR("DRM_IOCTL_MODE_MAP_DUMB fail");
        return -1;
    }

    buf->offset = mreq.offset;
    LV_LOG_TRACE("dump buf size %lu pitch %u offset %u",
            buf->size, buf->pitch, buf->offset);

    /* perform actual memory mapping */
    buf->map = mmap(0, creq.size, PROT_READ | PROT_WRITE, MAP_SHARED, drm_dev->fd, mreq.offset);
    if (buf->map == MAP_FAILED) {
        LV_LOG_ERROR("mmap fail");
        return -1;
    }

    /* create framebuffer object for the dumb-buffer */
    handles[0] = creq.handle;
    pitches[0] = creq.pitch;
    offsets[0] = 0;
    ret = drmModeAddFB2(drm_dev->fd, drm_dev->width, drm_dev->height, drm_dev->fourcc,
                        handles, pitches, offsets, &buf->id, 0);
    if (ret) {
        LV_LOG_ERROR("drmModeAddFB fail");
        return -1;
    }

    ret = drmPrimeHandleToFD(drm_dev->fd, buf->handle, 0, &buf->fd);
    if (ret) {
        LV_LOG_ERROR("drmPrimeHandleToFD fail");
        return -1;
    }

    if (ioctl(buf->fd, DMA_BUF_IOCTL_GET_PHY_ADDR, &buf->phy_addr) < 0)
        LV_LOG_ERROR("DMA_BUF_IOCTL_GET_PHY_ADDR fail");

    return 0;
}

static int drm_setup_buffers(drm_dev_t * drm_dev)
{
    int ret;

    /*Allocate DUMB buffers*/
    ret = drm_allocate_dumb(drm_dev, &drm_dev->drm_bufs[0]);
    if (ret)
        return ret;

    ret = drm_allocate_dumb(drm_dev, &drm_dev->drm_bufs[1]);
    if (ret)
        return ret;

    return 0;
}

static inline void *get_fb_buf_by_id(aic_disp_t *aic_disp, int id)
{
    drm_dev_t * drm_dev = &aic_disp->drm_dev;

    if (id == 1)
        return (void *)drm_dev->drm_bufs[id].map;
    else
        return (void *)drm_dev->drm_bufs[0].map;
}

static inline void wait_sync_ready(aic_disp_t *aic_disp)
{
    while (aic_disp->sync_ready == false) {
        if (aic_disp->exit_status)
                break;

        lv_thread_sync_wait(&aic_disp->sync_notify);
    }
}

static inline void disp_do_blit(aic_disp_t *aic_disp, lv_display_t *disp, lv_draw_buf_t *disp_buf)
{
    drm_dev_t * drm_dev = lv_display_get_driver_data(disp);
    void *dest_buf = get_fb_buf_by_id(aic_disp, aic_disp->buf_id);
    int32_t hor_res = lv_display_get_horizontal_resolution(disp);
    int32_t ver_res = lv_display_get_vertical_resolution(disp);
    lv_color_format_t cf = lv_display_get_color_format(disp);
    int32_t src_stride = lv_draw_buf_width_to_stride(hor_res, cf);
    int32_t dst_stride = (int32_t)drm_dev->drm_bufs[0].pitch;
    lv_display_rotation_t rotation = lv_display_get_rotation(disp);
#if LV_USE_DRAW_GE2D
    lv_draw_ge2d_rotate(disp_buf->data, dest_buf, hor_res, ver_res,
                        src_stride, dst_stride, rotation, cf);
#else
    lv_draw_sw_rotate(disp_buf->data, dest_buf, hor_res, ver_res,
                      src_stride, dst_stride, rotation, cf);
#endif
}

static inline void disp_draw_buf_flush(aic_disp_t *aic_disp, lv_display_t *disp)
{
    lv_draw_buf_t *disp_buf = lv_display_get_buf_active(disp);
    drm_dev_t * drm_dev = lv_display_get_driver_data(disp);
    int idx = aic_disp->buf_id;

    wait_sync_ready(aic_disp);
    aic_disp->sync_ready = false;
    disp_do_blit(aic_disp, disp, disp_buf);

    if (drm_atomic_set_plane(aic_disp, drm_dev, &drm_dev->drm_bufs[idx])) {
        LV_LOG_ERROR("Flush fail");
        return;
    } else {
        LV_LOG_TRACE("Flush done");
    }

    aic_disp->flush_act = true;
    lv_thread_sync_signal(&aic_disp->sync);
}

static void drm_flush_wait(lv_display_t * disp)
{
    aic_disp_t *aic_disp = lv_display_get_user_data(disp);
    drm_dev_t * drm_dev = lv_display_get_driver_data(disp);

    if (aic_disp->draw_buf)
        return;

    struct pollfd pfd;
    pfd.fd = drm_dev->fd;
    pfd.events = POLLIN;

    while (drm_dev->req) {
        int ret;
        do {
            ret = poll(&pfd, 1, -1);
        } while (ret == -1 && errno == EINTR);

        if (ret > 0) {
            drmHandleEvent(drm_dev->fd, &drm_dev->drm_event_ctx);
        } else {
            LV_LOG_ERROR("poll failed: %s", strerror(errno));
            return;
        }
    }
}

static void drm_flush(lv_display_t * disp, const lv_area_t * area, uint8_t * px_map)
{
    if (!lv_display_flush_is_last(disp)) return;

    LV_UNUSED(area);
    LV_UNUSED(px_map);
    aic_disp_t *aic_disp = lv_display_get_user_data(disp);
    drm_dev_t * drm_dev = lv_display_get_driver_data(disp);

    if (aic_disp->draw_buf) {
        aic_disp->buf_id  =  aic_disp->buf_id > 0 ? 0 : 1;
        disp_draw_buf_flush(aic_disp, disp);
        return;
    }

    for (int idx = 0; idx < 2; idx++) {
        if (drm_dev->drm_bufs[idx].map == px_map) {
            /*Request buffer swap*/
            if (drm_atomic_set_plane(aic_disp, drm_dev, &drm_dev->drm_bufs[idx])) {
                LV_LOG_ERROR("Flush fail");
                return;
            } else {
                display_cal_frame_rate(aic_disp);
                LV_LOG_TRACE("Flush done");
            }
        }
    }
}

static void aic_display_thread(void *ptr)
{
    aic_disp_t *aic_disp = (aic_disp_t *)ptr;
    drm_dev_t * drm_dev = &aic_disp->drm_dev;

    struct pollfd pfd;
    pfd.fd = drm_dev->fd;
    pfd.events = POLLIN;

    while(1) {
        while (aic_disp->flush_act == false) {
            if(aic_disp->exit_status)
                break;

            lv_thread_sync_wait(&aic_disp->sync);
        }

        aic_disp->flush_act = false;

        while (drm_dev->req) {
            int ret;
            do {
                ret = poll(&pfd, 1, -1);
            } while (ret == -1 && errno == EINTR);

            if (ret > 0) {
                drmHandleEvent(drm_dev->fd, &drm_dev->drm_event_ctx);
            } else {
                LV_LOG_ERROR("aic display thread poll failed: %s", strerror(errno));
                return;
            }
        }

        aic_disp->sync_ready = true;
        lv_thread_sync_signal(&aic_disp->sync_notify);

        if (aic_disp->exit_status) {
            LV_LOG_INFO("Ready to exit aic display thread.");
            break;
        }
    }

    lv_thread_sync_delete(&aic_disp->sync);
    lv_thread_sync_delete(&aic_disp->sync_notify);
    LV_LOG_INFO("Exit aic display thread.");
}

#if LV_USE_PROFILER
#include <sys/syscall.h>
#include <sys/types.h>
#include <stdio.h>
#include <time.h>

static uint32_t my_get_tick_us_cb(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000000 + ts.tv_nsec / 1000;
}

static int my_get_tid_cb(void)
{
    return (int)syscall(SYS_gettid);
}

static int my_get_cpu_cb(void)
{
    int cpu_id = 0;
    syscall(SYS_getcpu, &cpu_id, NULL);
    return cpu_id;
}

FILE *f_trace = NULL;

static void my_log_print_cb(const char * buf)
{
    fprintf(f_trace, "%s", buf);
    // fflush(f_trace);
}

void my_profiler_init(void)
{
    f_trace = fopen(LV_PROFILER_OUTPUT_PATH, "w");
    if (f_trace == NULL) {
        fprintf(stderr, "failed to open lvgl_trace file\n");
        return;
    }

    lv_profiler_builtin_config_t config;
    lv_profiler_builtin_config_init(&config);
    config.tick_per_sec = 1000000; /* One second is equal to 1000000 microseconds */
    config.tick_get_cb = my_get_tick_us_cb;
    config.tid_get_cb = my_get_tid_cb;
    config.cpu_get_cb = my_get_cpu_cb;
    config.flush_cb = my_log_print_cb;
    lv_profiler_builtin_init(&config);
}
#endif

void lv_port_disp_init(void)
{
#if LV_USE_MPP_DEC
    lv_mpp_dec_init();
#endif

#if LV_USE_DRAW_GE2D
    lv_draw_ge2d_init();
#endif

    aic_disp_t * aic_disp = lv_malloc_zeroed(sizeof(aic_disp_t));
    LV_ASSERT_MALLOC(aic_disp);
    if (aic_disp == NULL)
        return;

#if defined(LV_DISPLAY_ROTATE_EN)
    aic_disp->rotate_en = true;
#endif

#if defined(LV_ROTATE_DEGREE)
    aic_disp->rotate_degree = LV_ROTATE_DEGREE / 90;
#endif

    if (lv_linux_drm_set_file(aic_disp, "/dev/dri/card0", -1))
        return;

    if (aic_disp->rotate_en) {
        aic_disp->sync_ready = true;
        lv_thread_sync_init(&aic_disp->sync);
        lv_thread_sync_init(&aic_disp->sync_notify);
        lv_thread_init(&aic_disp->thread, LV_THREAD_PRIO_HIGH,
                   aic_display_thread, 8 * 1024, aic_disp);
    }

#if LV_USE_PROFILER
    my_profiler_init();
#endif
}
