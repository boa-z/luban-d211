/*
 * Copyright (C) 2024-2025 ArtInChip Technology Co., Ltd.
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Authors:  Ning Fang <ning.fang@artinchip.com>
 */

#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <linux/dma-buf.h>
#include <linux/dma-heap.h>
#include "lv_draw_ge2d.h"
#include "lv_draw_ge2d_utils.h"

#if LV_USE_DRAW_GE2D

int lv_dmabuf_sync_range(int buf_fd, unsigned int start, unsigned long width, unsigned long height, unsigned long stride)
{
    int ret;
    struct dma_buf_range sync = { 0 };

    sync.start = (unsigned long)start;
    // when set DMA_BUF_SYNC_RANGE_CROP flag, sync.size means image height
    sync.size = height;
    sync.width = width;
    sync.stride = stride;

    sync.flags = DMA_BUF_SYNC_WB_INV_RANGE | DMA_BUF_SYNC_RANGE_CROP |  DMA_BUF_SYNC_PHY_ADDR;

    ret = ioctl(buf_fd, DMA_BUF_IOCTL_SYNC_RANGE, &sync);
    if (ret)
        LV_LOG_ERROR("dmabuf_sync_range ret: %d", ret);

    return 0;
}

static void ge2d_buf_invalidate_cache_cb(const lv_draw_buf_t *draw_buf, const lv_area_t *area)
{
    const lv_image_header_t *header = &draw_buf->header;
    uint32_t stride = header->stride;
    lv_color_format_t cf = header->cf;
    unsigned int phy_addr;

    if (!disp_buf_check(draw_buf->data))
        return;

    phy_addr = disp_buf_draw_addr(draw_buf->data);
    if (phy_addr > 0) {
        uint32_t bpp = lv_color_format_get_size(cf);
        int32_t width = lv_area_get_width(area);
        int32_t height = lv_area_get_height(area);
        unsigned int address = phy_addr;
        int32_t flush_line_size = (int32_t)width * (int32_t)bpp;

        address = address + (area->x1 * (int32_t)bpp) + (stride * (uint32_t)area->y1);
        lv_dmabuf_sync_range(disp_buf_fd(draw_buf->data), address, flush_line_size, height, stride);
    }
}

void lv_draw_buf_ge2d_init_handlers(void)
{
    lv_draw_buf_handlers_t * handlers = lv_draw_buf_get_handlers();

    handlers->invalidate_cache_cb = ge2d_buf_invalidate_cache_cb;
}

#endif /*LV_USE_DRAW_GE2D*/
