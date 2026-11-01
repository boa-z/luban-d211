/*
 * Copyright (c) 2024-2025, ArtInChip Technology Co., Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Authors:  Ning Fang <ning.fang@artinchip.com>
 */

#include <unistd.h>
#include <stdlib.h>
#include <time.h>
#include <sys/time.h>
#include <stdio.h>
#include <sys/mman.h>
#include <signal.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include "lvgl.h"
#include "aic_ui.h"
#include "mpp_ge.h"
#include "canvas_image.h"
#include "dma_allocator.h"
#include "frame_allocator.h"

#define ALIGN_8B(x) (((x) + (7)) & ~7)

#if LVGL_VERSION_MAJOR == 9
static struct mpp_ge *g_ge = NULL;

void lv_mpp_image_free(mpp_decoder_data_t *image)
{
    if (image) {
        if (image->data[0])
            dmabuf_munmap((unsigned char*)image->data[0], image->size[0]);

        if (image->dec_buf.fd[0] > 0) {
            dmabuf_free(image->dec_buf.fd[0]);
        }

        free(image);
    }

    return;
}

mpp_decoder_data_t *lv_mpp_image_alloc(int width, int height, enum mpp_pixel_format fmt)
{
    mpp_decoder_data_t *image = NULL;
    int dma_fd = -1;
    int size = 0;

    image = (mpp_decoder_data_t *)malloc(sizeof(mpp_decoder_data_t));
    if (!image) {
        LV_LOG_ERROR("malloc failed");
         goto alloc_error;
    }

    memset(image, 0, sizeof(mpp_decoder_data_t));

    dma_fd = dmabuf_device_open();
    if (dma_fd < 0) {
        LV_LOG_ERROR("dmabuf_device_open failed");
        goto alloc_error;
    }

    if (fmt == MPP_FMT_ARGB_8888) {
        image->dec_buf.stride[0] = ALIGN_8B(width * 4);
    } else if (fmt == MPP_FMT_RGB_565) {
        image->dec_buf.stride[0] = ALIGN_8B(width * 2);
    } else {
        LV_LOG_ERROR("unsupport fmt:%d", fmt);
        goto alloc_error;
    }

    size = image->dec_buf.stride[0] * height;
    image->size[0] = size;
    image->dec_buf.buf_type = MPP_DMA_BUF_FD;
    image->dec_buf.size.width = width;
    image->dec_buf.size.height = height;
    image->dec_buf.format = fmt;
    image->dec_buf.fd[0] = dmabuf_alloc(dma_fd, size);

    if (image->dec_buf.fd[0] < 0) {
        LV_LOG_ERROR("dmabuf_alloc failed");
        goto alloc_error;
    } else {
        image->data[0] = dmabuf_mmap(image->dec_buf.fd[0], size);
        if (image->data[0] == MAP_FAILED) {
            image->data[0] = NULL;
            LV_LOG_ERROR("dmabuf_mmap failed");
            goto alloc_error;
        }
    }

    dmabuf_device_close(dma_fd);
    return image;

alloc_error:
    lv_mpp_image_free(image);

    if (dma_fd > 0)
        dmabuf_device_close(dma_fd);

    return NULL;
}

int lv_ge_fill(struct mpp_buf *buf, enum ge_fillrect_type type,
        unsigned int start_color,  unsigned int end_color, int blend)
{
    int ret;
    struct ge_fillrect fill = { 0 };

    if (!g_ge)
        g_ge = mpp_ge_open();

    /* fill info */
    fill.type = type;
    fill.start_color = start_color;
    fill.end_color = end_color;

    /* dst buf */
    memcpy(&fill.dst_buf, buf, sizeof(struct mpp_buf));

    /* ctrl */
    fill.ctrl.flags = 0;
    fill.ctrl.alpha_en = blend;

    ret =  mpp_ge_fillrect(g_ge, &fill);
    if (ret < 0) {
        LV_LOG_WARN("fillrect1 fail\n");
        return LV_RES_INV;
    }

    ret = mpp_ge_emit(g_ge);
    if (ret < 0) {
        LV_LOG_WARN("emit fail\n");
        return LV_RES_INV;
    }

    ret = mpp_ge_sync(g_ge);
    if (ret < 0) {
        LV_LOG_WARN("sync fail\n");
        return LV_RES_INV;
    }

    return LV_RES_OK;
}

#else

//TODO: support V8
void *lv_mpp_image_alloc(int width, int height, enum mpp_pixel_format fmt)
{
    return NULL;
}

#endif
