/*
 * Copyright (c) 2026, ArtInChip Technology Co., Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Authors:  zequan liang <zequan.liang@artinchip.com>
 */

#ifndef ARTVG_OS_H
#define ARTVG_OS_H

#include <stdint.h>
#include <video/mpp_types.h>

typedef struct artvg_os artvg_os_t;

void * artvg_os_malloc(uint32_t size);

void artvg_os_free(void * memory);

void * artvg_os_realloc(void *memory, uint32_t size);

void artvg_os_sleep(uint32_t msec);

artvg_os_t *artvg_os_ctx_create(void);

int artvg_os_ctx_destroy(artvg_os_t *ctx);

uint64_t artvg_os_thread_id(void);

int artvg_os_mutex_lock(artvg_os_t *ctx);

int artvg_os_mutex_unlock(artvg_os_t *ctx);

int32_t artvg_os_dma_dev_open(artvg_os_t *ctx);

void artvg_os_dma_dev_close(artvg_os_t *ctx);

int32_t artvg_os_dma_buf_alloc(artvg_os_t *ctx, uint32_t size, struct dma_buf_info *dmabuf_info);

int32_t artvg_os_dma_buf_release(artvg_os_t *ctx, struct dma_buf_info *dmabuf_info);

int32_t artvg_os_dma_buf_map(artvg_os_t *ctx, struct dma_buf_info *dmabuf_info, uint32_t size, void **addr);

int32_t artvg_os_dma_buf_unmap(artvg_os_t *ctx, uint32_t size, void *addr);

int32_t artvg_os_dma_buf_sync(artvg_os_t *ctx, struct dma_buf_info *dmabuf_info);

int32_t artvg_os_dma_buf_sync_range(artvg_os_t *ctx, struct dma_buf_info *dmabuf_info,
                                    void *start, uint32_t size);

int32_t artvg_os_dma_buf_get_phy_addr(artvg_os_t *ctx, int fd, uint32_t *phy_addr);

#endif
