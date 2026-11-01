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

/* External image declarations */
extern const image_dsc_t alpha_baby_card_240x120;
extern const image_dsc_t alpha_poney_240x120;

/* DMA buffer mapping functions */
static int dma_buf_mmap(struct mpp_buf *buf, void *map[])
{
    for (int i = 0; i < 3; i++) {
        if (buf->fd[i] < 0) {
            map[i] = NULL;
            continue;
        }

        size_t map_size = buf->stride[i] * buf->size.height;
        void *map_mem = mmap(NULL, map_size, PROT_READ | PROT_WRITE, MAP_SHARED, buf->fd[i], 0);

        if (map_mem == MAP_FAILED) {
            printf("mmap error for fd[%d]\n", i);
            map[i] = NULL;
            return (i == 0) ? -1 : 0;
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

    for (int i = 0; i < 3; i++) {
        if (map[i] && buf->fd[i] >= 0) {
            size_t map_size = buf->stride[i] * buf->size.height;
            munmap(map[i], map_size);
            map[i] = NULL;
        }
    }
}

/* Display usage information */
void show_usage(const char *program_name)
{
    printf("Usage: %s [options]\n", program_name);
    printf("\nAffine transformation test program - supports multiple transformations\n");

    printf("\n=== Transformation Parameters ===\n");
    printf("  --matrix-direct <m00,m01,m02,m10,m11,m12,m20,m21,m22>\n");
    printf("                        Direct 3x3 matrix specification (comma-separated)\n");
    printf("  --translate <tx,ty>   Translation (default: 0,0)\n");
    printf("  --scale <sx,sy>       Scaling (default: 1.0,1.0)\n");
    printf("  --rotate <angle>      Rotation angle in degrees (default: 0.0)\n");
    printf("  --skew <kx,ky>        Skew/shear (default: 0.0,0.0)\n");
    printf("  --center <cx,cy>      Rotation center point (default: source image center)\n");
    printf("  --transform-order <order> Transformation order (t:translate, s:scale, r:rotate, k:skew)\n");
    printf("                        Example: 'trs' means translate->rotate->scale (default: 'trs')\n");

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

    printf("\n=== Auto Bounding Box Parameters ===\n");
    printf("  --auto-bbox <0|1>     Enable auto bounding box transform (default: 0)\n");

    printf("\n=== Help ===\n");
    printf("  --help                Show this help message\n");

    printf("\n=== Examples ===\n");
    printf("  1. Translation:\n");
    printf("     %s --translate 100,50\n", program_name);
    printf("  2. Scaling and rotation:\n");
    printf("     %s --scale 1.5,1.5 --rotate 45.0\n", program_name);
    printf("  3. Direct matrix:\n");
    printf("     %s --matrix-direct 0.707,-0.707,100,0.707,0.707,50,0,0,1\n", program_name);
}

/* Parse comma-separated float list */
static int parse_float_list(const char *str, float *values, int max_count)
{
    char *token;
    char *str_copy = strdup(str);
    char *saveptr = NULL;
    int count = 0;

    if (!str_copy) return -1;

    token = strtok_r(str_copy, ",", &saveptr);
    while (token && count < max_count) {
        values[count++] = atof(token);
        token = strtok_r(NULL, ",", &saveptr);
    }

    free(str_copy);
    return count;
}

/* Parse comma-separated integer list */
static int parse_int_list(const char *str, int *values, int max_count)
{
    char *token;
    char *str_copy = strdup(str);
    char *saveptr = NULL;
    int count = 0;

    if (!str_copy) return -1;

    token = strtok_r(str_copy, ",", &saveptr);
    while (token && count < max_count) {
        values[count++] = atoi(token);
        token = strtok_r(NULL, ",", &saveptr);
    }

    free(str_copy);
    return count;
}

/* Create transformation matrix */
static void create_transform_matrix(artvg_matrix_t *matrix,
                                   const float *translate,
                                   const float *scale,
                                   float rotate_angle,
                                   const float *skew,
                                   const float *center,
                                   const char *order)
{
    float cx = center[0];
    float cy = center[1];

    artvg_matrix_identity(matrix);

    if (!order || strlen(order) == 0) {
        order = "trs";
    }

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
                if (cx != 0.0f || cy != 0.0f) {
                    artvg_point_t center_point = {cx, cy};
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

        default:
            printf("Warning: Unknown transformation order character '%c'\n", order[i]);
            continue;
        }
    }
}

/* Print matrix */
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

/* Main function */
int main(int argc, char **argv)
{
    struct artvg *vg = NULL;
    struct mpp_buf *src = NULL;
    struct mpp_buf dst = {0};
    void *src_mmap[3] = {NULL};
    drm_dev_t drm_dev = {0};
    artvg_ctrl_t ctrl = {0};
    artvg_transform_edge_t transform_edge = {0};
    artvg_matrix_t matrix;

    /* Default parameters */
    float translate[2] = {0.0f, 0.0f};
    float scale[2] = {1.0f, 1.0f};
    float rotate_angle = 0.0f;
    float skew[2] = {0.0f, 0.0f};
    float center[2] = {0.0f, 0.0f};
    int center_specified = 0;
    char transform_order[10] = "trsk";

    float direct_matrix[9] = {1.0f, 0.0f, 0.0f,
                              0.0f, 1.0f, 0.0f,
                              0.0f, 0.0f, 1.0f};
    int use_direct_matrix = 0;

    int src_crop[4] = {0, 0, 0, 0};
    int src_crop_en = 0;

    int dst_crop[4] = {0, 0, 0, 0};
    int dst_crop_en = 0;

    int auto_bbox_en = 0;  /* Auto bounding box transform, default: disabled */

    /* Blending control defaults */
    memset(&ctrl, 0, sizeof(artvg_ctrl_t));
    ctrl.alpha_en = 0;
    ctrl.alpha_rules = 0;  /* ARTVG_BLEND_DEFAULT */

    /* Edge fill control defaults */
    memset(&transform_edge, 0, sizeof(artvg_transform_edge_t));
    transform_edge.fill_en = 0;
    transform_edge.blend_en = 0;
    transform_edge.fill_color = 0xFF000000;
    transform_edge.pad_mode = ARTVG_PAD_MODE_FIXED_COLOR;

    /* Command line option definition */
    static struct option long_options[] = {
        /* Transformation parameters */
        {"matrix-direct", required_argument, 0, 0},
        {"translate", required_argument, 0, 0},
        {"scale", required_argument, 0, 0},
        {"rotate", required_argument, 0, 0},
        {"skew", required_argument, 0, 0},
        {"center", required_argument, 0, 0},
        {"transform-order", required_argument, 0, 0},

        /* Source image parameters */
        {"src-crop", required_argument, 0, 0},

        /* Destination image parameters */
        {"dst-crop", required_argument, 0, 0},

        /* Blending control parameters */
        {"alpha-en", required_argument, 0, 0},
        {"alpha-rules", required_argument, 0, 0},

        /* Edge fill control parameters */
        {"fill-bg-en", required_argument, 0, 0},
        {"blend-edge-en", required_argument, 0, 0},
        {"edge-color", required_argument, 0, 0},
        {"edge-pad-mode", required_argument, 0, 0},

        /* Auto bounding box parameters */
        {"auto-bbox", required_argument, 0, 0},

        /* Help */
        {"help", no_argument, 0, 'h'},
        {0, 0, 0, 0}
    };

    int opt;
    int option_index = 0;

    printf("=== ARTVG Affine Transformation Test ===\n\n");

    /* Parse command line arguments */
    while (1) {
        opt = getopt_long(argc, argv, "h", long_options, &option_index);
        if (opt == -1)
            break;

        switch (opt) {
        case 0:
            if (strncmp(long_options[option_index].name, "matrix-direct", strlen("matrix-direct")) == 0) {
                if (parse_float_list(optarg, direct_matrix, 9) == 9) {
                    use_direct_matrix = 1;
                } else {
                    printf("Error: Matrix requires 9 comma-separated floats\n");
                    return -1;
                }
            } else if (strncmp(long_options[option_index].name, "translate", strlen("translate")) == 0) {
                if (parse_float_list(optarg, translate, 2) != 2) {
                    printf("Error: Translation requires 2 comma-separated floats\n");
                    return -1;
                }
            } else if (strncmp(long_options[option_index].name, "scale", strlen("scale")) == 0) {
                if (parse_float_list(optarg, scale, 2) != 2) {
                    printf("Error: Scaling requires 2 comma-separated floats\n");
                    return -1;
                }
            } else if (strncmp(long_options[option_index].name, "rotate", strlen("rotate")) == 0) {
                rotate_angle = atof(optarg);
            } else if (strncmp(long_options[option_index].name, "skew", strlen("skew")) == 0) {
                if (parse_float_list(optarg, skew, 2) != 2) {
                    printf("Error: Skew requires 2 comma-separated floats\n");
                    return -1;
                }
            } else if (strncmp(long_options[option_index].name, "center", strlen("center")) == 0) {
                if (parse_float_list(optarg, center, 2) != 2) {
                    printf("Error: Center point requires 2 comma-separated floats\n");
                    return -1;
                }
                center_specified = 1;
            } else if (strncmp(long_options[option_index].name, "transform-order", strlen("transform-order")) == 0) {
                strncpy(transform_order, optarg, sizeof(transform_order) - 1);
                transform_order[sizeof(transform_order) - 1] = '\0';
            } else if (strncmp(long_options[option_index].name, "src-crop", strlen("src-crop")) == 0) {
                if (parse_int_list(optarg, src_crop, 4) == 4) {
                    src_crop_en = 1;
                } else {
                    printf("Error: Source crop requires 4 comma-separated integers\n");
                    return -1;
                }
            } else if (strncmp(long_options[option_index].name, "dst-crop", strlen("dst-crop")) == 0) {
                if (parse_int_list(optarg, dst_crop, 4) == 4) {
                    dst_crop_en = 1;
                } else {
                    printf("Error: Destination crop requires 4 comma-separated integers\n");
                    return -1;
                }
            } else if (strncmp(long_options[option_index].name, "alpha-en", strlen("alpha-en")) == 0) {
                ctrl.alpha_en = atoi(optarg);
            } else if (strncmp(long_options[option_index].name, "alpha-rules", strlen("alpha-rules")) == 0) {
                ctrl.alpha_rules = atoi(optarg);
            } else if (strncmp(long_options[option_index].name, "fill-bg-en", strlen("fill-bg-en")) == 0) {
                transform_edge.fill_en = atoi(optarg);
            } else if (strncmp(long_options[option_index].name, "blend-edge-en", strlen("blend-edge-en")) == 0) {
                transform_edge.blend_en = atoi(optarg);
            } else if (strncmp(long_options[option_index].name, "edge-color", strlen("edge-color")) == 0) {
                transform_edge.fill_color = strtoul(optarg, NULL, 0);
            } else if (strncmp(long_options[option_index].name, "edge-pad-mode", strlen("edge-pad-mode")) == 0) {
                transform_edge.pad_mode = atoi(optarg);
            } else if (strncmp(long_options[option_index].name, "auto-bbox", strlen("auto-bbox")) == 0) {
                auto_bbox_en = atoi(optarg);
            }
            break;

        case 'h':
            show_usage(argv[0]);
            return 0;

        default:
            printf("Unknown option, use --help for usage\n");
            return -1;
        }
    }

    /* Set source dimensions from image header */
    int src_width = alpha_baby_card_240x120.header.w;
    int src_height = alpha_baby_card_240x120.header.h;

    /* If no center point specified, use source image center */
    if (!center_specified) {
        center[0] = src_width / 2.0f;
        center[1] = src_height / 2.0f;
    }

    /* If source crop not specified, use full image */
    if (!src_crop_en) {
        src_crop[0] = 0;
        src_crop[1] = 0;
        src_crop[2] = src_width;
        src_crop[3] = src_height;
        src_crop_en = 1;
    }

    /* Create transformation matrix */
    if (use_direct_matrix) {
        /* Use directly specified matrix */
        for (int i = 0; i < 3; i++) {
            for (int j = 0; j < 3; j++) {
                matrix.m[i][j] = direct_matrix[i * 3 + j];
            }
        }
        printf("Using directly specified matrix\n");
    } else {
        /* Create matrix from transformation parameters */
        create_transform_matrix(&matrix, translate, scale, rotate_angle,
                               skew, center, transform_order);

        printf("Transformation parameters:\n");
        printf("  Translation: (%.2f, %.2f)\n", translate[0], translate[1]);
        printf("  Scaling: (%.2f, %.2f)\n", scale[0], scale[1]);
        printf("  Rotation: %.2f degrees\n", rotate_angle);
        printf("  Skew: (%.2f, %.2f)\n", skew[0], skew[1]);
        printf("  Center point: (%.2f, %.2f)\n", center[0], center[1]);
        printf("  Transformation order: %s\n", transform_order);
    }

    print_matrix(&matrix, "Affine");

    vg = artvg_create();
    if (!vg) {
        printf("Failed to open ARTVG device\n");
        return -1;
    }
    printf("ARTVG device opened successfully\n");

    src = artvg_allocate(vg, src_width, src_height, MPP_FMT_ARGB_8888);
    if (!src) {
        printf("Failed to allocate source buffer (%dx%d)\n", src_width, src_height);
        goto cleanup;
    }
    printf("Source buffer allocated successfully\n");

    if (src_crop_en) {
        src->crop_en = 1;
        src->crop.x = src_crop[0];
        src->crop.y = src_crop[1];
        src->crop.width = src_crop[2];
        src->crop.height = src_crop[3];
    }

    if (dma_buf_mmap(src, src_mmap) != 0) {
        printf("Failed to map source buffer\n");
        goto cleanup;
    }

    if (image_array_read(&alpha_baby_card_240x120, src_mmap[0], src->size.height * src->stride[0]) != 0) {
        printf("Failed to load source image\n");
        goto cleanup;
    }
    dmabuf_sync(src->fd[0], DMA_BUF_SYNC_END);
    printf("Source image loaded: %dx%d\n", src_width, src_height);

    if (drm_device_open(&drm_dev, "/dev/dri/card0", -1) != 0) {
        printf("Failed to open DRM device\n");
        goto cleanup;
    }
    printf("DRM device opened successfully\n");

    if (drm_buffer_to_mpp_buffer(&drm_dev, 0, &dst) != 0) {
        printf("Failed to convert DRM buffer to MPP buffer\n");
        goto cleanup;
    }

    if (dst_crop_en) {
        dst.crop_en = 1;
        dst.crop.x = dst_crop[0];
        dst.crop.y = dst_crop[1];
        dst.crop.width = dst_crop[2];
        dst.crop.height = dst_crop[3];
    }

    printf("Destination buffer: %dx%d\n", dst.size.width, dst.size.height);

    if (artvg_add_dma_fd(vg, dst.fd[0]) != ARTVG_SUCCESS) {
        printf("Failed to add DMA FD\n");
        goto cleanup;
    }
    printf("DMA FD added successfully\n");

    if (ctrl.alpha_en) {
        void *drm_map = drm_framebuffer_get_map(&drm_dev, 0);
        if (drm_map) {
            if (image_array_read_with_stride(&alpha_poney_240x120, drm_map, dst.stride[0]) != 0) {
                printf("Failed to load background image, using gray background\n");
                memset(drm_map, 0x80, dst.stride[0] * dst.size.height);
            } else {
                printf("Background image loaded successfully\n");
            }
            dmabuf_sync(dst.fd[0], DMA_BUF_SYNC_END);
        }
    }

    printf("\nBlending control parameters:\n");
    printf("  Alpha blending: %s (rules: %d)\n",
           ctrl.alpha_en ? "enabled" : "disabled", ctrl.alpha_rules);

    printf("\nEdge fill control parameters:\n");
    printf("  Background fill: %s\n", transform_edge.fill_en ? "enabled" : "disabled");
    printf("  Edge blending: %s\n", transform_edge.blend_en ? "enabled" : "disabled");
    printf("  Edge color: 0x%08X\n", transform_edge.fill_color);
    printf("  Edge fill mode: %d\n", transform_edge.pad_mode);

    printf("\nAuto bounding box transform: %s\n", auto_bbox_en ? "enabled" : "disabled");

    if (auto_bbox_en && !dst_crop_en) {
        struct mpp_rect src_bbx = {0};
        struct mpp_rect transformed_bbx = {0};
        struct mpp_rect clip_rect = {0};

        if (src->crop_en) {
            src_bbx = src->crop;
        } else {
            src_bbx.x = 0;
            src_bbx.y = 0;
            src_bbx.width = src->size.width;
            src_bbx.height = src->size.height;
        }

        if (dst.crop_en) {
            memcpy(&clip_rect, &dst.crop, sizeof(struct mpp_rect));
        } else {
            clip_rect.x = 0;
            clip_rect.y = 0;
            clip_rect.width = dst.size.width;
            clip_rect.height = dst.size.height;
        }

        if (artvg_transform_bounding_box(&src_bbx, &matrix, &clip_rect, &transformed_bbx) == ARTVG_SUCCESS) {
            dst.crop_en = 1;
            dst.crop.x = transformed_bbx.x;
            dst.crop.y = transformed_bbx.y;
            dst.crop.width = transformed_bbx.width + transformed_bbx.x;
            dst.crop.height = transformed_bbx.height + transformed_bbx.y;

            if (dst.crop.width > dst.size.width) {
                dst.crop.width = dst.size.width;
            }
            if (dst.crop.height > dst.size.height) {
                dst.crop.height = dst.size.height;
            }

            printf("Auto-calculated dst crop from transform: x=%d, y=%d, width=%d, height=%d\n",
                   dst.crop.x, dst.crop.y, dst.crop.width, dst.crop.height);
        }
    }

    printf("\nExecuting affine transformation blit...\n");

    artvg_error_t err = artvg_affine(vg, src, &dst, NULL,
                                          &matrix, &ctrl, &transform_edge);
    if (err != ARTVG_SUCCESS) {
        printf("Affine transformation failed: %d\n", err);
        goto cleanup;
    }
    printf("Affine transformation executed successfully\n");

    artvg_dumping_cmd(vg);

    if (artvg_flush(vg) != ARTVG_SUCCESS) {
        printf("Failed to flush buffer\n");
    } else {
        printf("Buffer flushed successfully\n");
    }

    if (artvg_wait_finish(vg) != ARTVG_SUCCESS) {
        printf("Failed to finish buffer operations\n");
    } else {
        printf("Buffer operations completed\n");
    }

    if (drm_buffer_flush(&drm_dev, 0) != 0) {
        printf("Failed to flush DRM buffer\n");
    } else {
        printf("DRM buffer flushed successfully\n");
    }

    if (drm_wait_vsync(&drm_dev) != 0) {
        printf("Failed to wait for vertical sync\n");
        artvg_remove_dma_fd(vg, dst.fd[0]);
        goto cleanup;
    } else {
        printf("Vertical sync wait successful\n");
    }

    if (artvg_remove_dma_fd(vg, dst.fd[0]) != ARTVG_SUCCESS) {
        printf("Failed to remove DMA FD\n");
        goto cleanup;
    } else {
        printf("DMA FD removed successfully\n");
    }

    printf("\nAffine transformation test completed! Result displayed on screen.\n");
    printf("Press Enter to exit...\n");
    getchar();

cleanup:
    if (src) {
        dma_buf_unmap(src, src_mmap);
        artvg_free(vg, src);
    }
    drm_device_close(&drm_dev);
    if (vg) {
        artvg_destroy(vg);
    }

    printf("Test program exited normally\n");
    return 0;
}

