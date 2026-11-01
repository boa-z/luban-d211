/*
 * Copyright (c) 2026, ArtInChip Technology Co., Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Authors:  zequan liang <zequan.liang@artinchip.com>
 */

#ifndef ARTVG_DRV_H
#define ARTVG_DRV_H
#include "artvg.h"

typedef struct artvg_submit
{
    uint8_t *cmd;
    uint32_t len;
    uint32_t batch_id;
    unsigned int edge_buffer_size;
} artvg_submit_t;

typedef struct artvg_version {
    uint32_t version;
} artvg_version_t;

typedef enum artvg_command {
    ARTVG_COMMAND_SUBMIT,            /* data is artvg_submit_t (submit command buffer via ioctl) */
    ARTVG_COMMAND_WAIT_BATCH,        /* data is uint32_t *batch_id (wait for specific batch) */
    ARTVG_COMMAND_VERSION,           /* data is artvg_version_t */
    ARTVG_COMMAND_GET_BATCH,         /* data is uint32_t *batch_id */
    ARTVG_COMMAND_ALLOC_EDGE_BUF,    /* data is uint32_t *size */
    ARTVG_COMMAND_FREE_EDGE_BUF,     /* data is NULL */
} artvg_command_t;

typedef struct artvg_drv artvg_drv_t;

artvg_drv_t *artvg_drv_open(void);
int artvg_drv_command(artvg_drv_t *ctx, artvg_command_t cmd, void *data);
int artvg_drv_close(artvg_drv_t *ctx);

#endif
