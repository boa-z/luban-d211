/*
 * Copyright (c) 2026, ArtInChip Technology Co., Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Authors:  zequan liang <zequan.liang@artinchip.com>
 *
 * ArtVG Blit Stress Test
 * Multi-instance concurrent stress test for artvg_blit() operation
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include "artvg_stress_framework.h"
#include "image_array.h"

/***********************
 *  BLIT-SPECIFIC CONFIGS
 **********************/
#define SRC_IMAGE_WIDTH  240
#define SRC_IMAGE_HEIGHT 120

typedef struct {
    int rotate;
    int mirror;
    int crop_en;
    int crop_x;
    int crop_y;
    int crop_width;
    int crop_height;
    int alpha_en;
    int alpha_mode;
} blit_config_t;

static const blit_config_t g_blit_configs[] = {
    {ARTVG_ROTATE_0, ARTVG_MIRROR_NONE, 1, 0, 0, 240, 120, 1, ARTVG_ALPHA_PIXEL},
    {ARTVG_ROTATE_90, ARTVG_MIRROR_NONE, 1, 123, 321, 120, 240, 1, ARTVG_ALPHA_PIXEL},
    {ARTVG_ROTATE_180, ARTVG_MIRROR_NONE, 1, 681, 120, 240, 120, 1, ARTVG_ALPHA_GLOBAL},
    {ARTVG_ROTATE_270, ARTVG_MIRROR_NONE, 1, 480, 240, 120, 240, 1, ARTVG_ALPHA_GLOBAL},
    {ARTVG_ROTATE_0, ARTVG_MIRROR_HORIZONTAL, 1, 240, 120, 240, 120, 0, 0},
    {ARTVG_ROTATE_0, ARTVG_MIRROR_VERTICAL, 1, 321, 213, 240, 120, 0, 0},
    {ARTVG_ROTATE_90, ARTVG_MIRROR_HORIZONTAL, 1, 781, 325, 120, 240, 1, ARTVG_ALPHA_MIXED},
    {ARTVG_ROTATE_270, ARTVG_MIRROR_VERTICAL, 1, 220, 110, 120, 240, 1, ARTVG_ALPHA_MIXED}
};

#define NUM_BLIT_CONFIGS (sizeof(g_blit_configs) / sizeof(g_blit_configs[0]))

/***********************
 *  EXTERNAL IMAGE DATA
 **********************/
extern const image_dsc_t alpha_baby_card_240x120;

/***********************
 *  BLIT OPERATION
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

static int perform_blit_operation(struct artvg *vg, struct mpp_buf *src,
                                  struct mpp_buf *dst, const blit_config_t *config)
{
    if (!vg || !src || !dst || !config) {
        printf("ERROR: vg=%p, src=%p, dst=%p, config=%p\n",
               vg, src, dst, config);
        return -1;
    }

    artvg_ctrl_t ctrl = {0};
    artvg_blit_ctl_t blit_ctl = {0};

    blit_ctl.rotate = config->rotate;
    blit_ctl.mirror = config->mirror;
    ctrl.alpha_en = config->alpha_en;
    ctrl.src_alpha_mode = config->alpha_mode;

    artvg_error_t ret;

    ret = artvg_blit(vg, src, dst, NULL, &ctrl, &blit_ctl);
    if (ret != ARTVG_SUCCESS) {
        printf("[ERROR] artvg_blit failed: %d\n", ret);
        printf("  Config: rotate=%d, mirror=%d, alpha_en=%d, alpha_mode=%d\n",
               config->rotate, config->mirror, config->alpha_en, config->alpha_mode);
        print_buffer_params("Blit Src Failed", src);
        print_buffer_params("Blit Dst Failed", dst);
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
 **********************/
/* Initialize thread-specific resources - allocate source buffer for this thread */
static int blit_init_thread_resources(void *thread_ctx)
{
    stress_thread_context_t *tctx = (stress_thread_context_t *)thread_ctx;
    struct mpp_buf *src_buf;
    void *src_mmap;
    size_t map_size;

    /* Allocate source buffer using new API */
    src_buf = artvg_allocate(tctx->vg, SRC_IMAGE_WIDTH, SRC_IMAGE_HEIGHT, MPP_FMT_ARGB_8888);
    if (src_buf == NULL) {
        printf("[Thread %d] Failed to allocate source buffer\n", tctx->thread_id);
        return -1;
    }

    /* Map and load image data */
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

    /* Store source buffer in thread's user data */
    tctx->user_data = src_buf;
    return 0;
}

/* Cleanup thread-specific resources - free source buffer */
static void blit_cleanup_thread_resources(void *thread_ctx)
{
    stress_thread_context_t *tctx = (stress_thread_context_t *)thread_ctx;
    struct mpp_buf *src_buf = (struct mpp_buf *)tctx->user_data;

    if (src_buf) {
        artvg_free(tctx->vg, src_buf);
        tctx->user_data = NULL;
    }
}

/***********************
 *  TEST OPS IMPLEMENTATION
 **********************/
/* Simplified draw callback - manages parameters internally */
static int blit_draw_callback(void *thread_ctx, int buffer_idx)
{
    stress_thread_context_t *tctx = (stress_thread_context_t *)thread_ctx;
    stress_render_context_t *ctx = tctx->ctx;
    struct mpp_buf *src_buf = (struct mpp_buf *)tctx->user_data;

    /* Internal parameter management */
    int blit_idx = tctx->current_param_index % NUM_BLIT_CONFIGS;
    int param_index = tctx->current_param_index;

    blit_config_t config = {0};

    /* Initialize private_buf to zero to avoid undefined values */
    memset(&tctx->private_buf, 0, sizeof(struct mpp_buf));

    /* Copy shared buffer to private buffer (thread-safe, no lock needed) */
    memcpy(&tctx->private_buf, &ctx->draw_buffers[buffer_idx], sizeof(struct mpp_buf));

    /* Copy blit config parameters (also thread-safe with memcpy) */
    memcpy(&config, &g_blit_configs[blit_idx], sizeof(blit_config_t));

    /* Modify private buffer parameters (no other thread can access this) */
    tctx->private_buf.crop_en = config.crop_en;
    tctx->private_buf.crop.x = config.crop_x;
    tctx->private_buf.crop.y = config.crop_y;
    tctx->private_buf.crop.width = config.crop_width;
    tctx->private_buf.crop.height = config.crop_height;

    /* Execute blit operation with private buffer (completely lock-free) */
    if (perform_blit_operation(tctx->vg, src_buf, &tctx->private_buf, &config) != 0) {
        printf("[Thread %d] Blit operation FAILED at param_index=%d, buffer_idx=%d\n",
               tctx->thread_id, param_index, buffer_idx);
        printf("  Config: rotate=%d, mirror=%d, crop_en=%d, crop=(%d,%d,%d,%d)\n",
               config.rotate, config.mirror, config.crop_en,
               config.crop_x, config.crop_y, config.crop_width, config.crop_height);
        stress_record_failure(tctx, STRESS_FAIL_REASON_OP_FAILED);
        return -1;
    }

    return 0;
}

static const char *blit_fail_reason_to_string(int reason)
{
    switch (reason) {
    case STRESS_FAIL_REASON_NO_BUFFER:
        return "Failed to get drawable buffer";
    case STRESS_FAIL_REASON_OP_FAILED:
        return "Blit operation failed";
    default:
        return "Unknown failure";
    }
}

/***********************
 *  TEST OPS TABLE
 **********************/
static const stress_test_ops_t g_blit_ops = {
    .test_name = "ArtVG Blit Concurrent Stress Test",

    .init_test_resources = NULL,  /* Not needed */
    .init_thread_resources = blit_init_thread_resources,
    .cleanup_thread_resources = blit_cleanup_thread_resources,
    .cleanup_test_resources = NULL,  /* Not needed */
    .draw_callback = blit_draw_callback,
    .collect_and_print_results = NULL, /* Use default result printing */
    .fail_reason_to_string = blit_fail_reason_to_string,
};

/***********************
 *  MAIN ENTRY POINT
 **********************/
int main(int argc, char **argv)
{
    return stress_run_test(argc, argv, &g_blit_ops);
}
