/*
 * Copyright (c) 2026, ArtInChip Technology Co., Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Authors:  zequan liang <zequan.liang@artinchip.com>
 *
 * ArtVG Gradient Stress Test
 * Multi-instance concurrent stress test for artvg_fill_gradient() operation
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include "artvg_stress_framework.h"

/***********************
 *  GRADIENT-SPECIFIC CONFIGURATIONS
 **********************/

typedef struct {
    int32_t start_x;
    int32_t start_y;
    int32_t end_x;
    int32_t end_y;
    uint32_t colors[VLC_MAX_GRADIENT_STOPS];
    uint8_t stops[VLC_MAX_GRADIENT_STOPS];
    uint32_t count;
    artvg_gradient_precision_t precision;
    artvg_gradient_spread_t spread;
    uint8_t alpha_en;
    artvg_alpha_blend_t alpha_rules;
    uint8_t src_global_alpha;
    uint8_t dst_global_alpha;
} gradient_config_t;

typedef struct {
    int crop_en;
    int crop_x;
    int crop_y;
    int crop_width;
    int crop_height;
} crop_config_t;

static const crop_config_t g_crop_configs[] = {
    {0, 0, 0, 123, 456},
    {1, 0, 0, 640, 480},
    {1, 100, 100, 320, 240},
    {1, 200, 150, 800, 400},
    {1, 50, 50, 920, 530}
};

static const gradient_config_t g_gradient_configs[] = {
    {0, 0, 1024, 600,
     {0xFFFF0000, 0xFF0000FF}, {0, 255}, 2,
     ARTVG_GRADIENT_PRECISION_256, ARTVG_GRADIENT_SPREAD_PAD,
     0, ARTVG_BLEND_SRC_OVER, 255, 255},
    {100, 100, 924, 500,
     {0xFF00FF00, 0xFFFFFF00}, {0, 255}, 2,
     ARTVG_GRADIENT_PRECISION_64, ARTVG_GRADIENT_SPREAD_REFLECT,
     0, ARTVG_BLEND_SRC_OVER, 255, 255},
    {0, 0, 800, 600,
     {0xFFFF0000, 0xFF00FF00, 0xFF0000FF}, {0, 128, 255}, 3,
     ARTVG_GRADIENT_PRECISION_16, ARTVG_GRADIENT_SPREAD_REPEAT,
     0, ARTVG_BLEND_SRC_OVER, 255, 255},
    {0, 0, 1024, 600,
     {0xFFFFFFFF, 0xFF000000}, {0, 255}, 2,
     ARTVG_GRADIENT_PRECISION_256, ARTVG_GRADIENT_SPREAD_PAD,
     1, ARTVG_BLEND_SRC_OVER, 200, 255},
    {0, 600, 1024, 0,
     {0xFFFF00FF, 0xFF00FFFF}, {0, 255}, 2,
     ARTVG_GRADIENT_PRECISION_256, ARTVG_GRADIENT_SPREAD_PAD,
     0, ARTVG_BLEND_ADD, 255, 255},
    {0, 0, 1024, 600,
     {0xFFFF0000, 0xFFFFFF00, 0xFF00FF00, 0xFF0000FF},
     {0, 84, 168, 255}, 4,
     ARTVG_GRADIENT_PRECISION_64, ARTVG_GRADIENT_SPREAD_PAD,
     0, ARTVG_BLEND_SRC_OVER, 255, 255},
    {200, 200, 600, 400,
     {0xFF0000FF, 0xFFFF0000}, {0, 255}, 2,
     ARTVG_GRADIENT_PRECISION_256, ARTVG_GRADIENT_SPREAD_NONE,
     0, ARTVG_BLEND_SRC_OVER, 255, 255},
    {0, 0, 1024, 600,
     {0xFFFFFF00, 0xFF00FF00}, {0, 255}, 2,
     ARTVG_GRADIENT_PRECISION_256, ARTVG_GRADIENT_SPREAD_PAD,
     0, ARTVG_BLEND_XOR, 255, 255},
    /* Test ARTVG_GRADIENT_PRECISION_NONE - solid color fill */
    {0, 0, 1024, 600,
     {0xFF00FF00, 0xFFFF0000}, {0, 255}, 1,
     ARTVG_GRADIENT_PRECISION_NONE, ARTVG_GRADIENT_SPREAD_NONE,
     0, ARTVG_BLEND_SRC_OVER, 255, 255}
};

#define NUM_CROP_CONFIGS (sizeof(g_crop_configs) / sizeof(g_crop_configs[0]))
#define NUM_GRADIENT_CONFIGS (sizeof(g_gradient_configs) / sizeof(g_gradient_configs[0]))

/***********************
 *  GRADIENT OPERATION
 **********************/
static int perform_gradient_operation(struct artvg *vg, struct mpp_buf *dst,
                                      const gradient_config_t *grad_cfg)
{
    artvg_gradient_t gradient;
    artvg_ctrl_t ctrl;

    memset(&gradient, 0, sizeof(artvg_gradient_t));
    memset(&ctrl, 0, sizeof(artvg_ctrl_t));

    gradient.start.x = grad_cfg->start_x;
    gradient.start.y = grad_cfg->start_y;
    gradient.end.x = grad_cfg->end_x;
    gradient.end.y = grad_cfg->end_y;
    gradient.count = grad_cfg->count;
    gradient.precision = grad_cfg->precision;
    gradient.spread = grad_cfg->spread;

    for (uint32_t i = 0; i < grad_cfg->count; i++) {
        gradient.colors[i] = grad_cfg->colors[i];
        gradient.stops[i] = grad_cfg->stops[i];
    }

    memset(&ctrl, 0, sizeof(artvg_ctrl_t));
    ctrl.alpha_en = grad_cfg->alpha_en;
    ctrl.alpha_rules = grad_cfg->alpha_rules;
    ctrl.src_global_alpha = grad_cfg->src_global_alpha;
    ctrl.dst_global_alpha = grad_cfg->dst_global_alpha;

    if (artvg_fill_gradient(vg, dst, &ctrl, &gradient) != ARTVG_SUCCESS ||
        artvg_flush(vg) != ARTVG_SUCCESS ||
        artvg_wait_finish(vg) != ARTVG_SUCCESS) {
        return -1;
    }
    return 0;
}

/***********************
 *  TEST OPS IMPLEMENTATION
 **********************/

/* Simplified draw callback - manages parameters internally */
static int gradient_draw_callback(void *thread_ctx, int buffer_idx)
{
    stress_thread_context_t *tctx = (stress_thread_context_t *)thread_ctx;
    stress_render_context_t *ctx = tctx->ctx;

    /* Internal parameter management */
    int gradient_idx = tctx->current_param_index % NUM_GRADIENT_CONFIGS;
    int crop_idx = tctx->current_param_index % NUM_CROP_CONFIGS;

    gradient_config_t grad_cfg;
    crop_config_t crop_cfg;

    /* Initialize private_buf to zero to avoid undefined values */
    memset(&tctx->private_buf, 0, sizeof(struct mpp_buf));

    /* Copy shared buffer to private buffer (thread-safe, no lock needed) */
    memcpy(&tctx->private_buf, &ctx->draw_buffers[buffer_idx], sizeof(struct mpp_buf));

    /* Copy gradient and crop config parameters */
    memcpy(&grad_cfg, &g_gradient_configs[gradient_idx], sizeof(gradient_config_t));
    memcpy(&crop_cfg, &g_crop_configs[crop_idx], sizeof(crop_config_t));

    /* Modify private buffer parameters with crop config */
    tctx->private_buf.crop_en = crop_cfg.crop_en;
    tctx->private_buf.crop.x = crop_cfg.crop_x;
    tctx->private_buf.crop.y = crop_cfg.crop_y;
    tctx->private_buf.crop.width = crop_cfg.crop_width;
    tctx->private_buf.crop.height = crop_cfg.crop_height;

    /* Execute gradient operation with private buffer (completely lock-free) */
    int result = perform_gradient_operation(tctx->vg, &tctx->private_buf, &grad_cfg);
    if (result != 0) {
        stress_record_failure(tctx, STRESS_FAIL_REASON_OP_FAILED);
        return -1;
    }

    return 0;
}

static const char *gradient_fail_reason_to_string(int reason)
{
    switch (reason) {
    case STRESS_FAIL_REASON_NO_BUFFER:
        return "Failed to get drawable buffer";
    case STRESS_FAIL_REASON_OP_FAILED:
        return "Gradient operation failed";
    default:
        return "Unknown failure";
    }
}

/***********************
 *  TEST OPS TABLE
 **********************/
static const stress_test_ops_t g_gradient_ops = {
    .test_name = "ArtVG Gradient Concurrent Stress Test",

    .init_test_resources = NULL,      /* No extra resources needed */
    .cleanup_test_resources = NULL,
    .draw_callback = gradient_draw_callback,
    .collect_and_print_results = NULL, /* Use default result printing */
    .fail_reason_to_string = gradient_fail_reason_to_string,
};

/***********************
 *  MAIN ENTRY POINT
 **********************/
int main(int argc, char **argv)
{
    return stress_run_test(argc, argv, &g_gradient_ops);
}
