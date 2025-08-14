/*
 * Copyright (c) 2024-2025, ArtInChip Technology Co., Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Authors:  Ning Fang <ning.fang@artinchip.com>
 */

#ifndef CANVAS_IMAGE_H
#define CANVAS_IMAGE_H

#ifdef __cplusplus
extern "C" {
#endif

#if LVGL_VERSION_MAJOR == 9

#include "lvgl.h"
#include "aic_ui.h"
#include "mpp_ge.h"
#include "lv_mpp_dec.h"

mpp_decoder_data_t *lv_mpp_image_alloc(int width, int height, enum mpp_pixel_format fmt);

void lv_mpp_image_flush_cache(mpp_decoder_data_t *image);

void lv_mpp_image_free(mpp_decoder_data_t *image);

int lv_ge_fill(struct mpp_buf *buf, enum ge_fillrect_type type,
               unsigned int start_color,  unsigned int end_color, int blend);

#endif

#ifdef __cplusplus
} /*extern "C"*/
#endif

#endif //CANVAS_IMAGE_H
