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
#include "artvg.h"
#include "vg_drm.h"

void show_usage(char *program_name)
{
    printf("Usage: %s [options]\n", program_name);
    printf("Options:\n");
    printf("  -c, --clear-color <hex>    Clear color in hex format (default: 0xffffffff)\n");
    printf("\nClear Crop Options:\n");
    printf("  --crop_en <value>              Crop en (default: 0)\n");
    printf("  --crop_x <value>              Crop X position for clearing (default: 0)\n");
    printf("  --crop_y <value>              Crop Y position for clearing (default: 0)\n");
    printf("  --crop_width <value>          Crop width for clearing (default: 0, means full width)\n");
    printf("  --crop_height <value>         Crop height for clearing (default: 0, means full height)\n");

    printf("  --help                     Show this help message\n");
}

int main(int argc, char **argv)
{
    struct artvg *vg = NULL;
    struct mpp_buf buf = {0};
    drm_dev_t drm_dev = {0};
    unsigned int clear_color = 0xffffffff;
    int crop_en = 0;
    int crop_x = 0;
    int crop_y = 0;
    int crop_width = 0;
    int crop_height = 0;

    int opt;
    int option_index = 0;
    char *opt_name = NULL;
    static struct option long_options[] = {
        {"clear-color", required_argument, 0, 'c'},
        {"crop_en", required_argument, 0, 0},
        {"crop_x", required_argument, 0, 0},
        {"crop_y", required_argument, 0, 0},
        {"crop_width", required_argument, 0, 0},
        {"crop_height", required_argument, 0, 0},
        {"help", no_argument, 0, '?'},
        {0, 0, 0, 0}
    };

    while ((opt = getopt_long(argc, argv, "c:", long_options, &option_index)) != -1) {
        switch (opt) {
        case 'c':
            clear_color = strtoul(optarg, NULL, 16);
            break;
        case 0: /* Long option */
            opt_name = (char *)long_options[option_index].name;

            if (strncmp(opt_name, "crop_en", strlen("crop_en")) == 0)
                crop_en = atoi(optarg);
            else if (strncmp(opt_name, "crop_x", strlen("crop_x")) == 0)
                crop_x = atoi(optarg);
            else if (strncmp(opt_name, "crop_y", strlen("crop_y")) == 0)
                crop_y = atoi(optarg);
            else if (strncmp(opt_name, "crop_width", strlen("crop_width")) == 0)
                crop_width = atoi(optarg);
            else if (strncmp(opt_name, "crop_height", strlen("crop_height")) == 0)
                crop_height = atoi(optarg);
            else if (strncmp(opt_name, "help", strlen("help")) == 0) {
                show_usage(argv[0]);
                return 0;
            }
            break;
        case '?':
        default:
            show_usage(argv[0]);
            return -1;
        }
    }

    vg = artvg_create();
    if (!vg) {
        printf("Failed to open artvg\n");
        return -1;
    }

    if (drm_device_open(&drm_dev, "/dev/dri/card0", -1) != 0) {
        printf("Failed to open DRM device\n");
        goto cleanup;
    }

    if (drm_buffer_to_mpp_buffer(&drm_dev, 0, &buf) != 0) {
        printf("Failed to convert DRM buffer to MPP buffer\n");
        goto cleanup;
    }

    if (artvg_add_dma_fd(vg, buf.fd[0]) != ARTVG_SUCCESS) {
        printf("Failed to add DMA FD\n");
        goto cleanup;
    }

    buf.crop_en = crop_en;
    buf.crop.x = crop_x;
    buf.crop.y = crop_y;
    buf.crop.width = crop_width;
    buf.crop.height = crop_height;
    printf("crop_en: %d, crop_x: %d, crop_y: %d, crop_width: %d, crop_height: %d\n",
        buf.crop_en, buf.crop.x, buf.crop.y, buf.crop.width, buf.crop.height);

    if (artvg_clear(vg, &buf, clear_color) != ARTVG_SUCCESS) {
        printf("Failed to clear buffer\n");
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

    if (artvg_remove_dma_fd(vg, buf.fd[0]) != ARTVG_SUCCESS) {
        printf("Failed to remove DMA FD\n");
        goto cleanup;
    }

    getchar();

cleanup:
    drm_device_close(&drm_dev);
    if (vg)
        artvg_destroy(vg);
    return 0;
}