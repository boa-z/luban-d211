/*
 * Copyright (c) 2023-2026, ArtInChip Technology Co., Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * LVGL AIC Display implementation — AICFB and DRM backends.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <linux/fb.h>
#include <video/artinchip_fb.h>

#include "lv_aic_display.h"

static int aicfb_open(struct lv_aic_display *disp)
{
    struct fb_var_screeninfo var = {0};

    disp->fd = open("/dev/fb0", O_RDWR);
    if (disp->fd < 0) {
        fprintf(stderr, "AICFB: open /dev/fb0 failed: %s\n", strerror(errno));
        return -1;
    }

    if (ioctl(disp->fd, FBIOGET_VSCREENINFO, &var) < 0) {
        fprintf(stderr, "AICFB: FBIOGET_VSCREENINFO failed: %s\n", strerror(errno));
        close(disp->fd);
        disp->fd = -1;
        return -1;
    }

    disp->screen_w = var.xres;
    disp->screen_h = var.yres;
    return 0;
}

static void aicfb_close(struct lv_aic_display *disp)
{
    if (disp->fd >= 0) {
        close(disp->fd);
        disp->fd = -1;
    }
}

static int aicfb_set(struct lv_aic_display *disp, struct lv_aic_display_layer *layer)
{
    struct aicfb_layer_data l = {0};

    l.layer_id = layer->layer_id;
    l.enable = 1;
    l.scale_size.width = layer->width;
    l.scale_size.height = layer->height;
    l.pos.x = layer->x;
    l.pos.y = layer->y;

    if (layer->rotation == MPP_ROTATION_0 || layer->rotation == MPP_ROTATION_180) {
        l.buf.size.width = layer->src_w;
        l.buf.size.height = layer->src_h;
    } else {
        l.buf.size.width = layer->src_h;
        l.buf.size.height = layer->src_w;
    }

    l.buf.format = layer->format;
    l.buf.buf_type = MPP_PHY_ADDR;

    for (int i = 0; i < layer->plane_num; i++) {
        l.buf.stride[i] = layer->stride[i];
        l.buf.phy_addr[i] = layer->phy_addr[i];
    }

    if (ioctl(disp->fd, AICFB_UPDATE_LAYER_CONFIG, &l) < 0) {
        perror("AICFB_UPDATE_LAYER_CONFIG");
        return -1;
    }
    ioctl(disp->fd, AICFB_WAIT_FOR_VSYNC, NULL);
    return 0;
}

static int aicfb_disable(struct lv_aic_display *disp, int layer_id)
{
    struct aicfb_layer_data l = {0};

    l.layer_id = layer_id;
    l.enable = 0;
    if (ioctl(disp->fd, AICFB_UPDATE_LAYER_CONFIG, &l) < 0) {
        perror("AICFB_UPDATE_LAYER_CONFIG (disable)");
        return -1;
    }
    return 0;
}

int lv_aic_display_open(struct lv_aic_display *disp, int type)
{
    if (!disp)
        return -1;

    memset(disp, 0, sizeof(*disp));
    disp->type = type;
    disp->fd = -1;

    switch (type) {
    case LV_AIC_DISPLAY_AICFB:
        return aicfb_open(disp);
    case LV_AIC_DISPLAY_DRM:
        fprintf(stderr, "DRM display not implemented yet\n");
        return -1;
    default:
        fprintf(stderr, "Unknown display type %d\n", type);
        return -1;
    }
}

void lv_aic_display_close(struct lv_aic_display *disp)
{
    if (!disp)
        return;

    switch (disp->type) {
    case LV_AIC_DISPLAY_AICFB:
        aicfb_close(disp);
        break;
    case LV_AIC_DISPLAY_DRM:
        break;
    default:
        break;
    }
}

int lv_aic_display_get_size(struct lv_aic_display *disp, int *w, int *h)
{
    if (!disp || !w || !h)
        return -1;

    *w = disp->screen_w;
    *h = disp->screen_h;
    return 0;
}

int lv_aic_display_set(struct lv_aic_display *disp, struct lv_aic_display_layer *layer)
{
    if (!disp || !layer)
        return -1;

    switch (disp->type) {
    case LV_AIC_DISPLAY_AICFB:
        return aicfb_set(disp, layer);
    case LV_AIC_DISPLAY_DRM:
        return -1;
    default:
        return -1;
    }
}

int lv_aic_display_disable(struct lv_aic_display *disp, int layer_id)
{
    if (!disp)
        return -1;

    switch (disp->type) {
    case LV_AIC_DISPLAY_AICFB:
        return aicfb_disable(disp, layer_id);
    case LV_AIC_DISPLAY_DRM:
        return -1;
    default:
        return -1;
    }
}
