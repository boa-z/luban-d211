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

#define DEFAULT_SOURCE_WIDTH  240
#define DEFAULT_SOURCE_HEIGHT 120
#define DEFAULT_MASK_WIDTH    240
#define DEFAULT_MASK_HEIGHT   120
#define DEFAULT_SRC_COLOR     0xFFFF0000
#define DEFAULT_BG_COLOR      0xFF303030
#define DEFAULT_MASK_ALPHA_1  255
#define DEFAULT_MASK_ALPHA_2  0
#define DEFAULT_GRID_SIZE     16
#define DEFAULT_LINE_WIDTH    2

/* Helper macro for error checking and cleanup */
#define CHECK_ERROR(call, msg) do { \
    if ((call) != ARTVG_SUCCESS) { \
        printf("Failed to %s\n", msg); \
        goto cleanup; \
    } \
} while(0)

/**
 * @brief Fill A8 mask buffer with grid pattern
 * @param buf_ptr Pointer to mask buffer memory
 * @param width Buffer width
 * @param height Buffer height
 * @param stride Buffer stride
 * @param alpha1 Alpha value for grid lines (0-255)
 * @param alpha2 Alpha value for empty areas (0-255)
 * @param grid_size Size of each grid cell
 * @param line_width Width of grid lines
 */
static void fill_grid_mask(void *buf_ptr, int width, int height, int stride,
                            uint8_t alpha1, uint8_t alpha2,
                            int grid_size, int line_width)
{
    uint8_t *pixel = (uint8_t *)buf_ptr;
    int row, col;

    for (row = 0; row < height; row++) {
        for (col = 0; col < width; col++) {
            int x_in_cell = col % grid_size;
            int y_in_cell = row % grid_size;
            /* Check if pixel is on grid line (horizontal or vertical) */
            if (x_in_cell < line_width || y_in_cell < line_width) {
                pixel[col] = alpha1;
            } else {
                pixel[col] = alpha2;
            }
        }
        pixel += stride;
    }
}

static int dma_buf_mmap(struct mpp_buf *buf, void *map[])
{
    int i;

    for (i = 0; i < 3; i++) {
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
    int i;

    if (!buf || !map)
        return;

    for (i = 0; i < 3; i++) {
        if (map[i] && buf->fd[i] >= 0) {
            size_t map_size = buf->stride[i] * buf->size.height;
            munmap(map[i], map_size);
            map[i] = NULL;
        }
    }
}


void show_usage(char *program_name)
{
    printf("Usage: %s [options]\n", program_name);
    printf("ARTVG Mask DRM Test - Test mask blending functionality\n");
    printf("\nOptions:\n");
    printf("  -w, --width <pixels>           Source and mask width (default: %d)\n", DEFAULT_SOURCE_WIDTH);
    printf("  -h, --height <pixels>          Source and mask height (default: %d)\n", DEFAULT_SOURCE_HEIGHT);
    printf("  -c, --src-color <hex>          Source buffer color in hex (default: 0x%08X)\n", DEFAULT_SRC_COLOR);
    printf("  -b, --bg-color <hex>           DRM background clear color in hex (default: 0x%08X)\n", DEFAULT_BG_COLOR);
    printf("  --alpha1 <value>               Grid line alpha value (0-255, default: %d)\n", DEFAULT_MASK_ALPHA_1);
    printf("  --alpha2 <value>               Empty area alpha value (0-255, default: %d)\n", DEFAULT_MASK_ALPHA_2);
    printf("  --grid-size <pixels>           Grid cell size (default: %d)\n", DEFAULT_GRID_SIZE);
    printf("  --line-width <pixels>          Grid line width (default: %d)\n", DEFAULT_LINE_WIDTH);
    printf("  --help                         Show this help message\n");
    printf("\nExample:\n");
    printf("  %s -w 320 -h 240 --alpha1 255 --alpha2 0 --grid-size 20 --line-width 2\n", program_name);
}

int main(int argc, char **argv)
{
    struct artvg *vg = NULL;
    void *mask_mmap[3] = {NULL};
    struct mpp_buf *src = NULL;
    struct mpp_buf *mask = NULL;
    struct mpp_buf dst = {0};
    drm_dev_t drm_dev = {0};
    artvg_ctrl_t ctrl = {0};
    artvg_blit_ctl_t blit_ctl = {0};

    int src_width = DEFAULT_SOURCE_WIDTH;
    int src_height = DEFAULT_SOURCE_HEIGHT;
    int mask_width = DEFAULT_MASK_WIDTH;
    int mask_height = DEFAULT_MASK_HEIGHT;
    uint32_t src_color = DEFAULT_SRC_COLOR;
    uint32_t bg_color = DEFAULT_BG_COLOR;
    uint8_t alpha1 = DEFAULT_MASK_ALPHA_1;
    uint8_t alpha2 = DEFAULT_MASK_ALPHA_2;
    int grid_size = DEFAULT_GRID_SIZE;
    int line_width = DEFAULT_LINE_WIDTH;

    int opt;
    int option_index = 0;
    static struct option long_options[] = {
        {"width", required_argument, 0, 'w'},
        {"height", required_argument, 0, 'h'},
        {"src-color", required_argument, 0, 'c'},
        {"bg-color", required_argument, 0, 'b'},
        {"alpha1", required_argument, 0, 0},
        {"alpha2", required_argument, 0, 0},
        {"grid-size", required_argument, 0, 0},
        {"line-width", required_argument, 0, 0},
        {"help", no_argument, 0, '?'},
        {0, 0, 0, 0}
    };

    // Parse command line arguments
    while ((opt = getopt_long(argc, argv, "w:h:c:b:", long_options, &option_index)) != -1) {
        switch (opt) {
        case 'w':
            src_width = mask_width = atoi(optarg);
            break;
        case 'h':
            src_height = mask_height = atoi(optarg);
            break;
        case 'c':
            src_color = strtoul(optarg, NULL, 16);
            break;
        case 'b':
            bg_color = strtoul(optarg, NULL, 16);
            break;
        case 0: /* Long option */
            if (strncmp(long_options[option_index].name, "alpha1", strlen("alpha1")) == 0) {
                alpha1 = (uint8_t)atoi(optarg);
            } else if (strncmp(long_options[option_index].name, "alpha2", strlen("alpha2")) == 0) {
                alpha2 = (uint8_t)atoi(optarg);
            } else if (strncmp(long_options[option_index].name, "grid-size", strlen("grid-size")) == 0) {
                grid_size = atoi(optarg);
            } else if (strncmp(long_options[option_index].name, "line-width", strlen("line-width")) == 0) {
                line_width = atoi(optarg);
            } else if (strncmp(long_options[option_index].name, "help", strlen("help")) == 0) {
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

    printf("ARTVG Mask DRM Test\n");
    printf("===================\n");
    printf("Source size: %dx%d\n", src_width, src_height);
    printf("Mask size: %dx%d\n", mask_width, mask_height);
    printf("Source color: 0x%08X\n", src_color);
    printf("Background color: 0x%08X\n", bg_color);
    printf("Alpha values: %d / %d\n", alpha1, alpha2);
    printf("Grid size: %d, Line width: %d\n", grid_size, line_width);
    printf("\n");

    // Open ARTVG device
    vg = artvg_create();
    if (!vg) {
        printf("Failed to open artvg\n");
        return -1;
    }

    // Allocate source buffer
    src = artvg_allocate(vg, src_width, src_height, MPP_FMT_ARGB_8888);
    if (!src) {
        printf("Failed to allocate source buffer\n");
        goto cleanup;
    }

    printf("Filling source buffer with color 0x%08X using artvg_clear\n", src_color);
    CHECK_ERROR(artvg_clear(vg, src, src_color), "clear source buffer");
    CHECK_ERROR(artvg_flush(vg), "flush buffer");
    CHECK_ERROR(artvg_wait_finish(vg), "finish buffer");
    printf("Source buffer filled successfully\n");

    // Allocate mask buffer (A8 format)
    mask = artvg_allocate(vg, mask_width, mask_height, MPP_FMT_A8);
    if (!mask) {
        printf("Failed to allocate mask buffer\n");
        goto cleanup;
    }

    // Map mask buffer and fill with grid pattern
    if (dma_buf_mmap(mask, mask_mmap) != 0) {
        printf("Failed to map mask buffer memory\n");
        goto cleanup;
    }

    fill_grid_mask(mask_mmap[0], mask->size.width, mask->size.height,
                   mask->stride[0], alpha1, alpha2, grid_size, line_width);
    dmabuf_sync(mask->fd[0], DMA_BUF_SYNC_END);
    printf("Mask buffer filled with grid pattern (alpha: %d/%d, grid: %d, line: %d)\n",
           alpha1, alpha2, grid_size, line_width);

    // Open DRM device
    if (drm_device_open(&drm_dev, "/dev/dri/card0", -1) != 0) {
        printf("Failed to open DRM device\n");
        dma_buf_unmap(mask, mask_mmap);
        goto cleanup;
    }

    // Convert DRM buffer to MPP buffer
    if (drm_buffer_to_mpp_buffer(&drm_dev, 0, &dst) != 0) {
        printf("Failed to convert DRM buffer to MPP buffer\n");
        goto cleanup;
    }

    // Add DMA FD to ARTVG
    if (artvg_add_dma_fd(vg, dst.fd[0]) != ARTVG_SUCCESS) {
        printf("Failed to add DMA FD\n");
        goto cleanup;
    }

    printf("Clearing DRM buffer with background color 0x%08X\n", bg_color);
    CHECK_ERROR(artvg_clear(vg, &dst, bg_color), "clear buffer");
    CHECK_ERROR(artvg_flush(vg), "flush buffer");
    CHECK_ERROR(artvg_wait_finish(vg), "finish buffer");
    printf("DRM buffer cleared successfully\n");

    // Step 2: Blit source with mask to destination
    printf("Blitting source with mask to destination...\n");

    // Initialize control structures for mask blending
    ctrl.alpha_en = 0;  /* Enable alpha blending */
    ctrl.alpha_rules = ARTVG_BLEND_DEFAULT;
    ctrl.src_alpha_mode = ARTVG_ALPHA_PIXEL;
    ctrl.dst_alpha_mode = ARTVG_ALPHA_PIXEL;
    ctrl.src_global_alpha = 255;
    ctrl.dst_global_alpha = 255;

    blit_ctl.rotate = ARTVG_ROTATE_0;
    blit_ctl.mirror = ARTVG_MIRROR_NONE;

    dst.crop_en = 1;
    dst.crop.x = 0;
    dst.crop.y = 0;
    dst.crop.width = src_width;
    dst.crop.height = src_height;

    CHECK_ERROR(artvg_blit(vg, src, &dst, mask, &ctrl, &blit_ctl),
                "execute blit operation with mask");

    artvg_dumping_cmd(vg);

    CHECK_ERROR(artvg_flush(vg), "flush buffer");
    CHECK_ERROR(artvg_wait_finish(vg), "finish buffer");
    printf("Blit with mask completed successfully\n");

    if (drm_buffer_flush(&drm_dev, 0) != 0)
        printf("Failed to flush DRM buffer\n");

    if (drm_wait_vsync(&drm_dev) != 0) {
        printf("Failed to wait for vsync\n");
        goto cleanup;
    }

    printf("DRM buffer flushed and displayed\n");

    if (artvg_remove_dma_fd(vg, dst.fd[0]) != ARTVG_SUCCESS)
        printf("Failed to remove DMA FD\n");

    printf("\nTest completed successfully. Press Enter to exit...\n");
    getchar();

cleanup:
    // Cleanup resources
    printf("Cleaning up resources...\n");

    dma_buf_unmap(mask, mask_mmap);
    if (mask)
        artvg_free(vg, mask);

    if (src)
        artvg_free(vg, src);

    drm_device_close(&drm_dev);
    if (vg)
        artvg_destroy(vg);

    printf("Cleanup complete.\n");
    return 0;
}
