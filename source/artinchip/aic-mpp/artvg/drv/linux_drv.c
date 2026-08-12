/*
 * Copyright (c) 2026, ArtInChip Technology Co., Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Authors:  zequan liang <zequan.liang@artinchip.com>
 */

#include <stdio.h>
#include <stdint.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <video/artinchip_artvg.h>
#include "artvg_drv.h"
#include "artvg_os.h"

struct artvg_drv
{
    int fd;
};

static int do_submit(artvg_drv_t *vg, artvg_submit_t *submit)
{
    struct artvg_submit_info submit_info;

    if (!vg || !submit || vg->fd < 0) {
        return -EINVAL;
    }

    submit_info.cmd_buf = (uint64_t)(uintptr_t)submit->cmd;
    submit_info.cmd_len = submit->len;
    submit_info.edge_buffer_size = submit->edge_buffer_size;

    if (ioctl(vg->fd, IOC_VG_SUBMIT, &submit_info) < 0) {
        return -errno;
    }

    submit->batch_id = submit_info.batch_id;

    return 0;
}

static int do_version(artvg_drv_t *vg, artvg_version_t *version)
{
    if (!vg || !version || vg->fd < 0) {
        return -EINVAL;
    }

    if (ioctl(vg->fd, IOC_VG_VERSION, version) < 0) {
        return -errno;
    }

    return 0;
}

static int do_wait_batch(artvg_drv_t *vg, uint32_t *batch_id)
{
    if (!vg || !batch_id || vg->fd < 0) {
        return -EINVAL;
    }

    int ret = ioctl(vg->fd, IOC_VG_SYNC_BATCH, batch_id);
    if (ret < 0) {
        return -errno;
    }

    return 0;
}

static int do_alloc_edge_buf(artvg_drv_t *vg, uint32_t *size)
{
    if (!vg || !size || vg->fd < 0) {
        return -EINVAL;
    }

    if (ioctl(vg->fd, IOC_VG_ALLOC_EDGE_BUF, size) < 0) {
        return -errno;
    }

    return 0;
}

static int do_free_edge_buf(artvg_drv_t *vg)
{
    if (!vg || vg->fd < 0) {
        return -EINVAL;
    }

    if (ioctl(vg->fd, IOC_VG_FREE_EDGE_BUF) < 0) {
        return -errno;
    }

    return 0;
}

static int do_get_batch(artvg_drv_t *vg, uint32_t *data)
{
    if (!vg || !data || vg->fd < 0) {
        return -EINVAL;
    }

    int ret = ioctl(vg->fd, IOC_VG_GET_BATCH, data);
    if (ret < 0) {
        return -errno;
    }

    return 0;
}

artvg_drv_t *artvg_drv_open(void)
{
    artvg_drv_t *vg;

    vg = artvg_os_malloc(sizeof(artvg_drv_t));
    if (!vg) {
        return NULL;
    }

    vg->fd = open("/dev/artvg", O_RDWR);
    if (vg->fd < 0) {
        artvg_os_free(vg);
        return NULL;
    }

    return vg;
}

int artvg_drv_close(artvg_drv_t *ctx)
{
    if (!ctx) {
        return -EINVAL;
    }

    if (ctx->fd >= 0) {
        close(ctx->fd);
    }
    artvg_os_free(ctx);

    return 0;
}

int artvg_drv_command(artvg_drv_t *ctx, const artvg_command_t cmd, void *data)
{
    if (!ctx) {
        return -EINVAL;
    }

    switch (cmd)
    {
    case ARTVG_COMMAND_SUBMIT:
        return do_submit(ctx, (artvg_submit_t *)data);
    case ARTVG_COMMAND_VERSION:
        return do_version(ctx, (artvg_version_t *)data);
    case ARTVG_COMMAND_WAIT_BATCH:
        return do_wait_batch(ctx, (uint32_t *)data);
    case ARTVG_COMMAND_GET_BATCH:
        return do_get_batch(ctx, (uint32_t *)data);
    case ARTVG_COMMAND_ALLOC_EDGE_BUF:
        return do_alloc_edge_buf(ctx, (uint32_t *)data);
    case ARTVG_COMMAND_FREE_EDGE_BUF:
        return do_free_edge_buf(ctx);
    default:
        return -EINVAL;
    }
    return 0;
}
