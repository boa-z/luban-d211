/*
 * Copyright (c) 2023-2026, ArtInChip Technology Co., Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * LVGL AIC Display abstraction — wraps AICFB / DRM video layer operations.
 */

#ifndef _LV_AIC_DISPLAY_H_
#define _LV_AIC_DISPLAY_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define LV_AIC_DISPLAY_PLANE_MAX 3

/**********************
 *      TYPEDEFS
 **********************/

enum lv_aic_display_type {
    LV_AIC_DISPLAY_AICFB = 0, /* legacy framebuffer*/
    LV_AIC_DISPLAY_DRM,       /* DRM atomic modeset */
};

struct lv_aic_display_layer {
    int layer_id;      /* video layer index (0=video, 1=ui overlay, etc.) */
    int x, y;          /* destination position on screen */
    int width, height; /* destination size */
    int src_w, src_h;  /* source frame size */
    int rotation;      /* rotation (MPP_ROTATION_0/90/180/270) */
    int format;        /* pixel format (MPP_FMT_NV16 etc.) */
    int plane_num;     /* number of planes */
    uint32_t phy_addr[LV_AIC_DISPLAY_PLANE_MAX]; /* DVP 1.0: physical addresses */
    int fd[LV_AIC_DISPLAY_PLANE_MAX];            /* DVP 2.0: dma-buf fds */
    uint32_t stride[LV_AIC_DISPLAY_PLANE_MAX];   /* line stride per plane */
};

struct lv_aic_display {
    int type;
    int fd; /* fb / drm fd */
    int screen_w;
    int screen_h;
};

/**********************
 * GLOBAL PROTOTYPES
 **********************/

int lv_aic_display_open(struct lv_aic_display *disp, int type);
void lv_aic_display_close(struct lv_aic_display *disp);
int lv_aic_display_get_size(struct lv_aic_display *disp, int *w, int *h);
int lv_aic_display_set(struct lv_aic_display *disp, struct lv_aic_display_layer *layer);
int lv_aic_display_disable(struct lv_aic_display *disp, int layer_id);

#ifdef __cplusplus
}
#endif

#endif /* _LV_AIC_DISPLAY_H_ */
