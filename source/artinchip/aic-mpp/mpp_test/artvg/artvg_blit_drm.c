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

extern const image_dsc_t alpha_baby_card_240x120;
extern const image_dsc_t alpha_poney_240x120;

void show_usage(char *program_name)
{
    printf("Usage: %s [options]\n", program_name);
    printf("Options:\n");
    printf("  --rotate <0|1|2|3>     Rotation mode (0: 0°, 1: 90°, 2: 180°, 3: 270°)\n");
    printf("  --mirror <0|1|2>       Mirror mode (0: none, 1: horizontal, 2: vertical)\n");
    printf("  --help                 Show this help message\n");
}

int main(int argc, char **argv)
{
    struct artvg *vg = NULL;
    void *src_mmap[3] = {NULL};
    struct mpp_buf *src = NULL;
    struct mpp_buf dst = {0};
    drm_dev_t drm_dev = {0};
    artvg_ctrl_t ctrl = {0};
    artvg_blit_ctl_t blit_ctl = {0};

    int rotate = 0;
    int mirror = 0;
    static struct option long_options[] = {
        {"rotate", required_argument, 0, 'r'},
        {"mirror", required_argument, 0, 'm'},
        {"help", no_argument, 0, 'h'},
        {0, 0, 0, 0}
    };

    int opt;
    while ((opt = getopt_long(argc, argv, "h", long_options, NULL)) != -1) {
        switch (opt) {
        case 'r':
            rotate = atoi(optarg);
            break;
        case 'm':
            mirror = atoi(optarg);
            break;
        case 'h':
        default:
            show_usage(argv[0]);
            return 0;
        }
    }

    vg = artvg_create();
    if (!vg) {
        printf("Failed to open artvg\n");
        return -1;
    }

    src = artvg_allocate(vg, alpha_baby_card_240x120.header.w, alpha_baby_card_240x120.header.h, MPP_FMT_ARGB_8888);
    if (!src) {
        printf("Failed to allocate source buffer\n");
        goto cleanup;
    }

    if (dma_buf_mmap(src, src_mmap) != 0) {
        printf("Failed to map buffer memory\n");
        goto cleanup;
    }

    if (image_array_read(&alpha_baby_card_240x120, src_mmap[0], src->size.height * src->stride[0]) != 0) {
        printf("Failed to load source image, size = %d\n", alpha_baby_card_240x120.data_size);
        goto cleanup;
    }
    dmabuf_sync(src->fd[0], DMA_BUF_SYNC_END);

    if (drm_device_open(&drm_dev, "/dev/dri/card0", -1) != 0) {
        printf("Failed to open DRM device\n");
        goto cleanup;
    }

    if (drm_buffer_to_mpp_buffer(&drm_dev, 0, &dst) != 0) {
        printf("Failed to convert DRM buffer to MPP buffer\n");
        goto cleanup;
    }

    if (artvg_add_dma_fd(vg, dst.fd[0]) != ARTVG_SUCCESS) {
        printf("Failed to add DMA FD\n");
        goto cleanup;
    }

    void *drm_map = drm_framebuffer_get_map(&drm_dev, 0);
    if (drm_map && image_array_read_with_stride(&alpha_poney_240x120, drm_map, dst.stride[0]) != 0) {
        printf("Failed to load background image\n");
    }
    dmabuf_sync(dst.fd[0], DMA_BUF_SYNC_END);

    ctrl.alpha_en = 0;
    blit_ctl.rotate = rotate;
    blit_ctl.mirror = mirror;

    dst.crop_en = 1;
    if (rotate == ARTVG_ROTATE_90 || rotate == ARTVG_ROTATE_270) {
        dst.crop.width = 120;
        dst.crop.height = 240;
    } else {
        dst.crop.width = 240;
        dst.crop.height = 120;
    }

    if (artvg_blit(vg, src, &dst, NULL, &ctrl, &blit_ctl) != ARTVG_SUCCESS) {
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

    if (artvg_remove_dma_fd(vg, dst.fd[0]) != ARTVG_SUCCESS) {
        printf("Failed to remove DMA FD\n");
        goto cleanup;
    }

    printf("Test completed successfully. Press Enter to exit...\n");
    getchar();

cleanup:
    if (src) {
        dma_buf_unmap(src, src_mmap);
        artvg_free(vg, src);
    }
    drm_device_close(&drm_dev);
    if (vg)
        artvg_destroy(vg);

    return 0;
}
