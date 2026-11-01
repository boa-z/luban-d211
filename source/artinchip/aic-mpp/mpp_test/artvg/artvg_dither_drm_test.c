/*
 * Copyright (c) 2026, ArtInChip Technology Co., Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Authors:  zequan liang <zequan.liang@artinchip.com>
 *
 * Dither Test: Generate smooth gradient and output to RGB565 format
 *              to demonstrate dithering effect.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>
#include <sys/mman.h>
#include <unistd.h>
#include "artvg.h"
#include "vg_drm.h"

#define DRM_DEVICE_PATH "/dev/dri/card0"

void show_usage(char *program_name)
{
    printf("Usage: %s [options]\n", program_name);
    printf("\nDescription:\n");
    printf("  Test dithering effect by rendering gradient to RGB565 format.\n");
    printf("  Step1: Fill gradient to ARGB8888 buffer\n");
    printf("  Step2: Blit ARGB8888 -> RGB565 with dither\n");
    printf("  Step3: Blit RGB565 -> DRM buffer for display\n");
    printf("\nOptions:\n");
    printf("  --dither <0-2>            Dither mode (0: OFF, 1: STANDARD, 2: RANDOM)\n");
    printf("                            Default: 1 (STANDARD)\n");
    printf("  --half                    Use half screen size (default: full screen)\n");
    printf("  --color1 <0xRRGGBB>       Start color (default: 0x000000 black)\n");
    printf("  --color2 <0xRRGGBB>       End color (default: 0xFFFFFF white)\n");
    printf("  --horizontal              Use horizontal gradient (default: vertical)\n");
    printf("  --help                    Show this help message\n");

    printf("\nExamples:\n");
    printf("  # Test with dither OFF - will show banding\n");
    printf("  %s --dither 0\n\n", program_name);
    printf("  # Test with STANDARD dither - smooth gradient\n");
    printf("  %s --dither 1\n\n", program_name);
    printf("  # Half screen grey gradient (most obvious for banding)\n");
    printf("  %s --half --color1 0x404040 --color2 0xC0C0C0\n", program_name);
}

int main(int argc, char **argv)
{
    struct artvg *vg = NULL;
    struct mpp_buf *grad_buf = NULL;
    struct mpp_buf *rgb565_buf = NULL;
    struct mpp_buf out_buf = {0};
    drm_dev_t drm_dev = {0};
    artvg_gradient_t gradient = {0};
    artvg_ctrl_t ctrl = {0};
    artvg_blit_ctl_t blit_ctl = {0};

    int dither_mode = ARTVG_DITHER_STANDARD;
    int half_screen = 0;
    uint32_t color1 = 0x000000;
    uint32_t color2 = 0xFFFFFF;
    int horizontal = 0;

    int opt;
    static struct option long_options[] = {
        {"dither", required_argument, 0, 0},
        {"half", no_argument, 0, 0},
        {"color1", required_argument, 0, 0},
        {"color2", required_argument, 0, 0},
        {"horizontal", no_argument, 0, 0},
        {"help", no_argument, 0, 'h'},
        {0, 0, 0, 0}
    };

    /* Parse arguments */
    while (1) {
        int option_index = 0;
        opt = getopt_long(argc, argv, "h", long_options, &option_index);
        if (opt == -1)
            break;

        switch (opt) {
        case 0:
            if (strncmp(long_options[option_index].name, "dither", strlen("dither")) == 0) {
                dither_mode = atoi(optarg);
                if (dither_mode < 0 || dither_mode > 2) {
                    printf("Invalid dither mode, using STANDARD\n");
                    dither_mode = ARTVG_DITHER_STANDARD;
                }
            } else if (strncmp(long_options[option_index].name, "half", strlen("half")) == 0) {
                half_screen = 1;
            } else if (strncmp(long_options[option_index].name, "color1", strlen("color1")) == 0) {
                color1 = strtoul(optarg, NULL, 0) & 0xFFFFFF;
            } else if (strncmp(long_options[option_index].name, "color2", strlen("color2")) == 0) {
                color2 = strtoul(optarg, NULL, 0) & 0xFFFFFF;
            } else if (strncmp(long_options[option_index].name, "horizontal", strlen("horizontal")) == 0) {
                horizontal = 1;
            }
            break;
        case 'h':
        default:
            show_usage(argv[0]);
            return 0;
        }
    }

    /* Open ARTVG */
    vg = artvg_create();
    if (!vg) {
        printf("Failed to open artvg\n");
        return -1;
    }

    /* Open DRM device */
    if (drm_device_open(&drm_dev, DRM_DEVICE_PATH, -1) != 0) {
        printf("Failed to open DRM device\n");
        goto cleanup;
    }
    if (drm_buffer_to_mpp_buffer(&drm_dev, 0, &out_buf) != 0) {
        printf("Failed to get DRM buffer\n");
        goto cleanup;
    }

    printf("=== ARTVG Dither Test ===\n");
    printf("Screen size: %dx%d\n", out_buf.size.width, out_buf.size.height);

    /* Calculate size */
    int width = half_screen ? out_buf.size.width / 2 : out_buf.size.width;
    int height = half_screen ? out_buf.size.height / 2 : out_buf.size.height;
    grad_buf = artvg_allocate(vg, width, height, MPP_FMT_ARGB_8888);
    if (!grad_buf) {
        printf("Failed to allocate gradient buffer (ARGB8888)\n");
        goto cleanup;
    }
    rgb565_buf = artvg_allocate(vg, width, height, MPP_FMT_RGB_565);
    if (!rgb565_buf) {
        printf("Failed to allocate RGB565 buffer\n");
        goto cleanup;
    }

    printf("Buffer size: %dx%d\n", width, height);
    printf("  - Gradient: ARGB8888\n");
    printf("  - Dither output: RGB565\n");

    /* Setup gradient */
    if (horizontal) {
        gradient.start.x = 0;
        gradient.start.y = 0;
        gradient.end.x = width;
        gradient.end.y = 0;
    } else {
        gradient.start.x = 0;
        gradient.start.y = 0;
        gradient.end.x = 0;
        gradient.end.y = height;
    }

    gradient.colors[0] = 0xFF000000 | color1;
    gradient.colors[1] = 0xFF000000 | color2;
    gradient.stops[0] = 0;
    gradient.stops[1] = 255;
    gradient.count = 2;
    gradient.precision = ARTVG_GRADIENT_PRECISION_256;
    gradient.spread = ARTVG_GRADIENT_SPREAD_PAD;

    printf("Gradient: %s 0x%06X -> 0x%06X\n",
           horizontal ? "horizontal" : "vertical", color1, color2);
    memset(&ctrl, 0, sizeof(ctrl));
    ctrl.alpha_en = 0;

    if (artvg_fill_gradient(vg, grad_buf, &ctrl, &gradient) != ARTVG_SUCCESS) {
        printf("Failed to fill gradient\n");
        goto cleanup;
    }

    /* Step 2: Blit ARGB8888 -> RGB565 with dither */
    memset(&ctrl, 0, sizeof(ctrl));
    ctrl.alpha_en = 0;
    ctrl.output_dither = dither_mode;

    printf("\nConverting ARGB8888 -> RGB565 (dither: %s)\n",
           dither_mode == ARTVG_DITHER_OFF ? "OFF" :
           (dither_mode == ARTVG_DITHER_STANDARD ? "STANDARD" : "RANDOM"));

    if (dither_mode == ARTVG_DITHER_OFF) {
        printf("  *** WARNING: Expect visible color banding! ***\n");
    } else {
        printf("  *** Dither enabled - gradient should be smooth ***\n");
    }

    /* Use blit2 to convert to RGB565 with dither */
    if (artvg_blit2(vg, grad_buf, NULL, rgb565_buf, NULL, &ctrl, &blit_ctl) != ARTVG_SUCCESS) {
        printf("Failed to convert to RGB565\n");
        goto cleanup;
    }
    if (artvg_add_dma_fd(vg, out_buf.fd[0]) != ARTVG_SUCCESS) {
        printf("Failed to add DMA FD\n");
        goto cleanup;
    }

    out_buf.crop_en = 1;
    out_buf.crop.x = 0;
    out_buf.crop.y = 0;
    out_buf.crop.width = width;
    out_buf.crop.height = height;

    memset(&ctrl, 0, sizeof(ctrl));
    ctrl.alpha_en = 0;

    if (artvg_blit(vg, rgb565_buf, NULL, &out_buf, &ctrl, &blit_ctl) != ARTVG_SUCCESS) {
        printf("Failed to blit to output\n");
        goto cleanup;
    }

    /* Dump command queue */
    artvg_dumping_cmd(vg);

    /* Flush and finish */
    if (artvg_flush(vg) != ARTVG_SUCCESS) {
        printf("Failed to flush\n");
        goto cleanup;
    }

    if (artvg_wait_finish(vg) != ARTVG_SUCCESS) {
        printf("Failed to wait finish\n");
        goto cleanup;
    }

    /* Display on screen */
    if (drm_buffer_flush(&drm_dev, 0) != 0) {
        printf("Failed to flush DRM buffer\n");
    }

    if (drm_wait_vsync(&drm_dev) != 0) {
        printf("Failed to wait vsync\n");
    }

    printf("\nDither test completed! Result displayed on screen.\n");
    printf("\nPress Enter to exit...\n");
    getchar();

cleanup:
    if (grad_buf) {
        artvg_free(vg, grad_buf);
    }
    if (rgb565_buf) {
        artvg_free(vg, rgb565_buf);
    }
    if (out_buf.fd[0] > 0) {
        artvg_remove_dma_fd(vg, out_buf.fd[0]);
    }
    drm_device_close(&drm_dev);
    if (vg) {
        artvg_destroy(vg);
    }
    return 0;
}
