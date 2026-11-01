/*
 * Copyright (c) 2026, ArtInChip Technology Co., Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Authors:  zequan liang <zequan.liang@artinchip.com>
 *
 * ArtVG Clear Stress Test
 * Multi-instance concurrent stress test for artvg_clear() operation
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "artvg_stress_framework.h"

/***********************
 *  CLEAR-SPECIFIC CONFIGS
 **********************/
static const stress_crop_config_t g_crop_configs[] = {
    {0, 0, 0, 123, 456},
    {1, 0, 0, 640, 480},
    {1, 100, 100, 320, 240},
    {1, 200, 150, 800, 400},
    {1, 50, 50, 920, 530},
    {1, 0, 0, 1024, 600},
    {1, 150, 200, 400, 300},
    {1, 300, 100, 600, 450},
    {1, 80, 80, 512, 384},
    {1, 250, 180, 700, 350},
    {1, 400, 50, 500, 500},
    {1, 180, 250, 650, 320}
};

static const uint32_t g_colors[] = {
    0xFFFF0000, 0xFF00FF00, 0xFF0000FF, 0xFFFFFFFF,
    0xFF000000, 0xFFFFFF00, 0xFFFF00FF, 0xFF00FFFF,
    0xFF808080, 0xFF800000, 0xFF008000, 0xFF000080
};

#define NUM_CROP_CONFIGS (sizeof(g_crop_configs) / sizeof(g_crop_configs[0]))
#define NUM_COLORS (sizeof(g_colors) / sizeof(g_colors[0]))

/***********************
 *  CLEAR OPERATION
 **********************/
static void print_buffer_params(const char *prefix, struct mpp_buf *buf)
{
    if (!buf) {
        printf("%s: buffer is NULL\n", prefix);
        return;
    }

    printf("========== %s Buffer Parameters ==========\n", prefix);
    printf("  buf_type: %d\n", buf->buf_type);
    printf("  fd[0]: %d, fd[1]: %d, fd[2]: %d\n",
           buf->fd[0], buf->fd[1], buf->fd[2]);
    printf("  phy_addr[0]: 0x%x, phy_addr[1]: 0x%x, phy_addr[2]: 0x%x\n",
           buf->phy_addr[0], buf->phy_addr[1], buf->phy_addr[2]);
    printf("  stride[0]: %u, stride[1]: %u, stride[2]: %u\n",
           buf->stride[0], buf->stride[1], buf->stride[2]);
    printf("  size: %dx%d\n", buf->size.width, buf->size.height);
    printf("  format: %d (0x%x)\n", buf->format, buf->format);
    printf("  crop_en: %d\n", buf->crop_en);
    printf("  crop: x=%d, y=%d, width=%d, height=%d\n",
           buf->crop.x, buf->crop.y, buf->crop.width, buf->crop.height);
    printf("  flags: 0x%x\n", buf->flags);
    printf("==============================================\n");
}

static int perform_clear_operation(struct artvg *vg, struct mpp_buf *buf, uint32_t color)
{
    if (!vg || !buf) {
        printf("ERROR: vg=%p, buf=%p\n", vg, buf);
        return -1;
    }

    artvg_error_t ret;

    ret = artvg_clear(vg, buf, color);
    if (ret != ARTVG_SUCCESS) {
        printf("[ERROR] artvg_clear failed: %d\n", ret);
        print_buffer_params("Clear Failed", buf);
        printf("  color: 0x%x\n", color);
        return -1;
    }

    ret = artvg_flush(vg);
    if (ret != ARTVG_SUCCESS) {
        printf("[ERROR] artvg_flush failed: %d\n", ret);
        print_buffer_params("Flush Failed", buf);
        return -1;
    }

    ret = artvg_wait_finish(vg);
    if (ret != ARTVG_SUCCESS) {
        printf("[ERROR] artvg_wait_finish failed: %d\n", ret);
        print_buffer_params("Wait Finish Failed", buf);
        return -1;
    }

    return 0;
}

/***********************
 *  TEST OPS IMPLEMENTATION
 **********************/

/* Simplified draw callback - manages parameters internally */
static int clear_draw_callback(void *thread_ctx, int buffer_idx)
{
    stress_thread_context_t *tctx = (stress_thread_context_t *)thread_ctx;
    stress_render_context_t *ctx = tctx->ctx;

    /* Internal parameter management */
    int color_idx = tctx->current_param_index % NUM_COLORS;
    int crop_idx = tctx->current_param_index % NUM_CROP_CONFIGS;
    int param_index = tctx->current_param_index;

    stress_crop_config_t crop_config = {0};
    uint32_t color = 0;

    /* Initialize private_buf to zero to avoid undefined values */
    memset(&tctx->private_buf, 0, sizeof(struct mpp_buf));

    /* Copy shared buffer to private buffer (thread-safe, no lock needed) */
    memcpy(&tctx->private_buf, &ctx->draw_buffers[buffer_idx], sizeof(struct mpp_buf));

    /* Copy crop config parameters (also thread-safe with memcpy) */
    memcpy(&crop_config, &g_crop_configs[crop_idx], sizeof(stress_crop_config_t));
    color = g_colors[color_idx];

    /* Modify private buffer parameters (no other thread can access this) */
    tctx->private_buf.crop_en = crop_config.crop_en;
    tctx->private_buf.crop.x = crop_config.crop_x;
    tctx->private_buf.crop.y = crop_config.crop_y;
    tctx->private_buf.crop.width = crop_config.crop_width;
    tctx->private_buf.crop.height = crop_config.crop_height;

    /* Execute clear operation with private buffer (completely lock-free) */
    if (perform_clear_operation(tctx->vg, &tctx->private_buf, color) != 0) {
        printf("[Thread %d] Clear operation FAILED at param_index=%d, buffer_idx=%d\n",
               tctx->thread_id, param_index, buffer_idx);
        printf("  Config: crop_en=%d, crop=(%d,%d,%d,%d), color=0x%x\n",
               crop_config.crop_en, crop_config.crop_x, crop_config.crop_y,
               crop_config.crop_width, crop_config.crop_height, color);
        stress_record_failure(tctx, STRESS_FAIL_REASON_OP_FAILED);
        return -1;
    }

    return 0;
}

static const char *clear_fail_reason_to_string(int reason)
{
    switch (reason) {
    case STRESS_FAIL_REASON_NO_BUFFER:
        return "Failed to get drawable buffer";
    case STRESS_FAIL_REASON_OP_FAILED:
        return "Clear operation failed";
    default:
        return "Unknown failure";
    }
}

/***********************
 *  TEST OPS TABLE
 **********************/
static const stress_test_ops_t g_clear_ops = {
    .test_name = "ArtVG Clear Concurrent Stress Test",

    .init_test_resources = NULL,      /* No extra resources needed */
    .cleanup_test_resources = NULL,
    .draw_callback = clear_draw_callback,
    .collect_and_print_results = NULL, /* Use default result printing */
    .fail_reason_to_string = clear_fail_reason_to_string,
};

/***********************
 *  MAIN ENTRY POINT
 **********************/
int main(int argc, char **argv)
{
    return stress_run_test(argc, argv, &g_clear_ops);
}
