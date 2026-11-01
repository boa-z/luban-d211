/*
 * Copyright (c) 2026, ArtInChip Technology Co., Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Authors:  zequan liang <zequan.liang@artinchip.com>
 *
 * ArtVG Affine Stress Test
 * Multi-instance concurrent stress test for artvg_affine() operation
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include "artvg_stress_framework.h"
#include "image_array.h"

/***********************
 *  AFFINE-SPECIFIC CONFIGURATIONS
 **********************/
#define SRC_IMAGE_WIDTH  240
#define SRC_IMAGE_HEIGHT 120
#define MAX_AFFINE_CONFIGS 16

typedef struct {
    float translate_x;
    float translate_y;
    float scale_x;
    float scale_y;
    float rotate_angle;
    float skew_x;
    float skew_y;
    int crop_en;
    int crop_x;
    int crop_y;
    int crop_width;
    int crop_height;
    int alpha_en;
    int alpha_rules;
    int fill_bg_en;
    int blend_edge_en;
} affine_config_t;

/* Default affine test configurations (used when no command line args provided) */
static const affine_config_t g_affine_configs_default[] = {
    /* Config 0: Identity (no transform) */
    {0.0f, 0.0f, 1.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    /* Config 1: Translation */
    {100.0f, 50.0f, 1.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    /* Config 2: Scale up */
    {0.0f, 0.0f, 1.5f, 1.5f, 0.0f, 0.0f, 0.0f, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    /* Config 3: Scale down */
    {0.0f, 0.0f, 0.5f, 0.5f, 0.0f, 0.0f, 0.0f, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    /* Config 4: Rotate 45 degrees */
    {0.0f, 0.0f, 1.0f, 1.0f, 45.0f, 0.0f, 0.0f, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    /* Config 5: Rotate 90 degrees */
    {0.0f, 0.0f, 1.0f, 1.0f, 90.0f, 0.0f, 0.0f, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    /* Config 6: Skew X */
    {0.0f, 0.0f, 1.0f, 1.0f, 0.0f, 11.0f, 0.0f, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    /* Config 7: Skew Y */
    {0.0f, 0.0f, 1.0f, 1.0f, 0.0f, 0.0f, 11.0f, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    /* Config 8: Complex transform with alpha */
    {50.0f, 50.0f, 1.2f, 1.2f, 30.0f, 6.0f, 6.0f, 0, 0, 0, 0, 0, 1, ARTVG_BLEND_SRC_OVER, 0, 0},
    /* Config 9: Complex transform with negative values and alpha */
    {-50.0f, -50.0f, 0.8f, 0.8f, -30.0f, -6.0f, -6.0f, 0, 0, 0, 0, 0, 1, ARTVG_BLEND_SRC_OVER, 0, 0}
};

/* Runtime configurations (uses defaults) */
static affine_config_t g_affine_configs[MAX_AFFINE_CONFIGS];
static int g_num_affine_configs = 0;

/***********************
 *  EXTERNAL IMAGE DATA
 **********************/
extern const image_dsc_t alpha_baby_card_240x120;

/***********************
 *  AFFINE OPERATION
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

static void create_affine_matrix(artvg_matrix_t *matrix, const affine_config_t *config)
{
    artvg_matrix_identity(matrix);

    if (config->translate_x != 0.0f || config->translate_y != 0.0f)
        artvg_matrix_translate(config->translate_x, config->translate_y, matrix);

    if (config->scale_x != 1.0f || config->scale_y != 1.0f)
        artvg_matrix_scale(config->scale_x, config->scale_y, matrix);

    if (config->rotate_angle != 0.0f)
        artvg_matrix_rotate(config->rotate_angle, matrix);

    if (config->skew_x != 0.0f || config->skew_y != 0.0f)
        artvg_matrix_skew(config->skew_x, config->skew_y, matrix);
}

static int perform_affine_operation(struct artvg *vg, struct mpp_buf *src,
                                    struct mpp_buf *dst, const affine_config_t *config)
{
    if (!vg || !src || !dst || !config) {
        printf("ERROR: vg=%p, src=%p, dst=%p, config=%p\n",
               vg, src, dst, config);
        return -1;
    }

    artvg_ctrl_t ctrl = {0};
    artvg_transform_edge_t transform_edge = {0};
    artvg_matrix_t matrix;

    ctrl.alpha_en = config->alpha_en;
    ctrl.alpha_rules = config->alpha_rules;

    transform_edge.fill_en = config->fill_bg_en;
    transform_edge.blend_en = config->blend_edge_en;
    transform_edge.fill_color = 0xFF000000;
    transform_edge.pad_mode = ARTVG_PAD_MODE_FIXED_COLOR;

    create_affine_matrix(&matrix, config);

    artvg_error_t ret = artvg_affine(vg, src, dst, NULL, &matrix, &ctrl, &transform_edge);
    if (ret != ARTVG_SUCCESS) {
        printf("[ERROR] artvg_affine failed: %d\n", ret);
        printf("  Config: translate=(%.1f,%.1f), scale=(%.1f,%.1f), rotate=%.1f, skew=(%.1f,%.1f)\n",
               config->translate_x, config->translate_y,
               config->scale_x, config->scale_y,
               config->rotate_angle,
               config->skew_x, config->skew_y);
        print_buffer_params("Affine Src Failed", src);
        print_buffer_params("Affine Dst Failed", dst);
        return -1;
    }

    ret = artvg_flush(vg);
    if (ret != ARTVG_SUCCESS) {
        printf("[ERROR] artvg_flush failed: %d\n", ret);
        print_buffer_params("Flush Failed (Dst)", dst);
        return -1;
    }

    ret = artvg_wait_finish(vg);
    if (ret != ARTVG_SUCCESS) {
        printf("[ERROR] artvg_wait_finish failed: %d\n", ret);
        print_buffer_params("Wait Finish Failed (Dst)", dst);
        return -1;
    }

    return 0;
}

/***********************
 *  SOURCE BUFFER MANAGEMENT (PER-THREAD)
 *  Each thread maintains its own source buffer
 **********************/
static int affine_init_thread_resources(void *thread_ctx)
{
    stress_thread_context_t *tctx = (stress_thread_context_t *)thread_ctx;
    struct mpp_buf *src_buf;
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

static void affine_cleanup_thread_resources(void *thread_ctx)
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
static int affine_draw_callback(void *thread_ctx, int buffer_idx)
{
    stress_thread_context_t *tctx = (stress_thread_context_t *)thread_ctx;
    stress_render_context_t *ctx = tctx->ctx;
    struct mpp_buf *src_buf = (struct mpp_buf *)tctx->user_data;

    int affine_idx = tctx->current_param_index % g_num_affine_configs;

    affine_config_t config = {0};

    memset(&tctx->private_buf, 0, sizeof(struct mpp_buf));
    memcpy(&tctx->private_buf, &ctx->draw_buffers[buffer_idx], sizeof(struct mpp_buf));
    memcpy(&config, &g_affine_configs[affine_idx], sizeof(affine_config_t));

    tctx->private_buf.crop_en = config.crop_en;
    tctx->private_buf.crop.x = config.crop_x;
    tctx->private_buf.crop.y = config.crop_y;
    tctx->private_buf.crop.width = config.crop_width;
    tctx->private_buf.crop.height = config.crop_height;

    if (tctx->private_buf.crop.x + tctx->private_buf.crop.width > tctx->private_buf.size.width) {
        tctx->private_buf.crop.width = tctx->private_buf.size.width - tctx->private_buf.crop.x;
    }
    if (tctx->private_buf.crop.y + tctx->private_buf.crop.height > tctx->private_buf.size.height) {
        tctx->private_buf.crop.height = tctx->private_buf.size.height - tctx->private_buf.crop.y;
    }

    if (perform_affine_operation(tctx->vg, src_buf, &tctx->private_buf, &config) != 0) {
        printf("[Thread %d] Affine operation FAILED at param_index=%d, buffer_idx=%d\n",
               tctx->thread_id, tctx->current_param_index, buffer_idx);
        printf("  Config: translate=(%.1f,%.1f), scale=(%.1f,%.1f), rotate=%.1f\n",
               config.translate_x, config.translate_y,
               config.scale_x, config.scale_y,
               config.rotate_angle);
        stress_record_failure(tctx, STRESS_FAIL_REASON_OP_FAILED);
        return -1;
    }

    return 0;
}

static const char *affine_fail_reason_to_string(int reason)
{
    switch (reason) {
    case STRESS_FAIL_REASON_NO_BUFFER:
        return "Failed to get drawable buffer";
    case STRESS_FAIL_REASON_OP_FAILED:
        return "Affine operation failed";
    default:
        return "Unknown failure";
    }
}

/***********************
 *  TEST OPERATIONS TABLE
 **********************/
static const stress_test_ops_t g_affine_ops = {
    .test_name = "ArtVG Affine Concurrent Stress Test",

    .init_test_resources = NULL,
    .init_thread_resources = affine_init_thread_resources,
    .cleanup_thread_resources = affine_cleanup_thread_resources,
    .cleanup_test_resources = NULL,
    .draw_callback = affine_draw_callback,
    .collect_and_print_results = NULL,
    .fail_reason_to_string = affine_fail_reason_to_string,
};

/***********************
 *  MAIN ENTRY POINT
 *  Program entry - delegates to stress test framework
 **********************/
int main(int argc, char **argv)
{
    /* Initialize default configs */
    g_num_affine_configs = sizeof(g_affine_configs_default) / sizeof(g_affine_configs_default[0]);
    memcpy(g_affine_configs, g_affine_configs_default,
           g_num_affine_configs * sizeof(affine_config_t));

    return stress_run_test(argc, argv, &g_affine_ops);
}
