/*
 * Copyright (c) 2026, ArtInChip Technology Co., Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Authors:  zequan liang <zequan.liang@artinchip.com>
 */

#ifndef ARTVG_CONTEXT_H
#define ARTVG_CONTEXT_H

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "mpp_list.h"
#include "video/artinchip_artvg.h"

#include "artvg.h"
#include "artvg_reg.h"
#include "include/artvg_os.h"
#include "artvg_drv.h"

#define ARTVG_LOG_LEVEL_NONE  0
#define ARTVG_LOG_LEVEL_ERROR 1
#define ARTVG_LOG_LEVEL_WARN  2
#define ARTVG_LOG_LEVEL_INFO  3
#define ARTVG_LOG_LEVEL_DEBUG 4

#ifndef ARTVG_LOG_LEVEL
#define ARTVG_LOG_LEVEL ARTVG_LOG_LEVEL_WARN
#endif

#if ARTVG_LOG_LEVEL >= ARTVG_LOG_LEVEL_DEBUG
#define artvg_log_debug(fmt, ...)  printf("[VG][D] %s-%d: "fmt, __FUNCTION__, __LINE__, ##__VA_ARGS__)
#else
#define artvg_log_debug(fmt, ...)
#endif

#if ARTVG_LOG_LEVEL >= ARTVG_LOG_LEVEL_INFO
#define artvg_log_info(fmt, ...)   printf("[VG][I] %s-%d: "fmt, __FUNCTION__, __LINE__, ##__VA_ARGS__)
#else
#define artvg_log_info(fmt, ...)
#endif

#if ARTVG_LOG_LEVEL >= ARTVG_LOG_LEVEL_WARN
#define artvg_log_warn(fmt, ...)   printf("[VG][W] %s-%d: "fmt, __FUNCTION__, __LINE__, ##__VA_ARGS__)
#else
#define artvg_log_warn(fmt, ...)
#endif

#if ARTVG_LOG_LEVEL >= ARTVG_LOG_LEVEL_ERROR
#define artvg_log_err(fmt, ...)    printf("[VG][E] %s-%d: "fmt, __FUNCTION__, __LINE__, ##__VA_ARGS__)
#else
#define artvg_log_err(fmt, ...)
#endif

#define ARTVG_RETURN_ERROR(func) \
    if ((error = func) != ARTVG_SUCCESS) \
        return error

#define ARTVG_ERR_ROOLBACK(func, slot, saved_count) \
    do { \
        if ((error = func) != ARTVG_SUCCESS) { \
            (slot)->cmd_count = (saved_count); \
            return error; \
        } \
    } while(0)

#define ARTVG_BREAK_ERROR(func) \
    if ((error = func) != ARTVG_SUCCESS) \
        break

#define ARTVG_ERROR_HANDLER(func) \
    if ((error = func) != ARTVG_SUCCESS) \
        goto ErrorHandler

#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]))

/* ==================== Version and configuration ==================== */
#define ARTVG_VERSION_ID_1_0 0x100
#define ARTVG_VERSION_STRING "ArtVG v1.0"
#define ARTVG_VERSION_STRING_MAX_LEN 32
#define CSC_COEFFS_NUM 12
#define VALID_SPACE (4096)

/* ==================== Multi-threading slot configuration ==================== */
#define ARTVG_CMD_SLOT_COUNT 2
#define ARTVG_CMD_SLOT_SIZE (32 * 1024)

/* ==================== Alignment macros ==================== */
#define ALIGN_2B(x) (((x) + (1)) & ~(1))
#define ALIGN_8B(x) (((x) + (7)) & ~(7))
#define ALIGN_16B(x) (((x) + (15)) & ~(15))
#define ALIGN_32B(x) (((x) + (31)) & ~(31))
#define ALIGN_64B(x) (((x) + (63)) & ~(63))
#define ALIGN_1024B(x) (((x) + (1023)) & ~(1023))
#define ARTVG_CACHE_LINE_SIZE 64
#define ALIGN_CACHE_LINE(x) (((x) + (ARTVG_CACHE_LINE_SIZE - 1)) & ~(ARTVG_CACHE_LINE_SIZE - 1))

/* ==================== Utility macros ==================== */
#define MIN(a, b) ((a) > (b) ? (b) : (a))
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define CLAMP(x, min_val, max_val) ((x) < (min_val) ? (min_val) : ((x) > (max_val) ? (max_val) : (x)))
#define MATRIX_EPSILON 1e-6f

/* ==================== Vector section constants ==================== */
/* next field: bits[23:0]=section size (data + 8), bits[63:24]=next phys addr
 * Max section size = 0xFFFFFF (24-bit), so max data = 0xFFFFFF - 8
 */
#define VECTOR_NEXT_FIELD_SIZE      8
#define VECTOR_NEXT_SIZE_MASK       0xFFFFFF
#define VECTOR_NEXT_ADDR_SHIFT      24
#define VECTOR_MAX_DATA_SIZE        (VECTOR_NEXT_SIZE_MASK - VECTOR_NEXT_FIELD_SIZE)

/* ==================== Curve flat limit constraints ==================== */
#define ARTVG_CURVE_FLAT_LIMIT_MIN     1
#define ARTVG_CURVE_FLAT_LIMIT_DEFAULT 10
#define ARTVG_CURVE_FLAT_LIMIT_MAX     255

/* ==================== Path buffer/path flags (internal use only) ==================== */
#define ARTVG_PATH_BUF_FLAG_PREALLOC    0x00000001  /* Buffer is pre-allocated (reuse mode) */
#define ARTVG_PATH_BUF_FLAG_VECTOR_POOL 0x00000002  /* Vector section from pool */

#define ARTVG_VECTOR_POOL_MIN_BLOCK_SIZE 256

/* ==================== Hardware vector commands ==================== */
#define VECTOR_CMD_HW_MOVE_TO 0x00
#define VECTOR_CMD_HW_LINE_TO 0x01
#define VECTOR_CMD_HW_CONIC_TO 0x02
#define VECTOR_CMD_HW_CUBIC_TO 0x03

/* Vector command hardware sizes (bytes) */
#define VECTOR_CMD_HW_SIZE_MOVE     8
#define VECTOR_CMD_HW_SIZE_LINE     8
#define VECTOR_CMD_HW_SIZE_QUAD     16
#define VECTOR_CMD_HW_SIZE_CUBIC    24

/* Vector section constants */
#define ARTVG_VECTOR_SECTION_SIZE   4096
#define VECTOR_SECTION_DATA_SIZE    (ARTVG_VECTOR_SECTION_SIZE - VECTOR_NEXT_FIELD_SIZE)

/* ==================== Edge buffer estimation ==================== */
#define ARTVG_EDGE_BUFFER_MULTIPLIER    3
#define ARTVG_EDGE_BUFFER_PER_LINE      (256 * ARTVG_EDGE_BUFFER_MULTIPLIER)

/* ==================== Affine transform type flags ==================== */
#define ARTVG_AFFINE_TRANS_IMAGE   0
#define ARTVG_AFFINE_TRANS_VECTOR  1

#define ARTVG_LUT_BLOCK_ALIGN_MODE 0
#define DMA_BUF_MIN_SIZE 4096 /* DMA buffer minimum allocation size (PAGE_SIZE = 4096 bytes) */

struct dma_fd_entry
{
    struct mpp_list list;
    int fd;
    uint32_t phy_addr;
};

struct cmd_slot {
    struct mpp_list list;
    uint64_t tid;
    uint32_t *cmd_buf;
    uint32_t cmd_count;
    uint32_t batch_id;
    unsigned int edge_buffer_size;
};

typedef struct {
        struct mpp_list list;
        uint32_t phy_addr;
        void *virt_addr;
} vector_pool_node_t;

struct artvg
{
    artvg_drv_t *drv;
    artvg_os_t *os;

    struct mpp_list fd_map_list;

    struct mpp_list free_list;
    struct mpp_list used_list;

    bool use_vector_pool;
    uint32_t vector_pool_block_count;
    uint32_t vector_pool_block_size;
    vector_pool_node_t *vector_pool_nodes;
    struct mpp_list vector_pool_free_list;
    struct dma_buf_info vector_pool_dma;
    void *vector_pool_base_addr;
};

/* ==================== Command queue management ==================== */
artvg_error_t reserve_command_buffer_space(artvg_t *vg, struct cmd_slot *slot);
void command_queue_add_command(struct cmd_slot *slot, uint32_t offset, uint32_t reg_count, uint32_t *data, bool end);

/* ==================== Slot management for multi-threading ==================== */
struct cmd_slot *acquire_slot_for_thread(artvg_t *vg);

/* ==================== Coordinate transformation ==================== */
int transform(artvg_point_t *result, float x, float y, artvg_matrix_t *matrix);

/* ==================== Configuration functions ==================== */
artvg_error_t configure_fill_color(struct cmd_slot *slot, artvg_gradient_t *gradient);
artvg_error_t configure_affine_transform(struct cmd_slot *slot, artvg_matrix_t *matrix, uint8_t trans_type);
artvg_error_t configure_fill_edge_color(struct cmd_slot *slot, artvg_transform_edge_t *transform_edge);
artvg_error_t configure_projective_transform(struct cmd_slot *slot, uint32_t phy_addr);
artvg_error_t configure_src_surface(artvg_t *vg, struct cmd_slot *slot, struct mpp_buf *src, struct mpp_buf *mask,
                                    artvg_ctrl_t *ctrl, uint32_t src_func_select, bool use_csc3,
                                    const struct mpp_rect *rect, enum mpp_pixel_format out_fmt);
artvg_error_t configure_dst_surface(artvg_t *vg, struct cmd_slot *slot, struct mpp_buf *dst, artvg_ctrl_t *ctrl,
                                    const struct mpp_rect *rect, enum mpp_pixel_format out_fmt);
artvg_error_t configure_output(artvg_t *vg, struct cmd_slot *slot, struct mpp_buf *output, artvg_dither_t dither_mode,
                               const struct mpp_rect *rect, artvg_projective_out_blk_mode_t trans_out_blk_mode);
artvg_error_t configure_palette(artvg_t *vg, struct cmd_slot *slot, struct mpp_buf *buf);
artvg_error_t configure_blend_mode_finish(struct cmd_slot *slot, artvg_ctrl_t *ctrl);

/* ==================== Buffer management ==================== */
void get_crop_rect(const struct mpp_buf *buffer, struct mpp_rect *rect);
artvg_error_t artvg_alloc_buffer_and_map(artvg_t *vg, uint32_t size, struct dma_buf_info *buf_info, void **addr);
artvg_error_t artvg_free_buffer_and_unmap(artvg_t *vg, struct dma_buf_info *buf_info, uint32_t size, void **addr);

/* ==================== Utility functions ==================== */
artvg_error_t artvg_resolution_detect(artvg_t *vg, uint32_t width, uint32_t height, int vector_draw);

/* ==================== Validation helper functions ==================== */
artvg_error_t artvg_validate_dither_scan_order(artvg_ctrl_t *ctrl);

#endif
