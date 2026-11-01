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

#define DEFAULT_SRC_FORMAT          MPP_FMT_ARGB_8888
#define DEFAULT_DST_FORMAT          MPP_FMT_NV12
#define DEFAULT_SRC_COLOR_SPACE     MPP_COLOR_SPACE_BT709
#define DEFAULT_DST_COLOR_SPACE     MPP_COLOR_SPACE_BT709

#define GRADIENT_COLOR1             0xFFFF0000
#define GRADIENT_COLOR2             0xFF0000FF

#define DRM_DEVICE_PATH             "/dev/dri/card0"
#define DRM_CONNECTOR_ID            -1

typedef struct {
    enum mpp_pixel_format src_format;
    enum mpp_pixel_format dst_format;
    enum mpp_color_space src_color_space;
    enum mpp_color_space dst_color_space;
    int width;
    int height;
} csc_test_config_t;

static int parse_format(const char *str)
{
    if (!str) return -1;

    if (strncmp(str, "argb8888", strlen("argb8888")) == 0 || strncmp(str, "ARGB8888", strlen("ARGB8888")) == 0)
        return MPP_FMT_ARGB_8888;
    else if (strncmp(str, "abgr8888", strlen("abgr8888")) == 0 || strncmp(str, "ABGR8888", strlen("ABGR8888")) == 0)
        return MPP_FMT_ABGR_8888;
    else if (strncmp(str, "rgba8888", strlen("rgba8888")) == 0 || strncmp(str, "RGBA8888", strlen("RGBA8888")) == 0)
        return MPP_FMT_RGBA_8888;
    else if (strncmp(str, "bgra8888", strlen("bgra8888")) == 0 || strncmp(str, "BGRA8888", strlen("BGRA8888")) == 0)
        return MPP_FMT_BGRA_8888;

    else if (strncmp(str, "rgb888", strlen("rgb888")) == 0 || strncmp(str, "RGB888", strlen("RGB888")) == 0)
        return MPP_FMT_RGB_888;
    else if (strncmp(str, "bgr888", strlen("bgr888")) == 0 || strncmp(str, "BGR888", strlen("BGR888")) == 0)
        return MPP_FMT_BGR_888;
    else if (strncmp(str, "rgb565", strlen("rgb565")) == 0 || strncmp(str, "RGB565", strlen("RGB565")) == 0)
        return MPP_FMT_RGB_565;
    else if (strncmp(str, "bgr565", strlen("bgr565")) == 0 || strncmp(str, "BGR565", strlen("BGR565")) == 0)
        return MPP_FMT_BGR_565;

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

/* Check if format is YUV */
static int is_yuv_format(enum mpp_pixel_format fmt)
{
    return (fmt >= MPP_FMT_YUV420P && fmt <= MPP_FMT_YUV444P);
}

/* Initialize default configuration */
static void init_default_config(csc_test_config_t *config)
{
    memset(config, 0, sizeof(csc_test_config_t));

    config->src_format = DEFAULT_SRC_FORMAT;
    config->dst_format = DEFAULT_DST_FORMAT;
    config->src_color_space = DEFAULT_SRC_COLOR_SPACE;
    config->dst_color_space = DEFAULT_DST_COLOR_SPACE;
}

/* Print usage information */
static void print_usage(const char *prog_name)
{
    printf("Usage: %s [OPTIONS]\n", prog_name);
    printf("\nARTVG Color Space Conversion Test - RGB <-> YUV format conversion\n");
    printf("\nConversion direction is automatically determined by src_format and dst_format:\n");
    printf("  RGB src + YUV dst = RGB -> YUV conversion\n");
    printf("  YUV src + RGB dst = YUV -> RGB conversion\n");

    printf("\nFormats:\n");
    printf("  --src_format <format>        Source format (default: argb8888)\n");
    printf("  --dst_format <format>        Target format (default: nv12)\n");
    printf("  --list_formats               List all supported formats\n");

    printf("\nColor spaces:\n");
    printf("  --src_color_space <bt601|bt601_full|bt709|bt709_full>\n");
    printf("                              Source color space (default: bt709)\n");
    printf("  --dst_color_space <bt601|bt601_full|bt709|bt709_full>\n");
    printf("                              Destination color space (default: bt709)\n");

    printf("\nOther:\n");
    printf("  --help                       Show this help\n");

    printf("\nSupported formats:\n");
    printf("  RGB: argb8888, rgb565, rgb888, bgr888\n");
    printf("  YUV Planar: yuv420p, yuv422p, yuv444p\n");
    printf("  YUV Semi-planar: nv12, nv21, nv16, nv61\n");
    printf("  YUV Packed: yuyv, yvyu, uyvy, vyuy\n");

    printf("\nExamples:\n");
    printf("  %s --src_format argb8888 --dst_format nv12 --src_color_space bt709 --dst_color_space bt709\n", prog_name);
    printf("  %s --src_format yuv420p --dst_format argb8888 --src_color_space bt601_full --dst_color_space bt709\n", prog_name);
    printf("  %s --src_format argb8888 --dst_format nv12 --src_color_space bt601 --dst_color_space bt709_full\n", prog_name);
}

/* List all supported formats */
static void list_formats(void)
{
    printf("=== Supported Formats ===\n\n");

    printf("RGB Formats:\n");
    printf("  argb8888 - 32-bit ARGB\n");
    printf("  rgb565   - 16-bit RGB\n");
    printf("  rgb888   - 24-bit RGB\n");
    printf("  bgr888   - 24-bit BGR\n");

    printf("\nYUV Formats:\n");
    printf("  yuv420p  - YUV 4:2:0 planar (3 planes)\n");
    printf("  nv12     - YUV 4:2:0 semi-planar (2 planes)\n");
    printf("  nv21     - YUV 4:2:0 semi-planar swapped (2 planes)\n");
    printf("  yuv422p  - YUV 4:2:2 planar (3 planes)\n");
    printf("  nv16     - YUV 4:2:2 semi-planar (2 planes)\n");
    printf("  nv61     - YUV 4:2:2 semi-planar swapped (2 planes)\n");
    printf("  yuyv     - YUYV 4:2:2 packed (1 plane)\n");
    printf("  yvyu     - YVYU 4:2:2 packed (1 plane)\n");
    printf("  uyvy     - UYVY 4:2:2 packed (1 plane)\n");
    printf("  vyuy     - VYUY 4:2:2 packed (1 plane)\n");
    printf("  yuv444p  - YUV 4:4:4 planar (3 planes)\n");
}

/* Parse command line arguments */
static int parse_arguments(int argc, char **argv, csc_test_config_t *config)
{
    static struct option long_options[] = {
        {"src_format", required_argument, 0, 0},
        {"dst_format", required_argument, 0, 0},
        {"list_formats", no_argument, 0, 0},
        {"src_color_space", required_argument, 0, 0},
        {"dst_color_space", required_argument, 0, 0},
        {"help", no_argument, 0, 'h'},
        {0, 0, 0, 0}
    };

    int option_index = 0;
    int c;

    while (1) {
        c = getopt_long(argc, argv, "h", long_options, &option_index);

        if (c == -1)
            break;

        if (c == 0) {
            const char *opt_name = long_options[option_index].name;

            if (strncmp(opt_name, "src_format", strlen("src_format")) == 0) {
                config->src_format = parse_format(optarg);
                if (config->src_format < 0) {
                    fprintf(stderr, "Error: Invalid source format: %s\n", optarg);
                    return -1;
                }
            } else if (strncmp(opt_name, "dst_format", strlen("dst_format")) == 0) {
                config->dst_format = parse_format(optarg);
                if (config->dst_format < 0) {
                    fprintf(stderr, "Error: Invalid destination format: %s\n", optarg);
                    return -1;
                }
            } else if (strncmp(opt_name, "list_formats", strlen("list_formats")) == 0) {
                list_formats();
                return 1;
            } else if (strncmp(opt_name, "src_color_space", strlen("src_color_space")) == 0) {
                config->src_color_space = parse_color_space(optarg);
                if (config->src_color_space < 0) {
                    fprintf(stderr, "Error: Invalid source color space: %s\n", optarg);
                    return -1;
                }
            } else if (strncmp(opt_name, "dst_color_space", strlen("dst_color_space")) == 0) {
                config->dst_color_space = parse_color_space(optarg);
                if (config->dst_color_space < 0) {
                    fprintf(stderr, "Error: Invalid destination color space: %s\n", optarg);
                    return -1;
                }
            }
        } else if (c == 'h') {
            print_usage(argv[0]);
            return 1;
        }
    }

    return 0;
}

/* Unified conversion test - direction determined from formats */
static int run_conversion_test(artvg_t *vg, drm_dev_t *drm_dev, csc_test_config_t *config)
{
    struct mpp_buf *src_buf = NULL;
    struct mpp_buf *dst_buf = NULL;
    struct mpp_buf display_buf = {0};
    artvg_ctrl_t ctrl;
    artvg_gradient_t gradient;
    artvg_error_t ret;
    int src_is_yuv = is_yuv_format(config->src_format);
    int dst_is_yuv = is_yuv_format(config->dst_format);

    printf("\n=== CSC Conversion Test ===\n");

    // Setup control structure
    memset(&ctrl, 0, sizeof(artvg_ctrl_t));
    ctrl.alpha_en = 0;

    // Setup fixed gradient (red to blue, full screen)
    memset(&gradient, 0, sizeof(gradient));
    gradient.start.x = 0;
    gradient.start.y = 0;
    gradient.end.x = config->width;
    gradient.end.y = config->height;
    gradient.colors[0] = GRADIENT_COLOR1;  // Red
    gradient.colors[1] = GRADIENT_COLOR2;  // Blue
    gradient.stops[0] = 0;
    gradient.stops[1] = 255;
    gradient.count = 2;
    gradient.precision = ARTVG_GRADIENT_PRECISION_256;
    gradient.spread = ARTVG_GRADIENT_SPREAD_PAD;

    printf("Gradient: Red (0x%08X) to Blue (0x%08X), full screen diagonal\n",
           gradient.colors[0], gradient.colors[1]);

    if (src_is_yuv && !dst_is_yuv) {
        // YUV -> RGB conversion
        printf("Mode: YUV to RGB conversion\n");
        printf("Source: YUV format %d, color space %d\n", config->src_format, config->src_color_space);
        printf("Destination: RGB format %d, color space %d\n", config->dst_format, config->dst_color_space);

        // Step 1: Create temporary RGB buffer with gradient
        src_buf = artvg_allocate(vg, config->width, config->height, MPP_FMT_ARGB_8888);
        if (!src_buf) {
            printf("Failed to allocate temp RGB buffer\n");
            return -1;
        }

        ret = artvg_fill_gradient(vg, src_buf, &ctrl, &gradient);
        if (ret != ARTVG_SUCCESS) {
            printf("Failed to fill gradient: %d\n", ret);
            artvg_free(vg, src_buf);
            return -1;
        }

        artvg_flush(vg);
        artvg_wait_finish(vg);
        printf("Temporary RGB buffer filled with gradient\n");

        // Step 2: Allocate YUV buffer and convert RGB to YUV
        dst_buf = artvg_allocate(vg, config->width, config->height, config->src_format);
        if (!dst_buf) {
            printf("Failed to allocate YUV buffer\n");
            artvg_free(vg, src_buf);
            return -1;
        }
        dst_buf->flags = config->src_color_space;

        printf("Generating YUV source data (RGB -> YUV %d)...\n", dst_buf->format);

        ret = artvg_blit(vg, src_buf, dst_buf, NULL, &ctrl, NULL);
        artvg_dumping_cmd(vg);
        if (ret != ARTVG_SUCCESS) {
            printf("Failed to convert RGB to YUV: %d\n", ret);
            artvg_free(vg, dst_buf);
            artvg_free(vg, src_buf);
            return -1;
        }

        artvg_flush(vg);
        artvg_wait_finish(vg);

        // Free temp RGB buffer, keep YUV data in dst_buf
        artvg_free(vg, src_buf);

        // Move YUV buffer to src_buf for final conversion
        src_buf = dst_buf;
        dst_buf = NULL;

        // Step 3: Setup display buffer
        if (drm_buffer_to_mpp_buffer(drm_dev, 0, &display_buf) != 0) {
            printf("Failed to convert DRM buffer to MPP buffer\n");
            artvg_free(vg, src_buf);
            return -1;
        }

        ret = artvg_add_dma_fd(vg, display_buf.fd[0]);
        if (ret != ARTVG_SUCCESS) {
            printf("Failed to add DMA FD: %d\n", ret);
            artvg_free(vg, src_buf);
            return -1;
        }

        // Set output color space for display buffer
        display_buf.flags = config->dst_color_space;

        // Step 4: Convert YUV to RGB for display
        printf("Converting YUV to RGB for display...\n");
        display_buf.crop_en = 1;
        display_buf.crop.x = 0;
        display_buf.crop.y = 0;
        display_buf.crop.width = config->width;
        display_buf.crop.height = config->height;

        ret = artvg_blit(vg, src_buf, &display_buf, NULL, &ctrl, NULL);
        if (ret != ARTVG_SUCCESS) {
            printf("Failed to perform YUV to RGB conversion: %d\n", ret);
            artvg_remove_dma_fd(vg, display_buf.fd[0]);
            artvg_free(vg, src_buf);
            return -1;
        }

        artvg_flush(vg);
        artvg_wait_finish(vg);

        // Cleanup
        artvg_remove_dma_fd(vg, display_buf.fd[0]);
        artvg_free(vg, src_buf);

    } else if (!src_is_yuv && dst_is_yuv) {
        // RGB -> YUV conversion
        printf("Mode: RGB to YUV conversion\n");
        printf("Source: RGB format %d\n", config->src_format);
        printf("Destination: YUV format %d, color space %d\n", config->dst_format, config->dst_color_space);

        // Allocate source RGB buffer
        src_buf = artvg_allocate(vg, config->width, config->height, config->src_format);
        if (!src_buf) {
            printf("Failed to allocate source buffer\n");
            return -1;
        }

        // Fill source buffer with gradient
        printf("Filling source buffer with gradient...\n");

        ret = artvg_fill_gradient(vg, src_buf, &ctrl, &gradient);
        if (ret != ARTVG_SUCCESS) {
            printf("Failed to fill gradient: %d\n", ret);
            artvg_free(vg, src_buf);
            return -1;
        }

        artvg_flush(vg);
        artvg_wait_finish(vg);
        printf("Source buffer filled with gradient\n");

        // Allocate destination YUV buffer
        dst_buf = artvg_allocate(vg, config->width, config->height, config->dst_format);
        if (!dst_buf) {
            printf("Failed to allocate destination buffer\n");
            artvg_free(vg, src_buf);
            return -1;
        }
        dst_buf->flags = config->dst_color_space;

        // Setup display buffer
        if (drm_buffer_to_mpp_buffer(drm_dev, 0, &display_buf) != 0) {
            printf("Failed to convert DRM buffer to MPP buffer\n");
            artvg_free(vg, dst_buf);
            artvg_free(vg, src_buf);
            return -1;
        }

        ret = artvg_add_dma_fd(vg, display_buf.fd[0]);
        if (ret != ARTVG_SUCCESS) {
            printf("Failed to add DMA FD: %d\n", ret);
            artvg_free(vg, dst_buf);
            artvg_free(vg, src_buf);
            return -1;
        }

        // Set output color space for display buffer
        display_buf.flags = config->dst_color_space;

        // Perform RGB to YUV conversion, then YUV to RGB for display
        printf("Converting RGB to YUV, then back to RGB for display...\n");

        ret = artvg_blit2(vg, src_buf, dst_buf, &display_buf, NULL, &ctrl, NULL);
        if (ret != ARTVG_SUCCESS) {
            printf("Failed to perform conversion: %d\n", ret);
            artvg_remove_dma_fd(vg, display_buf.fd[0]);
            artvg_free(vg, dst_buf);
            artvg_free(vg, src_buf);
            return -1;
        }

        artvg_flush(vg);
        artvg_wait_finish(vg);

        // Cleanup
        artvg_remove_dma_fd(vg, display_buf.fd[0]);
        artvg_free(vg, dst_buf);
        artvg_free(vg, src_buf);

    } else {
        printf("Error: Invalid format combination\n");
        printf("       Need one RGB and one YUV format for conversion\n");
        return -1;
    }

    printf("Conversion completed\n");

    // Display the result
    printf("Displaying result...\n");
    drm_buffer_flush(drm_dev, 0);
    drm_wait_vsync(drm_dev);

    printf("=== Test Complete ===\n\n");
    return 0;
}

/* Main function */
int main(int argc, char **argv)
{
    artvg_t *vg = NULL;
    drm_dev_t drm_dev = {0};
    csc_test_config_t config;
    int ret;

    printf("=== ARTVG Color Space Conversion Test ===\n");
    printf("RGB <-> YUV Format Conversion with Hardware Acceleration\n\n");

    // Initialize default configuration
    init_default_config(&config);

    // Parse command line arguments
    ret = parse_arguments(argc, argv, &config);
    if (ret == 1) {
        return 0;
    } else if (ret == -1) {
        return -1;  // Error
    }

    // Open ArtVG device
    vg = artvg_create();
    if (!vg) {
        printf("Failed to open ARTVG device\n");
        return -1;
    }
    printf("ARTVG device opened successfully\n");

    // Open DRM device (fixed path)
    if (drm_device_open(&drm_dev, DRM_DEVICE_PATH, DRM_CONNECTOR_ID) != 0) {
        printf("Failed to open DRM device\n");
        artvg_destroy(vg);
        return -1;
    }
    printf("DRM device opened successfully: %dx%d\n", drm_dev.width, drm_dev.height);

    // Set resolution from DRM
    config.width = drm_dev.width;
    config.height = drm_dev.height;

    printf("Test configuration:\n");
    printf("  Source format: %d (%s)\n", config.src_format,
           is_yuv_format(config.src_format) ? "YUV" : "RGB");
    printf("  Destination format: %d (%s)\n", config.dst_format,
           is_yuv_format(config.dst_format) ? "YUV" : "RGB");
    printf("  Source color space: %d\n", config.src_color_space);
    printf("  Destination color space: %d\n", config.dst_color_space);
    printf("  Resolution: %dx%d\n", config.width, config.height);
    printf("\n");

    // Run the conversion test
    ret = run_conversion_test(vg, &drm_dev, &config);

    getchar();  // Wait for Enter key

    // Cleanup
    drm_device_close(&drm_dev);
    artvg_destroy(vg);

    if (ret == 0) {
        printf("\n=== Test Completed Successfully ===\n");
    } else {
        printf("\n=== Test Failed ===\n");
    }

    return ret;
}
