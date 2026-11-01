/*
 * Copyright (c) 2023-2026, ArtInChip Technology Co., Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Authors:  Zequan Liang <zequan.liang@artinchip.com>
 *
 * LVGL v9 AIC Camera widget
 */

#ifndef LV_AIC_CAMERA_H
#define LV_AIC_CAMERA_H

#ifdef __cplusplus
extern "C" {
#endif

/*********************
 *      INCLUDES
 *********************/
#include "lvgl.h"
#if LVGL_VERSION_MAJOR == 9
#include <stdint.h>
#include "lv_aic_vin.h"

/**********************
 *      TYPEDEFS
 **********************/

extern const lv_obj_class_t lv_aic_camera_class;

typedef struct {
    lv_obj_t obj;
    uint32_t format;
    uint32_t width;
    uint32_t height;
    struct aic_camera_ctx_s *aic_ctx;
} lv_aic_camera_t;

struct lv_aic_display;

typedef int (*lv_aic_camera_frame_cb)(void *user_data, struct vin_video_buf *buf, int index, int w,
                                      int h, int format);

typedef enum {
    LV_AIC_CAMERA_FORMAT_NV16,
    LV_AIC_CAMERA_FORMAT_NV12,
    LV_AIC_CAMERA_FORMAT_YUV400,
    _LV_AIC_CAMERA_FORMAT_LAST
} lv_aic_camera_format;

/**********************
 * GLOBAL PROTOTYPES
 **********************/

lv_obj_t *lv_aic_camera_create(lv_obj_t *parent);
lv_res_t lv_aic_camera_set_format(lv_obj_t *obj, lv_aic_camera_format format);
lv_res_t lv_aic_camera_set_sensor_fmt(lv_obj_t *obj, int width, int height);
lv_res_t lv_aic_camera_open(lv_obj_t *obj);
lv_res_t lv_aic_camera_start(lv_obj_t *obj);
lv_res_t lv_aic_camera_stop(lv_obj_t *obj);
lv_res_t lv_aic_camera_display_start(lv_obj_t *obj);
lv_res_t lv_aic_camera_display_stop(lv_obj_t *obj);

void lv_aic_camera_set_frame_callback(lv_obj_t *obj, lv_aic_camera_frame_cb cb, void *user_data);
struct lv_aic_display *lv_aic_camera_get_display(lv_obj_t *obj);
int lv_aic_camera_q_buf(lv_obj_t *obj, int index);

#endif
#ifdef __cplusplus
}
#endif

#endif /* LV_AIC_CAMERA_H */
