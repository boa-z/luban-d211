/*
 * Copyright (c) 2026, ArtInChip Technology Co., Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Authors:  zequan liang <zequan.liang@artinchip.com>
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>
#include <sys/mman.h>
#include <unistd.h>
#include <math.h>
#include "artvg.h"
#include "vg_drm.h"
#include "image_array.h"

extern const image_dsc_t alpha_baby_card_240x120;
extern const image_dsc_t alpha_poney_240x120;

#define MAX_PLANES 3
#define MAX_ORDER_LEN 10

#define DEFAULT_PERSPECTIVE_X       0
#define DEFAULT_PERSPECTIVE_Y       0
#define DEFAULT_LUT_MODE            5   /* ARTVG_PROJECTIVE_LUT_MOD_S16_8 */
#define DEFAULT_BLOCK_SIZE          1   /* 16x16 */
#define DEFAULT_OUT_BLK_MODE        1   /* 16x16 */
#define DEFAULT_COLOR_SPACE         MPP_COLOR_SPACE_BT709

typedef enum {
    PARSE_SUCCESS,
    PARSE_ERROR_MEMORY,
    PARSE_ERROR_INVALID_COUNT
} parse_result_t;

typedef struct {
    struct artvg *vg;
    struct mpp_buf *src;
    struct mpp_buf *yuv;
    struct mpp_buf dst;
    void *src_mmap[MAX_PLANES];
    void *yuv_mmap[MAX_PLANES];
    drm_dev_t drm_dev;
    int dma_fd_added;
    int lut_created;
    int src_allocated;
    int src_mapped;
    int yuv_allocated;
    int yuv_mapped;
    int drm_opened;
} test_context_t;

typedef struct {
    /* Projective transformation configuration */
    float perspective[2];
    int lut_mode;
    int block_size;
    int out_blk_mode;

    /* CSC configuration */
    enum mpp_pixel_format yuv_format;  /* Intermediate YUV format */
    enum mpp_color_space yuv_color_space;  /* YUV color space for CSC */

    /* Transformation parameters */
    float translate[2];
    float scale[2];
    float rotate_angle;
    float skew[2];
    float center[2];
    char transform_order[MAX_ORDER_LEN];

    /* Crop configuration */
    int src_crop[4];
    int src_crop_en;
    int dst_crop[4];
    int dst_crop_en;

    /* Blending control */
    int alpha_en;
    int alpha_rules;

    /* Edge fill control */
    artvg_transform_edge_t transform_edge;

    /* Resolution */
    int width;
    int height;
} projective_csc_config_t;

static int dma_buf_mmap(struct mpp_buf *buf, void *map[])
{
    if (!buf || !map) {
        return -1;
    }

    for (int i = 0; i < MAX_PLANES; i++) {
        map[i] = NULL;

        if (buf->fd[i] < 0) {
            continue;
        }

        size_t map_size = buf->stride[i] * buf->size.height;
        void *map_mem = mmap(NULL, map_size, PROT_READ | PROT_WRITE,
                             MAP_SHARED, buf->fd[i], 0);

        if (map_mem == MAP_FAILED) {
            printf("mmap error for fd[%d]\n", i);
            if (i == 0) {
                return -1;
            }
            continue;
        }

        memset(map_mem, 0x0, map_size);
        if (dmabuf_sync(buf->fd[i], DMA_BUF_SYNC_END) != 0) {
            VG_LOG_WARN("Failed to sync DMA buffer for plane %d", i);
        }
        map[i] = map_mem;
    }

    return 0;
}

static void dma_buf_unmap(struct mpp_buf *buf, void *map[])
{
    if (!buf || !map) {
        return;
    }

    for (int i = 0; i < MAX_PLANES; i++) {
        if (!map[i] || buf->fd[i] < 0) {
            continue;
        }

        size_t map_size = buf->stride[i] * buf->size.height;
        if (munmap(map[i], map_size) == -1) {
            printf("munmap error for fd[%d]\n", i);
        }
        map[i] = NULL;
    }
}

static parse_result_t parse_number_list(const char *str, void *values,
                                        int max_count, int is_float)
{
    char *str_copy = strdup(str);
    if (!str_copy) {
        return PARSE_ERROR_MEMORY;
    }

    char *saveptr = NULL;
    char *token = strtok_r(str_copy, ",", &saveptr);
    int count = 0;

    while (token && count < max_count) {
        if (is_float) {
            ((float *)values)[count++] = atof(token);
        } else {
            ((int *)values)[count++] = atoi(token);
        }
        token = strtok_r(NULL, ",", &saveptr);
    }

    if (count != max_count) {
        free(str_copy);
        return PARSE_ERROR_INVALID_COUNT;
    }

    free(str_copy);

    return PARSE_SUCCESS;
}

static int parse_format(const char *str)
{
    if (!str) return -1;

    /* RGBA formats */
    if (strncmp(str, "argb8888", strlen("argb8888")) == 0 || strncmp(str, "ARGB8888", strlen("ARGB8888")) == 0)
        return MPP_FMT_ARGB_8888;
    else if (strncmp(str, "abgr8888", strlen("abgr8888")) == 0 || strncmp(str, "ABGR8888", strlen("ABGR8888")) == 0)
        return MPP_FMT_ABGR_8888;
    else if (strncmp(str, "rgba8888", strlen("rgba8888")) == 0 || strncmp(str, "RGBA8888", strlen("RGBA8888")) == 0)
        return MPP_FMT_RGBA_8888;
    else if (strncmp(str, "bgra8888", strlen("bgra8888")) == 0 || strncmp(str, "BGRA8888", strlen("BGRA8888")) == 0)
        return MPP_FMT_BGRA_8888;

    /* RGB formats */
    else if (strncmp(str, "rgb888", strlen("rgb888")) == 0 || strncmp(str, "RGB888", strlen("RGB888")) == 0)
        return MPP_FMT_RGB_888;
    else if (strncmp(str, "bgr888", strlen("bgr888")) == 0 || strncmp(str, "BGR888", strlen("BGR888")) == 0)
        return MPP_FMT_BGR_888;
    else if (strncmp(str, "rgb565", strlen("rgb565")) == 0 || strncmp(str, "RGB565", strlen("RGB565")) == 0)
        return MPP_FMT_RGB_565;
    else if (strncmp(str, "bgr565", strlen("bgr565")) == 0 || strncmp(str, "BGR565", strlen("BGR565")) == 0)
        return MPP_FMT_BGR_565;

    /* YUV formats */
    else if (strncmp(str, "yuv420p", strlen("yuv420p")) == 0 || strncmp(str, "YUV420P", strlen("YUV420P")) == 0)
        return MPP_FMT_YUV420P;
    else if (strncmp(str, "nv12", strlen("nv12")) == 0 || strncmp(str, "NV12", strlen("NV12")) == 0)
        return MPP_FMT_NV12;
    else if (strncmp(str, "nv21", strlen("nv21")) == 0 || strncmp(str, "NV21", strlen("NV21")) == 0)
        return MPP_FMT_NV21;
    else if (strncmp(str, "yuv422p", strlen("yuv422p")) == 0 || strncmp(str, "YUV422P", strlen("YUV422P")) == 0)
        return MPP_FMT_YUV422P;
    else if (strncmp(str, "nv16", strlen("nv16")) == 0 || strncmp(str, "NV16", strlen("NV16")) == 0)
        return MPP_FMT_NV16;
    else if (strncmp(str, "nv61", strlen("nv61")) == 0 || strncmp(str, "NV61", strlen("NV61")) == 0)
        return MPP_FMT_NV61;
    else if (strncmp(str, "yuyv", strlen("yuyv")) == 0 || strncmp(str, "YUYV", strlen("YUYV")) == 0)
        return MPP_FMT_YUYV;
    else if (strncmp(str, "yvyu", strlen("yvyu")) == 0 || strncmp(str, "YVYU", strlen("YVYU")) == 0)
        return MPP_FMT_YVYU;
    else if (strncmp(str, "uyvy", strlen("uyvy")) == 0 || strncmp(str, "UYVY", strlen("UYVY")) == 0)
        return MPP_FMT_UYVY;
    else if (strncmp(str, "vyuy", strlen("vyuy")) == 0 || strncmp(str, "VYUY", strlen("VYUY")) == 0)
        return MPP_FMT_VYUY;
    else if (strncmp(str, "yuv444p", strlen("yuv444p")) == 0 || strncmp(str, "YUV444P", strlen("YUV444P")) == 0)
        return MPP_FMT_YUV444P;

    return atoi(str);
}

static int parse_color_space(const char *str)
{
    if (!str) return -1;

    if (strncmp(str, "bt601_full", strlen("bt601_full")) == 0 || strncmp(str, "BT601_FULL", strlen("BT601_FULL")) == 0)
        return MPP_COLOR_SPACE_BT601_FULL_RANGE;
    else if (strncmp(str, "bt601", strlen("bt601")) == 0 || strncmp(str, "BT601", strlen("BT601")) == 0)
        return MPP_COLOR_SPACE_BT601;
    else if (strncmp(str, "bt709_full", strlen("bt709_full")) == 0 || strncmp(str, "BT709_FULL", strlen("BT709_FULL")) == 0)
        return MPP_COLOR_SPACE_BT709_FULL_RANGE;
    else if (strncmp(str, "bt709", strlen("bt709")) == 0 || strncmp(str, "BT709", strlen("BT709")) == 0)
        return MPP_COLOR_SPACE_BT709;

    return atoi(str);
}

static void create_transform_matrix(artvg_matrix_t *matrix,
                                   const float *translate,
                                   const float *scale,
                                   float rotate_angle,
                                   const float *skew,
                                   const float *center,
                                   const float *perspective,
                                   const char *order)
{
    const char *default_order = "trskp";

    if (!order || strlen(order) == 0) {
        order = default_order;
    }

    artvg_matrix_identity(matrix);

    for (int i = 0; order[i] != '\0'; i++) {
        switch (order[i]) {
        case 't':
            if (translate[0] != 0.0f || translate[1] != 0.0f) {
                artvg_matrix_translate(translate[0], translate[1], matrix);
            }
            break;

        case 's':
            if (scale[0] != 1.0f || scale[1] != 1.0f) {
                artvg_matrix_scale(scale[0], scale[1], matrix);
            }
            break;

        case 'r':
            if (rotate_angle != 0.0f) {
                if (center[0] != 0.0f || center[1] != 0.0f) {
                    artvg_point_t center_point = {center[0], center[1]};
                    artvg_matrix_rotate_point(&center_point, rotate_angle, matrix);
                } else {
                    artvg_matrix_rotate(rotate_angle, matrix);
                }
            }
            break;

        case 'k':
            if (skew[0] != 0.0f || skew[1] != 0.0f) {
                artvg_matrix_skew(skew[0], skew[1], matrix);
            }
            break;

        case 'p':
            if (perspective[0] != 0.0f || perspective[1] != 0.0f) {
                artvg_matrix_perspective(perspective[0], perspective[1], matrix);
            }
            break;

        default:
            printf("Warning: Unknown transformation order character '%c'\n", order[i]);
            continue;
        }
    }
}

static void print_matrix(const artvg_matrix_t *matrix, const char *name)
{
    printf("%s matrix:\n", name);
    for (int i = 0; i < 3; i++) {
        printf("  [ ");
        for (int j = 0; j < 3; j++) {
            printf("%8.4f ", matrix->m[i][j]);
        }
        printf("]\n");
    }
}

static void init_default_config(projective_csc_config_t *config)
{
    memset(config, 0, sizeof(projective_csc_config_t));

    /* Projective defaults */
    config->perspective[0] = DEFAULT_PERSPECTIVE_X;
    config->perspective[1] = DEFAULT_PERSPECTIVE_Y;
    config->lut_mode = DEFAULT_LUT_MODE;
    config->block_size = DEFAULT_BLOCK_SIZE;
    config->out_blk_mode = DEFAULT_OUT_BLK_MODE;

    /* CSC defaults - NV12 intermediate YUV */
    config->yuv_format = MPP_FMT_NV12;
    config->yuv_color_space = DEFAULT_COLOR_SPACE;

    /* Transformation defaults */
    config->translate[0] = 0.0f;
    config->translate[1] = 0.0f;
    config->scale[0] = 1.0f;
    config->scale[1] = 1.0f;
    config->rotate_angle = 0.0f;
    config->skew[0] = 0.0f;
    config->skew[1] = 0.0f;
    config->center[0] = 0.0f;
    config->center[1] = 0.0f;
    strncpy(config->transform_order, "trskp", MAX_ORDER_LEN - 1);

    /* Edge fill defaults */
    config->transform_edge.fill_en = 0;
    config->transform_edge.blend_en = 0;
    config->transform_edge.fill_color = 0xFF000000;
    config->transform_edge.pad_mode = ARTVG_PAD_MODE_FIXED_COLOR;
}

static void show_usage(const char *program_name)
{
    printf("Usage: %s [options]\n", program_name);
    printf("\nARTVG Projective Transformation with CSC Test\n");
    printf("Combines projective transformation with color space conversion\n");

    printf("\n=== Projective Configuration ===\n");
    printf("  --perspective <px,py>   Perspective values (default: 0.001,0.001)\n");
    printf("  --lut-mode <0-7>        LUT mode (default: 5=S16_8)\n");
    printf("  --block-size <0-3>      LUT block size (default: 1=16x16)\n");
    printf("  --out-blk-mode <0-1>    Output block mode (default: 1=16x16)\n");

    printf("\n=== Transformation Parameters ===\n");
    printf("  --translate <tx,ty>     Translation (default: 0,0)\n");
    printf("  --scale <sx,sy>         Scaling (default: 1.0,1.0)\n");
    printf("  --rotate <angle>        Rotation angle in degrees (default: 0.0)\n");
    printf("  --skew <kx,ky>          Skew/shear (default: 0.0,0.0)\n");
    printf("  --center <cx,cy>        Rotation center point (default: source image center)\n");
    printf("  --transform-order <order> Transformation order (t:translate, s:scale, r:rotate, k:skew, p:perspective)\n");
    printf("                          Example: 'trsp' means translate->rotate->scale->perspective (default: 'trskp')\n");

    printf("\n=== CSC Configuration ===\n");
    printf("  --yuv-format <format>   Intermediate YUV format (default: nv12)\n");
    printf("  --yuv-color-space <cs>  YUV color space for conversion (default: bt709)\n");
    printf("\n  Supported YUV formats:\n");
    printf("    nv12, nv21, yuv420p, yuv422p, nv16, nv61, yuyv, yvyu, uyvy, vyuy, yuv444p\n");
    printf("\n  Supported color spaces:\n");
    printf("    bt601, bt601_full, bt709, bt709_full\n");
    printf("\n  Test flow: RGB -> YUV (CSC2 via Blit) -> RGB (CSC3 via projective2)\n");

    printf("\n=== Crop Parameters ===\n");
    printf("  --src-crop <x,y,w,h>    Source crop region (default: full image)\n");
    printf("  --dst-crop <x,y,w,h>    Destination crop region (default: no crop)\n");

    printf("\n=== Blending Control Parameters ===\n");
    printf("  --alpha-en <0|1>        Enable alpha blending (default: 0)\n");
    printf("  --alpha-rules <value>   Alpha blend rules (default: 0=ARTVG_BLEND_DEFAULT)\n");

    printf("\n=== Edge Fill Control Parameters ===\n");
    printf("  --fill-bg-en <0|1>      Enable background fill (default: 0)\n");
    printf("  --blend-edge-en <0|1>   Enable edge blending (default: 0)\n");
    printf("  --edge-color <hex>      Edge fill color (default: 0xFF000000)\n");
    printf("  --edge-pad-mode <0-2>   Edge fill mode (default: 0=ARTVG_PAD_MODE_FIXED_COLOR)\n");

    printf("\n=== Help ===\n");
    printf("  --help                  Show this help message\n");

    printf("\n=== Examples ===\n");
    printf("  1. Default test (RGB -> NV12 -> RGB with projective transform):\n");
    printf("     %s\n", program_name);
    printf("  2. Use NV21 intermediate format:\n");
    printf("     %s --yuv-format nv21 --yuv-color-space bt709\n", program_name);
    printf("  3. Custom perspective with rotation:\n");
    printf("     %s --perspective 0.002,0.001 --rotate 30 --lut-mode 5\n", program_name);
    printf("  4. Use BT601 color space:\n");
    printf("     %s --yuv-color-space bt601\n", program_name);
}

static void cleanup_context(test_context_t *ctx, artvg_projective_t **projective)
{
    if (!ctx) {
        return;
    }

    if (ctx->lut_created && projective && *projective) {
        artvg_projective_free(ctx->vg, *projective);
        *projective = NULL;
    }
    if (ctx->dma_fd_added && ctx->vg && ctx->dst.fd[0] >= 0) {
        artvg_remove_dma_fd(ctx->vg, ctx->dst.fd[0]);
    }
    if (ctx->yuv_mapped) {
        dma_buf_unmap(ctx->yuv, ctx->yuv_mmap);
    }
    if (ctx->yuv_allocated && ctx->yuv) {
        artvg_free(ctx->vg, ctx->yuv);
        ctx->yuv = NULL;
    }
    if (ctx->src_mapped) {
        dma_buf_unmap(ctx->src, ctx->src_mmap);
    }
    if (ctx->src_allocated && ctx->src) {
        artvg_free(ctx->vg, ctx->src);
        ctx->src = NULL;
    }
    if (ctx->drm_opened) {
        drm_device_close(&ctx->drm_dev);
    }
    if (ctx->vg) {
        artvg_destroy(ctx->vg);
    }
}

static int parse_arguments(int argc, char **argv, projective_csc_config_t *config)
{
    static struct option long_options[] = {
        {"perspective", required_argument, 0, 'p'},
        {"lut-mode", required_argument, 0, 'l'},
        {"block-size", required_argument, 0, 'b'},
        {"out-blk-mode", required_argument, 0, 'o'},
        {"translate", required_argument, 0, 't'},
        {"scale", required_argument, 0, 's'},
        {"rotate", required_argument, 0, 'r'},
        {"skew", required_argument, 0, 'k'},
        {"center", required_argument, 0, 'c'},
        {"transform-order", required_argument, 0, 'x'},
        {"yuv-format", required_argument, 0, '1'},
        {"yuv-color-space", required_argument, 0, '3'},
        {"src-crop", required_argument, 0, '5'},
        {"dst-crop", required_argument, 0, '6'},
        {"alpha-en", required_argument, 0, 'a'},
        {"alpha-rules", required_argument, 0, 'u'},
        {"fill-bg-en", required_argument, 0, 'f'},
        {"blend-edge-en", required_argument, 0, 'e'},
        {"edge-color", required_argument, 0, '7'},
        {"edge-pad-mode", required_argument, 0, '8'},
        {"help", no_argument, 0, 'h'},
        {0, 0, 0, 0}
    };

    int opt;
    while ((opt = getopt_long(argc, argv, "h", long_options, NULL)) != -1) {
        switch (opt) {
        case 'p':
            if (parse_number_list(optarg, config->perspective, 2, 1) != PARSE_SUCCESS) {
                printf("Error: Perspective requires 2 comma-separated floats\n");
                return -1;
            }
            break;
        case 'l':
            config->lut_mode = atoi(optarg);
            if (config->lut_mode < 0 || config->lut_mode > 7) {
                printf("Error: LUT mode must be 0-7\n");
                return -1;
            }
            break;
        case 'b':
            config->block_size = atoi(optarg);
            if (config->block_size < 0 || config->block_size > 3) {
                printf("Error: Block size must be 0-3\n");
                return -1;
            }
            break;
        case 'o':
            config->out_blk_mode = atoi(optarg);
            if (config->out_blk_mode < 0 || config->out_blk_mode > 1) {
                printf("Error: Output block mode must be 0-1\n");
                return -1;
            }
            break;
        case 't':
            if (parse_number_list(optarg, config->translate, 2, 1) != PARSE_SUCCESS) {
                printf("Error: Translation requires 2 comma-separated floats\n");
                return -1;
            }
            break;
        case 's':
            if (parse_number_list(optarg, config->scale, 2, 1) != PARSE_SUCCESS) {
                printf("Error: Scaling requires 2 comma-separated floats\n");
                return -1;
            }
            break;
        case 'r':
            config->rotate_angle = atof(optarg);
            break;
        case 'k':
            if (parse_number_list(optarg, config->skew, 2, 1) != PARSE_SUCCESS) {
                printf("Error: Skew requires 2 comma-separated floats\n");
                return -1;
            }
            break;
        case 'c':
            if (parse_number_list(optarg, config->center, 2, 1) != PARSE_SUCCESS) {
                printf("Error: Center point requires 2 comma-separated floats\n");
                return -1;
            }
            break;
        case 'x':
            strncpy(config->transform_order, optarg, MAX_ORDER_LEN - 1);
            config->transform_order[MAX_ORDER_LEN - 1] = '\0';
            break;
        case '1':
            config->yuv_format = parse_format(optarg);
            if (config->yuv_format < 0) {
                printf("Error: Invalid YUV format: %s\n", optarg);
                return -1;
            }
            break;
        case '3':
            config->yuv_color_space = parse_color_space(optarg);
            if (config->yuv_color_space < 0) {
                printf("Error: Invalid color space: %s\n", optarg);
                return -1;
            }
            break;
        case '5':
            if (parse_number_list(optarg, config->src_crop, 4, 0) != PARSE_SUCCESS) {
                printf("Error: Source crop requires 4 comma-separated integers\n");
                return -1;
            }
            config->src_crop_en = 1;
            break;
        case '6':
            if (parse_number_list(optarg, config->dst_crop, 4, 0) != PARSE_SUCCESS) {
                printf("Error: Destination crop requires 4 comma-separated integers\n");
                return -1;
            }
            config->dst_crop_en = 1;
            break;
        case 'a':
            config->alpha_en = atoi(optarg);
            break;
        case 'u':
            config->alpha_rules = atoi(optarg);
            break;
        case 'f':
            config->transform_edge.fill_en = atoi(optarg);
            break;
        case 'e':
            config->transform_edge.blend_en = atoi(optarg);
            break;
        case '7':
            config->transform_edge.fill_color = strtoul(optarg, NULL, 0);
            break;
        case '8':
            config->transform_edge.pad_mode = atoi(optarg);
            break;
        case 'h':
            show_usage(argv[0]);
            return 1;
        default:
            printf("Unknown option, use --help for usage\n");
            return -1;
        }
    }

    return 0;
}

int main(int argc, char **argv)
{
    test_context_t ctx = {0};
    projective_csc_config_t config;
    artvg_ctrl_t ctrl = {0};
    artvg_ctrl_t blit_ctrl = {0};
    artvg_projective_t *projective = NULL;
    artvg_blit_ctl_t blit_ctl = {0};
    artvg_matrix_t matrix;

    int src_width = alpha_baby_card_240x120.header.w;
    int src_height = alpha_baby_card_240x120.header.h;

    printf("=== ARTVG Projective CSC Test ===\n");
    printf("Test flow: RGB -> YUV (CSC2 via Blit) -> RGB (CSC3 via projective2)\n\n");

    /* Initialize default configuration */
    init_default_config(&config);

        int ret = parse_arguments(argc, argv, &config);
    if (ret == 1) {
        return 0;  /* Help displayed */
    } else if (ret == -1) {
        return -1;  /* Error */
    }

    /* Set default center if not specified */
    if (config.center[0] == 0.0f && config.center[1] == 0.0f) {
        config.center[0] = src_width / 2.0f;
        config.center[1] = src_height / 2.0f;
    }

    /* Set default src crop if not specified */
    if (!config.src_crop_en) {
        config.src_crop[0] = 0;
        config.src_crop[1] = 0;
        config.src_crop[2] = src_width;
        config.src_crop[3] = src_height;
        config.src_crop_en = 1;
    }

    /* Create transformation matrix */
    create_transform_matrix(&matrix, config.translate, config.scale,
                           config.rotate_angle, config.skew, config.center,
                           config.perspective, config.transform_order);

    /* Print configuration */
    printf("Configuration:\n");
    printf("  Source: ARGB8888 (RGB image)\n");
    printf("  Intermediate YUV format: %d\n", config.yuv_format);
    printf("  YUV color space: %d\n", config.yuv_color_space);
    printf("  Output: RGB (DRM framebuffer)\n");

    printf("\nProjective configuration:\n");
    printf("  Perspective: (%.6f, %.6f)\n", config.perspective[0], config.perspective[1]);
    printf("  LUT mode: %d\n", config.lut_mode);
    printf("  Block size: %s\n",
           config.block_size == 0 ? "8x8" :
           config.block_size == 1 ? "16x16" :
           config.block_size == 2 ? "32x32" : "64x64");
    printf("  Output block mode: %s\n", config.out_blk_mode == 0 ? "8x8" : "16x16");

    printf("\nTransformation parameters:\n");
    printf("  Translation: (%.2f, %.2f)\n", config.translate[0], config.translate[1]);
    printf("  Scaling: (%.2f, %.2f)\n", config.scale[0], config.scale[1]);
    printf("  Rotation: %.2f degrees\n", config.rotate_angle);
    printf("  Skew: (%.2f, %.2f)\n", config.skew[0], config.skew[1]);
    printf("  Center point: (%.2f, %.2f)\n", config.center[0], config.center[1]);
    printf("  Transformation order: %s\n", config.transform_order);

    print_matrix(&matrix, "Projective");

    /* Create ARTVG context */
    ctx.vg = artvg_create();
    if (!ctx.vg) {
        printf("Failed to open ARTVG device\n");
        return -1;
    }
    printf("\nARTVG device opened successfully\n");

    /* ============================================
     * Step 1: Setup RGB source buffer (load image)
     * ============================================ */
    ctx.src = artvg_allocate(ctx.vg, src_width, src_height, MPP_FMT_ARGB_8888);
    if (!ctx.src) {
        printf("Failed to allocate source buffer (%dx%d)\n", src_width, src_height);
        goto __cleanup_context;
    }
    ctx.src_allocated = 1;
    printf("Source buffer allocated successfully\n");

    if (config.src_crop_en) {
        ctx.src->crop_en = 1;
        ctx.src->crop.x = config.src_crop[0];
        ctx.src->crop.y = config.src_crop[1];
        ctx.src->crop.width = config.src_crop[2];
        ctx.src->crop.height = config.src_crop[3];
    }

    if (dma_buf_mmap(ctx.src, ctx.src_mmap) != 0) {
        printf("Failed to map source buffer\n");
        goto __cleanup_context;
    }
    ctx.src_mapped = 1;

    /* Load RGB image */
    if (image_array_read(&alpha_baby_card_240x120, ctx.src_mmap[0],
                         ctx.src->size.height * ctx.src->stride[0]) != 0) {
        printf("Failed to load source image\n");
        goto __cleanup_context;
    }
    printf("Source image loaded: %dx%d (RGB)\n", src_width, src_height);
    dmabuf_sync(ctx.src->fd[0], DMA_BUF_SYNC_END);

    /* ============================================
     * Step 2: Setup YUV intermediate buffer
     * ============================================ */
    ctx.yuv = artvg_allocate(ctx.vg, src_width, src_height, config.yuv_format);
    if (!ctx.yuv) {
        printf("Failed to allocate YUV intermediate buffer (%dx%d)\n", src_width, src_height);
        goto __cleanup_context;
    }
    ctx.yuv_allocated = 1;
    ctx.yuv->flags = config.yuv_color_space;
    printf("YUV intermediate buffer allocated successfully\n");

    if (dma_buf_mmap(ctx.yuv, ctx.yuv_mmap) != 0) {
        printf("Failed to map YUV buffer\n");
        goto __cleanup_context;
    }
    ctx.yuv_mapped = 1;

    /* ============================================
     * Step 3: Open DRM and get destination buffer
     * ============================================ */
    if (drm_device_open(&ctx.drm_dev, "/dev/dri/card0", -1) != 0) {
        printf("Failed to open DRM device\n");
        goto __cleanup_context;
    }
    ctx.drm_opened = 1;
    printf("DRM device opened successfully\n");
    if (drm_buffer_to_mpp_buffer(&ctx.drm_dev, 0, &ctx.dst) != 0) {
        printf("Failed to convert DRM buffer to MPP buffer\n");
        goto __cleanup_context;
    }

    if (config.dst_crop_en) {
        ctx.dst.crop_en = 1;
        ctx.dst.crop.x = config.dst_crop[0];
        ctx.dst.crop.y = config.dst_crop[1];
        ctx.dst.crop.width = config.dst_crop[2];
        ctx.dst.crop.height = config.dst_crop[3];
    }

    printf("Destination buffer: %dx%d\n", ctx.dst.size.width, ctx.dst.size.height);

    if (artvg_add_dma_fd(ctx.vg, ctx.dst.fd[0]) != ARTVG_SUCCESS) {
        printf("Failed to add DMA FD\n");
        goto __cleanup_context;
    }
    ctx.dma_fd_added = 1;
    printf("DMA FD added successfully\n");

    /* Setup alpha blending */
    ctrl.alpha_en = config.alpha_en;
    ctrl.alpha_rules = config.alpha_rules;

    if (config.alpha_en) {
        void *drm_map = drm_framebuffer_get_map(&ctx.drm_dev, 0);
        if (drm_map) {
            if (image_array_read_with_stride(&alpha_poney_240x120, drm_map,
                                             ctx.dst.stride[0]) != 0) {
                printf("Failed to load background image, using gray background\n");
                memset(drm_map, 0x80, ctx.dst.stride[0] * ctx.dst.size.height);
            } else {
                printf("Background image loaded successfully\n");
            }
            dmabuf_sync(ctx.dst.fd[0], DMA_BUF_SYNC_END);
        }
    }

    printf("\nBlending control parameters:\n");
    printf("  Alpha blending: %s (rules: %d)\n",
           ctrl.alpha_en ? "enabled" : "disabled", ctrl.alpha_rules);

    printf("\nEdge fill control parameters:\n");
    printf("  Background fill: %s\n", config.transform_edge.fill_en ? "enabled" : "disabled");
    printf("  Edge blending: %s\n", config.transform_edge.blend_en ? "enabled" : "disabled");
    printf("  Edge color: 0x%08X\n", config.transform_edge.fill_color);
    printf("  Edge pad mode: %d\n", config.transform_edge.pad_mode);

    /* ============================================
     * Step 4: RGB -> YUV conversion using Blit (CSC2)
     * ============================================ */
    printf("\nStep 1: Converting RGB to YUV using Blit (CSC2)...\n");
    printf("  Source: RGB (ARGB8888)\n");
    printf("  Destination: YUV (%d)\n", config.yuv_format);

    artvg_error_t err = artvg_blit2(ctx.vg, ctx.src, ctx.yuv, ctx.yuv, NULL,
                                    &blit_ctrl, &blit_ctl);
    if (err != ARTVG_SUCCESS) {
        printf("RGB to YUV conversion failed: %d\n", err);
        goto __cleanup_context;
    }

    printf("RGB to YUV conversion completed\n");

    /* Flush and wait for Blit to complete */
    if (artvg_flush(ctx.vg) != ARTVG_SUCCESS) {
        printf("Failed to flush after Blit\n");
    }
    if (artvg_wait_finish(ctx.vg) != ARTVG_SUCCESS) {
        printf("Failed to wait for Blit completion\n");
    }

    /* ============================================
     * Step 5: Display intermediate result (YUV -> RGB, left side)
     * ============================================ */
    printf("\nStep 2: Displaying intermediate result (YUV -> RGB, left side)...\n");
    struct mpp_buf dst_left = ctx.dst;
    int left_offset_x = 0;
    int left_offset_y = 0;

    /* Set crop for left side display */
    dst_left.crop_en = 1;
    dst_left.crop.x = left_offset_x;
    dst_left.crop.y = left_offset_y;
    dst_left.crop.width = src_width;
    dst_left.crop.height = src_height;

    /* Use blit2 to convert YUV back to RGB and display on left side */
    err = artvg_blit2(ctx.vg, ctx.yuv, &dst_left, &dst_left, NULL, &blit_ctrl, &blit_ctl);
    if (err != ARTVG_SUCCESS) {
        printf("Intermediate YUV -> RGB display failed: %d\n", err);
        goto __cleanup_context;
    }
    printf("Intermediate result displayed on left side (offset: %d, %d)\n", left_offset_x, left_offset_y);

    /* Flush and wait for intermediate display */
    if (artvg_flush(ctx.vg) != ARTVG_SUCCESS) {
        printf("Failed to flush after intermediate display\n");
    }
    if (artvg_wait_finish(ctx.vg) != ARTVG_SUCCESS) {
        printf("Failed to wait for intermediate display completion\n");
    }

    /* ============================================
     * Step 6: Create projective LUT
     * ============================================ */
    printf("\nStep 3: Creating projective LUT...\n");
    projective = artvg_projective_allocate(
        ctx.vg, &matrix, &config.transform_edge,
        (artvg_projective_lut_mode_t)config.lut_mode,
        (artvg_projective_lut_block_size_t)config.block_size,
        (artvg_projective_out_blk_mode_t)config.out_blk_mode,
        ctx.dst.size.width, ctx.dst.size.height
    );
    if (!projective) {
        printf("Failed to create projective LUT\n");
        goto __cleanup_context;
    }
    ctx.lut_created = 1;
    printf("Projective LUT created successfully (size: %u bytes)\n", projective->size);

    /* ============================================
     * Step 7: YUV -> RGB using projective2 (CSC3), right side
     * ============================================ */
    printf("\nStep 4: Executing projective transformation (YUV -> RGB, right side)...\n");
    printf("  Source: YUV (converted from RGB via CSC2)\n");
    printf("  Destination: RGB (DRM framebuffer, right side)\n");
    printf("  CSC3 will automatically convert YUV input to RGB\n");
    struct mpp_buf dst_right = ctx.dst;
    int right_offset_x = src_width + 10;  /* 10 pixels gap between left and right */
    int right_offset_y = 0;

    /* Set crop for right side display */
    dst_right.crop_en = 1;
    dst_right.crop.x = right_offset_x;
    dst_right.crop.y = right_offset_y;
    dst_right.crop.width = ctx.dst.size.width;
    dst_right.crop.height = ctx.dst.size.height;

    err = artvg_projective(ctx.vg, ctx.yuv, &dst_right, &ctrl, projective);
    if (err != ARTVG_SUCCESS) {
        printf("Projective transformation failed: %d\n", err);
        goto __cleanup_context;
    }

    printf("Projective transformation executed successfully (right side, offset: %d, %d)\n",
           right_offset_x, right_offset_y);

    artvg_dumping_cmd(ctx.vg);

    if (artvg_flush(ctx.vg) != ARTVG_SUCCESS) {
        printf("Failed to flush buffer\n");
    }

    if (artvg_wait_finish(ctx.vg) != ARTVG_SUCCESS) {
        printf("Failed to finish buffer operations\n");
    }

    if (drm_buffer_flush(&ctx.drm_dev, 0) != 0) {
        printf("Failed to flush DRM buffer\n");
    }

    if (drm_wait_vsync(&ctx.drm_dev) != 0) {
        printf("Failed to wait for vertical sync\n");
    }

    printf("\n========================================\n");
    printf("Test completed successfully!\n");
    printf("Display layout:\n");
    printf("  Left side:  YUV -> RGB (CSC3 via Blit, intermediate result)\n");
    printf("  Right side: YUV -> RGB (CSC3 + Projective, final result)\n");
    printf("========================================\n");
    printf("Press Enter to exit...\n");

    getchar();

__cleanup_context:
    cleanup_context(&ctx, &projective);
    printf("Test program exited normally\n");
    return 0;
}
