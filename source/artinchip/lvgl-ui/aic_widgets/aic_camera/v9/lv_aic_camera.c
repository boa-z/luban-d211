/*
 * Copyright (c) 2023-2026, ArtInChip Technology Co., Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Authors:  Zequan Liang <zequan.liang@artinchip.com>
 *
 * LVGL v9 AIC Camera widget — Linux port.
 * Displays live camera preview via AICFB video layer overlay.
 */

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include "lv_aic_vin.h"
#include "lv_aic_display.h"
#include "lv_aic_camera.h"
#include <linux/videodev2.h>
#include <linux/v4l2-subdev.h>
#include <video/artinchip_fb.h>
#include <video/mpp_types.h>
#include <linux/media-bus-format.h>

/**********************
 *      DEFINES
 **********************/

#define MY_CLASS         (&lv_aic_camera_class)
#define VID_SCALE_OFFSET 0
#define CAM_DEV_ID       0

/**********************
 *      TYPEDEFS
 **********************/

typedef enum {
    LV_AIC_CAMERA_STATUS_INIT,
    LV_AIC_CAMERA_STATUS_READY,
    LV_AIC_CAMERA_STATUS_START,
    LV_AIC_CAMERA_STATUS_RUNNING,
    LV_AIC_CAMERA_STATUS_STOP,
    LV_AIC_CAMERA_STATUS_DELETE,
    _LV_AIC_CAMERA_STATUS_LAST
} lv_aic_camera_status;

struct aic_camera_ctx_s {
    int w;
    int h;
    int frame_size;
    int rotation;
    int dst_fmt;
    struct vin_fmt src_fmt;
    struct lv_aic_video_data vin_data;
    struct mpp_rect dst_rect;

    struct lv_aic_vin_dev vin_dev;
    struct lv_aic_display display;

    lv_aic_camera_frame_cb frame_cb;
    void *frame_cb_data;

    volatile uint32_t status;
    volatile bool is_display;
    lv_mutex_t mutex;
    lv_thread_sync_t video_sync;
    lv_thread_t video_thread;
    lv_thread_sync_t video_exit_sync;
};

/**********************
 *  STATIC PROTOTYPES
 **********************/

static void lv_aic_camera_constructor(const lv_obj_class_t *class_p, lv_obj_t *obj);
static void lv_aic_camera_destructor(const lv_obj_class_t *class_p, lv_obj_t *obj);
static void lv_aic_camera_event(const lv_obj_class_t *class_p, lv_event_t *e);

static int format_conversion(lv_aic_camera_format format);
static int sensor_get_sensor_fmt(struct aic_camera_ctx_s *aic_ctx);
static int lv_camera_video_layer_set(struct aic_camera_ctx_s *aic_ctx, int index);
static int lv_camera_video_layer_disable(struct aic_camera_ctx_s *aic_ctx);
static int lv_camera_set_sensor_fmt(struct aic_camera_ctx_s *aic_ctx);
static int lv_camera_set_vin_subdev_fmt(struct aic_camera_ctx_s *aic_ctx);
static int lv_camera_set_out_fmt(struct aic_camera_ctx_s *aic_ctx);
static void lv_camera_draw_video_layer_entry(void *ptr);

/**********************
 *  CLASS DEFINITION
 **********************/

const lv_obj_class_t lv_aic_camera_class = {
    .constructor_cb = lv_aic_camera_constructor,
    .destructor_cb = lv_aic_camera_destructor,
    .event_cb = lv_aic_camera_event,
    .width_def = LV_SIZE_CONTENT,
    .height_def = LV_SIZE_CONTENT,
    .instance_size = sizeof(lv_aic_camera_t),
    .base_class = &lv_obj_class,
    .name = "aic_camera",
};

/**********************
 *  PUBLIC API
 **********************/

lv_obj_t *lv_aic_camera_create(lv_obj_t *parent)
{
    lv_obj_t *obj = lv_obj_class_create_obj(MY_CLASS, parent);
    lv_obj_class_init_obj(obj);
    return obj;
}

lv_res_t lv_aic_camera_set_sensor_fmt(lv_obj_t *obj, int width, int height)
{
    lv_aic_camera_t *camera = (lv_aic_camera_t *)obj;

    camera->width = width;
    camera->height = height;
    return LV_RES_OK;
}

lv_res_t lv_aic_camera_set_format(lv_obj_t *obj, lv_aic_camera_format format)
{
    lv_aic_camera_t *camera = (lv_aic_camera_t *)obj;
    camera->format = format;
    return LV_RES_OK;
}

lv_res_t lv_aic_camera_open(lv_obj_t *obj)
{
    lv_aic_camera_t *camera = (lv_aic_camera_t *)obj;
    struct aic_camera_ctx_s *aic_ctx = camera->aic_ctx;

    if (aic_ctx == NULL) {
        aic_ctx = lv_malloc(sizeof(struct aic_camera_ctx_s));
        if (aic_ctx == NULL) {
            LV_LOG_ERROR("lv_aic_camera_open: alloc failed");
            return LV_RES_INV;
        }
        lv_memset(aic_ctx, 0, sizeof(struct aic_camera_ctx_s));
        aic_ctx->status = LV_AIC_CAMERA_STATUS_INIT;
    }

    if (aic_ctx->status != LV_AIC_CAMERA_STATUS_INIT) {
        LV_LOG_WARN("camera already opened, status=%d", aic_ctx->status);
        return LV_RES_OK;
    }

    if (lv_aic_vin_open(&aic_ctx->vin_dev) < 0) {
        LV_LOG_ERROR("lv_aic_vin_open failed");
        goto camera_error;
    }

    if (sensor_get_sensor_fmt(aic_ctx) < 0) {
        LV_LOG_ERROR("sensor_get_sensor_fmt failed");
        goto camera_error;
    }

    /* override with user-specified resolution from JSON config */
    if (camera->width > 0 && camera->height > 0) {
        aic_ctx->src_fmt.width = camera->width;
        aic_ctx->src_fmt.height = camera->height;
        aic_ctx->w = camera->width;
        aic_ctx->h = camera->height;
    }

    aic_ctx->dst_fmt = format_conversion(camera->format);
    if (aic_ctx->dst_fmt == V4L2_PIX_FMT_NV16)
        aic_ctx->frame_size = aic_ctx->w * aic_ctx->h * 2;
    else if (aic_ctx->dst_fmt == V4L2_PIX_FMT_NV12)
        aic_ctx->frame_size = (aic_ctx->w * aic_ctx->h * 3) >> 1;

    aic_ctx->vin_data.num_buffers = VID_BUF_NUM;
    aic_ctx->rotation = MPP_ROTATION_0;

    if (lv_camera_set_sensor_fmt(aic_ctx) < 0) {
        LV_LOG_ERROR("lv_camera_set_sensor_fmt failed");
        goto camera_error;
    }

    if (lv_camera_set_vin_subdev_fmt(aic_ctx) < 0) {
        LV_LOG_ERROR("lv_camera_set_vin_subdev_fmt failed");
        goto camera_error;
    }

    if (lv_aic_display_open(&aic_ctx->display, LV_AIC_DISPLAY_AICFB) < 0) {
        LV_LOG_ERROR("lv_aic_display_open failed");
        goto camera_error;
    }

    if (lv_camera_set_out_fmt(aic_ctx) < 0) {
        LV_LOG_ERROR("lv_camera_set_out_fmt failed");
        goto camera_error;
    }

    lv_obj_update_layout(obj);
    lv_area_t fill_area = {0};
    lv_obj_get_coords(obj, &fill_area);

    aic_ctx->dst_rect.x = fill_area.x1;
    aic_ctx->dst_rect.y = fill_area.y1;
    aic_ctx->dst_rect.width = fill_area.x2 - fill_area.x1 + 1;
    aic_ctx->dst_rect.height = fill_area.y2 - fill_area.y1 + 1;

    /* fallback: widget on hidden tab may return 0 coords */
    if (aic_ctx->dst_rect.width <= 0)
        aic_ctx->dst_rect.width = aic_ctx->w;
    if (aic_ctx->dst_rect.height <= 0)
        aic_ctx->dst_rect.height = aic_ctx->h;

    camera->aic_ctx = aic_ctx;
    lv_mutex_init(&aic_ctx->mutex);
    lv_thread_sync_init(&aic_ctx->video_sync);
    lv_thread_init(&aic_ctx->video_thread, 20, lv_camera_draw_video_layer_entry, 4 * 1024, camera);
    lv_thread_sync_init(&aic_ctx->video_exit_sync);

    aic_ctx->status = LV_AIC_CAMERA_STATUS_READY;
    lv_obj_refresh_self_size(obj);
    return LV_RES_OK;

camera_error:
    lv_aic_display_close(&aic_ctx->display);
    lv_aic_vin_close(&aic_ctx->vin_dev);
    if (aic_ctx)
        lv_free(aic_ctx);
    camera->aic_ctx = NULL;
    return LV_RES_INV;
}

lv_res_t lv_aic_camera_start(lv_obj_t *obj)
{
    lv_aic_camera_t *camera = (lv_aic_camera_t *)obj;
    struct aic_camera_ctx_s *aic_ctx = camera->aic_ctx;

    if (aic_ctx && aic_ctx->status == LV_AIC_CAMERA_STATUS_READY) {
        lv_mutex_lock(&aic_ctx->mutex);
        aic_ctx->status = LV_AIC_CAMERA_STATUS_START;
        lv_mutex_unlock(&aic_ctx->mutex);
        lv_thread_sync_signal(&aic_ctx->video_sync);
        return LV_RES_OK;
    }
    LV_LOG_WARN("camera start: invalid status");
    return LV_RES_INV;
}

lv_res_t lv_aic_camera_display_start(lv_obj_t *obj)
{
    lv_aic_camera_t *camera = (lv_aic_camera_t *)obj;
    struct aic_camera_ctx_s *aic_ctx;

    if (!camera || !camera->aic_ctx)
        return LV_RES_INV;

    aic_ctx = camera->aic_ctx;
    lv_mutex_lock(&aic_ctx->mutex);
    aic_ctx->is_display = 1;
    lv_mutex_unlock(&aic_ctx->mutex);
    return LV_RES_OK;
}

lv_res_t lv_aic_camera_display_stop(lv_obj_t *obj)
{
    lv_aic_camera_t *camera = (lv_aic_camera_t *)obj;
    struct aic_camera_ctx_s *aic_ctx;

    if (!camera || !camera->aic_ctx)
        return LV_RES_INV;

    aic_ctx = camera->aic_ctx;
    lv_mutex_lock(&aic_ctx->mutex);
    aic_ctx->is_display = 0;
    lv_mutex_unlock(&aic_ctx->mutex);
    lv_camera_video_layer_disable(aic_ctx);
    return LV_RES_OK;
}

void lv_aic_camera_set_frame_callback(lv_obj_t *obj, lv_aic_camera_frame_cb cb, void *user_data)
{
    lv_aic_camera_t *camera = (lv_aic_camera_t *)obj;
    if (!camera->aic_ctx)
        return;
    camera->aic_ctx->frame_cb = cb;
    camera->aic_ctx->frame_cb_data = user_data;

    LV_LOG_INFO("set frame callback: %p", cb);
}

struct lv_aic_display *lv_aic_camera_get_display(lv_obj_t *obj)
{
    lv_aic_camera_t *camera = (lv_aic_camera_t *)obj;
    if (!camera->aic_ctx)
        return NULL;
    return &camera->aic_ctx->display;
}

lv_res_t lv_aic_camera_stop(lv_obj_t *obj)
{
    lv_aic_camera_t *camera = (lv_aic_camera_t *)obj;
    struct aic_camera_ctx_s *aic_ctx = camera->aic_ctx;

    if (aic_ctx && aic_ctx->status == LV_AIC_CAMERA_STATUS_RUNNING) {
        lv_mutex_lock(&aic_ctx->mutex);
        aic_ctx->status = LV_AIC_CAMERA_STATUS_STOP;
        lv_mutex_unlock(&aic_ctx->mutex);
    }
    return LV_RES_OK;
}

/**********************
 *  STATIC FUNCTIONS
 **********************/

static int format_conversion(lv_aic_camera_format format)
{
    switch (format) {
    case LV_AIC_CAMERA_FORMAT_NV16:
        return V4L2_PIX_FMT_NV16;
    case LV_AIC_CAMERA_FORMAT_NV12:
        return V4L2_PIX_FMT_NV12;
    default:
        return V4L2_PIX_FMT_NV12;
    }
}

static int sensor_get_sensor_fmt(struct aic_camera_ctx_s *aic_ctx)
{
    struct vin_fmt fmt = {0};

    if (lv_aic_vin_get_sensor_fmt(&aic_ctx->vin_dev, &fmt) < 0) {
        LV_LOG_ERROR("Failed to get sensor format");
        return -1;
    }

    aic_ctx->src_fmt = fmt;
    aic_ctx->w = aic_ctx->src_fmt.width;
    aic_ctx->h = aic_ctx->src_fmt.height;

    /* 8-bit mono sensor: force YUV400 */
    if (fmt.code == MEDIA_BUS_FMT_Y8_1X8 || fmt.code == MEDIA_BUS_FMT_Y10_1X10) {
        LV_LOG_INFO("Mono sensor, forcing YUV400");
        aic_ctx->dst_fmt = MPP_FMT_YUV400;
    }
    return 0;
}

static int lv_camera_set_sensor_fmt(struct aic_camera_ctx_s *aic_ctx)
{
    return lv_aic_vin_set_sensor_fmt(&aic_ctx->vin_dev, &aic_ctx->src_fmt);
}

static int lv_camera_set_vin_subdev_fmt(struct aic_camera_ctx_s *aic_ctx)
{
    return lv_aic_vin_set_vin_subdev_fmt(&aic_ctx->vin_dev, &aic_ctx->src_fmt);
}

static int lv_camera_set_out_fmt(struct aic_camera_ctx_s *aic_ctx)
{
    struct dvp_out_fmt fmt = {0};

    fmt.width = aic_ctx->src_fmt.width;
    fmt.height = aic_ctx->src_fmt.height;
    fmt.pixelformat = aic_ctx->dst_fmt;
    fmt.num_planes = VID_BUF_PLANE_NUM;

    return lv_aic_vin_set_out_fmt(&aic_ctx->vin_dev, CAM_DEV_ID, &fmt);
}

static int lv_camera_stream_off(struct aic_camera_ctx_s *aic_ctx)
{
    return lv_aic_vin_stream_off(&aic_ctx->vin_dev, CAM_DEV_ID);
}

static int lv_camera_stream_on(struct aic_camera_ctx_s *aic_ctx)
{
    return lv_aic_vin_stream_on(&aic_ctx->vin_dev, CAM_DEV_ID);
}

static int lv_camera_dq_buf(struct aic_camera_ctx_s *aic_ctx, int *index)
{
    return lv_aic_vin_dq_buf(&aic_ctx->vin_dev, CAM_DEV_ID, index);
}

static int lv_camera_q_buf(struct aic_camera_ctx_s *aic_ctx, int index)
{
    return lv_aic_vin_q_buf(&aic_ctx->vin_dev, CAM_DEV_ID, index);
}

int lv_aic_camera_q_buf(lv_obj_t *obj, int index)
{
    lv_aic_camera_t *camera = (lv_aic_camera_t *)obj;
    if (!camera || !camera->aic_ctx)
        return -1;
    return lv_camera_q_buf(camera->aic_ctx, index);
}

static int lv_camera_req_buf(struct aic_camera_ctx_s *aic_ctx)
{
    struct lv_aic_video_data *vdata = &aic_ctx->vin_data;

    if (lv_aic_vin_req_buf(&aic_ctx->vin_dev, CAM_DEV_ID, vdata) < 0) {
        LV_LOG_ERROR("Failed to request buf");
        return -1;
    }

    return 0;
}

static void lv_camera_release_buf(struct aic_camera_ctx_s *aic_ctx)
{
    lv_aic_vin_release_buf(&aic_ctx->vin_data);
}

static int lv_camera_video_layer_set(struct aic_camera_ctx_s *aic_ctx, int index)
{
    struct lv_aic_display_layer layer = {0};
    struct vin_video_buf *binfo = NULL;

    if (index >= aic_ctx->vin_data.num_buffers) {
        LV_LOG_ERROR("index %d is ove range %d", index, aic_ctx->vin_data.num_buffers);
        return -1;
    }
    binfo = &aic_ctx->vin_data.binfo[index];
    lv_mutex_lock(&aic_ctx->mutex);
    layer.x = aic_ctx->dst_rect.x;
    layer.y = aic_ctx->dst_rect.y;
    layer.width = aic_ctx->dst_rect.width;
    layer.height = aic_ctx->dst_rect.height;
    lv_mutex_unlock(&aic_ctx->mutex);

    layer.src_w = aic_ctx->w;
    layer.src_h = aic_ctx->h;
    layer.rotation = aic_ctx->rotation;
    layer.layer_id = AICFB_LAYER_TYPE_VIDEO;
    layer.format = aic_ctx->dst_fmt;
    layer.format = (aic_ctx->dst_fmt == V4L2_PIX_FMT_NV16) ? MPP_FMT_NV16 : MPP_FMT_NV12;
    layer.plane_num = VID_BUF_PLANE_NUM;

    for (int i = 0; i < VID_BUF_PLANE_NUM; i++) {
        layer.phy_addr[i] = binfo->planes[i].buf;
        layer.stride[i] =
            (aic_ctx->rotation == MPP_ROTATION_0 || aic_ctx->rotation == MPP_ROTATION_180)
                ? aic_ctx->w
                : aic_ctx->h;
    }

    return lv_aic_display_set(&aic_ctx->display, &layer);
}

static int lv_camera_video_layer_disable(struct aic_camera_ctx_s *aic_ctx)
{
    return lv_aic_display_disable(&aic_ctx->display, AICFB_LAYER_TYPE_VIDEO);
}

static void lv_camera_draw_video_layer_entry(void *ptr)
{
    lv_aic_camera_t *camera = (lv_aic_camera_t *)ptr;
    struct aic_camera_ctx_s *aic_ctx = camera->aic_ctx;
    int last_index = -1;
    int index, i, format;
    int ret;

    lv_thread_sync_wait(&aic_ctx->video_sync);

    if (aic_ctx->status == LV_AIC_CAMERA_STATUS_STOP)
        goto thread_exit;

    if (lv_camera_req_buf(aic_ctx) < 0) {
        LV_LOG_ERROR("request_buf failed");
        goto thread_exit;
    }

    for (i = 0; i < VID_BUF_NUM; i++) {
        if (lv_camera_q_buf(aic_ctx, i) < 0) {
            LV_LOG_ERROR("queue_buf %d failed", i);
            goto thread_exit;
        }
    }

    if (!aic_ctx->frame_cb) {
        LV_LOG_ERROR("set recorder frame_cb failed");
        goto thread_exit;
    }
    lv_mutex_lock(&aic_ctx->mutex);
    aic_ctx->status = LV_AIC_CAMERA_STATUS_RUNNING;
    aic_ctx->is_display = 1;
    lv_mutex_unlock(&aic_ctx->mutex);

    if (lv_camera_stream_on(aic_ctx) < 0) {
        LV_LOG_WARN("camera start error");
        goto thread_exit;
    }

    while (aic_ctx->status == LV_AIC_CAMERA_STATUS_RUNNING) {
        if (lv_camera_dq_buf(aic_ctx, &index) < 0) {
            usleep(5000);
            continue;
        }

        /* display current frame on video layer */
        if (aic_ctx->is_display)
            lv_camera_video_layer_set(aic_ctx, index);

        /* send PREVIOUS frame to recorder (1-frame delay).
         * recorder takes ownership; all QBUF handled by giveback */
        if (last_index >= 0) {
            if (!aic_ctx->frame_cb) {
                LV_LOG_ERROR("frame_cb not register.");
                lv_camera_q_buf(aic_ctx, last_index);
                last_index = index;
                usleep(5000);
                continue;
            }
            format = (aic_ctx->dst_fmt == V4L2_PIX_FMT_NV16) ? MPP_FMT_NV16 : MPP_FMT_NV12;
            ret = aic_ctx->frame_cb(aic_ctx->frame_cb_data, &aic_ctx->vin_data.binfo[last_index],
                                    last_index, aic_ctx->w, aic_ctx->h, format);
            if (ret != 0) {
                LV_LOG_WARN("send frame %d failed, ret:%d.", last_index, ret);
                lv_camera_q_buf(aic_ctx, last_index);
                last_index = index;
                usleep(5000);
                continue;
            }
        }

        last_index = index;
    }

thread_exit:
    lv_camera_release_buf(aic_ctx);
    lv_mutex_lock(&aic_ctx->mutex);
    aic_ctx->status = LV_AIC_CAMERA_STATUS_DELETE;
    lv_mutex_unlock(&aic_ctx->mutex);
    lv_thread_sync_signal(&aic_ctx->video_exit_sync);
}

/**********************
 *  CLASS CALLBACKS
 **********************/

static void lv_aic_camera_constructor(const lv_obj_class_t *class_p, lv_obj_t *obj)
{
    LV_UNUSED(class_p);
    lv_aic_camera_t *camera = (lv_aic_camera_t *)obj;
    camera->format = LV_AIC_CAMERA_FORMAT_NV16;
    camera->aic_ctx = NULL;
}

static void lv_aic_camera_destructor(const lv_obj_class_t *class_p, lv_obj_t *obj)
{
    LV_UNUSED(class_p);
    LV_TRACE_OBJ_CREATE("begin");

    lv_aic_camera_t *camera = (lv_aic_camera_t *)obj;
    struct aic_camera_ctx_s *aic_ctx = camera->aic_ctx;

    if (!aic_ctx) {
        LV_TRACE_OBJ_CREATE("finished (null ctx)");
        return;
    }

    /* 1. Notify thread to stop */
    lv_mutex_lock(&aic_ctx->mutex);
    aic_ctx->status = LV_AIC_CAMERA_STATUS_STOP;
    lv_mutex_unlock(&aic_ctx->mutex);

    /* 2. Stop streaming first to unblock dq_buf */
    lv_camera_stream_off(aic_ctx);

    /* 3. Wake thread in case it hasn't started */
    lv_thread_sync_signal(&aic_ctx->video_sync);

    /* 4. Wait for thread to exit */
    lv_thread_sync_wait(&aic_ctx->video_exit_sync);
    lv_thread_sync_delete(&aic_ctx->video_exit_sync);

    /* 5. Delete thread */
    lv_thread_delete(&aic_ctx->video_thread);

    lv_camera_video_layer_disable(aic_ctx);
    lv_aic_display_close(&aic_ctx->display);
    lv_aic_vin_close(&aic_ctx->vin_dev);

    lv_mutex_delete(&aic_ctx->mutex);
    lv_thread_sync_delete(&aic_ctx->video_sync);
    lv_free(aic_ctx);

    LV_TRACE_OBJ_CREATE("finished");
}

static void lv_aic_camera_event(const lv_obj_class_t *class_p, lv_event_t *e)
{
    LV_UNUSED(class_p);

    lv_event_code_t code = lv_event_get_code(e);
    lv_result_t res = lv_obj_event_base(MY_CLASS, e);
    if (res != LV_RESULT_OK)
        return;

    lv_obj_t *obj = lv_event_get_current_target(e);
    lv_aic_camera_t *camera = (lv_aic_camera_t *)obj;
    struct aic_camera_ctx_s *aic_ctx = camera->aic_ctx;

    if (!aic_ctx)
        return;

    if (code == LV_EVENT_GET_SELF_SIZE) {
        lv_point_t *p = lv_event_get_param(e);
        p->x = aic_ctx->w;
        p->y = aic_ctx->h;
    } else if (code == LV_EVENT_DRAW_MAIN) {
        lv_area_t fill_area;
        char fill_src[128];

        lv_obj_get_coords(obj, &fill_area);

        lv_mutex_lock(&aic_ctx->mutex);
        aic_ctx->dst_rect.x = fill_area.x1;
        aic_ctx->dst_rect.y = fill_area.y1;
        aic_ctx->dst_rect.width = fill_area.x2 - fill_area.x1 + 1;
        aic_ctx->dst_rect.height = fill_area.y2 - fill_area.y1 + 1;
        lv_mutex_unlock(&aic_ctx->mutex);

        uint8_t bg_opa = lv_obj_get_style_bg_opa(obj, LV_PART_MAIN);
        lv_color_t bg_color = lv_obj_get_style_bg_color(obj, LV_PART_MAIN);
        uint32_t color =
            (bg_opa << 24) | (bg_color.red << 16) | (bg_color.green << 8) | bg_color.blue;

        snprintf(fill_src, sizeof(fill_src), "L:/%dx%d_0_%08x.fake",
                 (int)(fill_area.x2 - fill_area.x1 + 1), (int)(fill_area.y2 - fill_area.y1 + 1),
                 color);

        lv_draw_image_dsc_t draw_dsc;
        lv_draw_image_dsc_init(&draw_dsc);
        lv_obj_init_draw_image_dsc(obj, LV_PART_MAIN, &draw_dsc);
        draw_dsc.src = fill_src;

        lv_layer_t *layer = lv_event_get_layer(e);
        lv_draw_image(layer, &draw_dsc, &fill_area);
    }
}
