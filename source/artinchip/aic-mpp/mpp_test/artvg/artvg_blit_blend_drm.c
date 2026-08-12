/*
 * Copyright (c) 2026, ArtInChip Technology Co., Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Authors:  zequan liang <zequan.liang@artinchip.com>
 */

#include <stdio.h>
#include <sys/stat.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>
#include "artvg.h"
#include "vg_drm.h"
#include "image_array.h"

#define DEFAULT_WIDTH          400
#define DEFAULT_HEIGHT         400
#define SRC_CROP_WIDTH         300
#define SRC_CROP_HEIGHT        300
#define SRC_CROP_X             0
#define SRC_CROP_Y             0
#define DST_CROP_WIDTH         300
#define DST_CROP_HEIGHT        300
#define DST_CROP_X             100
#define DST_CROP_Y             100
#define DRM_DEVICE_PATH        "/dev/dri/card0"
#define INVALID_FD             -1

void show_usage(char *program_name)
{
    printf("Usage: %s [options]\n", program_name);
    printf("Options:\n");
    printf("  --rotate <0|1|2|3>        Rotation mode (0: 0°, 1: 90°, 2: 180°, 3: 270°)\n");
    printf("  --mirror <0|1|2>          Mirror mode (0: none, 1: horizontal, 2: vertical)\n");

    printf("\n  Alpha Blend Control:\n");
    printf("  --alpha-en <0|1>          Enable alpha blending (default: 1)\n");
    printf("  --alpha-rules <0-15>      Alpha blend rules (default: 0=ARTVG_BLEND_DEFAULT)\n");
    printf("  --src-alpha-mode <0-3>    Source alpha mode (default: 0=ARTVG_ALPHA_PIXEL)\n");
    printf("  --dst-alpha-mode <0-3>    Destination alpha mode (default: 0=ARTVG_ALPHA_PIXEL)\n");
    printf("  --src-global-alpha <0-255> Source global alpha (default: 255)\n");
    printf("  --dst-global-alpha <0-255> Destination global alpha (default: 255)\n");


    printf("\n  Color Key Control:\n");
    printf("  --color-key-en <0|1>      Enable color key (default: 0)\n");
    printf("  --color-key-value <hex>   Color key value in 0xAARRGGBB format (default: 0x00000000)\n");

    printf("\n  Recolor Control:\n");
    printf("  --src-recolor-en <0|1>    Enable source recolor (default: 0)\n");
    printf("  --src-recolor-value <hex> Source recolor value in 0xAARRGGBB format (default: 0x00000000)\n");

    printf("\n  Premultiply Control:\n");
    printf("  --src-premul-mode <0-3>   Source premultiply mode (default: 0)\n");
    printf("  --dst-premul-mode <0-3>   Destination premultiply mode (default: 0)\n");
    printf("  --output-unpremul-en <0|1> Enable output unpremultiply (default: 0)\n");

    printf("\n  Dither Control:\n");
    printf("  --output-dither <0-3>     Output dither mode (default: 0=ARTVG_DITHER_DISABLE)\n");

    printf("\n  Color Space Control:\n");
    printf("  --src-csc-space <0-3>     Source color space (default: 0)\n");
    printf("  --dst-csc-space <0-3>     Destination color space (default: 0)\n");

    printf("\n  Help:\n");
    printf("  --help                    Show this help message\n");
}

int main(int argc, char **argv)
{
    struct artvg *vg = NULL;
    struct mpp_buf *src = NULL;
    struct mpp_buf *dst = NULL;
    struct mpp_buf out = {0};
    drm_dev_t drm_dev = {0};
    artvg_ctrl_t ctrl = {0};
    artvg_blit_ctl_t blit_ctl = {0};

    int src_allocated = 0;
    int dst_allocated = 0;
    int drm_opened = 0;
    int dma_fd_added = 0;
    int dma_fd = INVALID_FD;

    int rotate = 0;
    int mirror = 0;

    int alpha_en = 1;
    int alpha_rules = ARTVG_BLEND_DEFAULT;
    int src_alpha_mode = ARTVG_ALPHA_PIXEL;
    int dst_alpha_mode = ARTVG_ALPHA_PIXEL;
    int src_global_alpha = 255;
    int dst_global_alpha = 255;

    int color_key_en = 0;
    unsigned int color_key_value = 0x00000000;

    int src_recolor_en = 0;
    unsigned int src_recolor_value = 0x00000000;

    int src_premul_mode = 0;
    int dst_premul_mode = 0;
    int output_unpremul_en = 0;

    int output_dither = ARTVG_DITHER_OFF;

    int src_csc_space = 0;
    int dst_csc_space = 0;

    int opt;
    static struct option long_options[] = {
        {"rotate", required_argument, 0, 0},
        {"mirror", required_argument, 0, 0},

        {"alpha-en", required_argument, 0, 0},
        {"alpha-rules", required_argument, 0, 0},
        {"src-alpha-mode", required_argument, 0, 0},
        {"dst-alpha-mode", required_argument, 0, 0},
        {"src-global-alpha", required_argument, 0, 0},
        {"dst-global-alpha", required_argument, 0, 0},

        {"color-key-en", required_argument, 0, 0},
        {"color-key-value", required_argument, 0, 0},

        {"src-recolor-en", required_argument, 0, 0},
        {"src-recolor-value", required_argument, 0, 0},

        {"src-premul-mode", required_argument, 0, 0},
        {"dst-premul-mode", required_argument, 0, 0},
        {"output-unpremul-en", required_argument, 0, 0},

        {"output-dither", required_argument, 0, 0},

        {"src-csc-space", required_argument, 0, 0},
        {"dst-csc-space", required_argument, 0, 0},

        {"help", no_argument, 0, 'h'},
        {0, 0, 0, 0}
    };

    while (1) {
        int option_index = 0;
        opt = getopt_long(argc, argv, "h", long_options, &option_index);
        if (opt == -1)
            break;

        switch (opt) {
        case 0:
            if (strncmp(long_options[option_index].name, "rotate", strlen("rotate")) == 0) {
                rotate = atoi(optarg);
            } else if (strncmp(long_options[option_index].name, "mirror", strlen("mirror")) == 0) {
                mirror = atoi(optarg);
            }
            else if (strncmp(long_options[option_index].name, "alpha-en", strlen("alpha-en")) == 0) {
                alpha_en = atoi(optarg);
            } else if (strncmp(long_options[option_index].name, "alpha-rules", strlen("alpha-rules")) == 0) {
                alpha_rules = atoi(optarg);
            } else if (strncmp(long_options[option_index].name, "src-alpha-mode", strlen("src-alpha-mode")) == 0) {
                src_alpha_mode = atoi(optarg);
            } else if (strncmp(long_options[option_index].name, "dst-alpha-mode", strlen("dst-alpha-mode")) == 0) {
                dst_alpha_mode = atoi(optarg);
            } else if (strncmp(long_options[option_index].name, "src-global-alpha", strlen("src-global-alpha")) == 0) {
                src_global_alpha = atoi(optarg);
            } else if (strncmp(long_options[option_index].name, "dst-global-alpha", strlen("dst-global-alpha")) == 0) {
                dst_global_alpha = atoi(optarg);
            } else if (strncmp(long_options[option_index].name, "color-key-en", strlen("color-key-en")) == 0) {
                color_key_en = atoi(optarg);
            } else if (strncmp(long_options[option_index].name, "color-key-value", strlen("color-key-value")) == 0) {
                color_key_value = strtoul(optarg, NULL, 0);
            } else if (strncmp(long_options[option_index].name, "src-recolor-en", strlen("src-recolor-en")) == 0) {
                src_recolor_en = atoi(optarg);
            } else if (strncmp(long_options[option_index].name, "src-recolor-value", strlen("src-recolor-value")) == 0) {
                src_recolor_value = strtoul(optarg, NULL, 0);
            } else if (strncmp(long_options[option_index].name, "src-premul-mode", strlen("src-premul-mode")) == 0) {
                src_premul_mode = atoi(optarg);
            } else if (strncmp(long_options[option_index].name, "dst-premul-mode", strlen("dst-premul-mode")) == 0) {
                dst_premul_mode = atoi(optarg);
            } else if (strncmp(long_options[option_index].name, "output-unpremul-en", strlen("output-unpremul-en")) == 0) {
                output_unpremul_en = atoi(optarg);
            } else if (strncmp(long_options[option_index].name, "output-dither", strlen("output-dither")) == 0) {
                output_dither = atoi(optarg);
            } else if (strncmp(long_options[option_index].name, "src-csc-space", strlen("src-csc-space")) == 0) {
                src_csc_space = atoi(optarg);
            } else if (strncmp(long_options[option_index].name, "dst-csc-space", strlen("dst-csc-space")) == 0) {
                dst_csc_space = atoi(optarg);
            }

            else {
                printf("Warning: Unknown option: %s\n", long_options[option_index].name);
            }
            break;
        case 'h':
        default:
            show_usage(argv[0]);
            return 0;
        }
    }

    printf("=== ARTVG Blit Orient Test Configuration ===\n");
    printf("Orientation: rotate=%d, mirror=%d\n", rotate, mirror);
    printf("Alpha Blend: en=%d, rules=%d, src_mode=%d, dst_mode=%d\n",
           alpha_en, alpha_rules, src_alpha_mode, dst_alpha_mode);
    printf("Global Alpha: src=%d, dst=%d\n", src_global_alpha, dst_global_alpha);
    printf("Color Key: en=%d, value=0x%08x\n", color_key_en, color_key_value);
    printf("============================================\n\n");

    vg = artvg_create();
    if (!vg) {
        printf("Failed to open artvg\n");
        return -1;
    }

    src = artvg_allocate(vg, DEFAULT_WIDTH, DEFAULT_HEIGHT, MPP_FMT_ARGB_8888);
    if (!src) {
        printf("Failed to allocate source buffer\n");
        goto cleanup;
    }
    src_allocated = 1;
    src->flags = src_csc_space;
    src->crop_en = 1;
    src->crop.x = SRC_CROP_X;
    src->crop.y = SRC_CROP_Y;
    src->crop.width = SRC_CROP_WIDTH;
    src->crop.height = SRC_CROP_HEIGHT;

    artvg_clear(vg, src, 0x80ff0000);

    dst = artvg_allocate(vg, DEFAULT_WIDTH, DEFAULT_HEIGHT, MPP_FMT_ARGB_8888);
    if (!dst) {
        printf("Failed to allocate destination buffer\n");
        goto cleanup;
    }
    dst_allocated = 1;
    dst->flags = dst_csc_space;
    dst->crop_en = 1;
    dst->crop.x = DST_CROP_X;
    dst->crop.y = DST_CROP_Y;
    dst->crop.width = DST_CROP_WIDTH;
    dst->crop.height = DST_CROP_HEIGHT;

    artvg_clear(vg, dst, 0x800000ff);

    if (drm_device_open(&drm_dev, DRM_DEVICE_PATH, -1) != 0) {
        printf("Failed to open DRM device\n");
        goto cleanup;
    }
    drm_opened = 1;

    memset(&out, 0, sizeof(out));
    if (drm_buffer_to_mpp_buffer(&drm_dev, 0, &out) != 0) {
        printf("Failed to convert DRM buffer to MPP buffer\n");
        goto cleanup;
    }

    if (artvg_add_dma_fd(vg, out.fd[0]) != ARTVG_SUCCESS) {
        printf("Failed to add DMA FD\n");
        goto cleanup;
    }
    dma_fd_added = 1;
    dma_fd = out.fd[0];

    out.crop_en = 1;
    out.crop.x = 0;
    out.crop.y = 0;
    out.crop.width = DEFAULT_WIDTH;
    out.crop.height = DEFAULT_HEIGHT;

    memset(&ctrl, 0, sizeof(ctrl));
    ctrl.alpha_en = alpha_en;
    ctrl.alpha_rules = alpha_rules;
    ctrl.output_unpremul_en = output_unpremul_en;

    ctrl.color_key_en = color_key_en;
    ctrl.color_key_value = color_key_value;

    ctrl.src_alpha_mode = src_alpha_mode;
    ctrl.src_recolor_value = src_recolor_value;
    ctrl.src_global_alpha = src_global_alpha;
    ctrl.src_recolor_en = src_recolor_en;
    ctrl.src_premul_mode = src_premul_mode;

    ctrl.dst_alpha_mode = dst_alpha_mode;
    ctrl.dst_global_alpha = dst_global_alpha;
    ctrl.dst_premul_mode = dst_premul_mode;

    ctrl.output_dither = output_dither;

    memset(&blit_ctl, 0, sizeof(blit_ctl));
    blit_ctl.rotate = rotate;
    blit_ctl.mirror = mirror;

    src->crop_en = 0;
    dst->crop_en = 0;

    if (artvg_blit2(vg, src, dst, &out, NULL, &ctrl, &blit_ctl) != ARTVG_SUCCESS) {
        printf("Failed to execute blit operation\n");
        goto cleanup;
    }

    artvg_dumping_cmd(vg);

    if (artvg_flush(vg) != ARTVG_SUCCESS) {
        printf("Failed to flush buffer\n");
        goto cleanup;
    }

    if (artvg_wait_finish(vg) != ARTVG_SUCCESS) {
        printf("Failed to finish buffer\n");
        goto cleanup;
    }

    if (drm_buffer_flush(&drm_dev, 0) != 0) {
        printf("Failed to flush buffer\n");
    }

    if (drm_wait_vsync(&drm_dev) != 0) {
        printf("Failed to wait for vsync\n");
        goto cleanup;
    }

    if (dma_fd_added && dma_fd != INVALID_FD) {
        if (artvg_remove_dma_fd(vg, dma_fd) != ARTVG_SUCCESS) {
            printf("Failed to remove DMA FD\n");
        }
        dma_fd_added = 0;
    }

    printf("Test completed successfully. Press Enter to exit...\n");

    getchar();

cleanup:
    if (dma_fd_added && dma_fd != INVALID_FD && vg != NULL) {
        artvg_remove_dma_fd(vg, dma_fd);
    }

    if (src_allocated && vg != NULL) {
        artvg_free(vg, src);
    }

    if (dst_allocated && vg != NULL) {
        artvg_free(vg, dst);
    }


    if (drm_opened) {
        drm_device_close(&drm_dev);
    }

    if (vg != NULL) {
        artvg_destroy(vg);
    }

    return 0;
}
