/*
 * Copyright (c) 2026, ArtInChip Technology Co., Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Authors:  zequan liang <zequan.liang@artinchip.com>
 *
 * ArtVG Projective Stress Test
 * Multi-instance concurrent stress test for artvg_projective2() operation
 * Each iteration creates and destroys LUT to test stability
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include "artvg_stress_framework.h"
#include "image_array.h"

/***********************
 *  PROJECTIVE-SPECIFIC CONFIGURATIONS
 **********************/
#define SRC_IMAGE_WIDTH  240
#define SRC_IMAGE_HEIGHT 120
#define MAX_PROJECTIVE_CONFIGS 16

typedef struct {
    float translate_x;
    float translate_y;
    float scale_x;
    float scale_y;
    float rotate_angle;
    float skew_x;
    float skew_y;
    float perspective_x;
    float perspective_y;
    float center_x;
    float center_y;
    int lut_mode;
    int block_size;
    int out_blk_mode;
    int fill_bg_en;
    int blend_edge_en;
    uint32_t edge_color;
    int edge_pad_mode;
    int alpha_en;
    int alpha_rules;
    int crop_en;
    int crop_x;
    int crop_y;
    int crop_width;
    int crop_height;
} projective_config_t;

/* Default projective test configurations */
static const projective_config_t g_projective_configs_default[] = {
    /* Config 0: Simple translation */
    {50.0f, 30.0f, 1.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 120.0f, 60.0f,
     5, 1, 1, 0, 0, 0xFF000000, 0, 0, 0, 0, 0, 0, 0, 0},

    /* Config 1: Rotation 30 degrees */
    {0.0f, 0.0f, 1.0f, 1.0f, 30.0f, 0.0f, 0.0f, 0.0f, 0.0f, 120.0f, 60.0f,
     5, 1, 1, 0, 0, 0xFF000000, 0, 0, 0, 0, 0, 0, 0, 0},

    /* Config 2: Scale up */
    {0.0f, 0.0f, 1.5f, 1.5f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 120.0f, 60.0f,
     5, 1, 1, 0, 0, 0xFF000000, 0, 0, 0, 0, 0, 0, 0, 0},

    /* Config 3: Skew only */
    {0.0f, 0.0f, 1.0f, 1.0f, 0.0f, 17.0f, 11.0f, 0.0f, 0.0f, 120.0f, 60.0f,
     5, 1, 1, 0, 0, 0xFF000000, 0, 0, 0, 0, 0, 0, 0, 0},

    /* Config 4: Perspective transformation */
    {0.0f, 0.0f, 1.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0001f, 0.0001f, 120.0f, 60.0f,
     5, 1, 1, 0, 0, 0xFF000000, 0, 0, 0, 0, 0, 0, 0, 0},

    /* Config 5: Combined transformation */
    {20.0f, 10.0f, 1.2f, 0.8f, 15.0f, 6.0f, 6.0f, 0.00005f, 0.00003f, 120.0f, 60.0f,
     5, 1, 1, 0, 0, 0xFF000000, 0, 0, 0, 0, 0, 0, 0, 0},

    /* Config 6: Rotation 90 degrees with crop */
    {0.0f, 0.0f, 1.0f, 1.0f, 90.0f, 0.0f, 0.0f, 0.0f, 0.0f, 120.0f, 60.0f,
     5, 1, 1, 0, 0, 0xFF000000, 0, 0, 0, 1, 0, 0, 120, 120},

    /* Config 7: With alpha blending */
    {30.0f, 20.0f, 1.0f, 1.0f, 45.0f, 0.0f, 0.0f, 0.0f, 0.0f, 120.0f, 60.0f,
     5, 1, 1, 0, 0, 0xFF000000, 0, 1, 0, 0, 0, 0, 0, 0},

    /* Config 8: With edge fill */
    {40.0f, 40.0f, 1.3f, 1.3f, 60.0f, 0.0f, 0.0f, 0.0f, 0.0f, 120.0f, 60.0f,
     5, 1, 1, 1, 0, 0xFFFF0000, 0, 0, 0, 0, 0, 0, 0, 0},

    /* Config 9: Different LUT mode */
    {0.0f, 0.0f, 1.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.00008f, 0.00008f, 120.0f, 60.0f,
     3, 2, 1, 0, 0, 0xFF000000, 0, 0, 0, 0, 0, 0, 0, 0},

    /* Config 10: 8x8 block size */
    {10.0f, 10.0f, 0.9f, 0.9f, 25.0f, 0.0f, 0.0f, 0.00003f, 0.00003f, 120.0f, 60.0f,
     5, 0, 1, 0, 0, 0xFF000000, 0, 0, 0, 0, 0, 0, 0, 0},

    /* Config 11: 32x32 block size with edge blend */
    {-20.0f, -10.0f, 1.1f, 1.1f, -20.0f, 0.0f, 0.0f, 0.00006f, 0.00004f, 120.0f, 60.0f,
     5, 2, 1, 1, 1, 0xFF00FF00, 1, 1, 0, 0, 0, 0, 0, 0},
};

/* Runtime configurations */
static projective_config_t g_projective_configs[MAX_PROJECTIVE_CONFIGS];
static int g_num_projective_configs = 0;

/***********************
 *  EXTERNAL IMAGE DATA
 **********************/
extern const image_dsc_t alpha_baby_card_240x120;

/***********************
 *  MATRIX OPERATIONS
 **********************/
static void create_transform_matrix(artvg_matrix_t *matrix, const projective_config_t *config)
{
    artvg_matrix_identity(matrix);

    if (config->translate_x != 0.0f || config->translate_y != 0.0f)
        artvg_matrix_translate(config->translate_x, config->translate_y, matrix);

    if (config->scale_x != 1.0f || config->scale_y != 1.0f)
        artvg_matrix_scale(config->scale_x, config->scale_y, matrix);

    if (config->rotate_angle != 0.0f) {
        if (config->center_x != 0.0f || config->center_y != 0.0f) {
            artvg_point_t center = {config->center_x, config->center_y};
            artvg_matrix_rotate_point(&center, config->rotate_angle, matrix);
        } else {
            artvg_matrix_rotate(config->rotate_angle, matrix);
        }
    }

    if (config->skew_x != 0.0f || config->skew_y != 0.0f)
        artvg_matrix_skew(config->skew_x, config->skew_y, matrix);

    if (config->perspective_x != 0.0f || config->perspective_y != 0.0f)
        artvg_matrix_perspective(config->perspective_x, config->perspective_y, matrix);
}

/***********************
 *  PROJECTIVE OPERATION
 **********************/
static int perform_projective_operation(struct artvg *vg, struct mpp_buf *src,
                                        struct mpp_buf *dst,
                                        const projective_config_t *config)
{
    if (!vg || !src || !dst || !config) {
        printf("ERROR: vg=%p, src=%p, dst=%p, config=%p\n",
               vg, src, dst, config);
        return -1;
    }

    artvg_ctrl_t ctrl = {0};
    artvg_transform_edge_t transform_edge = {0};
    artvg_projective_t *projective = NULL;
    artvg_matrix_t matrix;
    artvg_error_t ret;

    create_transform_matrix(&matrix, config);

    ctrl.alpha_en = config->alpha_en;
    ctrl.alpha_rules = config->alpha_rules;

    transform_edge.fill_en = config->fill_bg_en;
    transform_edge.blend_en = config->blend_edge_en;
    transform_edge.fill_color = config->edge_color;
    transform_edge.pad_mode = config->edge_pad_mode;

    projective = artvg_projective_allocate(
        vg, &matrix, &transform_edge,
        (artvg_projective_lut_mode_t)config->lut_mode,
        (artvg_projective_lut_block_size_t)config->block_size,
        (artvg_projective_out_blk_mode_t)config->out_blk_mode,
        dst->size.width, dst->size.height
    );
    if (projective == NULL) {
        printf("[ERROR] artvg_projective_allocate failed\n");
        return -1;
    }

    ret = artvg_projective2(vg, src, dst, dst, &ctrl, projective);
    if (ret != ARTVG_SUCCESS) {
        printf("[ERROR] artvg_projective2 failed: %d\n", ret);
        artvg_projective_free(vg, projective);
        return -1;
    }

    ret = artvg_flush(vg);
    if (ret != ARTVG_SUCCESS) {
        printf("[ERROR] artvg_flush failed: %d\n", ret);
        artvg_projective_free(vg, projective);
        return -1;
    }

    ret = artvg_wait_finish(vg);
    if (ret != ARTVG_SUCCESS) {
        printf("[ERROR] artvg_wait_finish failed: %d\n", ret);
        artvg_projective_free(vg, projective);
        return -1;
    }

    artvg_projective_free(vg, projective);

    return 0;
}

/***********************
 *  SOURCE BUFFER MANAGEMENT (PER-THREAD)
 **********************/
static int projective_init_thread_resources(void *thread_ctx)
{
    stress_thread_context_t *tctx = (stress_thread_context_t *)thread_ctx;
    struct mpp_buf *src_buf = NULL;
    void *src_mmap;
    size_t map_size;

    src_buf = artvg_allocate(tctx->vg, SRC_IMAGE_WIDTH, SRC_IMAGE_HEIGHT, MPP_FMT_ARGB_8888);
    if (src_buf == NULL) {
        printf("[Thread %d] Failed to allocate source buffer\n", tctx->thread_id);
        return -1;
    }

    map_size = src_buf->stride[0] * src_buf->size.height;
    src_mmap = mmap(NULL, map_size, PROT_READ | PROT_WRITE, MAP_SHARED, src_buf->fd[0], 0);
    if (src_mmap == MAP_FAILED) {
        printf("[Thread %d] Failed to mmap source buffer\n", tctx->thread_id);
        artvg_free(tctx->vg, src_buf);
        return -1;
    }

    if (image_array_read(&alpha_baby_card_240x120, src_mmap, map_size) != 0) {
        printf("[Thread %d] Failed to load source image\n", tctx->thread_id);
        munmap(src_mmap, map_size);
        artvg_free(tctx->vg, src_buf);
        return -1;
    }

    munmap(src_mmap, map_size);
    tctx->user_data = src_buf;

    return 0;
}

static void projective_cleanup_thread_resources(void *thread_ctx)
{
    stress_thread_context_t *tctx = (stress_thread_context_t *)thread_ctx;
    struct mpp_buf *src_buf = (struct mpp_buf *)tctx->user_data;

    if (src_buf) {
        artvg_free(tctx->vg, src_buf);
        tctx->user_data = NULL;
    }
}

/***********************
 *  TEST OPERATIONS IMPLEMENTATION
 **********************/
static int projective_draw_callback(void *thread_ctx, int buffer_idx)
{
    stress_thread_context_t *tctx = (stress_thread_context_t *)thread_ctx;
    const stress_render_context_t *ctx = tctx->ctx;
    struct mpp_buf *src_buf = (struct mpp_buf *)tctx->user_data;

    int config_idx = tctx->current_param_index % g_num_projective_configs;

    projective_config_t config = {0};

    memset(&tctx->private_buf, 0, sizeof(struct mpp_buf));
    memcpy(&tctx->private_buf, &ctx->draw_buffers[buffer_idx], sizeof(struct mpp_buf));
    memcpy(&config, &g_projective_configs[config_idx], sizeof(projective_config_t));

    tctx->private_buf.crop_en = config.crop_en;
    tctx->private_buf.crop.x = config.crop_x;
    tctx->private_buf.crop.y = config.crop_y;
    tctx->private_buf.crop.width = config.crop_width;
    tctx->private_buf.crop.height = config.crop_height;

    if (perform_projective_operation(tctx->vg, src_buf, &tctx->private_buf, &config) != 0) {
        printf("[Thread %d] Projective operation FAILED at param_index=%d, buffer_idx=%d\n",
               tctx->thread_id, tctx->current_param_index, buffer_idx);
        printf("  Config: translate=(%.1f,%.1f), scale=(%.1f,%.1f), rotate=%.1f\n",
               config.translate_x, config.translate_y,
               config.scale_x, config.scale_y, config.rotate_angle);
        stress_record_failure(tctx, STRESS_FAIL_REASON_OP_FAILED);
        return -1;
    }

    return 0;
}

static const char *projective_fail_reason_to_string(int reason)
{
    switch (reason) {
    case STRESS_FAIL_REASON_NO_BUFFER:
        return "Failed to get drawable buffer";
    case STRESS_FAIL_REASON_OP_FAILED:
        return "Projective operation failed";
    default:
        return "Unknown failure";
    }
}

/***********************
 *  TEST OPERATIONS TABLE
 **********************/
static const stress_test_ops_t g_projective_ops = {
    .test_name = "ArtVG Projective Concurrent Stress Test",

    .init_test_resources = NULL,
    .init_thread_resources = projective_init_thread_resources,
    .cleanup_thread_resources = projective_cleanup_thread_resources,
    .cleanup_test_resources = NULL,
    .draw_callback = projective_draw_callback,
    .collect_and_print_results = NULL,
    .fail_reason_to_string = projective_fail_reason_to_string,
};

/***********************
 *  MAIN ENTRY POINT
 **********************/
int main(int argc, char **argv)
{
    int num_configs = sizeof(g_projective_configs_default) / sizeof(g_projective_configs_default[0]);

    if (num_configs > MAX_PROJECTIVE_CONFIGS) {
        printf("WARNING: Number of default configs (%d) exceeds MAX_PROJECTIVE_CONFIGS (%d),\n"
               "         only first %d configs will be used.\n",
               num_configs, MAX_PROJECTIVE_CONFIGS, MAX_PROJECTIVE_CONFIGS);
        g_num_projective_configs = MAX_PROJECTIVE_CONFIGS;
    } else {
        g_num_projective_configs = num_configs;
    }

    memcpy(g_projective_configs, g_projective_configs_default,
           g_num_projective_configs * sizeof(projective_config_t));

    return stress_run_test(argc, argv, &g_projective_ops);
}
