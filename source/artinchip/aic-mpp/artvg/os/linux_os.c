/*
 * Copyright (c) 2026, ArtInChip Technology Co., Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Authors:  zequan liang <zequan.liang@artinchip.com>
 */

#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <pthread.h>
#include <sys/ioctl.h>
#include <linux/dma-buf.h>
#include <linux/dma-heap.h>
#include <sys/mman.h>
#include "artvg_os.h"
#include "dma_allocator.h"

struct artvg_os {
    int dmabuf_fd;
    pthread_mutex_t mutex;
};

void * artvg_os_malloc(uint32_t size)
{
    return malloc(size);
}

void artvg_os_free(void * memory)
{
    free(memory);
}

void * artvg_os_realloc(void *memory, uint32_t size)
{
    return realloc(memory, size);
}

void artvg_os_sleep(uint32_t msec)
{
    usleep(msec * 1000);
}

artvg_os_t *artvg_os_ctx_create(void)
{
    artvg_os_t *os = malloc(sizeof(struct artvg_os));

    if (!os)
        return NULL;

    memset(os, 0, sizeof(struct artvg_os));

    os->dmabuf_fd = -1;

    if (pthread_mutex_init(&os->mutex, NULL) != 0) {
        free(os);
        return NULL;
    }

    return os;
}

int artvg_os_ctx_destroy(artvg_os_t *ctx)
{
    if (ctx) {
        artvg_os_dma_dev_close(ctx);
        pthread_mutex_destroy(&ctx->mutex);
        free(ctx);
    }
    return 0;
}

int32_t artvg_os_dma_dev_open(artvg_os_t *ctx)
{
    if (!ctx) {
        return -1;
    }
    ctx->dmabuf_fd = dmabuf_device_open();
    if (ctx->dmabuf_fd < 0) {
        return -1;
    }

    return 0;
}

void artvg_os_dma_dev_close(artvg_os_t *ctx)
{
    if (!ctx) {
        return;
    }
    dmabuf_device_close(ctx->dmabuf_fd);
    ctx->dmabuf_fd = -1;
}

int32_t artvg_os_dma_buf_alloc(artvg_os_t *ctx, uint32_t size, struct dma_buf_info *dmabuf_info)
{
    if (!ctx || ctx->dmabuf_fd < 0 || !dmabuf_info) {
        return -1;
    }

    dmabuf_info->fd = dmabuf_alloc(ctx->dmabuf_fd, size);
    if (dmabuf_info->fd < 0) {
        return -1;
    }
    return 0;
}

int32_t artvg_os_dma_buf_release(artvg_os_t *ctx, struct dma_buf_info *dmabuf_info)
{
    if (!ctx || !dmabuf_info) {
        return -1;
    }
    dmabuf_free(dmabuf_info->fd);
    dmabuf_info->fd = -1;
    return 0;
}

int32_t artvg_os_dma_buf_map(artvg_os_t *ctx, struct dma_buf_info *dmabuf_info, uint32_t size, void **addr)
{
    if (!ctx || !dmabuf_info || !addr) {
        return -1;
    }
    *addr = dmabuf_mmap(dmabuf_info->fd, size);
    if (*addr == NULL) {
        return -1;
    }
    return 0;
}

int32_t artvg_os_dma_buf_unmap(artvg_os_t *ctx, uint32_t size, void *addr)
{
    if (!ctx || !addr) {
        return -1;
    }
    dmabuf_munmap(addr, size);
    return 0;
}

int32_t artvg_os_dma_buf_sync(artvg_os_t *ctx, struct dma_buf_info *dmabuf_info)
{
    if (!ctx || !dmabuf_info || dmabuf_info->fd < 0) {
        return -1;
    }

    return dmabuf_sync(dmabuf_info->fd, CACHE_FLUSH);
}

int32_t artvg_os_dma_buf_sync_range(artvg_os_t *ctx, struct dma_buf_info *dmabuf_info,
                                    void *start, uint32_t size)
{
    if (!ctx || !dmabuf_info || dmabuf_info->fd < 0 || !start || size == 0)
        return -1;

    return dmabuf_sync_range(dmabuf_info->fd, start, size, CACHE_FLUSH);
}

int32_t artvg_os_dma_buf_get_phy_addr(artvg_os_t *ctx, int fd, uint32_t *phy_addr)
{
    if (fd < 0 || !phy_addr) {
        return -1;
    }

    if (ioctl(fd, DMA_BUF_IOCTL_GET_PHY_ADDR, phy_addr) < 0) {
        return -1;
    }

    return 0;
}

uint64_t artvg_os_thread_id(void)
{
    return (uint64_t)pthread_self();
}

int artvg_os_mutex_lock(artvg_os_t *ctx)
{
    if (!ctx)
        return -1;
    return pthread_mutex_lock(&ctx->mutex);
}

int artvg_os_mutex_unlock(artvg_os_t *ctx)
{
    if (!ctx)
        return -1;
    return pthread_mutex_unlock(&ctx->mutex);
}
