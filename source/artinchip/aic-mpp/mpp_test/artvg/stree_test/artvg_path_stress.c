/*
 * Copyright (c) 2026, ArtInChip Technology Co., Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Authors:  zequan liang <zequan.liang@artinchip.com>
 *
 * ArtVG Path Stress Test
 * Multi-instance concurrent stress test for artvg_draw_path() operation
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "artvg_stress_framework.h"

/***********************
 *  PATH-SPECIFIC CONFIGS
 **********************/

/* Color definitions */
#define COLOR_RED          0xFFFF0000
#define COLOR_GREEN        0xFF00FF00
#define COLOR_BLUE         0xFF0000FF
#define COLOR_YELLOW       0xFFFFFF00
#define COLOR_CYAN         0xFF00FFFF
#define COLOR_MAGENTA      0xFFFF00FF
#define COLOR_WHITE        0xFFFFFFFF
#define COLOR_BLACK        0xFF000000
#define COLOR_ORANGE       0xFFFFA500
#define COLOR_PURPLE       0xFF800080

/* Path command opcodes */
#define VLC_OP_MOVE         0x02
#define VLC_OP_LINE         0x04
#define VLC_OP_QUAD         0x06
#define VLC_OP_CUBIC        0x08

/* Path types */
typedef enum {
    PATH_TYPE_TRIANGLE = 0,
    PATH_TYPE_QUAD_BEZIER,
    PATH_TYPE_CUBIC_BEZIER,
    PATH_TYPE_HEART,
    PATH_TYPE_WAVE,
    PATH_TYPE_FILL_RULE,
    PATH_TYPE_COUNT
} path_type_t;

/* Path data for triangle: MOVE + LINE commands */
static float g_path_triangle[] = {
    (float)(VLC_OP_MOVE), 0.0f, 400.0f,
    (float)(VLC_OP_LINE), 400.0f, 400.0f,
    (float)(VLC_OP_LINE), 200.0f, 0.0f,
    (float)(VLC_OP_LINE), 0.0f, 400.0f,
};

/* Path data for quadratic bezier curves */
static float g_path_quad_bezier[] = {
    (float)(VLC_OP_MOVE), 0.0f, 400.0f,
    (float)(VLC_OP_LINE), 400.0f, 400.0f,
    (float)(VLC_OP_QUAD), 400.0f, 200.0f, 200.0f, 0.0f,
    (float)(VLC_OP_QUAD), 0.0f, 200.0f, 0.0f, 400.0f,
};

/* Path data for cubic bezier curves */
static float g_path_cubic_bezier[] = {
    (float)(VLC_OP_MOVE), 200.0f, 400.0f,
    (float)(VLC_OP_LINE), 300.0f, 300.0f,
    (float)(VLC_OP_CUBIC), 254.0f, 228.0f, 365.0f, 190.0f, 300.0f, 100.0f,
    (float)(VLC_OP_CUBIC), 300.0f, 197.0f, 200.0f, 106.0f, 200.0f, 0.0f,
    (float)(VLC_OP_CUBIC), 132.0f, 0.0f, 158.0f, 187.0f, 100.0f, 100.0f,
    (float)(VLC_OP_CUBIC), 0.0f, 100.0f, 200.0f, 300.0f, 100.0f, 300.0f,
    (float)(VLC_OP_LINE), 200.0f, 400.0f,
};

/* Path data for heart shape */
static float g_path_heart[] = {
    (float)(VLC_OP_MOVE), 200.0f, 100.0f,
    (float)(VLC_OP_CUBIC), 200.0f, 50.0f, 150.0f, 0.0f, 100.0f, 0.0f,
    (float)(VLC_OP_CUBIC), 50.0f, 0.0f, 0.0f, 50.0f, 0.0f, 100.0f,
    (float)(VLC_OP_CUBIC), 0.0f, 180.0f, 100.0f, 280.0f, 200.0f, 380.0f,
    (float)(VLC_OP_CUBIC), 300.0f, 280.0f, 400.0f, 180.0f, 400.0f, 100.0f,
    (float)(VLC_OP_CUBIC), 400.0f, 50.0f, 350.0f, 0.0f, 300.0f, 0.0f,
    (float)(VLC_OP_CUBIC), 250.0f, 0.0f, 200.0f, 50.0f, 200.0f, 100.0f,
};

/* Path data for wave shape */
static float g_path_wave[] = {
    (float)(VLC_OP_MOVE), 0.0f, 200.0f,
    (float)(VLC_OP_QUAD), 50.0f, 100.0f, 100.0f, 200.0f,
    (float)(VLC_OP_QUAD), 150.0f, 300.0f, 200.0f, 200.0f,
    (float)(VLC_OP_QUAD), 250.0f, 100.0f, 300.0f, 200.0f,
    (float)(VLC_OP_QUAD), 350.0f, 300.0f, 400.0f, 200.0f,
    (float)(VLC_OP_LINE), 400.0f, 400.0f,
    (float)(VLC_OP_LINE), 0.0f, 400.0f,
    (float)(VLC_OP_LINE), 0.0f, 200.0f,
};

/* Path data for fill rule testing */
static float g_path_fill_rule[] = {
    (float)(VLC_OP_MOVE), 0.0f, 400.0f,
    (float)(VLC_OP_LINE), 200.0f, 0.0f,
    (float)(VLC_OP_LINE), 400.0f, 400.0f,
    (float)(VLC_OP_LINE), 0.0f, 400.0f,
    (float)(VLC_OP_MOVE), 0.0f, 0.0f,
    (float)(VLC_OP_LINE), 400.0f, 0.0f,
    (float)(VLC_OP_LINE), 200.0f, 400.0f,
    (float)(VLC_OP_LINE), 0.0f, 0.0f,
};

/* Path info structure */
typedef struct {
    const char *name;
    float *data;
    size_t size;
    float min_x, min_y, max_x, max_y;
    float default_center[2];
} path_info_t;

static path_info_t g_path_info[PATH_TYPE_COUNT] = {
    [PATH_TYPE_TRIANGLE] = {
        .name = "triangle",
        .data = g_path_triangle,
        .size = sizeof(g_path_triangle),
        .min_x = 0.0f, .min_y = 0.0f, .max_x = 400.0f, .max_y = 400.0f,
        .default_center = {200.0f, 200.0f}
    },
    [PATH_TYPE_QUAD_BEZIER] = {
        .name = "quad_bezier",
        .data = g_path_quad_bezier,
        .size = sizeof(g_path_quad_bezier),
        .min_x = 0.0f, .min_y = 0.0f, .max_x = 400.0f, .max_y = 400.0f,
        .default_center = {200.0f, 200.0f}
    },
    [PATH_TYPE_CUBIC_BEZIER] = {
        .name = "cubic_bezier",
        .data = g_path_cubic_bezier,
        .size = sizeof(g_path_cubic_bezier),
        .min_x = 0.0f, .min_y = 0.0f, .max_x = 400.0f, .max_y = 400.0f,
        .default_center = {200.0f, 200.0f}
    },
    [PATH_TYPE_HEART] = {
        .name = "heart",
        .data = g_path_heart,
        .size = sizeof(g_path_heart),
        .min_x = 0.0f, .min_y = 0.0f, .max_x = 400.0f, .max_y = 380.0f,
        .default_center = {200.0f, 190.0f}
    },
    [PATH_TYPE_WAVE] = {
        .name = "wave",
        .data = g_path_wave,
        .size = sizeof(g_path_wave),
        .min_x = 0.0f, .min_y = 100.0f, .max_x = 400.0f, .max_y = 400.0f,
        .default_center = {200.0f, 250.0f}
    },
    [PATH_TYPE_FILL_RULE] = {
        .name = "fill_rule",
        .data = g_path_fill_rule,
        .size = sizeof(g_path_fill_rule),
        .min_x = 0.0f, .min_y = 0.0f, .max_x = 400.0f, .max_y = 400.0f,
        .default_center = {200.0f, 200.0f}
    },
};

/* Path test configuration */
typedef struct {
    path_type_t path_type;
    uint32_t color;
    float translate_x;
    float translate_y;
    float scale_x;
    float scale_y;
    float rotate_angle;
    artvg_fill_rule_t fill_rule;
    uint8_t curve_flat_limit;
} path_config_t;

static const path_config_t g_path_configs[] = {
    /* Triangle with different transformations */
    {PATH_TYPE_TRIANGLE, COLOR_RED, 100.0f, 100.0f, 1.0f, 1.0f, 0.0f, ARTVG_FILL_NON_ZERO, 10},
    {PATH_TYPE_TRIANGLE, COLOR_BLUE, 200.0f, 150.0f, 0.8f, 0.8f, 45.0f, ARTVG_FILL_NON_ZERO, 10},
    {PATH_TYPE_TRIANGLE, COLOR_GREEN, 50.0f, 50.0f, 1.2f, 1.2f, 90.0f, ARTVG_FILL_NON_ZERO, 10},

    /* Quadratic bezier */
    {PATH_TYPE_QUAD_BEZIER, COLOR_YELLOW, 100.0f, 100.0f, 1.0f, 1.0f, 0.0f, ARTVG_FILL_NON_ZERO, 10},
    {PATH_TYPE_QUAD_BEZIER, COLOR_CYAN, 150.0f, 100.0f, 0.9f, 0.9f, 30.0f, ARTVG_FILL_NON_ZERO, 10},

    /* Cubic bezier */
    {PATH_TYPE_CUBIC_BEZIER, COLOR_MAGENTA, 100.0f, 50.0f, 1.0f, 1.0f, 0.0f, ARTVG_FILL_NON_ZERO, 10},
    {PATH_TYPE_CUBIC_BEZIER, COLOR_ORANGE, 120.0f, 80.0f, 0.85f, 0.85f, 60.0f, ARTVG_FILL_NON_ZERO, 10},

    /* Heart shape */
    {PATH_TYPE_HEART, COLOR_RED, 100.0f, 50.0f, 1.0f, 1.0f, 0.0f, ARTVG_FILL_NON_ZERO, 10},
    {PATH_TYPE_HEART, COLOR_PURPLE, 80.0f, 40.0f, 1.1f, 1.1f, 15.0f, ARTVG_FILL_NON_ZERO, 10},

    /* Wave shape */
    {PATH_TYPE_WAVE, COLOR_BLUE, 50.0f, 100.0f, 1.0f, 1.0f, 0.0f, ARTVG_FILL_NON_ZERO, 10},
    {PATH_TYPE_WAVE, COLOR_CYAN, 80.0f, 120.0f, 0.9f, 0.9f, -20.0f, ARTVG_FILL_NON_ZERO, 10},

    /* Fill rule test with different fill rules */
    {PATH_TYPE_FILL_RULE, COLOR_WHITE, 100.0f, 100.0f, 1.0f, 1.0f, 0.0f, ARTVG_FILL_NON_ZERO, 10},
    {PATH_TYPE_FILL_RULE, COLOR_YELLOW, 100.0f, 100.0f, 1.0f, 1.0f, 0.0f, ARTVG_FILL_EVEN_ODD, 10},

    /* Different curve flat limits */
    {PATH_TYPE_CUBIC_BEZIER, COLOR_GREEN, 100.0f, 100.0f, 1.0f, 1.0f, 0.0f, ARTVG_FILL_NON_ZERO, 5},
    {PATH_TYPE_CUBIC_BEZIER, COLOR_RED, 100.0f, 100.0f, 1.0f, 1.0f, 0.0f, ARTVG_FILL_NON_ZERO, 15},
};

#define NUM_PATH_CONFIGS (sizeof(g_path_configs) / sizeof(g_path_configs[0]))

static int g_edge_pool_enabled = 1;
static uint32_t g_edge_pool_size = 512 * 1024;

/***********************
 *  PATH BUILDER HELPER
 **********************/
static artvg_error_t parse_and_build_path(struct artvg *vg, artvg_path_t **path,
                                          const float *data, size_t size)
{
    artvg_error_t ret;
    int count = size / sizeof(float);
    int i = 0;

    *path = artvg_path_allocate(vg);
    if (*path == NULL) return ARTVG_OUT_OF_MEMORY;

    while (i < count) {
        uint8_t opcode = (uint8_t)data[i];
        switch (opcode) {
        case VLC_OP_MOVE:
            ret = artvg_path_move_to(vg, *path, data[i+1], data[i+2]);
            i += 3;
            break;
        case VLC_OP_LINE:
            ret = artvg_path_line_to(vg, *path, data[i+1], data[i+2]);
            i += 3;
            break;
        case VLC_OP_QUAD:
            ret = artvg_path_quad_to(vg, *path, data[i+1], data[i+2], data[i+3], data[i+4]);
            i += 5;
            break;
        case VLC_OP_CUBIC:
            ret = artvg_path_cubic_to(vg, *path, data[i+1], data[i+2], data[i+3], data[i+4], data[i+5], data[i+6]);
            i += 7;
            break;
        default:
            i++;
            break;
        }
        if (ret != ARTVG_SUCCESS) return ret;
    }

    return artvg_path_finish(vg, *path);
}

/***********************
 *  HELPER FUNCTIONS
 **********************/
static void create_transform_matrix(artvg_matrix_t *matrix,
                                   float translate_x, float translate_y,
                                   float scale_x, float scale_y,
                                   float rotate_angle,
                                   float center_x, float center_y)
{
    artvg_matrix_identity(matrix);

    /* Translation */
    if (translate_x != 0.0f || translate_y != 0.0f)
        artvg_matrix_translate(translate_x, translate_y, matrix);

    /* Scale */
    if (scale_x != 1.0f || scale_y != 1.0f)
        artvg_matrix_scale(scale_x, scale_y, matrix);

    /* Rotation around center point */
    if (rotate_angle != 0.0f) {
        if (center_x != 0.0f || center_y != 0.0f) {
            artvg_point_t center = {center_x, center_y};
            artvg_matrix_rotate_point(&center, rotate_angle, matrix);
        } else {
            artvg_matrix_rotate(rotate_angle, matrix);
        }
    }
}

/***********************
 *  PATH OPERATION
 **********************/
static int perform_path_operation(struct artvg *vg, struct mpp_buf *dst,
                                  const path_config_t *config)
{
    if (!vg || !dst || !config) {
        printf("ERROR: vg=%p, dst=%p, config=%p\n", vg, dst, config);
        return -1;
    }

    const path_info_t *info = &g_path_info[config->path_type];
    artvg_path_t *path = NULL;
    artvg_matrix_t matrix;
    artvg_ctrl_t ctrl = {0};
    artvg_vector_ctl_t vector_ctl = {0};
    artvg_gradient_t gradient = {0};
    artvg_error_t ret;

    /* Create path */
    ret = parse_and_build_path(vg, &path, info->data, info->size);
    if (ret != ARTVG_SUCCESS) {
        printf("[ERROR] parse_and_build_path failed: %d\n", ret);
        return -1;
    }

    /* Setup transformation matrix */
    create_transform_matrix(&matrix,
                           config->translate_x, config->translate_y,
                           config->scale_x, config->scale_y,
                           config->rotate_angle,
                           info->default_center[0], info->default_center[1]);

    /* Setup control parameters */
    ctrl.alpha_en = 0;

    vector_ctl.fill_rule = config->fill_rule;
    vector_ctl.curve_flat_limit = config->curve_flat_limit;

    /* Setup gradient (solid color for stress test) */
    gradient.colors[0] = config->color;
    gradient.count = 1;
    gradient.stops[0] = 0;
    gradient.precision = ARTVG_GRADIENT_PRECISION_NONE;
    gradient.spread = ARTVG_GRADIENT_SPREAD_NONE;

    /* Draw path */
    ret = artvg_draw_path(vg, dst, path, &matrix, &ctrl,
                          &gradient, &vector_ctl);
    if (ret != ARTVG_SUCCESS) {
        printf("[ERROR] artvg_draw_path failed: %d\n", ret);
        artvg_path_free(vg, path);
        return -1;
    }

    /* Flush and wait */
    ret = artvg_flush(vg);
    if (ret != ARTVG_SUCCESS) {
        printf("[ERROR] artvg_flush failed: %d\n", ret);
        artvg_path_free(vg, path);
        return -1;
    }

    ret = artvg_wait_finish(vg);
    if (ret != ARTVG_SUCCESS) {
        printf("[ERROR] artvg_wait_finish failed: %d\n", ret);
        artvg_path_free(vg, path);
        return -1;
    }

    /* Destroy path after drawing */
    ret = artvg_path_free(vg, path);
    if (ret != ARTVG_SUCCESS) {
        printf("[ERROR] artvg_path_free failed: %d\n", ret);
        return -1;
    }

    return 0;
}

/***********************
 *  TEST OPS IMPLEMENTATION
 **********************/
static int path_draw_callback(void *thread_ctx, int buffer_idx)
{
    stress_thread_context_t *tctx = (stress_thread_context_t *)thread_ctx;
    stress_render_context_t *ctx = tctx->ctx;

    int config_idx = tctx->current_param_index % NUM_PATH_CONFIGS;

    /* Initialize private_buf to zero to avoid undefined values */
    memset(&tctx->private_buf, 0, sizeof(struct mpp_buf));

    /* Copy shared buffer to private buffer (thread-safe, no lock needed) */
    memcpy(&tctx->private_buf, &ctx->draw_buffers[buffer_idx], sizeof(struct mpp_buf));

    /* Execute path operation with private buffer (completely lock-free) */
    if (perform_path_operation(tctx->vg, &tctx->private_buf, &g_path_configs[config_idx]) != 0) {
        printf("[Thread %d] Path operation FAILED at param_index=%d, buffer_idx=%d\n",
               tctx->thread_id, tctx->current_param_index, buffer_idx);
        stress_record_failure(tctx, STRESS_FAIL_REASON_OP_FAILED);
        return -1;
    }

    return 0;
}

static const char *path_fail_reason_to_string(int reason)
{
    switch (reason) {
    case STRESS_FAIL_REASON_NO_BUFFER:
        return "Failed to get drawable buffer";
    case STRESS_FAIL_REASON_OP_FAILED:
        return "Path operation failed";
    default:
        return "Unknown failure";
    }
}

/***********************
 *  THREAD RESOURCE MANAGEMENT
 **********************/
static int path_init_thread_resources(void *thread_ctx)
{
    stress_thread_context_t *tctx = (stress_thread_context_t *)thread_ctx;
    artvg_error_t ret;

    /* Pre-allocate edge buffer pool to reduce DMA allocation pressure */
    if (g_edge_pool_enabled) {
        ret = artvg_enable_edge_buffer_pool(tctx->vg, g_edge_pool_size);
        if (ret != ARTVG_SUCCESS) {
            printf("[Thread %d] Warning: artvg_enable_edge_buffer_pool failed: %d\n",
                   tctx->thread_id, ret);
        }
    }

    return 0;
}

static void path_cleanup_thread_resources(void *thread_ctx)
{
    stress_thread_context_t *tctx = (stress_thread_context_t *)thread_ctx;

    if (g_edge_pool_enabled)
        artvg_disable_edge_buffer_pool(tctx->vg);
}

/***********************
 *  CUSTOM OPTIONS
 **********************/
static const struct option g_path_custom_options[] = {
    {"edge-pool",      required_argument, 0, 256},
    {"edge-pool-size", required_argument, 0, 257},
    {0, 0, 0, 0}
};

static int path_handle_custom_option(int val, const char *optarg)
{
    switch (val) {
    case 256: g_edge_pool_enabled = atoi(optarg); break;
    case 257: g_edge_pool_size    = (uint32_t)atoi(optarg); break;
    default:  return -1;
    }
    return 0;
}

/***********************
 *  TEST OPS TABLE
 **********************/
static const stress_test_ops_t g_path_ops = {
    .test_name = "ArtVG Path Concurrent Stress Test",

    .init_test_resources = NULL,
    .init_thread_resources = path_init_thread_resources,
    .cleanup_thread_resources = path_cleanup_thread_resources,
    .cleanup_test_resources = NULL,
    .draw_callback = path_draw_callback,
    .collect_and_print_results = NULL,
    .fail_reason_to_string = path_fail_reason_to_string,
    .custom_long_options = g_path_custom_options,
    .handle_custom_option = path_handle_custom_option,
    .custom_usage_text =
        "Test-specific options:\n"
        "  --edge-pool <0|1>        Enable edge buffer pool (default: 1)\n"
        "  --edge-pool-size <bytes> Edge buffer pool size (default: 512K)\n",
};

/***********************
 *  MAIN ENTRY POINT
 **********************/
int main(int argc, char **argv)
{
    return stress_run_test(argc, argv, &g_path_ops);
}
