
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
#include "artvg.h"
#include "vg_drm.h"

void show_usage(const char *program_name)
{
    printf("Usage: %s [options]\n", program_name);
    printf("\nOptions:\n");
    printf("  --help                    Display this help information\n");

    printf("\nGradient control options:\n");
    printf("  --start-x <value>         Gradient start point X coordinate (default: 0)\n");
    printf("  --start-y <value>         Gradient start point Y coordinate (default: 0)\n");
    printf("  --end-x <value>           Gradient end point X coordinate (default: 1024)\n");
    printf("  --end-y <value>           Gradient end point Y coordinate (default: 600)\n");
    printf("  --color1 <0xAARRGGBB>     First color (default: 0xFFFF0000 red)\n");
    printf("  --color2 <0xAARRGGBB>     Second color (default: 0xFF0000FF blue)\n");
    printf("  --color3 <0xAARRGGBB>     Third color (optional)\n");
    printf("  --color4 <0xAARRGGBB>     Fourth color (optional)\n");
    printf("  --stop1 <0-255>           First color stop position (default: 0)\n");
    printf("  --stop2 <0-255>           Second color stop position (default: 255)\n");
    printf("  --stop3 <0-255>           Third color stop position (optional)\n");
    printf("  --stop4 <0-255>           Fourth color stop position (optional)\n");
    printf("  --count <value>           Color counts (default: 2)\n");
    printf("  --precision <0|1|2|3>     Gradient precision: 0=NONE, 1=256, 2=64, 3=16 (default: 1)\n");
    printf("  --spread <0-3>            Gradient spread mode (0: NONE, 1: PAD, 2: REFLECT, 3: REPEAT) (default: 1)\n");

    printf("\nAlpha blending control options:\n");
    printf("  --alpha-en <0|1>          Enable alpha blending (default: 0)\n");
    printf("  --alpha-rules <value>    Alpha blending rules (default: 0)\n");
    printf("  --src-global-alpha <0-255> Source global alpha value (default: 255)\n");
    printf("  --dst-global-alpha <0-255> Destination global alpha value (default: 255)\n");

    printf("\nExamples:\n");
    printf("  %s --start-x 0 --start-y 0 --end-x 1024 --end-y 600 --color1 0xFFFF0000 --color2 0xFF0000FF --stop1 0 --stop2 255\n", program_name);
    printf("  %s --start-x 100 --start-y 100 --end-x 540 --end-y 380 --color1 0xFFFF0000 --color2 0xFF00FF00 --color3 0xFF0000FF --stop1 0 --stop2 128 --stop3 255\n", program_name);
}

static void parse_gradient_precision(int prec, artvg_gradient_t *gradient)
{
    switch (prec) {
    case 0:
        gradient->precision = ARTVG_GRADIENT_PRECISION_NONE;
        break;
    case 2:
        gradient->precision = ARTVG_GRADIENT_PRECISION_64;
        break;
    case 3:
        gradient->precision = ARTVG_GRADIENT_PRECISION_16;
        break;
    default:
        gradient->precision = ARTVG_GRADIENT_PRECISION_256;
        break;
    }
}

static int parse_arguments(int argc, char **argv,
                          artvg_gradient_t *gradient,
                          artvg_ctrl_t *ctrl)
{
    static struct option long_options[] = {
        /* Gradient control */
        {"start-x", required_argument, 0, 0},
        {"start-y", required_argument, 0, 0},
        {"end-x", required_argument, 0, 0},
        {"end-y", required_argument, 0, 0},
        {"color1", required_argument, 0, 0},
        {"color2", required_argument, 0, 0},
        {"color3", required_argument, 0, 0},
        {"color4", required_argument, 0, 0},
        {"stop1", required_argument, 0, 0},
        {"stop2", required_argument, 0, 0},
        {"stop3", required_argument, 0, 0},
        {"stop4", required_argument, 0, 0},
        {"count", required_argument, 0, 0},
        {"precision", required_argument, 0, 0},
        {"spread", required_argument, 0, 0},

        /* Alpha blending control */
        {"alpha-en", required_argument, 0, 0},
        {"alpha-rules", required_argument, 0, 0},
        {"src-global-alpha", required_argument, 0, 0},
        {"dst-global-alpha", required_argument, 0, 0},

        /* Help */
        {"help", no_argument, 0, 'h'},
        {0, 0, 0, 0}
    };

    /* Default gradient parameters */
    gradient->start.x = 0;
    gradient->start.y = 0;
    gradient->end.x = 1024;
    gradient->end.y = 600;

    /* Default colors: red to blue gradient */
    gradient->colors[0] = 0xFFFF0000;
    gradient->colors[1] = 0xFF0000FF;
    gradient->stops[0] = 0;
    gradient->stops[1] = 255;
    gradient->count = 2;

    gradient->precision = ARTVG_GRADIENT_PRECISION_256;
    gradient->spread = ARTVG_GRADIENT_SPREAD_PAD;

    /* Default blending control parameters */
    memset(ctrl, 0, sizeof(artvg_ctrl_t));

    int opt;
    int option_index = 0;
    const char *name;

    while ((opt = getopt_long(argc, argv, "h", long_options, &option_index)) != -1) {
        switch (opt) {
        case 0:
            name = long_options[option_index].name;

            if (strncmp(name, "start-x", strlen("start-x")) == 0) {
                gradient->start.x = atoi(optarg);
            } else if (strncmp(name, "start-y", strlen("start-y")) == 0) {
                gradient->start.y = atoi(optarg);
            } else if (strncmp(name, "end-x", strlen("end-x")) == 0) {
                gradient->end.x = atoi(optarg);
            } else if (strncmp(name, "end-y", strlen("end-y")) == 0) {
                gradient->end.y = atoi(optarg);
            } else if (strncmp(name, "color1", strlen("color1")) == 0) {
                gradient->colors[0] = strtoul(optarg, NULL, 0);
            } else if (strncmp(name, "color2", strlen("color2")) == 0) {
                gradient->colors[1] = strtoul(optarg, NULL, 0);
            } else if (strncmp(name, "color3", strlen("color3")) == 0) {
                gradient->colors[2] = strtoul(optarg, NULL, 0);
                if (gradient->count < 3)
                    gradient->count = 3;
            } else if (strncmp(name, "color4", strlen("color4")) == 0) {
                gradient->colors[3] = strtoul(optarg, NULL, 0);
                if (gradient->count < 4)
                    gradient->count = 4;
            } else if (strncmp(name, "stop1", strlen("stop1")) == 0) {
                gradient->stops[0] = atoi(optarg);
            } else if (strncmp(name, "stop2", strlen("stop2")) == 0) {
                gradient->stops[1] = atoi(optarg);
            } else if (strncmp(name, "stop3", strlen("stop3")) == 0) {
                gradient->stops[2] = atoi(optarg);
                if (gradient->count < 3)
                    gradient->count = 3;
            } else if (strncmp(name, "stop4", strlen("stop4")) == 0) {
                gradient->stops[3] = atoi(optarg);
                if (gradient->count < 4)
                    gradient->count = 4;
            } else if (strncmp(name, "count", strlen("count")) == 0) {
                gradient->count = atoi(optarg);
            } else if (strncmp(name, "precision", strlen("precision")) == 0) {
                parse_gradient_precision(atoi(optarg), gradient);
            } else if (strncmp(name, "spread", strlen("spread")) == 0) {
                int spread = atoi(optarg);
                if (spread >= 0 && spread <= 3)
                    gradient->spread = spread;
                printf("Spread: %d\n", spread);
            } else if (strncmp(name, "alpha-en", strlen("alpha-en")) == 0) {
                ctrl->alpha_en = atoi(optarg);
            } else if (strncmp(name, "alpha-rules", strlen("alpha-rules")) == 0) {
                ctrl->alpha_rules = atoi(optarg);
            } else if (strncmp(name, "src-global-alpha", strlen("src-global-alpha")) == 0) {
                ctrl->src_global_alpha = atoi(optarg);
            } else if (strncmp(name, "dst-global-alpha", strlen("dst-global-alpha")) == 0) {
                ctrl->dst_global_alpha = atoi(optarg);
            }
            break;

        case 'h':
            show_usage(argv[0]);
            return 1;

        default:
            return -1;
        }
    }

    return 0;
}

/* Print gradient parameters */
void print_gradient_info(const artvg_gradient_t *gradient)
{
    printf("=== Linear Gradient Parameters ===\n");
    printf("Start point: (%d, %d)\n", gradient->start.x, gradient->start.y);
    printf("End point: (%d, %d)\n", gradient->end.x, gradient->end.y);
    printf("Color count: %d\n", gradient->count);

    for (uint32_t i = 0; i < gradient->count; i++) {
        printf("  Color[%d]: 0x%08X, position: %u (%.1f%%)\n",
               i, gradient->colors[i], gradient->stops[i], gradient->stops[i] * 100.0f / 255.0f);
    }

    printf("Precision: %d\n", (int)gradient->precision);
    printf("Spread mode: %d\n", (int)gradient->spread);
    printf("==================\n");
}

/* Main function */
int main(int argc, char **argv)
{
    struct artvg *vg = NULL;
    drm_dev_t drm_dev = {0};
    artvg_ctrl_t ctrl = {0};
    struct mpp_buf target_buf = {0};
    artvg_gradient_t gradient = {0};

    printf("=== ARTVG Linear Gradient Fill Test ===\n\n");

        int ret = parse_arguments(argc, argv, &gradient, &ctrl);
    if (ret == 1)
        return 0;
    if (ret == -1) {
        printf("Argument parsing error\n");
        return -1;
    }

    /* Print gradient configuration information */
    print_gradient_info(&gradient);

    /* Open ARTVG device */
    vg = artvg_create();
    if (!vg) {
        printf("Failed to open ARTVG device\n");
        return -1;
    }

    /* Open DRM device */
    if (drm_device_open(&drm_dev, "/dev/dri/card0", -1) != 0) {
        printf("Failed to open DRM device\n");
        goto cleanup;
    }
    if (drm_buffer_to_mpp_buffer(&drm_dev, 0, &target_buf) != 0) {
        printf("Failed to convert DRM buffer to MPP buffer\n");
        goto cleanup;
    }
    printf("DRM buffer converted: %dx%d\n",
           target_buf.size.width, target_buf.size.height);

    /* Add DMA FD to ARTVG */
    if (artvg_add_dma_fd(vg, target_buf.fd[0]) != ARTVG_SUCCESS) {
        printf("Failed to add DMA FD\n");
        goto cleanup;
    }

    /* Perform linear gradient fill operation */
    artvg_error_t err = artvg_fill_gradient(vg, &target_buf, &ctrl, &gradient);
    if (err != ARTVG_SUCCESS) {
        printf("Gradient fill failed: %d\n", err);
        goto cleanup;
    }

    /* Debug: dump command information */
    artvg_dumping_cmd(vg);

    /* Flush and finish operations */
    if (artvg_flush(vg) != ARTVG_SUCCESS) {
        printf("Failed to flush buffer\n");
        goto cleanup;
    }

    if (artvg_wait_finish(vg) != ARTVG_SUCCESS) {
        printf("Failed to complete buffer operation\n");
        goto cleanup;
    }
    if (drm_buffer_flush(&drm_dev, 0) != 0) {
        printf("Failed to flush DRM buffer\n");
    }

    if (drm_wait_vsync(&drm_dev) != 0) {
        printf("Failed to wait for vertical sync\n");
        goto cleanup;
    }

    if (artvg_remove_dma_fd(vg, target_buf.fd[0]) != ARTVG_SUCCESS) {
        printf("Failed to remove DMA FD\n");
    }

    printf("\nGradient fill test completed! Result displayed on screen.\n");
    printf("Press Enter to exit...\n");
    getchar();

cleanup:
    drm_device_close(&drm_dev);
    if (vg)
        artvg_destroy(vg);
    return 0;
}
