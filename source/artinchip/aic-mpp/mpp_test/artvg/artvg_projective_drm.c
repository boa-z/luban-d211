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

typedef enum {
    PARSE_SUCCESS,
    PARSE_ERROR_MEMORY,
    PARSE_ERROR_INVALID_COUNT
} parse_result_t;

typedef struct {
    struct artvg *vg;
    struct mpp_buf *src;
    struct mpp_buf dst;
    void *src_mmap[MAX_PLANES];
    drm_dev_t drm_dev;
    int dma_fd_added;
    int lut_created;
    int src_allocated;
    int src_mapped;
    int drm_opened;
} test_context_t;

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

static void show_usage(const char *program_name)
{
    printf("Usage: %s [options]\n", program_name);
    printf("\nProjective transformation test program - supports perspective and affine transformations\n");

    printf("\n=== Transformation Parameters ===\n");
    printf("  --translate <tx,ty>   Translation (default: 0,0)\n");
    printf("  --scale <sx,sy>       Scaling (default: 1.0,1.0)\n");
    printf("  --rotate <angle>      Rotation angle in degrees (default: 0.0)\n");
    printf("  --skew <kx,ky>        Skew/shear (default: 0.0,0.0)\n");
    printf("  --perspective <px,py> Perspective values (default: 0.0,0.0)\n");
    printf("  --center <cx,cy>      Rotation center point (default: source image center)\n");
    printf("  --transform-order <order> Transformation order (t:translate, s:scale, r:rotate, k:skew, p:perspective)\n");
    printf("                        Example: 'trsp' means translate->rotate->scale->perspective (default: 'trskp')\n");

    printf("\n=== LUT Configuration ===\n");
    printf("  --lut-mode <0-7>      LUT mode (default: 0=S10_6)\n");
    printf("  --block-size <0-3>    LUT block size (default: 1=16x16)\n");

    printf("\n=== Source Image Parameters ===\n");
    printf("  --src-crop <x,y,w,h>  Source crop region (default: 0,0,240,120)\n");

    printf("\n=== Destination Image Parameters ===\n");
    printf("  --dst-crop <x,y,w,h>  Destination crop region (default: no crop)\n");

    printf("\n=== Blending Control Parameters ===\n");
    printf("  --alpha-en <0|1>      Enable alpha blending (default: 0)\n");
    printf("  --alpha-rules <value> Alpha blend rules (default: 0=ARTVG_BLEND_DEFAULT)\n");

    printf("\n=== Edge Fill Control Parameters ===\n");
    printf("  --fill-bg-en <0|1>    Enable background fill (default: 0)\n");
    printf("  --blend-edge-en <0|1> Enable edge blending (default: 0)\n");
    printf("  --edge-color <hex>    Edge fill color (default: 0xFF000000)\n");
    printf("  --edge-pad-mode <0-2> Edge fill mode (default: 0=ARTVG_PAD_MODE_FIXED_COLOR)\n");

    printf("\n=== Help ===\n");
    printf("  --help                Show this help message\n");

    printf("\n=== Examples ===\n");
    printf("  1. No transformation (simple copy):\n");
    printf("     %s\n", program_name);
    printf("  2. Translation only:\n");
    printf("     %s --translate 100,50\n", program_name);
    printf("  3. Perspective transformation:\n");
    printf("     %s --perspective 0.001,0.001\n", program_name);
    printf("  4. Combined transformations:\n");
    printf("     %s --translate 100,50 --scale 1.5,1.0 --rotate 30.0 --perspective 0.0005,0.0003\n", program_name);
}

static void cleanup_context(test_context_t *ctx, artvg_projective_t *projective)
{
    if (!ctx) {
        return;
    }

    if (ctx->lut_created && projective) {
        artvg_projective_free(ctx->vg, projective);
    }
    if (ctx->dma_fd_added && ctx->vg && ctx->dst.fd[0] >= 0) {
        artvg_remove_dma_fd(ctx->vg, ctx->dst.fd[0]);
    }
    if (ctx->src_mapped && ctx->src) {
        dma_buf_unmap(ctx->src, ctx->src_mmap);
    }
    if (ctx->src_allocated && ctx->src) {
        artvg_free(ctx->vg, ctx->src);
    }
    if (ctx->drm_opened) {
        drm_device_close(&ctx->drm_dev);
    }
    if (ctx->vg) {
        artvg_destroy(ctx->vg);
    }
}

int main(int argc, char **argv)
{
    test_context_t ctx = {0};
    artvg_ctrl_t ctrl = {0};
    artvg_transform_edge_t transform_edge = {0};
    artvg_projective_t *projective = NULL;
    artvg_matrix_t matrix;

    float translate[2] = {0.0f, 0.0f};
    float scale[2] = {1.0f, 1.0f};
    float rotate_angle = 0.0f;
    float skew[2] = {0.0f, 0.0f};
    float perspective[2] = {0.0f, 0.0f};
    float center[2] = {0.0f, 0.0f};
    char transform_order[MAX_ORDER_LEN] = "trskp";

    int lut_mode = 5;
    int block_size = 1;
    int out_blk_mode = 1;

    int src_crop[4] = {0, 0, 0, 0};
    int src_crop_en = 0;

    int dst_crop[4] = {0, 0, 0, 0};
    int dst_crop_en = 0;

    transform_edge.fill_en = 0;
    transform_edge.blend_en = 0;
    transform_edge.fill_color = 0xFF000000;
    transform_edge.pad_mode = ARTVG_PAD_MODE_FIXED_COLOR;

    static struct option long_options[] = {
        {"translate", required_argument, 0, 't'},
        {"scale", required_argument, 0, 's'},
        {"rotate", required_argument, 0, 'r'},
        {"skew", required_argument, 0, 'k'},
        {"perspective", required_argument, 0, 'p'},
        {"center", required_argument, 0, 'c'},
        {"transform-order", required_argument, 0, 'o'},
        {"lut-mode", required_argument, 0, 'l'},
        {"block-size", required_argument, 0, 'b'},
        {"src-crop", required_argument, 0, '1'},
        {"dst-crop", required_argument, 0, '2'},
        {"alpha-en", required_argument, 0, 'a'},
        {"alpha-rules", required_argument, 0, 'u'},
        {"fill-bg-en", required_argument, 0, 'f'},
        {"blend-edge-en", required_argument, 0, 'e'},
        {"edge-color", required_argument, 0, '3'},
        {"edge-pad-mode", required_argument, 0, '4'},
        {"help", no_argument, 0, 'h'},
        {0, 0, 0, 0}
    };

    printf("=== ARTVG Projective Transformation Test ===\n\n");

    int opt;
    while ((opt = getopt_long(argc, argv, "h", long_options, NULL)) != -1) {
        switch (opt) {
        case 't':
            if (parse_number_list(optarg, translate, 2, 1) != PARSE_SUCCESS) {
                printf("Error: Translation requires 2 comma-separated floats\n");
                return -1;
            }
            break;
        case 's':
            if (parse_number_list(optarg, scale, 2, 1) != PARSE_SUCCESS) {
                printf("Error: Scaling requires 2 comma-separated floats\n");
                return -1;
            }
            break;
        case 'r':
            rotate_angle = atof(optarg);
            break;
        case 'k':
            if (parse_number_list(optarg, skew, 2, 1) != PARSE_SUCCESS) {
                printf("Error: Skew requires 2 comma-separated floats\n");
                return -1;
            }
            break;
        case 'p':
            if (parse_number_list(optarg, perspective, 2, 1) != PARSE_SUCCESS) {
                printf("Error: Perspective requires 2 comma-separated floats\n");
                return -1;
            }
            break;
        case 'c':
            if (parse_number_list(optarg, center, 2, 1) != PARSE_SUCCESS) {
                printf("Error: Center point requires 2 comma-separated floats\n");
                return -1;
            }
            break;
        case 'o':
            strncpy(transform_order, optarg, MAX_ORDER_LEN - 1);
            transform_order[MAX_ORDER_LEN - 1] = '\0';
            break;
        case 'l':
            lut_mode = atoi(optarg);
            if (lut_mode < 0 || lut_mode > 7) {
                printf("Error: LUT mode must be 0-7\n");
                return -1;
            }
            break;
        case 'b':
            block_size = atoi(optarg);
            if (block_size < 0 || block_size > 3) {
                printf("Error: Block size must be 0-3\n");
                return -1;
            }
            break;
        case '1':
            if (parse_number_list(optarg, src_crop, 4, 0) != PARSE_SUCCESS) {
                printf("Error: Source crop requires 4 comma-separated integers\n");
                return -1;
            }
            src_crop_en = 1;
            break;
        case '2':
            if (parse_number_list(optarg, dst_crop, 4, 0) != PARSE_SUCCESS) {
                printf("Error: Destination crop requires 4 comma-separated integers\n");
                return -1;
            }
            dst_crop_en = 1;
            break;
        case 'a':
            ctrl.alpha_en = atoi(optarg);
            break;
        case 'u':
            ctrl.alpha_rules = atoi(optarg);
            break;
        case 'f':
            transform_edge.fill_en = atoi(optarg);
            break;
        case 'e':
            transform_edge.blend_en = atoi(optarg);
            break;
        case '3':
            transform_edge.fill_color = strtoul(optarg, NULL, 0);
            break;
        case '4':
            transform_edge.pad_mode = atoi(optarg);
            break;
        case 'h':
            show_usage(argv[0]);
            return 0;
        default:
            printf("Unknown option, use --help for usage\n");
            return -1;
        }
    }

    int src_width = alpha_baby_card_240x120.header.w;
    int src_height = alpha_baby_card_240x120.header.h;

    if (center[0] == 0.0f && center[1] == 0.0f) {
        center[0] = src_width / 2.0f;
        center[1] = src_height / 2.0f;
    }

    if (!src_crop_en) {
        src_crop[0] = 0;
        src_crop[1] = 0;
        src_crop[2] = src_width;
        src_crop[3] = src_height;
        src_crop_en = 1;
    }

    create_transform_matrix(&matrix, translate, scale, rotate_angle,
                           skew, center, perspective, transform_order);

    printf("Transformation parameters:\n");
    printf("  Translation: (%.2f, %.2f)\n", translate[0], translate[1]);
    printf("  Scaling: (%.2f, %.2f)\n", scale[0], scale[1]);
    printf("  Rotation: %.2f degrees\n", rotate_angle);
    printf("  Skew: (%.2f, %.2f)\n", skew[0], skew[1]);
    printf("  Perspective: (%.6f, %.6f)\n", perspective[0], perspective[1]);
    printf("  Center point: (%.2f, %.2f)\n", center[0], center[1]);
    printf("  Transformation order: %s\n", transform_order);

    print_matrix(&matrix, "Projective");

    printf("\nLUT configuration:\n");
    printf("  LUT mode: %d\n", lut_mode);
    printf("  Block size: %s\n",
           block_size == 0 ? "8x8" :
           block_size == 1 ? "16x16" :
           block_size == 2 ? "32x32" : "64x64");

    ctx.vg = artvg_create();
    if (!ctx.vg) {
        printf("Failed to open ARTVG device\n");
        return -1;
    }
    printf("\nARTVG device opened successfully\n");

    ctx.src = artvg_allocate(ctx.vg, src_width, src_height, MPP_FMT_ARGB_8888);
    if (!ctx.src) {
        printf("Failed to allocate source buffer (%dx%d)\n", src_width, src_height);
        goto __cleanup_context;
    }
    ctx.src_allocated = 1;
    printf("Source buffer allocated successfully\n");

    if (src_crop_en) {
        ctx.src->crop_en = 1;
        ctx.src->crop.x = src_crop[0];
        ctx.src->crop.y = src_crop[1];
        ctx.src->crop.width = src_crop[2];
        ctx.src->crop.height = src_crop[3];
    }

    if (dma_buf_mmap(ctx.src, ctx.src_mmap) != 0) {
        printf("Failed to map source buffer\n");
        goto __cleanup_context;
    }
    ctx.src_mapped = 1;

    if (image_array_read(&alpha_baby_card_240x120, ctx.src_mmap[0],
                         ctx.src->size.height * ctx.src->stride[0]) != 0) {
        printf("Failed to load source image\n");
        goto __cleanup_context;
    }
    dmabuf_sync(ctx.src->fd[0], DMA_BUF_SYNC_END);
    printf("Source image loaded: %dx%d\n", src_width, src_height);

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

    if (dst_crop_en) {
        ctx.dst.crop_en = 1;
        ctx.dst.crop.x = dst_crop[0];
        ctx.dst.crop.y = dst_crop[1];
        ctx.dst.crop.width = dst_crop[2];
        ctx.dst.crop.height = dst_crop[3];
    }

    printf("Destination buffer: %dx%d\n", ctx.dst.size.width, ctx.dst.size.height);

    if (artvg_add_dma_fd(ctx.vg, ctx.dst.fd[0]) != ARTVG_SUCCESS) {
        printf("Failed to add DMA FD\n");
        goto __cleanup_context;
    }
    ctx.dma_fd_added = 1;
    printf("DMA FD added successfully\n");

    if (ctrl.alpha_en) {
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
    printf("  Background fill: %s\n", transform_edge.fill_en ? "enabled" : "disabled");
    printf("  Edge blending: %s\n", transform_edge.blend_en ? "enabled" : "disabled");
    printf("  Edge color: 0x%08X\n", transform_edge.fill_color);
    printf("  Edge pad mode: %d\n", transform_edge.pad_mode);

    printf("\nCreating projective LUT...\n");
    projective = artvg_projective_allocate(
        ctx.vg, &matrix, &transform_edge,
        (artvg_projective_lut_mode_t)lut_mode,
        (artvg_projective_lut_block_size_t)block_size,
        (artvg_projective_out_blk_mode_t)out_blk_mode,
        ctx.dst.size.width, ctx.dst.size.height
    );
    if (!projective) {
        printf("Failed to create projective LUT\n");
        goto __cleanup_context;
    }
    ctx.lut_created = 1;
    printf("Projective LUT created successfully (size: %u bytes)\n", projective->size);

    printf("\nExecuting projective transformation...\n");
    artvg_error_t err = artvg_projective2(ctx.vg, ctx.src, &ctx.dst, &ctx.dst,
                           &ctrl, projective);
    if (err != ARTVG_SUCCESS) {
        printf("Projective transformation failed: %d\n", err);
        goto __cleanup_context;
    }
    printf("Projective transformation executed successfully\n");

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

    printf("\nProjective transformation test completed! Result displayed on screen.\n");
    printf("Press Enter to exit...\n");

    getchar();

__cleanup_context:
    cleanup_context(&ctx, projective);
    printf("Test program exited normally\n");
    return 0;
}
