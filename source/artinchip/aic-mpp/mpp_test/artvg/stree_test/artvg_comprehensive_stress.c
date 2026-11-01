/*
 * Copyright (c) 2026, ArtInChip Technology Co., Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Authors:  zequan liang <zequan.liang@artinchip.com>
 *
 * ArtVG Comprehensive Stress Test
 * Multi-instance concurrent stress test with random module selection
 * Randomly calls various ArtVG functions (clear, blit, affine, projective, path, gradient)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <time.h>
#include "artvg_stress_framework.h"
#include "image_array.h"

/***********************
 *  MODULE TYPE DEFINITIONS
 **********************/
typedef enum {
    COMP_STRESS_CLEAR = 0,
    COMP_STRESS_BLIT,
    COMP_STRESS_AFFINE,
    COMP_STRESS_PROJECTIVE,
    COMP_STRESS_PATH,
    COMP_STRESS_GRADIENT,
    COMP_STRESS_TYPE_COUNT
} comp_stress_type_t;

/***********************
 *  EXTERNAL IMAGE DATA
 **********************/
extern const image_dsc_t alpha_baby_card_240x120;

/***********************
 *  GLOBAL CONFIGURATION
 **********************/
static int g_module_enabled[COMP_STRESS_TYPE_COUNT] = {1, 1, 1, 1, 1, 1};
static int g_module_weights[COMP_STRESS_TYPE_COUNT] = {10, 10, 10, 10, 10, 10};
static uint64_t g_module_call_count[COMP_STRESS_TYPE_COUNT] = {0};
static int g_edge_pool_enabled = 1;
static uint32_t g_edge_pool_size = 512 * 1024;

/***********************
 *  CLEAR MODULE CONFIG
 **********************/
static const stress_crop_config_t g_clear_crop_configs[] = {
    {0, 0, 0, 123, 456},
    {1, 0, 0, 640, 480},
    {1, 100, 100, 320, 240},
    {1, 200, 150, 800, 400},
    {1, 50, 50, 920, 530},
    {1, 0, 0, 1024, 600},
    {1, 150, 200, 400, 300},
    {1, 300, 100, 600, 450}
};

static const uint32_t g_clear_colors[] = {
    0xFFFF0000, 0xFF00FF00, 0xFF0000FF, 0xFFFFFFFF,
    0xFF000000, 0xFFFFFF00, 0xFFFF00FF, 0xFF00FFFF
};

#define NUM_CLEAR_CROP_CONFIGS (sizeof(g_clear_crop_configs) / sizeof(g_clear_crop_configs[0]))
#define NUM_CLEAR_COLORS (sizeof(g_clear_colors) / sizeof(g_clear_colors[0]))

/***********************
 *  BLIT MODULE CONFIG
 **********************/
#define BLIT_SRC_WIDTH  240
#define BLIT_SRC_HEIGHT 120

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
    {ARTVG_ROTATE_90, ARTVG_MIRROR_NONE, 1, 100, 100, 120, 240, 1, ARTVG_ALPHA_PIXEL},
    {ARTVG_ROTATE_180, ARTVG_MIRROR_NONE, 1, 200, 150, 240, 120, 1, ARTVG_ALPHA_GLOBAL},
    {ARTVG_ROTATE_270, ARTVG_MIRROR_NONE, 1, 300, 200, 120, 240, 1, ARTVG_ALPHA_GLOBAL},
    {ARTVG_ROTATE_0, ARTVG_MIRROR_HORIZONTAL, 1, 100, 50, 240, 120, 0, 0},
    {ARTVG_ROTATE_0, ARTVG_MIRROR_VERTICAL, 1, 200, 100, 240, 120, 0, 0},
    {ARTVG_ROTATE_90, ARTVG_MIRROR_HORIZONTAL, 1, 150, 150, 120, 240, 1, ARTVG_ALPHA_MIXED},
    {ARTVG_ROTATE_270, ARTVG_MIRROR_VERTICAL, 1, 250, 180, 120, 240, 1, ARTVG_ALPHA_MIXED}
};

#define NUM_BLIT_CONFIGS (sizeof(g_blit_configs) / sizeof(g_blit_configs[0]))

/***********************
 *  AFFINE MODULE CONFIG
 **********************/
#define MAX_AFFINE_CONFIGS 10

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
} affine_config_t;

static const affine_config_t g_affine_configs_default[] = {
    {0.0f, 0.0f, 1.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0, 0, 0, 0, 0, 0, 0},
    {100.0f, 50.0f, 1.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0, 0, 0, 0, 0, 0, 0},
    {0.0f, 0.0f, 1.5f, 1.5f, 0.0f, 0.0f, 0.0f, 0, 0, 0, 0, 0, 0, 0},
    {0.0f, 0.0f, 0.5f, 0.5f, 0.0f, 0.0f, 0.0f, 0, 0, 0, 0, 0, 0, 0},
    {0.0f, 0.0f, 1.0f, 1.0f, 45.0f, 0.0f, 0.0f, 0, 0, 0, 0, 0, 0, 0},
    {0.0f, 0.0f, 1.0f, 1.0f, 90.0f, 0.0f, 0.0f, 0, 0, 0, 0, 0, 0, 0},
    {0.0f, 0.0f, 1.0f, 1.0f, 0.0f, 11.0f, 0.0f, 0, 0, 0, 0, 0, 0, 0},
    {0.0f, 0.0f, 1.0f, 1.0f, 0.0f, 0.0f, 11.0f, 0, 0, 0, 0, 0, 0, 0},
    {50.0f, 50.0f, 1.2f, 1.2f, 30.0f, 6.0f, 6.0f, 0, 0, 0, 0, 0, 1, ARTVG_BLEND_SRC_OVER},
    {-50.0f, -50.0f, 0.8f, 0.8f, -30.0f, -6.0f, -6.0f, 0, 0, 0, 0, 0, 1, ARTVG_BLEND_SRC_OVER}
};

static affine_config_t g_affine_configs[MAX_AFFINE_CONFIGS];
static int g_num_affine_configs = 0;

/***********************
 *  PROJECTIVE MODULE CONFIG
 **********************/
#define MAX_PROJECTIVE_CONFIGS 10

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
    int lut_mode;
    int block_size;
    int alpha_en;
    int alpha_rules;
    int crop_en;
    int crop_x;
    int crop_y;
    int crop_width;
    int crop_height;
} projective_config_t;

static const projective_config_t g_projective_configs_default[] = {
    {50.0f, 30.0f, 1.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 5, 1, 0, 0, 0, 0, 0, 0, 0},
    {0.0f, 0.0f, 1.0f, 1.0f, 30.0f, 0.0f, 0.0f, 0.0f, 0.0f, 5, 1, 0, 0, 0, 0, 0, 0, 0},
    {0.0f, 0.0f, 1.5f, 1.5f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 5, 1, 0, 0, 0, 0, 0, 0, 0},
    {0.0f, 0.0f, 1.0f, 1.0f, 0.0f, 17.0f, 11.0f, 0.0f, 0.0f, 5, 1, 0, 0, 0, 0, 0, 0, 0},
    {0.0f, 0.0f, 1.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0001f, 0.0001f, 5, 1, 0, 0, 0, 0, 0, 0, 0},
    {20.0f, 10.0f, 1.2f, 0.8f, 15.0f, 6.0f, 6.0f, 0.00005f, 0.00003f, 5, 1, 0, 0, 0, 0, 0, 0, 0},
    {0.0f, 0.0f, 1.0f, 1.0f, 90.0f, 0.0f, 0.0f, 0.0f, 0.0f, 5, 1, 0, 0, 1, 0, 0, 120, 120},
    {30.0f, 20.0f, 1.0f, 1.0f, 45.0f, 0.0f, 0.0f, 0.0f, 0.0f, 5, 1, 1, 0, 0, 0, 0, 0, 0},
    {10.0f, 10.0f, 0.9f, 0.9f, 25.0f, 0.0f, 0.0f, 0.00003f, 0.00003f, 5, 0, 0, 0, 0, 0, 0, 0, 0},
    {-20.0f, -10.0f, 1.1f, 1.1f, -20.0f, 0.0f, 0.0f, 0.00006f, 0.00004f, 5, 2, 1, 0, 0, 0, 0, 0, 0}
};

static projective_config_t g_projective_configs[MAX_PROJECTIVE_CONFIGS];
static int g_num_projective_configs = 0;

/***********************
 *  PATH MODULE CONFIG
 **********************/
#define VLC_OP_MOVE         0x02
#define VLC_OP_LINE         0x04
#define VLC_OP_QUAD         0x06
#define VLC_OP_CUBIC        0x08

typedef enum {
    PATH_TYPE_TRIANGLE = 0,
    PATH_TYPE_QUAD_BEZIER,
    PATH_TYPE_CUBIC_BEZIER,
    PATH_TYPE_HEART,
    PATH_TYPE_WAVE,
    PATH_TYPE_COUNT
} path_type_t;

static float g_path_triangle[] = {
    (float)(VLC_OP_MOVE), 0.0f, 400.0f,
    (float)(VLC_OP_LINE), 400.0f, 400.0f,
    (float)(VLC_OP_LINE), 200.0f, 0.0f,
    (float)(VLC_OP_LINE), 0.0f, 400.0f,
};

static float g_path_quad_bezier[] = {
    (float)(VLC_OP_MOVE), 0.0f, 400.0f,
    (float)(VLC_OP_LINE), 400.0f, 400.0f,
    (float)(VLC_OP_QUAD), 400.0f, 200.0f, 200.0f, 0.0f,
    (float)(VLC_OP_QUAD), 0.0f, 200.0f, 0.0f, 400.0f,
};

static float g_path_cubic_bezier[] = {
    (float)(VLC_OP_MOVE), 200.0f, 400.0f,
    (float)(VLC_OP_LINE), 300.0f, 300.0f,
    (float)(VLC_OP_CUBIC), 254.0f, 228.0f, 365.0f, 190.0f, 300.0f, 100.0f,
    (float)(VLC_OP_CUBIC), 300.0f, 197.0f, 200.0f, 106.0f, 200.0f, 0.0f,
    (float)(VLC_OP_CUBIC), 132.0f, 0.0f, 158.0f, 187.0f, 100.0f, 100.0f,
    (float)(VLC_OP_CUBIC), 0.0f, 100.0f, 200.0f, 300.0f, 100.0f, 300.0f,
    (float)(VLC_OP_LINE), 200.0f, 400.0f,
};

static float g_path_heart[] = {
    (float)(VLC_OP_MOVE), 200.0f, 100.0f,
    (float)(VLC_OP_CUBIC), 200.0f, 50.0f, 150.0f, 0.0f, 100.0f, 0.0f,
    (float)(VLC_OP_CUBIC), 50.0f, 0.0f, 0.0f, 50.0f, 0.0f, 100.0f,
    (float)(VLC_OP_CUBIC), 0.0f, 180.0f, 100.0f, 280.0f, 200.0f, 380.0f,
    (float)(VLC_OP_CUBIC), 300.0f, 280.0f, 400.0f, 180.0f, 400.0f, 100.0f,
    (float)(VLC_OP_CUBIC), 400.0f, 50.0f, 350.0f, 0.0f, 300.0f, 0.0f,
    (float)(VLC_OP_CUBIC), 250.0f, 0.0f, 200.0f, 50.0f, 200.0f, 100.0f,
};

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
};

typedef struct {
    path_type_t path_type;
    uint32_t color;
    float translate_x;
    float translate_y;
    float scale_x;
    float scale_y;
    float rotate_angle;
} path_config_t;

static const path_config_t g_path_configs[] = {
    {PATH_TYPE_TRIANGLE, 0xFFFF0000, 100.0f, 100.0f, 1.0f, 1.0f, 0.0f},
    {PATH_TYPE_TRIANGLE, 0xFF0000FF, 200.0f, 150.0f, 0.8f, 0.8f, 45.0f},
    {PATH_TYPE_TRIANGLE, 0xFF00FF00, 50.0f, 50.0f, 1.2f, 1.2f, 90.0f},
    {PATH_TYPE_QUAD_BEZIER, 0xFFFFFF00, 100.0f, 100.0f, 1.0f, 1.0f, 0.0f},
    {PATH_TYPE_QUAD_BEZIER, 0xFF00FFFF, 150.0f, 100.0f, 0.9f, 0.9f, 30.0f},
    {PATH_TYPE_CUBIC_BEZIER, 0xFFFF00FF, 100.0f, 50.0f, 1.0f, 1.0f, 0.0f},
    {PATH_TYPE_CUBIC_BEZIER, 0xFFFFA500, 120.0f, 80.0f, 0.85f, 0.85f, 60.0f},
    {PATH_TYPE_HEART, 0xFFFF0000, 100.0f, 50.0f, 1.0f, 1.0f, 0.0f},
    {PATH_TYPE_HEART, 0xFF800080, 80.0f, 40.0f, 1.1f, 1.1f, 15.0f},
    {PATH_TYPE_WAVE, 0xFF0000FF, 50.0f, 100.0f, 1.0f, 1.0f, 0.0f},
    {PATH_TYPE_WAVE, 0xFF00FFFF, 80.0f, 120.0f, 0.9f, 0.9f, -20.0f}
};

#define NUM_PATH_CONFIGS (sizeof(g_path_configs) / sizeof(g_path_configs[0]))

/***********************
 *  GRADIENT MODULE CONFIG
 **********************/
typedef struct {
    int32_t start_x;
    int32_t start_y;
    int32_t end_x;
    int32_t end_y;
    uint32_t colors[4];
    uint8_t stops[4];
    uint32_t count;
    artvg_gradient_precision_t precision;
    artvg_gradient_spread_t spread;
} gradient_config_t;

static const gradient_config_t g_gradient_configs[] = {
    {0, 0, 1024, 600,
     {0xFFFF0000, 0xFF0000FF}, {0, 255}, 2,
     ARTVG_GRADIENT_PRECISION_256, ARTVG_GRADIENT_SPREAD_PAD},
    {100, 100, 924, 500,
     {0xFF00FF00, 0xFFFFFF00}, {0, 255}, 2,
     ARTVG_GRADIENT_PRECISION_64, ARTVG_GRADIENT_SPREAD_REFLECT},
    {0, 0, 800, 600,
     {0xFFFF0000, 0xFF00FF00, 0xFF0000FF}, {0, 128, 255}, 3,
     ARTVG_GRADIENT_PRECISION_16, ARTVG_GRADIENT_SPREAD_REPEAT},
    {0, 0, 1024, 600,
     {0xFFFFFFFF, 0xFF000000}, {0, 255}, 2,
     ARTVG_GRADIENT_PRECISION_256, ARTVG_GRADIENT_SPREAD_PAD},
    {0, 600, 1024, 0,
     {0xFFFF00FF, 0xFF00FFFF}, {0, 255}, 2,
     ARTVG_GRADIENT_PRECISION_256, ARTVG_GRADIENT_SPREAD_PAD},
    {0, 0, 1024, 600,
     {0xFFFF0000, 0xFFFFFF00, 0xFF00FF00, 0xFF0000FF},
     {0, 84, 168, 255}, 4,
     ARTVG_GRADIENT_PRECISION_64, ARTVG_GRADIENT_SPREAD_PAD},
    {200, 200, 600, 400,
     {0xFF0000FF, 0xFFFF0000}, {0, 255}, 2,
     ARTVG_GRADIENT_PRECISION_256, ARTVG_GRADIENT_SPREAD_NONE},
    {0, 0, 1024, 600,
     {0xFFFFFF00, 0xFF00FF00}, {0, 255}, 2,
     ARTVG_GRADIENT_PRECISION_256, ARTVG_GRADIENT_SPREAD_PAD},
    /* Test ARTVG_GRADIENT_PRECISION_NONE - solid color fill */
    {100, 100, 924, 500,
     {0xFF00FF00, 0xFFFF0000}, {0, 255}, 1,
     ARTVG_GRADIENT_PRECISION_NONE, ARTVG_GRADIENT_SPREAD_NONE}
};

#define NUM_GRADIENT_CONFIGS (sizeof(g_gradient_configs) / sizeof(g_gradient_configs[0]))

/***********************
 *  THREAD DATA STRUCTURE
 **********************/
typedef struct {
    struct mpp_buf *src_buf;
    comp_stress_type_t current_type;
} comp_stress_thread_data_t;

/***********************
 *  HELPER FUNCTIONS
 **********************/
static int get_total_weight(void)
{
    int total = 0;
    for (int i = 0; i < COMP_STRESS_TYPE_COUNT; i++) {
        if (g_module_enabled[i])
            total += g_module_weights[i];
    }
    return total;
}

static comp_stress_type_t random_select_module(void)
{
    int total_weight = get_total_weight();

    if (total_weight <= 0) {
        return COMP_STRESS_CLEAR;
    }

    int random_val = rand() % total_weight;
    int cumulative = 0;

    for (int i = 0; i < COMP_STRESS_TYPE_COUNT; i++) {
        if (!g_module_enabled[i]) continue;
        cumulative += g_module_weights[i];
        if (random_val < cumulative)
            return (comp_stress_type_t)i;
    }

    return COMP_STRESS_CLEAR;
}

static const char *module_type_to_string(comp_stress_type_t type)
{
    switch (type) {
    case COMP_STRESS_CLEAR:       return "clear";
    case COMP_STRESS_BLIT:        return "blit";
    case COMP_STRESS_AFFINE:      return "affine";
    case COMP_STRESS_PROJECTIVE:  return "projective";
    case COMP_STRESS_PATH:        return "path";
    case COMP_STRESS_GRADIENT:    return "gradient";
    default:                      return "unknown";
    }
}

/***********************
 *  SOURCE BUFFER MANAGEMENT
 **********************/
static int init_source_buffer(struct artvg *vg, struct mpp_buf **src_buf_out)
{
    struct mpp_buf *src_buf;
    void *src_mmap;
    size_t map_size;

    src_buf = artvg_allocate(vg, BLIT_SRC_WIDTH, BLIT_SRC_HEIGHT, MPP_FMT_ARGB_8888);
    if (src_buf == NULL) {
        return -1;
    }

    map_size = src_buf->stride[0] * src_buf->size.height;
    src_mmap = mmap(NULL, map_size, PROT_READ | PROT_WRITE, MAP_SHARED, src_buf->fd[0], 0);
    if (src_mmap == MAP_FAILED) {
        artvg_free(vg, src_buf);
        return -1;
    }

    if (image_array_read(&alpha_baby_card_240x120, src_mmap, map_size) != 0) {
        munmap(src_mmap, map_size);
        artvg_free(vg, src_buf);
        return -1;
    }

    munmap(src_mmap, map_size);
    *src_buf_out = src_buf;
    return 0;
}

static void cleanup_source_buffer(struct artvg *vg, struct mpp_buf *src_buf)
{
    if (src_buf) {
        artvg_free(vg, src_buf);
    }
}

/***********************
 *  MODULE OPERATIONS
 **********************/

/* Clear module */
static int clear_draw_callback(stress_thread_context_t *tctx, int buffer_idx)
{
    stress_render_context_t *ctx = tctx->ctx;

    int color_idx = tctx->current_param_index % NUM_CLEAR_COLORS;
    int crop_idx = tctx->current_param_index % NUM_CLEAR_CROP_CONFIGS;

    memset(&tctx->private_buf, 0, sizeof(struct mpp_buf));
    memcpy(&tctx->private_buf, &ctx->draw_buffers[buffer_idx], sizeof(struct mpp_buf));

    tctx->private_buf.crop_en = g_clear_crop_configs[crop_idx].crop_en;
    tctx->private_buf.crop.x = g_clear_crop_configs[crop_idx].crop_x;
    tctx->private_buf.crop.y = g_clear_crop_configs[crop_idx].crop_y;
    tctx->private_buf.crop.width = g_clear_crop_configs[crop_idx].crop_width;
    tctx->private_buf.crop.height = g_clear_crop_configs[crop_idx].crop_height;

    uint32_t color = g_clear_colors[color_idx];

    if (artvg_clear(tctx->vg, &tctx->private_buf, color) != ARTVG_SUCCESS ||
        artvg_flush(tctx->vg) != ARTVG_SUCCESS ||
        artvg_wait_finish(tctx->vg) != ARTVG_SUCCESS) {
        return -1;
    }

    return 0;
}

/* Blit module */
static int blit_draw_callback(stress_thread_context_t *tctx, int buffer_idx)
{
    stress_render_context_t *ctx = tctx->ctx;
    comp_stress_thread_data_t *data = (comp_stress_thread_data_t *)tctx->user_data;
    struct mpp_buf *src_buf = data->src_buf;

    int blit_idx = tctx->current_param_index % NUM_BLIT_CONFIGS;
    blit_config_t config;
    memcpy(&config, &g_blit_configs[blit_idx], sizeof(blit_config_t));

    memset(&tctx->private_buf, 0, sizeof(struct mpp_buf));
    memcpy(&tctx->private_buf, &ctx->draw_buffers[buffer_idx], sizeof(struct mpp_buf));

    tctx->private_buf.crop_en = config.crop_en;
    tctx->private_buf.crop.x = config.crop_x;
    tctx->private_buf.crop.y = config.crop_y;
    tctx->private_buf.crop.width = config.crop_width;
    tctx->private_buf.crop.height = config.crop_height;

    artvg_ctrl_t ctrl = {0};
    artvg_blit_ctl_t blit_ctl = {0};

    blit_ctl.rotate = config.rotate;
    blit_ctl.mirror = config.mirror;
    ctrl.alpha_en = config.alpha_en;
    ctrl.src_alpha_mode = config.alpha_mode;

    if (artvg_blit(tctx->vg, src_buf, &tctx->private_buf, NULL, &ctrl, &blit_ctl) != ARTVG_SUCCESS ||
        artvg_flush(tctx->vg) != ARTVG_SUCCESS ||
        artvg_wait_finish(tctx->vg) != ARTVG_SUCCESS) {
        return -1;
    }

    return 0;
}

/* Affine module */
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

static int affine_draw_callback(stress_thread_context_t *tctx, int buffer_idx)
{
    stress_render_context_t *ctx = tctx->ctx;
    comp_stress_thread_data_t *data = (comp_stress_thread_data_t *)tctx->user_data;
    struct mpp_buf *src_buf = data->src_buf;

    int affine_idx = tctx->current_param_index % g_num_affine_configs;
    affine_config_t config;
    memcpy(&config, &g_affine_configs[affine_idx], sizeof(affine_config_t));

    memset(&tctx->private_buf, 0, sizeof(struct mpp_buf));
    memcpy(&tctx->private_buf, &ctx->draw_buffers[buffer_idx], sizeof(struct mpp_buf));

    tctx->private_buf.crop_en = config.crop_en;
    tctx->private_buf.crop.x = config.crop_x;
    tctx->private_buf.crop.y = config.crop_y;
    tctx->private_buf.crop.width = config.crop_width;
    tctx->private_buf.crop.height = config.crop_height;

    artvg_ctrl_t ctrl = {0};
    artvg_transform_edge_t transform_edge = {0};
    artvg_matrix_t matrix;

    ctrl.alpha_en = config.alpha_en;
    ctrl.alpha_rules = config.alpha_rules;

    transform_edge.fill_en = 0;
    transform_edge.blend_en = 0;
    transform_edge.fill_color = 0xFF000000;
    transform_edge.pad_mode = ARTVG_PAD_MODE_FIXED_COLOR;

    create_affine_matrix(&matrix, &config);

    if (artvg_affine(tctx->vg, src_buf, &tctx->private_buf, NULL, &matrix, &ctrl, &transform_edge) != ARTVG_SUCCESS ||
        artvg_flush(tctx->vg) != ARTVG_SUCCESS ||
        artvg_wait_finish(tctx->vg) != ARTVG_SUCCESS) {
        return -1;
    }

    return 0;
}

/* Projective module */
static void create_projective_matrix(artvg_matrix_t *matrix, const projective_config_t *config)
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

    if (config->perspective_x != 0.0f || config->perspective_y != 0.0f)
        artvg_matrix_perspective(config->perspective_x, config->perspective_y, matrix);
}

static int projective_draw_callback(stress_thread_context_t *tctx, int buffer_idx)
{
    stress_render_context_t *ctx = tctx->ctx;
    comp_stress_thread_data_t *data = (comp_stress_thread_data_t *)tctx->user_data;
    struct mpp_buf *src_buf = data->src_buf;

    int config_idx = tctx->current_param_index % g_num_projective_configs;
    projective_config_t config;
    memcpy(&config, &g_projective_configs[config_idx], sizeof(projective_config_t));

    memset(&tctx->private_buf, 0, sizeof(struct mpp_buf));
    memcpy(&tctx->private_buf, &ctx->draw_buffers[buffer_idx], sizeof(struct mpp_buf));

    tctx->private_buf.crop_en = config.crop_en;
    tctx->private_buf.crop.x = config.crop_x;
    tctx->private_buf.crop.y = config.crop_y;
    tctx->private_buf.crop.width = config.crop_width;
    tctx->private_buf.crop.height = config.crop_height;

    artvg_ctrl_t ctrl = {0};
    artvg_transform_edge_t transform_edge = {0};
    artvg_projective_t *projective = NULL;
    artvg_matrix_t matrix;

    create_projective_matrix(&matrix, &config);

    ctrl.alpha_en = config.alpha_en;
    ctrl.alpha_rules = config.alpha_rules;

    transform_edge.fill_en = 0;
    transform_edge.blend_en = 0;
    transform_edge.fill_color = 0xFF000000;
    transform_edge.pad_mode = ARTVG_PAD_MODE_FIXED_COLOR;

    projective = artvg_projective_allocate(
            tctx->vg, &matrix, &transform_edge,
            (artvg_projective_lut_mode_t)config.lut_mode,
            (artvg_projective_lut_block_size_t)config.block_size,
            ARTVG_PROJECTIVE_OUT_BLK_MODE_16X16,
            tctx->private_buf.size.width, tctx->private_buf.size.height);
    if (projective == NULL) {
        return -1;
    }

    int ret = 0;
    if (artvg_projective2(tctx->vg, src_buf, &tctx->private_buf, &tctx->private_buf, &ctrl, projective) != ARTVG_SUCCESS ||
        artvg_flush(tctx->vg) != ARTVG_SUCCESS ||
        artvg_wait_finish(tctx->vg) != ARTVG_SUCCESS) {
        ret = -1;
    }

    artvg_projective_free(tctx->vg, projective);
    return ret;
}

/* Path module */
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

static void create_path_transform_matrix(artvg_matrix_t *matrix,
                                        float translate_x, float translate_y,
                                        float scale_x, float scale_y,
                                        float rotate_angle,
                                        float center_x, float center_y)
{
    artvg_matrix_identity(matrix);

    if (translate_x != 0.0f || translate_y != 0.0f)
        artvg_matrix_translate(translate_x, translate_y, matrix);

    if (scale_x != 1.0f || scale_y != 1.0f)
        artvg_matrix_scale(scale_x, scale_y, matrix);

    if (rotate_angle != 0.0f) {
        artvg_point_t center = {center_x, center_y};
        artvg_matrix_rotate_point(&center, rotate_angle, matrix);
    }
}

static int path_draw_callback(stress_thread_context_t *tctx, int buffer_idx)
{
    stress_render_context_t *ctx = tctx->ctx;

    int config_idx = tctx->current_param_index % NUM_PATH_CONFIGS;
    path_config_t config;
    memcpy(&config, &g_path_configs[config_idx], sizeof(path_config_t));

    memset(&tctx->private_buf, 0, sizeof(struct mpp_buf));
    memcpy(&tctx->private_buf, &ctx->draw_buffers[buffer_idx], sizeof(struct mpp_buf));

    path_info_t *info = &g_path_info[config.path_type];
    artvg_path_t *path = NULL;
    artvg_matrix_t matrix;
    artvg_ctrl_t ctrl = {0};
    artvg_vector_ctl_t vector_ctl = {0};
    artvg_gradient_t gradient = {0};

    if (parse_and_build_path(tctx->vg, &path, info->data, info->size) != ARTVG_SUCCESS) {
        return -1;
    }

    create_path_transform_matrix(&matrix,
                                config.translate_x, config.translate_y,
                                config.scale_x, config.scale_y,
                                config.rotate_angle,
                                info->default_center[0], info->default_center[1]);

    ctrl.alpha_en = 0;

    vector_ctl.fill_rule = ARTVG_FILL_NON_ZERO;
    vector_ctl.curve_flat_limit = 10;

    gradient.colors[0] = config.color;
    gradient.count = 1;
    gradient.stops[0] = 0;
    gradient.precision = ARTVG_GRADIENT_PRECISION_NONE;
    gradient.spread = ARTVG_GRADIENT_SPREAD_NONE;

    int ret = 0;
    if (artvg_draw_path(tctx->vg, &tctx->private_buf, path, &matrix, &ctrl,
                        &gradient, &vector_ctl) != ARTVG_SUCCESS ||
        artvg_flush(tctx->vg) != ARTVG_SUCCESS ||
        artvg_wait_finish(tctx->vg) != ARTVG_SUCCESS) {
        ret = -1;
    }

    artvg_path_free(tctx->vg, path);
    return ret;
}

/* Gradient module */
static int gradient_draw_callback(stress_thread_context_t *tctx, int buffer_idx)
{
    stress_render_context_t *ctx = tctx->ctx;

    int gradient_idx = tctx->current_param_index % NUM_GRADIENT_CONFIGS;
    gradient_config_t grad_cfg;
    memcpy(&grad_cfg, &g_gradient_configs[gradient_idx], sizeof(gradient_config_t));

    memset(&tctx->private_buf, 0, sizeof(struct mpp_buf));
    memcpy(&tctx->private_buf, &ctx->draw_buffers[buffer_idx], sizeof(struct mpp_buf));

    artvg_gradient_t gradient = {0};
    artvg_ctrl_t ctrl = {0};

    gradient.start.x = grad_cfg.start_x;
    gradient.start.y = grad_cfg.start_y;
    gradient.end.x = grad_cfg.end_x;
    gradient.end.y = grad_cfg.end_y;
    gradient.count = grad_cfg.count;
    gradient.precision = grad_cfg.precision;
    gradient.spread = grad_cfg.spread;

    for (uint32_t i = 0; i < grad_cfg.count; i++) {
        gradient.colors[i] = grad_cfg.colors[i];
        gradient.stops[i] = grad_cfg.stops[i];
    }

    memset(&ctrl, 0, sizeof(artvg_ctrl_t));
    ctrl.alpha_en = 0;

    if (artvg_fill_gradient(tctx->vg, &tctx->private_buf, &ctrl, &gradient) != ARTVG_SUCCESS ||
        artvg_flush(tctx->vg) != ARTVG_SUCCESS ||
        artvg_wait_finish(tctx->vg) != ARTVG_SUCCESS) {
        return -1;
    }

    return 0;
}

/* Module dispatch table */
static int (*g_module_draw_callbacks[COMP_STRESS_TYPE_COUNT])(stress_thread_context_t *, int) = {
    [COMP_STRESS_CLEAR] = clear_draw_callback,
    [COMP_STRESS_BLIT] = blit_draw_callback,
    [COMP_STRESS_AFFINE] = affine_draw_callback,
    [COMP_STRESS_PROJECTIVE] = projective_draw_callback,
    [COMP_STRESS_PATH] = path_draw_callback,
    [COMP_STRESS_GRADIENT] = gradient_draw_callback,
};

/***********************
 *  THREAD RESOURCE MANAGEMENT
 **********************/
static int comprehensive_init_thread_resources(void *thread_ctx)
{
    stress_thread_context_t *tctx = (stress_thread_context_t *)thread_ctx;
    comp_stress_thread_data_t *data;
    artvg_error_t ret;

    data = calloc(1, sizeof(comp_stress_thread_data_t));
    if (!data) {
        printf("[Thread %d] Failed to allocate thread data\n", tctx->thread_id);
        return -1;
    }

    /* Pre-allocate edge buffer pool to reduce DMA allocation pressure */
    if (g_edge_pool_enabled) {
        ret = artvg_enable_edge_buffer_pool(tctx->vg, g_edge_pool_size);
        if (ret != ARTVG_SUCCESS) {
            printf("[Thread %d] Warning: artvg_enable_edge_buffer_pool failed: %d\n",
                   tctx->thread_id, ret);
        }
    }

    /* Check if any module requiring source buffer is enabled */
    int need_src_buf = g_module_enabled[COMP_STRESS_BLIT] ||
                       g_module_enabled[COMP_STRESS_AFFINE] ||
                       g_module_enabled[COMP_STRESS_PROJECTIVE];

    if (need_src_buf) {
        if (init_source_buffer(tctx->vg, &data->src_buf) != 0) {
            printf("[Thread %d] Failed to initialize source buffer\n", tctx->thread_id);
            free(data);
            return -1;
        }
    }

    tctx->user_data = data;
    return 0;
}

static void comprehensive_cleanup_thread_resources(void *thread_ctx)
{
    stress_thread_context_t *tctx = (stress_thread_context_t *)thread_ctx;
    comp_stress_thread_data_t *data = (comp_stress_thread_data_t *)tctx->user_data;

    /* Cleanup edge buffer pool */
    if (g_edge_pool_enabled)
        artvg_disable_edge_buffer_pool(tctx->vg);

    if (data) {
        if (data->src_buf) {
            cleanup_source_buffer(tctx->vg, data->src_buf);
        }
        free(data);
        tctx->user_data = NULL;
    }
}

/***********************
 *  MAIN DRAW CALLBACK
 **********************/
static int comprehensive_draw_callback(void *thread_ctx, int buffer_idx)
{
    stress_thread_context_t *tctx = (stress_thread_context_t *)thread_ctx;
    comp_stress_thread_data_t *data = (comp_stress_thread_data_t *)tctx->user_data;

    /* Randomly select module based on weights */
    data->current_type = random_select_module();

    /* Call the selected module's draw callback */
    int ret = g_module_draw_callbacks[data->current_type](tctx, buffer_idx);

    /* Record module usage (thread-safe with atomic) */
    if (ret == 0) {
        __atomic_fetch_add(&g_module_call_count[data->current_type], 1, __ATOMIC_RELAXED);
    }

    return ret;
}

/***********************
 *  CUSTOM RESULTS COLLECTION
 **********************/
static void comprehensive_collect_and_print_results(void *render_ctx,
                                                   stress_thread_context_t *thread_contexts,
                                                   int num_threads)
{
    stress_render_context_t *ctx = (stress_render_context_t *)render_ctx;

    printf("\n=== Stress Test Results ===\n");
    printf("Threads: %d\n", num_threads);
    printf("Iterations per thread: %d\n", ctx->total_iterations);

    uint64_t total_draws = 0;
    uint64_t total_fails = 0;
    for (int i = 0; i < num_threads; i++) {
        total_draws += thread_contexts[i].draw_count;
        total_fails += thread_contexts[i].fail_count;
    }

    printf("Total draws: %" PRIu64 "\n", total_draws);
    printf("Total failed: %" PRIu64 "\n", total_fails);
    printf("DRM frames submitted: %" PRIu64 "\n", ctx->total_frames);

    double total_time = ctx->total_frames > 0 ?
        ((double)ctx->total_frames / 60.0) : 0;
    printf("Total time: %.2f seconds\n", total_time);

    if (total_draws > 0) {
        double avg_time = total_time * 1000.0 / total_draws;
        printf("Average time per draw: %.2f ms\n", avg_time);
        if (total_time > 0) {
            printf("Throughput: %.2f draws/sec\n", total_draws / total_time);
        }
    }

    printf("\nPer-thread statistics:\n");
    for (int i = 0; i < num_threads; i++) {
        double thread_time = thread_contexts[i].total_time_ms / 1000.0;
        printf("  Thread %d: Draws=%" PRIu64 ", Failed=%" PRIu64 ", Time=%.2fs\n",
               i, thread_contexts[i].draw_count, thread_contexts[i].fail_count, thread_time);
    }

    printf("\nModule usage breakdown:\n");
    uint64_t total_module_calls = 0;
    for (int i = 0; i < COMP_STRESS_TYPE_COUNT; i++) {
        total_module_calls += g_module_call_count[i];
    }

    for (int i = 0; i < COMP_STRESS_TYPE_COUNT; i++) {
        const char *name = module_type_to_string((comp_stress_type_t)i);
        uint64_t count = g_module_call_count[i];
        double percentage = total_module_calls > 0 ?
            ((double)count / total_module_calls * 100.0) : 0;
        printf("  %s: %" PRIu64 " calls (%.1f%%)\n", name, count, percentage);
    }
    printf("============================\n");
}

/***********************
 *  CUSTOM OPTIONS PRINTING
 **********************/
#define COMPREHENSIVE_USAGE \
    "Test-specific options:\n" \
    "  --enable-clear <0|1>      Enable clear module (default: 1)\n" \
    "  --enable-blit <0|1>       Enable blit module (default: 1)\n" \
    "  --enable-affine <0|1>     Enable affine module (default: 1)\n" \
    "  --enable-projective <0|1> Enable projective module (default: 1)\n" \
    "  --enable-path <0|1>       Enable path module (default: 1)\n" \
    "  --enable-gradient <0|1>   Enable gradient module (default: 1)\n" \
    "  --weight-clear <n>        Weight for clear module (default: 10)\n" \
    "  --weight-blit <n>         Weight for blit module (default: 10)\n" \
    "  --weight-affine <n>       Weight for affine module (default: 10)\n" \
    "  --weight-projective <n>   Weight for projective module (default: 10)\n" \
    "  --weight-path <n>         Weight for path module (default: 10)\n" \
    "  --weight-gradient <n>     Weight for gradient module (default: 10)\n" \
    "  --edge-pool <0|1>          Enable edge buffer pool (default: 1)\n" \
    "  --edge-pool-size <bytes>   Edge buffer pool size (default: 512K)\n"

/***********************
 *  CUSTOM OPTIONS
 **********************/
static const struct option g_custom_long_options[] = {
    {"enable-clear",       required_argument, 0, 256},
    {"enable-blit",        required_argument, 0, 257},
    {"enable-affine",      required_argument, 0, 258},
    {"enable-projective",  required_argument, 0, 259},
    {"enable-path",        required_argument, 0, 260},
    {"enable-gradient",    required_argument, 0, 261},
    {"weight-clear",       required_argument, 0, 262},
    {"weight-blit",        required_argument, 0, 263},
    {"weight-affine",      required_argument, 0, 264},
    {"weight-projective",  required_argument, 0, 265},
    {"weight-path",        required_argument, 0, 266},
    {"weight-gradient",    required_argument, 0, 267},
    {"edge-pool",          required_argument, 0, 268},
    {"edge-pool-size",     required_argument, 0, 269},
    {0, 0, 0, 0}
};

static int handle_custom_option(int val, const char *optarg)
{
    switch (val) {
    case 256: g_module_enabled[COMP_STRESS_CLEAR]       = atoi(optarg); break;
    case 257: g_module_enabled[COMP_STRESS_BLIT]        = atoi(optarg); break;
    case 258: g_module_enabled[COMP_STRESS_AFFINE]      = atoi(optarg); break;
    case 259: g_module_enabled[COMP_STRESS_PROJECTIVE]  = atoi(optarg); break;
    case 260: g_module_enabled[COMP_STRESS_PATH]        = atoi(optarg); break;
    case 261: g_module_enabled[COMP_STRESS_GRADIENT]    = atoi(optarg); break;
    case 262: g_module_weights[COMP_STRESS_CLEAR]       = atoi(optarg); break;
    case 263: g_module_weights[COMP_STRESS_BLIT]        = atoi(optarg); break;
    case 264: g_module_weights[COMP_STRESS_AFFINE]      = atoi(optarg); break;
    case 265: g_module_weights[COMP_STRESS_PROJECTIVE]  = atoi(optarg); break;
    case 266: g_module_weights[COMP_STRESS_PATH]        = atoi(optarg); break;
    case 267: g_module_weights[COMP_STRESS_GRADIENT]    = atoi(optarg); break;
    case 268: g_edge_pool_enabled                       = atoi(optarg); break;
    case 269: g_edge_pool_size                          = (uint32_t)atoi(optarg); break;
    default:  return -1;
    }
    return 0;
}

/***********************
 *  FAIL REASON STRING
 **********************/
static const char *comprehensive_fail_reason_to_string(int reason)
{
    switch (reason) {
    case STRESS_FAIL_REASON_NO_BUFFER:
        return "Failed to get drawable buffer";
    case STRESS_FAIL_REASON_OP_FAILED:
        return "Operation failed";
    default:
        return "Unknown failure";
    }
}

/***********************
 *  TEST OPS TABLE
 **********************/
static stress_test_ops_t g_comprehensive_ops = {
    .test_name = "ArtVG Comprehensive Concurrent Stress Test",

    .init_test_resources = NULL,
    .init_thread_resources = comprehensive_init_thread_resources,
    .cleanup_thread_resources = comprehensive_cleanup_thread_resources,
    .cleanup_test_resources = NULL,
    .draw_callback = comprehensive_draw_callback,
    .collect_and_print_results = comprehensive_collect_and_print_results,
    .fail_reason_to_string = comprehensive_fail_reason_to_string,
    .custom_long_options = g_custom_long_options,
    .handle_custom_option = handle_custom_option,
    .custom_usage_text = COMPREHENSIVE_USAGE,
};

/***********************
 *  MAIN ENTRY POINT
 **********************/
int main(int argc, char **argv)
{
    /* Initialize random seed */
    srand((unsigned int)time(NULL));

    /* Initialize affine configs */
    g_num_affine_configs = sizeof(g_affine_configs_default) / sizeof(g_affine_configs_default[0]);
    memcpy(g_affine_configs, g_affine_configs_default, g_num_affine_configs * sizeof(affine_config_t));

    /* Initialize projective configs */
    g_num_projective_configs = sizeof(g_projective_configs_default) / sizeof(g_projective_configs_default[0]);
    memcpy(g_projective_configs, g_projective_configs_default,
           g_num_projective_configs * sizeof(projective_config_t));

    return stress_run_test(argc, argv, &g_comprehensive_ops);
}
