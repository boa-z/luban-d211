/*
 * Copyright (c) 2026, ArtInChip Technology Co., Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Authors:  zequan liang <zequan.liang@artinchip.com>
 */


#include "artvg_context.h"

static const uint32_t yuv2rgb_bt601[CSC_COEFFS_NUM] = {
    0x04a8, 0x0000, 0x0662, 0x3212,
    0x04a8, 0x1e70, 0x1cc0, 0x087a,
    0x04a8, 0x0811, 0x0000, 0x2eb4};

static const uint32_t yuv2rgb_bt709[CSC_COEFFS_NUM] = {
    0x04a8, 0x0000, 0x0722, 0x3093,
    0x04a8, 0x1f27, 0x1ddf, 0x04ce,
    0x04a8, 0x0873, 0x0000, 0x2df2};

static const uint32_t yuv2rgb_bt601_full[CSC_COEFFS_NUM] = {
    0x0400, 0x0000, 0x059c, 0x34ca,
    0x0400, 0x1ea1, 0x1d26, 0x0877,
    0x0400, 0x0717, 0x0000, 0x31d4};

static const uint32_t yuv2rgb_bt709_full[CSC_COEFFS_NUM] = {
    0x0400, 0x0000, 0x064d, 0x3368,
    0x0400, 0x1f41, 0x1e22, 0x053e,
    0x0400, 0x076c, 0x0000, 0x3129};

/* ==================== Slot management for multi-threading ==================== */
static void release_slot(artvg_t *vg, struct cmd_slot *slot);

/* ====================  Format and color space processing ==================== */
static uint8_t map_pixel_format_to_hw(enum mpp_pixel_format format);
static uint8_t convert_color_space_mode(uint8_t flag);
static bool is_yuv_format(enum mpp_pixel_format format);
static bool has_alpha_channel(enum mpp_pixel_format format);

/* ====================  Blending and Alpha processing ==================== */
static uint8_t get_blend_mode_value(artvg_alpha_blend_t alpha_rules);
static void get_blending_factors(artvg_alpha_blend_t blend_mode, uint8_t *src_factor_mode, uint8_t *dst_factor_mode);

/* ====================  Helper functions ==================== */
static artvg_error_t get_phy_addr(artvg_t *vg, struct mpp_buf *buffer, uint32_t phy_addr[]);
static artvg_error_t get_crop_phy_addr(struct mpp_buf *buffer, uint32_t addr[], const struct mpp_rect *crop);
static artvg_error_t dma_fd_map(artvg_t *vg, int fd);
static artvg_error_t dma_fd_unmap(artvg_t *vg, int fd);
static void calculate_stride_value(enum mpp_pixel_format fmt, uint32_t input_width, uint32_t stride[]);
static void calculate_height_value(enum mpp_pixel_format fmt, uint32_t input_height, uint32_t height[]);
static void clean_up_fd_map_list(struct mpp_list *list);
static void update_bounding_box(struct mpp_rect *box, artvg_point_t *point);
static int calculate_gradient_offset_and_step(float x1, float y1, float x2, float y2,
                                        int32_t *offset, int32_t *h_step, int32_t *v_step);
static void generate_gradient_color_lut(artvg_gradient_t *grad, 
                                   uint32_t *lut, uint32_t lut_size);

artvg_t *artvg_create(void)
{
    artvg_t *vg = artvg_os_malloc(sizeof(artvg_t));
    if (vg == NULL) {
        artvg_log_err("malloc artvg handle failed\n");
        return NULL;
    }
    memset(vg, 0, sizeof(artvg_t));

    vg->os = artvg_os_ctx_create();
    if (vg->os == NULL) {
        artvg_log_err("create artvg os failed\n");
        goto create_failed;
    }

    if (artvg_os_dma_dev_open(vg->os) != 0) {
        artvg_log_err("open dma device failed\n");
        goto create_failed;
    }

    mpp_list_init(&vg->free_list);
    mpp_list_init(&vg->used_list);

    vg->drv = artvg_drv_open();
    if (vg->drv == NULL) {
        artvg_log_err("open artvg driver failed\n");
        goto create_failed;
    }

    for (int i = 0; i < ARTVG_CMD_SLOT_COUNT; i++) {
        struct cmd_slot *slot = artvg_os_malloc(sizeof(struct cmd_slot));
        if (!slot) {
            artvg_log_err("malloc slot %d failed\n", i);
            goto create_failed;
        }
        memset(slot, 0, sizeof(struct cmd_slot));
        slot->batch_id = 0;

        slot->cmd_buf = artvg_os_malloc(ARTVG_CMD_SLOT_SIZE);
        if (!slot->cmd_buf) {
            artvg_log_err("malloc slot %d cmd buf failed\n", i);
            artvg_os_free(slot);
            goto create_failed;
        }
        mpp_list_add_tail(&slot->list, &vg->free_list);
    }

    mpp_list_init(&vg->fd_map_list);

    artvg_log_debug("artvg open with %d pre-allocated slots (%d bytes each)\n",
                    ARTVG_CMD_SLOT_COUNT, ARTVG_CMD_SLOT_SIZE);

    return vg;
create_failed:
    if (vg) {
        struct cmd_slot *slot, *node;
        mpp_list_for_each_entry_safe(slot, node, &vg->free_list, list) {
            if (slot->cmd_buf) {
                artvg_os_free(slot->cmd_buf);
            }
            artvg_os_free(slot);
        }
        mpp_list_for_each_entry_safe(slot, node, &vg->used_list, list) {
            if (slot->cmd_buf) {
                artvg_os_free(slot->cmd_buf);
            }
            artvg_os_free(slot);
        }
        if (vg->os) {
            artvg_os_ctx_destroy(vg->os);
        }
        if (vg->drv) {
            artvg_drv_close(vg->drv);
        }
        artvg_os_free(vg);
    }
    return NULL;
}

artvg_error_t artvg_destroy(artvg_t *vg)
{
    struct cmd_slot *slot = NULL, *node = NULL;
    int ret = 0;

    if (!vg) {
        return ARTVG_INVALID_PARAM;
    }

    artvg_disable_edge_buffer_pool(vg);

    if (vg->use_vector_pool) {
        uint32_t total_size = vg->vector_pool_block_count * vg->vector_pool_block_size;
        if (vg->vector_pool_base_addr) {
            artvg_free_buffer_and_unmap(vg, &vg->vector_pool_dma,
                                        total_size, &vg->vector_pool_base_addr);
        }
        if (vg->vector_pool_nodes) {
            artvg_os_free(vg->vector_pool_nodes);
        }
        vg->use_vector_pool = false;
    }
    mpp_list_for_each_entry_safe(slot, node, &vg->free_list, list) {
        if (slot->cmd_buf) {
            artvg_os_free(slot->cmd_buf);
        }
        artvg_os_free(slot);
    }
    mpp_list_init(&vg->free_list);

    mpp_list_for_each_entry_safe(slot, node, &vg->used_list, list) {
        if (slot->cmd_buf) {
            artvg_os_free(slot->cmd_buf);
        }
        artvg_os_free(slot);
    }
    mpp_list_init(&vg->used_list);

    artvg_os_ctx_destroy(vg->os);

    ret = artvg_drv_close(vg->drv);
    if (ret < 0) {
        artvg_log_err("close artvg driver failed\n");
        return ARTVG_GENERIC_IO;
    }

    clean_up_fd_map_list(&vg->fd_map_list);

    artvg_os_free(vg);

    artvg_log_debug("artvg close\n");
    return ARTVG_SUCCESS;
}

artvg_error_t artvg_get_version(char *version, uint32_t len)
{
    if (!version) {
        return ARTVG_INVALID_PARAM;
    }

    uint32_t version_len = strlen(ARTVG_VERSION_STRING);
    uint32_t copy_len = (len < version_len) ? len : version_len;

    if (copy_len > 0) {
        strncpy(version, ARTVG_VERSION_STRING, copy_len);
        version[copy_len] = '\0';
    }

    return ARTVG_SUCCESS;
}

artvg_error_t artvg_flush(artvg_t *vg)
{
    uint32_t batch_id;
    struct cmd_slot *slot = NULL;
    artvg_submit_t submit = {0};
    int ret = 0;

    if (!vg) {
        return ARTVG_INVALID_PARAM;
    }

    slot = acquire_slot_for_thread(vg);
    if (!slot) {
        artvg_log_err("Failed to acquire slot for flush\n");
        return ARTVG_OUT_OF_RESOURCES;
    }

    if (slot->cmd_count == 0) {
        return ARTVG_SUCCESS;
    }

    submit.cmd = (uint8_t *)slot->cmd_buf;
    submit.len = slot->cmd_count * sizeof(uint32_t);
    submit.edge_buffer_size = slot->edge_buffer_size;

    ret = artvg_drv_command(vg->drv, ARTVG_COMMAND_SUBMIT, &submit);
    if (ret != ARTVG_SUCCESS) {
        artvg_log_err("flush submit failed\n");
        slot->batch_id = 0;
        slot->cmd_count = 0;
        release_slot(vg, slot);
        return ARTVG_GENERIC_IO;
    }
    batch_id = submit.batch_id;
    artvg_log_debug("artvg flush done, batch_id=%d\n", batch_id);

    slot->cmd_count = 0;
    slot->batch_id = batch_id;
    slot->edge_buffer_size = 0;

    return ARTVG_SUCCESS;
}

artvg_error_t artvg_wait_finish(artvg_t *vg)
{
    int ret = 0;
    struct cmd_slot *slot = NULL;
    uint32_t batch_id;

    if (!vg) {
        return ARTVG_INVALID_PARAM;
    }

    slot = acquire_slot_for_thread(vg);
    if (!slot) {
        artvg_log_err("Failed to acquire slot for wait\n");
        return ARTVG_OUT_OF_RESOURCES;
    }

    batch_id = slot->batch_id;

    /* If there's no pending batch and no commands to flush, return early */
    if (batch_id == 0 && slot->cmd_count == 0) {
        release_slot(vg, slot);
        return ARTVG_SUCCESS;
    }

    if (slot->cmd_count > 0) {
        artvg_log_warn("There are unflushed commands, please flush them first\n");
        release_slot(vg, slot);
        return ARTVG_NOT_SUPPORTED;
    }

    if (batch_id != 0) {
        ret = artvg_drv_command(vg->drv, ARTVG_COMMAND_WAIT_BATCH, &batch_id);
        if (ret != ARTVG_SUCCESS) {
            artvg_log_err("artvg wait batch %u failed\n", batch_id);
            release_slot(vg, slot);
            return ARTVG_GENERIC_IO;
        }
        artvg_log_debug("artvg finish batch %d\n", batch_id);
        slot->batch_id = 0;
    }

    release_slot(vg, slot);
    return ARTVG_SUCCESS;
}

struct mpp_buf *artvg_allocate(artvg_t *vg, int width, int height, enum mpp_pixel_format format)
{
    struct dma_buf_info dma_info[3] = {0};
    uint32_t allocated_planes = 0;
    uint32_t stride[3] = {0};
    uint32_t height_arr[3] = {0};
    uint32_t size[3] = {0};

    if (!vg || width <= 0 || height <= 0) {
        artvg_log_err("artvg_allocate: invalid parameters\n");
        return NULL;
    }

    struct mpp_buf *buffer = (struct mpp_buf *)artvg_os_malloc(sizeof(struct mpp_buf));
    if (!buffer) {
        artvg_log_err("artvg_allocate: failed to allocate mpp_buf structure\n");
        return NULL;
    }

    memset(buffer, 0, sizeof(struct mpp_buf));
    buffer->size.width = width;
    buffer->size.height = height;
    buffer->format = format;
    buffer->fd[0] = buffer->fd[1] = buffer->fd[2] = -1;

    calculate_stride_value(buffer->format, buffer->size.width, stride);
    calculate_height_value(buffer->format, buffer->size.height, height_arr);

    for (int i = 0; i < 3; i++) {
        buffer->stride[i] = stride[i];
        size[i] = stride[i] * height_arr[i];
        if (size[i] == 0) {
            break;
        }

        if (artvg_os_dma_buf_alloc(vg->os, size[i], &dma_info[allocated_planes]) != 0) {
            goto allocate_err;
        }

        if (dma_info[i].fd >= 0) {
            buffer->buf_type = MPP_DMA_BUF_FD;
            buffer->fd[i] = dma_info[i].fd;
        } else {
            buffer->buf_type = MPP_PHY_ADDR;
            buffer->phy_addr[i] = dma_info[i].phy_addr;
        }
        allocated_planes++;
    }

    if (buffer->buf_type == MPP_DMA_BUF_FD) {
        for (int i = 0; i < allocated_planes; i++) {
            if (dma_fd_map(vg, buffer->fd[i]) != 0) {
                artvg_log_err("map dma buffer failed\n");
                goto allocate_err;
            }
        }
    }

    artvg_log_debug("artvg_allocate: size %d, stride %d, height %d, format %d, buf_type %d\n",
                    buffer->size.width, buffer->stride[0], buffer->size.height, buffer->format, buffer->buf_type);
    return buffer;

allocate_err:
    if (buffer->buf_type == MPP_DMA_BUF_FD) {
        for (int i = 0; i < allocated_planes; i++) {
            if (buffer->fd[i] < 0)
                break;
            if (dma_fd_unmap(vg, buffer->fd[i]) != 0)
            {
                artvg_log_err("unmap dma buffer failed\n");
            }
        }
    }

    for (int i = 0; i < allocated_planes; i++) {
        artvg_os_dma_buf_release(vg->os, &dma_info[i]);
    }

    artvg_os_free(buffer);
    return NULL;
}

artvg_error_t artvg_free(artvg_t *vg, struct mpp_buf *buffer)
{
    if (!vg || !buffer) {
        return ARTVG_INVALID_PARAM;
    }

    artvg_log_debug("artvg_free: size %d, stride %d, height %d, format %d, buf_type %d\n",
                    buffer->size.width, buffer->stride[0], buffer->size.height, buffer->format, buffer->buf_type);

    if (buffer->buf_type == MPP_DMA_BUF_FD) {
        for (int i = 0; i < 3; i++) {
            if (buffer->fd[i] < 0)
                break;
            if (dma_fd_unmap(vg, buffer->fd[i]) != 0) {
                artvg_log_err("unmap dma buffer failed\n");
            }
        }
    }

    struct dma_buf_info dma_info = {0};
    for (int i = 0; i < 3; i++) {
        if (buffer->fd[i] >= 0 || buffer->phy_addr[i] != 0) {
            dma_info.fd = buffer->fd[i];
            dma_info.phy_addr = buffer->phy_addr[i];
            if (artvg_os_dma_buf_release(vg->os, &dma_info) != 0) {
                artvg_log_warn("free dma buffer fd %d failed\n", (int)buffer->fd[i]);
            }
        }
    }

    artvg_os_free(buffer);
    return ARTVG_SUCCESS;
}

artvg_error_t artvg_add_dma_fd(artvg_t *vg, int fd)
{
    if (!vg) {
        return ARTVG_INVALID_PARAM;
    }
    return dma_fd_map(vg, fd);
}

artvg_error_t artvg_remove_dma_fd(artvg_t *vg, int fd)
{
    if (!vg) {
        return ARTVG_INVALID_PARAM;
    }
    return dma_fd_unmap(vg, fd);
}

artvg_error_t artvg_clear(artvg_t *vg, struct mpp_buf *dst, uint32_t color)
{
    if (!vg || !dst) {
        return ARTVG_INVALID_PARAM;
    }

    artvg_ctrl_t ctrl = {0};
    artvg_gradient_t gradient = {0};
    gradient.count = 1;
    gradient.colors[0] = color;
    return artvg_fill_gradient(vg, dst, &ctrl, &gradient);
}

artvg_error_t artvg_fill_gradient(artvg_t *vg,
                                         struct mpp_buf *dst,
                                         artvg_ctrl_t *ctrl,
                                         artvg_gradient_t *gradient)
{
    return artvg_fill_gradient2(vg, dst, dst, ctrl, gradient);
}

artvg_error_t artvg_fill_gradient2(artvg_t *vg,
                                         struct mpp_buf *dst,
                                         struct mpp_buf *output,
                                         artvg_ctrl_t *ctrl,
                                         artvg_gradient_t *gradient)
{
    if (!vg || !dst || !output) {
        return ARTVG_INVALID_PARAM;
    }

    artvg_ctrl_t ctl_default = {0};
    artvg_gradient_t gradient_default = {0};
    artvg_error_t error = ARTVG_SUCCESS;

    gradient_default.count = 1;
    gradient_default.colors[0] = 0;

    gradient = gradient == NULL ? &gradient_default : gradient;
    ctrl = ctrl == NULL ? &ctl_default : ctrl;

    if (artvg_resolution_detect(vg, output->size.width, output->size.height, 0) != ARTVG_SUCCESS) {
        artvg_log_err("resolution not supported, width: %d, height: %d\n", output->size.width, output->size.height);
        return ARTVG_NOT_SUPPORTED;
    }

    struct cmd_slot *slot = acquire_slot_for_thread(vg);
    if (!slot) {
        return ARTVG_OUT_OF_RESOURCES;
    }

    if (reserve_command_buffer_space(vg, slot) != ARTVG_SUCCESS) {
        return ARTVG_OUT_OF_RESOURCES;
    }

    uint32_t cmd_count_saved = slot->cmd_count;
    struct mpp_rect dst_rect = {0};
    struct mpp_rect output_rect = {0};

    get_crop_rect(output, &output_rect);
    get_crop_rect(dst, &dst_rect);

    ARTVG_ERR_ROOLBACK(configure_fill_color(slot, gradient), slot, cmd_count_saved);

    uint32_t src_func_sel = COLOR_FILL_EN | FUNC_SELECT(0);
    src_func_sel |= COLOR_FILL_MODE(gradient->precision);
    src_func_sel |= FILL_SPREAD(gradient->spread);

    struct mpp_buf intern_src = {0};
    struct mpp_rect intern_src_rect = {0};
    intern_src.buf_type = MPP_PHY_ADDR;
    intern_src.format = MPP_FMT_ARGB_8888;

    ARTVG_ERR_ROOLBACK(configure_src_surface(vg, slot, &intern_src, NULL, ctrl,
                          src_func_sel, true, &intern_src_rect, output->format), slot, cmd_count_saved);

    ARTVG_ERR_ROOLBACK(configure_dst_surface(vg, slot, dst, ctrl, &dst_rect, output->format), slot, cmd_count_saved);

    ARTVG_ERR_ROOLBACK(configure_output(vg, slot, output, ctrl->output_dither, &output_rect, 0), slot, cmd_count_saved);

    ARTVG_ERR_ROOLBACK(configure_blend_mode_finish(slot, ctrl), slot, cmd_count_saved);

    artvg_log_debug("size %d, stride %d, height %d, format %d, buf_type %d, fd=%d\n",
                    dst->size.width, dst->stride[0], dst->size.height, dst->format, dst->buf_type, dst->fd[0]);

    return ARTVG_SUCCESS;
}

artvg_error_t artvg_dumping_cmd(artvg_t *vg)
{
    if (!vg) {
        return ARTVG_INVALID_PARAM;
    }

    /* Dump current thread's slot commands */
    struct cmd_slot *slot = acquire_slot_for_thread(vg);
    if (!slot) {
        artvg_log_err("Failed to acquire slot for dumping\n");
        return ARTVG_OUT_OF_RESOURCES;
    }

    printf("Command slot buffer capacity: %d bytes\n", ARTVG_CMD_SLOT_SIZE);
    printf("Command slot cmd_count: %u\n", slot->cmd_count);

    uint32_t *cmd_data = slot->cmd_buf;
    uint32_t offset = 0;

    while (offset < slot->cmd_count) {
        uint32_t header = cmd_data[offset];
        uint32_t reg_offset = (header >> 16) & 0xFFFF;
        uint32_t reg_count = (header >> 2) & 0x3FFF;
        uint32_t end_flag = header & 0x03;

        (void)reg_offset;
        printf("\n[CMD Header @ 0x%04x] Offset: 0x%04x, RegCount: %u, EndFlag: %u\n",
               (unsigned int)(offset * sizeof(uint32_t)), reg_offset, reg_count, end_flag);

        if (reg_count > 0) {
            printf("  Data:");
            for (uint32_t i = 0; i < reg_count; i++)
            {
                if (i % 4 == 0)
                    printf("\n    ");
                printf("0x%08x ", cmd_data[offset + 1 + i]);
            }
            printf("\n");
        }

        uint32_t cmd_size = reg_count + 1;
        offset += cmd_size;

        if (end_flag == 1)
            offset = ALIGN_2B(offset);
    }
    printf("\n");

    return ARTVG_SUCCESS;
}

artvg_error_t artvg_get_flush_batch(artvg_t *vg, uint32_t *batch_id)
{
    struct cmd_slot *slot = acquire_slot_for_thread(vg);
    if (!slot) {
        return ARTVG_OUT_OF_RESOURCES;
    }

    if (batch_id)
        *batch_id = slot->batch_id;

    return ARTVG_SUCCESS;
}

artvg_error_t artvg_wait_batch_finish(artvg_t *vg, uint32_t batch_id)
{
    artvg_error_t ret = ARTVG_SUCCESS;

    if (batch_id != 0) {
        ret = artvg_drv_command(vg->drv, ARTVG_COMMAND_WAIT_BATCH, &batch_id);
        if (ret != ARTVG_SUCCESS) {
            artvg_log_err("artvg wait batch %u failed\n", batch_id);
            return ARTVG_GENERIC_IO;
        }
        artvg_log_debug("artvg finish batch %d\n", batch_id);
    }

    return ret;
}

artvg_error_t artvg_get_completed_batch(artvg_t *vg, uint32_t *batch_id)
{
    uint32_t completed_batch_id = 0;
    int ret;

    ret = artvg_drv_command(vg->drv, ARTVG_COMMAND_GET_BATCH, &completed_batch_id);
    if (ret != 0) {
        artvg_log_err("artvg get batch %u failed\n", completed_batch_id);
        return ARTVG_GENERIC_IO;
    }

    if (batch_id)
        *batch_id = completed_batch_id;

    return ARTVG_SUCCESS;
}

artvg_error_t artvg_batch_completed(artvg_t *vg, uint32_t flush_batch_id, uint32_t completed_batch_id, bool *completed)
{
    if ((int32_t)(completed_batch_id - flush_batch_id) < 0) {
        if (completed)
            *completed = false;
    } else {
        if (completed)
            *completed = true;
    }

    return ARTVG_SUCCESS;
}

/* ==================== Command queue management ==================== */
artvg_error_t reserve_command_buffer_space(artvg_t *vg, struct cmd_slot *slot)
{
    artvg_error_t ret;

    if (!vg || !slot) {
        return ARTVG_INVALID_PARAM;
    }

    if (slot->cmd_count * sizeof(uint32_t) + VALID_SPACE > ARTVG_CMD_SLOT_SIZE) {
        artvg_log_debug("reserve_command_buffer_space: cmd_count %d, capacity %d, need %d\n",
                        slot->cmd_count, ARTVG_CMD_SLOT_SIZE, VALID_SPACE);
        ret = artvg_flush(vg);
        return ret;
    }
    return ARTVG_SUCCESS;
}

void command_queue_add_command(struct cmd_slot *slot, uint32_t offset, uint32_t reg_count, uint32_t *data, bool end)
{
    uint32_t *cmd_buf = NULL;
    uint32_t cmd_offset = 0;

    if (!slot) {
        artvg_log_err("Failed to acquire slot for command\n");
        return;
    }

    cmd_buf = slot->cmd_buf;
    cmd_offset = slot->cmd_count;

    uint32_t cmd_header = (offset & 0xFFFF) << 16 | (reg_count << 2) | (end ? 0x01 : 0x00);

    memcpy(&cmd_buf[cmd_offset], &cmd_header, sizeof(uint32_t));

    if (reg_count > 0 && data != NULL) {
        memcpy(&cmd_buf[cmd_offset + 1], data, reg_count * sizeof(uint32_t));
    }

    slot->cmd_count += reg_count + 1;

    if (end) {
        slot->cmd_count = ALIGN_2B(slot->cmd_count);
    }
}

/* Transform a 2D point by a given matrix. */
int transform(artvg_point_t *result, float x, float y, artvg_matrix_t *matrix)
{
    float pt_x;
    float pt_y;
    float pt_w;

    pt_x = (x * matrix->m[0][0]) + (y * matrix->m[0][1]) + matrix->m[0][2];
    pt_y = (x * matrix->m[1][0]) + (y * matrix->m[1][1]) + matrix->m[1][2];
    pt_w = (x * matrix->m[2][0]) + (y * matrix->m[2][1]) + matrix->m[2][2];

    if (pt_w <= 0.0f)
        return 0;

    result->x = (int)(pt_x / pt_w);
    result->y = (int)(pt_y / pt_w);

    return 1;
}

artvg_error_t artvg_transform_bounding_box(struct mpp_rect *in_bbx,
                                            artvg_matrix_t *matrix,
                                            struct mpp_rect *clip,
                                            struct mpp_rect *out_bbx)
{
    artvg_point_t temp = {0};

    if(!in_bbx || !matrix || !clip || !out_bbx)
       return ARTVG_INVALID_PARAM;
 
    memset(out_bbx, 0, sizeof(struct mpp_rect));

    if (!transform(&temp, in_bbx->x, in_bbx->y, matrix))
        return ARTVG_INVALID_PARAM;

    out_bbx->x = temp.x;
    out_bbx->y = temp.y;

    /* Transform image point (x, y+height). */
    if (!transform(&temp, in_bbx->x, (in_bbx->y + in_bbx->height), matrix))
        return ARTVG_INVALID_PARAM;
    update_bounding_box(out_bbx, &temp);

    /* Transform image point (x+width, y+height). */
    if (!transform(&temp, (in_bbx->x + in_bbx->width), (in_bbx->y + in_bbx->height),
                   matrix))
        return ARTVG_INVALID_PARAM;
    update_bounding_box(out_bbx, &temp);

    /* Transform image point (x+width, y). */
    if (!transform(&temp, (in_bbx->x + in_bbx->width), in_bbx->y, matrix))
        return ARTVG_INVALID_PARAM;
    update_bounding_box(out_bbx, &temp);

    /* Clip is required */
    if (clip) {
        out_bbx->x = MAX(out_bbx->x, clip->x);
        out_bbx->y = MAX(out_bbx->y, clip->y);
        out_bbx->width = MIN((out_bbx->x + out_bbx->width), (clip->x + clip->width)) - out_bbx->x;
        out_bbx->height = MIN((out_bbx->y + out_bbx->height), (clip->y + clip->height)) - out_bbx->y;
    }

    return ARTVG_SUCCESS;
}


/* ==================== Slot management for multi-threading ==================== */
struct cmd_slot *acquire_slot_for_thread(artvg_t *vg)
{
    uint64_t self = artvg_os_thread_id();
    struct cmd_slot *slot = NULL;

    artvg_os_mutex_lock(vg->os);

    mpp_list_for_each_entry(slot, &vg->used_list, list) {
        if (slot->tid == self) {
            artvg_os_mutex_unlock(vg->os);
            return slot;
        }
    }

    if (!mpp_list_empty(&vg->free_list)) {
        slot = mpp_list_first_entry(&vg->free_list, struct cmd_slot, list);
        mpp_list_del(&slot->list);
        mpp_list_add_tail(&slot->list, &vg->used_list);

        slot->tid = self;
        slot->cmd_count = 0;
        slot->batch_id = 0;

        artvg_os_mutex_unlock(vg->os);
        return slot;
    }

    artvg_os_mutex_unlock(vg->os);

    slot = artvg_os_malloc(sizeof(struct cmd_slot));
    if (!slot) {
        artvg_log_err("Failed to allocate slot\n");
        return NULL;
    }

    memset(slot, 0, sizeof(struct cmd_slot));

    slot->cmd_buf = artvg_os_malloc(ARTVG_CMD_SLOT_SIZE);
    if (!slot->cmd_buf) {
        artvg_log_err("Failed to allocate slot buffer\n");
        artvg_os_free(slot);
        return NULL;
    }

    slot->tid = self;
    slot->cmd_count = 0;

    artvg_os_mutex_lock(vg->os);
    mpp_list_add_tail(&slot->list, &vg->used_list);
    artvg_os_mutex_unlock(vg->os);

    artvg_log_debug("Allocated new slot for thread\n");
    return slot;
}

static void release_slot(artvg_t *vg, struct cmd_slot *slot)
{
    artvg_os_mutex_lock(vg->os);
    if (slot->tid != 0) {
        mpp_list_del(&slot->list);

        slot->tid = 0;
        slot->cmd_count = 0;
        slot->batch_id = 0;
        slot->edge_buffer_size = 0;

        mpp_list_add_tail(&slot->list, &vg->free_list);
    }
    artvg_os_mutex_unlock(vg->os);
}

/* ====================  Buffer utilities ==================== */
void get_crop_rect(const struct mpp_buf *buffer, struct mpp_rect *rect)
{
    if (buffer->crop_en) {
        memcpy(rect, &buffer->crop, sizeof(struct mpp_rect));
    } else {
        rect->x = 0;
        rect->y = 0;
        rect->width = buffer->size.width;
        rect->height = buffer->size.height;
    }
}

/* ====================  Configuration functions ==================== */
artvg_error_t get_crop_phy_addr(struct mpp_buf *buffer, uint32_t addr[], const struct mpp_rect *crop)
{
    int offset = 0;

    if (!buffer || !crop || (addr[0] == 0 && addr[1] == 0 && addr[2] == 0))
        return ARTVG_SUCCESS;

    uint32_t x_offset = crop->x;
    uint32_t y_offset = crop->y;
    const uint32_t *stride = buffer->stride;

    switch (buffer->format) {
    case MPP_FMT_ARGB_8888:
    case MPP_FMT_ABGR_8888:
    case MPP_FMT_RGBA_8888:
    case MPP_FMT_BGRA_8888:
    case MPP_FMT_XRGB_8888:
    case MPP_FMT_XBGR_8888:
    case MPP_FMT_RGBX_8888:
    case MPP_FMT_BGRX_8888:
        addr[0] += x_offset * 4 + y_offset * stride[0];
        break;
    case MPP_FMT_RGB_888:
    case MPP_FMT_BGR_888:
        addr[0] += x_offset * 3 + y_offset * stride[0];
        break;
    case MPP_FMT_ARGB_1555:
    case MPP_FMT_ABGR_1555:
    case MPP_FMT_RGBA_5551:
    case MPP_FMT_BGRA_5551:
    case MPP_FMT_RGB_565:
    case MPP_FMT_BGR_565:
    case MPP_FMT_ARGB_4444:
    case MPP_FMT_ABGR_4444:
    case MPP_FMT_RGBA_4444:
    case MPP_FMT_BGRA_4444:
        addr[0] += x_offset * 2 + y_offset * stride[0];
        break;
    case MPP_FMT_YUV420P:
        addr[0] += x_offset + y_offset * stride[0];
        offset = (x_offset >> 1) + (y_offset >> 1) * stride[1];
        addr[1] += offset;
        addr[2] += offset;
        break;
    case MPP_FMT_NV12:
    case MPP_FMT_NV21:
        addr[0] += x_offset + y_offset * stride[0];
        addr[1] += x_offset + (y_offset >> 1) * stride[1];
        break;
    case MPP_FMT_YUV400:
        addr[0] += x_offset + y_offset * stride[0];
        break;
    case MPP_FMT_YUV422P:
        addr[0] += x_offset + y_offset * stride[0];
        offset = (x_offset >> 1) + y_offset * stride[1];
        addr[1] += offset;
        addr[2] += offset;
        break;
    case MPP_FMT_NV16:
    case MPP_FMT_NV61:
        addr[0] += x_offset + y_offset * stride[0];
        addr[1] += x_offset + y_offset * stride[1];
        break;
    case MPP_FMT_YUYV:
    case MPP_FMT_YVYU:
    case MPP_FMT_UYVY:
    case MPP_FMT_VYUY:
        addr[0] += (x_offset << 1) + y_offset * stride[0];
        break;
    case MPP_FMT_YUV444P:
        addr[0] += x_offset + y_offset * stride[0];
        addr[1] += x_offset + y_offset * stride[1];
        addr[2] += x_offset + y_offset * stride[1];
        break;
    case MPP_FMT_PALETTE8:
    case MPP_FMT_A8:
        addr[0] += x_offset + y_offset * stride[0];
        break;
    default:
        return ARTVG_INVALID_PARAM;
    }

    return ARTVG_SUCCESS;
}

artvg_error_t configure_src_surface(artvg_t *vg, struct cmd_slot *slot,
                                    struct mpp_buf *src, struct mpp_buf *mask,
                                    artvg_ctrl_t *ctrl, uint32_t src_func_select, bool use_csc3,
                                    const struct mpp_rect *rect, enum mpp_pixel_format out_fmt)
{
    uint32_t src_phy_addr[3] = {0};
    uint8_t src_premul_en = 0;
    uint8_t src_is_yuv = false;
    uint8_t output_is_yuv = false;
    artvg_error_t ret = ARTVG_SUCCESS;

    if (!MPP_BUF_PREMULTIPLY_GET(src->flags) &&
        has_alpha_channel(src->format) &&
        ctrl->alpha_en) {
        src_premul_en = 1;
    }

    if (src->format == MPP_FMT_PALETTE8) {
        ret = configure_palette(vg, slot, src);
        if (ret != ARTVG_SUCCESS) {
            artvg_log_err("configure_src_surface: failed to configure palette, ret=%d\n", ret);
            return ret;
        }
    } else {
        ret = get_phy_addr(vg, src, src_phy_addr);
        if (ret != ARTVG_SUCCESS) {
            artvg_log_err("configure_src_surface: failed to get src physical address, ret=%d\n", ret);
            return ret;
        }
        ret = get_crop_phy_addr(src, src_phy_addr, rect);
        if (ret != ARTVG_SUCCESS) {
            artvg_log_err("configure_src_surface: failed to get crop phy addr, ret=%d\n", ret);
            return ret;
        }
    }

    uint32_t src_color_space = convert_color_space_mode(src->flags);
    artvg_log_info("configure_src_surface: format=%d, flags=%u, color_space=%u\n",
                   src->format, src->flags, src_color_space);
    uint32_t src_format = map_pixel_format_to_hw(src->format);
    uint32_t src_ctrl = 0;
    src_ctrl |= SRC_INPUT_FORMAT(src_format);
    src_ctrl |= SRC_ALPHA_MODE(ctrl->src_alpha_mode);
    src_ctrl |= SRC_G_ALPHA(ctrl->src_global_alpha);
    src_ctrl |= src_premul_en ? SRC_PRE_MUL_EN : 0;
    src_ctrl |= ctrl->src_premul_mode ? SRC_PRE_MUL_MODE : 0;
    src_ctrl |= ctrl->csc0_pos == ARTVG_CSC0_BEFORE_FILL_COLOR ? 0 : SRC_CSC0_POS;
    src_ctrl |= SRC_CSC0_CSC3_COLOR_SPACE(src_color_space);

    src_is_yuv = is_yuv_format(src->format);
    output_is_yuv = is_yuv_format(out_fmt);
    if ((src_is_yuv && ctrl->alpha_en) ||
        (src_is_yuv && !output_is_yuv)) {
        src_ctrl |= use_csc3 ? SRC_CSC3_EN : SRC_CSC0_EN;
    }
    artvg_log_info("CSC0/CSC3: format=%d, is_yuv=%d, CSC_EN=0x%x\n",
                   src->format, is_yuv_format(src->format),
                   is_yuv_format(src->format) ? (use_csc3 ? SRC_CSC3_EN : SRC_CSC0_EN) : 0);

    if (!is_yuv_format(src->format))
        src_ctrl |= SRC_SCAN_ORDER(ctrl->src_scan_order);
    if (src_phy_addr[0] || (src_func_select & FUNC_SELECT(3) || src_func_select & FUNC_SELECT(4))) {
        src_ctrl |= SRC_EN;
    }

    uint32_t src_cmds[] = {
        src_ctrl,                                             // 0x010
        SRC_INPUT_SIZE_SET(rect->width, rect->height),        // 0x014
        SRC_STRIDE_SET(src->stride[0], src->stride[1]),       // 0x018
        src_func_select,                                      // 0x01C
        src_phy_addr[0],                                      // 0x020
        src_phy_addr[1],                                      // 0x024
        src_phy_addr[2],                                      // 0x028
        0,                                                    // 0x02C
    };
    command_queue_add_command(slot, SRC_SURFACE_CTRL, ARRAY_SIZE(src_cmds), src_cmds, false);

    if (ctrl->src_recolor_en)
        command_queue_add_command(slot, SRC_RECOLOR, 1, &ctrl->src_recolor_value, false);

    // configure csc0 coef
    if (is_yuv_format(src->format) && use_csc3 == false) {
        artvg_log_info("Configuring CSC0 coefficients: color_space=%u, yuv_format=%d\n",
                       src_color_space, is_yuv_format(src->format));

        const uint32_t *csc_table_map[] = {
            yuv2rgb_bt601,
            yuv2rgb_bt709,
            yuv2rgb_bt601_full,
            yuv2rgb_bt709_full
        };
        const uint32_t *table = (src_color_space < 4) ? csc_table_map[src_color_space] : yuv2rgb_bt601;

        uint32_t csc_table[CSC_COEFFS_NUM / 2];
        for (int i = 0; i < CSC_COEFFS_NUM; i += 2) {
            csc_table[i / 2] = CSC_COEF_SET_PAIR(table[i], table[i + 1]);
        }

        command_queue_add_command(slot, CSC0_COEF(0), ARRAY_SIZE(csc_table), csc_table, false);
    }

    /* configure mask */
    if (!mask)
        return ARTVG_SUCCESS;
    if (mask->format != MPP_FMT_A8) {
        artvg_log_err("mask format is not A8, mask will not be applied");
        return ARTVG_INVALID_PARAM;
    }
    struct mpp_rect mask_rect = {0};
    uint32_t mask_phy_addr[3] = {0};

    get_crop_rect(mask, &mask_rect);

    ret = get_phy_addr(vg, mask, mask_phy_addr);
    if (ret != ARTVG_SUCCESS) {
        artvg_log_err("configure_src_surface: failed to get mask physical address, ret=%d\n", ret);
        return ret;
    }
    ret = get_crop_phy_addr(mask, mask_phy_addr, &mask_rect);
    if (ret != ARTVG_SUCCESS) {
        artvg_log_err("configure_src_surface: failed to get mask crop phy addr, ret=%d\n", ret);
        return ret;
    }
    uint32_t mask_cmds[] = {
        MASK_LOW_ADDR_SET(mask_phy_addr[0]), // 0x050
        MASK_STRIDE_SET(mask->stride[0]),    // 0x054
    };

    command_queue_add_command(slot, MASK_LOW_ADDR, ARRAY_SIZE(mask_cmds), mask_cmds, false);
    return ARTVG_SUCCESS;
}

artvg_error_t configure_dst_surface(artvg_t *vg, struct cmd_slot *slot, struct mpp_buf *dst, artvg_ctrl_t *ctrl,
                                    const struct mpp_rect *rect, enum mpp_pixel_format out_fmt)
{
    uint32_t dst_ctrl = 0;
    uint8_t dst_is_yuv = is_yuv_format(dst->format);
    uint8_t output_is_yuv = is_yuv_format(out_fmt);

    if (!ctrl || ctrl->alpha_en == false) {
        command_queue_add_command(slot, DST_SURFACE_CTRL, 1, &dst_ctrl, false);
        return ARTVG_SUCCESS;
    }

    uint8_t dst_premul_en = 0;
    if (!MPP_BUF_PREMULTIPLY_GET(dst->flags) &&
        has_alpha_channel(dst->format) &&
        ctrl->alpha_en) {
        dst_premul_en = 1;
    }

    uint32_t dst_phy_addr[3] = {0};
    artvg_error_t ret = get_phy_addr(vg, dst, dst_phy_addr);
    if (ret != ARTVG_SUCCESS) {
        artvg_log_err("configure_dst_surface: failed to get dst physical address, ret=%d\n", ret);
        return ret;
    }

    uint32_t dst_format = map_pixel_format_to_hw(dst->format);
    uint32_t dst_color_space = convert_color_space_mode(dst->flags);
    artvg_log_info("configure_dst_surface: format=%d, flags=%u, color_space=%u, pos=(%d,%d)\n",
                   dst->format, dst->flags, dst_color_space, rect->x, rect->y);

    dst_ctrl |= DST_EN | DST_INPUT_FORMAT(dst_format);
    dst_ctrl |= DST_ALPHA_MODE(ctrl->dst_alpha_mode);
    dst_ctrl |= DST_G_ALPHA(ctrl->dst_global_alpha);
    dst_ctrl |= dst_premul_en ? DST_PRE_MUL_EN : 0;
    dst_ctrl |= ctrl->dst_premul_mode ? DST_PRE_MUL_MODE : 0;
    dst_ctrl |= CSC1_COLOR_SPACE(dst_color_space);

    if ((dst_is_yuv && ctrl->alpha_en) ||
        (dst_is_yuv && !output_is_yuv)) {
        dst_ctrl |= DST_CSC1_EN;
    }

    uint32_t dst_cmds[] = {
        dst_ctrl,                                          // 0x0b0
        DST_INPUT_SIZE_SET(rect->width, rect->height),     // 0x0b4
        DST_STRIDE_SET(dst->stride[0], dst->stride[1]),  // 0x0b8
        DST_OFFSET_SET(rect->x, rect->y),                   // 0x0bc
        dst_phy_addr[0],                                   // 0x0c0
        dst_phy_addr[1],                                   // 0x0c4
        dst_phy_addr[2],                                   // 0x0c8
        0,                                                 // 0x0cc
    };
    command_queue_add_command(slot, DST_SURFACE_CTRL, ARRAY_SIZE(dst_cmds), dst_cmds, false);
    return ARTVG_SUCCESS;
}

artvg_error_t configure_output(artvg_t *vg, struct cmd_slot *slot, struct mpp_buf *output, artvg_dither_t dither_mode,
                               const struct mpp_rect *rect, artvg_projective_out_blk_mode_t trans_out_blk_mode)
{
    uint32_t output_phy_addr[3] = {0};
    artvg_error_t ret = get_phy_addr(vg, output, output_phy_addr);
    if (ret != ARTVG_SUCCESS) {
        artvg_log_err("configure_output: failed to get output physical address, ret=%d\n", ret);
        return ret;
    }

    uint32_t output_format = map_pixel_format_to_hw(output->format);
    uint32_t output_color_space = convert_color_space_mode(output->flags);
    artvg_log_info("configure_output: format=%d, flags=%u, color_space=%u, pos=(%d,%d), trans_out_blk_mode=%d\n",
                   output->format, output->flags, output_color_space, rect->x, rect->y, trans_out_blk_mode);

    uint32_t output_ctrl = 0;
    output_ctrl |= OUTPUT_FORMAT(output_format);
    output_ctrl |= is_yuv_format(output->format) ? OUTPUT_CSC2_EN : 0;
    output_ctrl |= CSC2_COLOR_SPACE(output_color_space);
    output_ctrl |= (trans_out_blk_mode == ARTVG_PROJECTIVE_OUT_BLK_MODE_8X8) ? TRANS_OUT_BLK_MODE : 0;

    /* Dither buffer addr is programmed by kernel in artvg_hw_start;
     * only the enable bit goes into the cmd stream. */
    if (dither_mode != ARTVG_DITHER_OFF && (output->format >= MPP_FMT_ARGB_1555 && output->format <= MPP_FMT_BGRA_4444)) {
        if (dither_mode == ARTVG_DITHER_RANDOM) {
            output_ctrl |= RAND_DITHER_EN;
        }
        output_ctrl |= DITHER_EN;
    }

    uint32_t output_cmds[] = {
        output_ctrl,                                             // 0x100
        OUTPUT_SIZE_SET(rect->width, rect->height),             // 0x104
        OUTPUT_STRIDE_SET(output->stride[0], output->stride[1]), // 0x108
        OUTPUT_OFFSET_SET(rect->x, rect->y),                     // 0x10c
        output_phy_addr[0],                                      // 0x110
        output_phy_addr[1],                                      // 0x114
        output_phy_addr[2],                                      // 0x118
        0,                                                       // 0x11c
    };
    command_queue_add_command(slot, OUTPUT_CTRL, ARRAY_SIZE(output_cmds), output_cmds, false);

    return ARTVG_SUCCESS;
}

// ensure the function setting at the end
artvg_error_t configure_blend_mode_finish(struct cmd_slot *slot, artvg_ctrl_t *ctrl)
{
    uint32_t blend_ctrl = 0;

    if (!ctrl || !ctrl->alpha_en) {
        command_queue_add_command(slot, BLENDING_CTRL, 1, &blend_ctrl, true);
        return ARTVG_SUCCESS;
    }

    uint8_t src_factor_mode = 0;
    uint8_t dst_factor_mode = 0;
    get_blending_factors(ctrl->alpha_rules, &src_factor_mode, &dst_factor_mode);

    blend_ctrl |= ctrl->output_unpremul_en ? RM_PRE_MUL : 0;
    blend_ctrl |= DST_FACTOR_MODE(dst_factor_mode);
    blend_ctrl |= SRC_FACTOR_MODE(src_factor_mode);
    blend_ctrl |= BLEND_MODE(get_blend_mode_value(ctrl->alpha_rules));
    blend_ctrl |= ctrl->color_key_en ? CK_EN : 0;
    blend_ctrl |= ctrl->alpha_en ? ALPHA_BLEND_EN : 0;

    uint32_t blend_cmd[] = {
        blend_ctrl,
        ctrl->color_key_value,
    };

    command_queue_add_command(slot, BLENDING_CTRL, ARRAY_SIZE(blend_cmd), blend_cmd, true);

    return ARTVG_SUCCESS;
}

artvg_error_t configure_fill_color(struct cmd_slot *slot, artvg_gradient_t *gradient)
{
    bool is_linear_gradient = gradient->count > 1;
    uint32_t color_lut[256] = {0};
    int32_t need_lut_size = 256;
    void *color_lut_ptr = NULL;
    float x1, y1, x2, y2;
    int32_t offset = 0;
    int32_t h_step = 0;
    int32_t v_step = 0;

    color_lut_ptr = color_lut;

    if (is_linear_gradient) {
        x1 = gradient->start.x;
        y1 = gradient->start.y;
        x2 = gradient->end.x;
        y2 = gradient->end.y;

        if (!gradient->user_color_lut ||
            (gradient->user_color_lut_size != 16 &&
             gradient->user_color_lut_size != 64 &&
             gradient->user_color_lut_size != 256)) {
            if (gradient->user_color_lut) {
                artvg_log_warn("Invalid user_color_lut_size %u, expected 16/64/256. "
                               "Gradient struct may not be initialized. Using internal LUT.\n",
                               gradient->user_color_lut_size);
            }
            switch (gradient->precision) {
            case ARTVG_GRADIENT_PRECISION_16:
                need_lut_size = 16;
                break;
            case ARTVG_GRADIENT_PRECISION_64:
                need_lut_size = 64;
                break;
            case ARTVG_GRADIENT_PRECISION_256:
                need_lut_size = 256;
                break;
            default:
                artvg_log_warn("Unknown gradient precision %d, using default 256\n",
                            gradient->precision);
                need_lut_size = 256;
                break;
            }

            generate_gradient_color_lut(gradient, color_lut_ptr, need_lut_size);
        } else {
            color_lut_ptr = gradient->user_color_lut;
            need_lut_size = gradient->user_color_lut_size;
        }

        calculate_gradient_offset_and_step(x1, y1, x2, y2, &offset, &h_step, &v_step);

        uint32_t grandient_cmds[] = {
            (uint32_t)offset, // 0x034
            (uint32_t)h_step, // 0x038
            (uint32_t)v_step, // 0x03C
        };
        command_queue_add_command(slot, LINEAR_GRAD_OFFSET, ARRAY_SIZE(grandient_cmds), grandient_cmds, false);
        command_queue_add_command(slot, COLOR_LUT(0), need_lut_size, (uint32_t *)color_lut_ptr, false);
    } else {
        uint32_t color = gradient->colors[0];
        command_queue_add_command(slot, SRC_FILL_COLOR, 1, &color, false);
    }

    return ARTVG_SUCCESS;
}

artvg_error_t configure_palette(artvg_t *vg, struct cmd_slot *slot, struct mpp_buf *buf)
{
    void *palette_addr = NULL;
    struct dma_buf_info dma_info = {0};

    if (buf->buf_type == MPP_DMA_BUF_FD) {
        dma_info.fd = buf->fd[0];
        if (artvg_os_dma_buf_map(vg->os, &dma_info, 1024, &palette_addr) != 0) {
            artvg_log_err("configure_palette: map dma buffer failed\n");
            return ARTVG_GENERIC_IO;
        }
        command_queue_add_command(slot, PALETTE_LUT(0), 256, (uint32_t *)palette_addr, false);
        artvg_os_dma_buf_unmap(vg->os, 1024, palette_addr);
    } else if (buf->buf_type == MPP_PHY_ADDR) {
        palette_addr = (void *)(uintptr_t)buf->phy_addr[0];
        command_queue_add_command(slot, PALETTE_LUT(0), 256, (uint32_t *)palette_addr, false);
    }
    return ARTVG_SUCCESS;
}

/* ====================  alloc functions ==================== */
artvg_error_t artvg_alloc_buffer_and_map(artvg_t *vg, uint32_t size, struct dma_buf_info *buf_info, void **addr)
{
    if (vg == NULL || size == 0 || buf_info == NULL || addr == NULL) {
        return ARTVG_INVALID_PARAM;
    }

    size = ALIGN_CACHE_LINE(size);

    if (artvg_os_dma_buf_alloc(vg->os, size, buf_info) != 0) {
        artvg_log_err("alloc dma buffer failed\n");
        return ARTVG_GENERIC_IO;
    }

    if (artvg_os_dma_buf_map(vg->os, buf_info, size, addr) != 0) {
        artvg_log_err("map dma buffer failed\n");
        artvg_os_dma_buf_release(vg->os, buf_info);
        return ARTVG_GENERIC_IO;
    }

    /* Get physical address from OS layer */
    if (artvg_os_dma_buf_get_phy_addr(vg->os, buf_info->fd, &buf_info->phy_addr) != 0) {
        artvg_log_err("get phy addr failed\n");
        artvg_os_dma_buf_unmap(vg->os, size, *addr);
        artvg_os_dma_buf_release(vg->os, buf_info);
        return ARTVG_GENERIC_IO;
    }

    artvg_log_debug("alloc size = %d, buf-info-fd=%d,%x, addr=%p\n", size, buf_info->fd, buf_info->phy_addr, *addr);

    return ARTVG_SUCCESS;
}

artvg_error_t artvg_free_buffer_and_unmap(artvg_t *vg, struct dma_buf_info *buf_info, uint32_t size, void **addr)
{
    if (vg == NULL || buf_info == NULL || addr == NULL) {
        artvg_log_err("free_buffer_and_unmap: invalid parameters\n");
        return ARTVG_INVALID_PARAM;
    }

    if (artvg_os_dma_buf_unmap(vg->os, size, *addr) != 0) {
        artvg_log_err("unmap dma buffer failed\n");
    }

    if (artvg_os_dma_buf_release(vg->os, buf_info) != 0) {
        artvg_log_err("release dma buffer failed\n");
    }

    artvg_log_debug("free size = %d, buf-info-fd=%d,%x, addr=%p\n", size, buf_info->fd, buf_info->phy_addr, *addr);

    *addr = NULL;

    return ARTVG_SUCCESS;
}

artvg_error_t artvg_gradient_fill_color_lut(artvg_gradient_t *gradient, void *color_lut, uint32_t size)
{
    if (!gradient || !color_lut || size == 0)
        return ARTVG_INVALID_PARAM;

    uint32_t need_lut_size = 256;

    switch (gradient->precision) {
      case ARTVG_GRADIENT_PRECISION_NONE:
          artvg_log_err("Cannot fill color LUT when precision is NONE (solid color mode)\n");
          return ARTVG_INVALID_PARAM;
      case ARTVG_GRADIENT_PRECISION_16:
          need_lut_size = 16;
          break;
      case ARTVG_GRADIENT_PRECISION_64:
          need_lut_size = 64;
          break;
      case ARTVG_GRADIENT_PRECISION_256:
          need_lut_size = 256;
          break;
      default:
        artvg_log_warn("Unknown gradient precision %d, using default 256\n",
                              gradient->precision);
        need_lut_size = 256;
        break;
    }

    if (size < need_lut_size) {
        artvg_log_err("filling the color LUT requires a larger buffer, needing %u, actual %u\n", need_lut_size, size);
        return ARTVG_INVALID_PARAM;
    }

    generate_gradient_color_lut(gradient, color_lut, size);
    gradient->user_color_lut = color_lut;
    gradient->user_color_lut_size = size;
    return ARTVG_SUCCESS;
}

static uint8_t map_pixel_format_to_hw(enum mpp_pixel_format format)
{
    // MPP description matches actual hardware description
    return format;
}

static uint8_t convert_color_space_mode(uint8_t flag)
{
    uint8_t value = MPP_BUF_COLOR_SPACE_GET(flag);
    switch (value) {
    case MPP_COLOR_SPACE_BT601:
        return 0x00;
    case MPP_COLOR_SPACE_BT709:
        return 0x01;
    case MPP_COLOR_SPACE_BT601_FULL_RANGE:
        return 0x02;
    case MPP_COLOR_SPACE_BT709_FULL_RANGE:
        return 0x03;
    default:
        artvg_log_warn("Invalid color space flag: %u, using BT601 (0) as default\n", flag);
        return 0;
    }
}

static bool is_yuv_format(enum mpp_pixel_format format)
{
    return format >= MPP_FMT_YUV420P;
}

static bool has_alpha_channel(enum mpp_pixel_format format)
{
    switch (format) {
    case MPP_FMT_ARGB_8888:
    case MPP_FMT_ABGR_8888:
    case MPP_FMT_RGBA_8888:
    case MPP_FMT_BGRA_8888:
    case MPP_FMT_ARGB_1555:
    case MPP_FMT_ABGR_1555:
    case MPP_FMT_RGBA_5551:
    case MPP_FMT_BGRA_5551:
    case MPP_FMT_ARGB_4444:
    case MPP_FMT_ABGR_4444:
    case MPP_FMT_RGBA_4444:
    case MPP_FMT_BGRA_4444:
        return true;
    default:
        return false;
    }
}

static uint8_t get_blend_mode_value(artvg_alpha_blend_t alpha_rules)
{
    switch (alpha_rules) {
    case ARTVG_BLEND_ARITH_ADD:
        return 1;
    case ARTVG_BLEND_ARITH_SUBTRACT:
        return 2;
    case ARTVG_BLEND_ARITH_MULTIPLY:
        return 3;
    default:
        return 0;
    }
}

static void get_blending_factors(artvg_alpha_blend_t blend_mode, uint8_t *src_factor_mode, uint8_t *dst_factor_mode)
{
    if (!src_factor_mode || !dst_factor_mode)
        return;

    switch (blend_mode) {
    case ARTVG_BLEND_DEFAULT:
        *src_factor_mode = 2;
        *dst_factor_mode = 3;
        break;
    case ARTVG_BLEND_CLEAR:
        *src_factor_mode = 0;
        *dst_factor_mode = 0;
        break;
    case ARTVG_BLEND_SRC:
        *src_factor_mode = 1;
        *dst_factor_mode = 0;
        break;
    case ARTVG_BLEND_SRC_OVER:
        *src_factor_mode = 1;
        *dst_factor_mode = 3;
        break;
    case ARTVG_BLEND_DST_OVER:
        *src_factor_mode = 5;
        *dst_factor_mode = 1;
        break;
    case ARTVG_BLEND_SRC_IN:
        *src_factor_mode = 4;
        *dst_factor_mode = 0;
        break;
    case ARTVG_BLEND_DST_IN:
        *src_factor_mode = 0;
        *dst_factor_mode = 2;
        break;
    case ARTVG_BLEND_SRC_OUT:
        *src_factor_mode = 5;
        *dst_factor_mode = 0;
        break;
    case ARTVG_BLEND_DST_OUT:
        *src_factor_mode = 0;
        *dst_factor_mode = 3;
        break;
    case ARTVG_BLEND_SRC_ATOP:
        *src_factor_mode = 4;
        *dst_factor_mode = 3;
        break;
    case ARTVG_BLEND_DST_ATOP:
        *src_factor_mode = 5;
        *dst_factor_mode = 2;
        break;
    case ARTVG_BLEND_ADD:
        *src_factor_mode = 1;
        *dst_factor_mode = 1;
        break;
    case ARTVG_BLEND_XOR:
        *src_factor_mode = 5;
        *dst_factor_mode = 3;
        break;
    case ARTVG_BLEND_DST:
        *src_factor_mode = 0;
        *dst_factor_mode = 1;
        break;
    default:
        *src_factor_mode = 2;
        *dst_factor_mode = 3;
        break;
    }
}

/* ====================  Helper functions implementation ==================== */
static void calculate_stride_value(enum mpp_pixel_format fmt, uint32_t input_width, uint32_t stride[])
{
    // Prevent overflow in multiplication (max bytes per pixel is 4)
    if (input_width > (UINT32_MAX / 4)) {
        artvg_log_err("input_width too large: %u\n", input_width);
        stride[0] = stride[1] = stride[2] = 0;
        return;
    }

    switch (fmt) {
    case MPP_FMT_ARGB_8888:
    case MPP_FMT_ABGR_8888:
    case MPP_FMT_RGBA_8888:
    case MPP_FMT_BGRA_8888:
    case MPP_FMT_XRGB_8888:
    case MPP_FMT_XBGR_8888:
    case MPP_FMT_RGBX_8888:
    case MPP_FMT_BGRX_8888:
        stride[0] = ALIGN_8B((input_width * 4));
        stride[1] = 0;
        stride[2] = 0;
        break;
    case MPP_FMT_ARGB_4444:
    case MPP_FMT_ABGR_4444:
    case MPP_FMT_RGBA_4444:
    case MPP_FMT_BGRA_4444:
    case MPP_FMT_RGB_565:
    case MPP_FMT_BGR_565:
    case MPP_FMT_ARGB_1555:
    case MPP_FMT_ABGR_1555:
    case MPP_FMT_RGBA_5551:
    case MPP_FMT_BGRA_5551:
        stride[0] = ALIGN_8B((input_width * 2));
        stride[1] = 0;
        stride[2] = 0;
        break;
    case MPP_FMT_RGB_888:
    case MPP_FMT_BGR_888:
        stride[0] = ALIGN_8B((input_width * 3));
        stride[1] = 0;
        stride[2] = 0;
        break;
    case MPP_FMT_A8:
    case MPP_FMT_PALETTE8:
        stride[0] = ALIGN_8B(input_width);
        stride[1] = 0;
        stride[2] = 0;
        break;
    case MPP_FMT_YUV420P:
        stride[0] = ALIGN_8B((input_width));
        stride[1] = ALIGN_8B((input_width / 2));
        stride[2] = ALIGN_8B((input_width / 2));
        break;
    case MPP_FMT_NV21:
    case MPP_FMT_NV12:
        stride[0] = ALIGN_8B((input_width));
        stride[1] = ALIGN_8B((input_width));
        stride[2] = 0;
        break;
    case MPP_FMT_YUV422P:
        stride[0] = ALIGN_8B((input_width));
        stride[1] = ALIGN_8B((input_width / 2));
        stride[2] = ALIGN_8B((input_width / 2));
        break;
    case MPP_FMT_NV16:
    case MPP_FMT_NV61:
        stride[0] = ALIGN_8B((input_width));
        stride[1] = ALIGN_8B((input_width));
        stride[2] = 0;
        break;
    case MPP_FMT_YUYV:
    case MPP_FMT_YVYU:
    case MPP_FMT_UYVY:
    case MPP_FMT_VYUY:
        stride[0] = ALIGN_8B((input_width * 2));
        stride[1] = 0;
        stride[2] = 0;
        break;
    case MPP_FMT_YUV400:
        stride[0] = ALIGN_8B(input_width);
        stride[1] = 0;
        stride[2] = 0;
        break;
    case MPP_FMT_YUV444P:
        stride[0] = ALIGN_8B(input_width);
        stride[1] = ALIGN_8B(input_width);
        stride[2] = ALIGN_8B(input_width);
        break;
    default:
        artvg_log_err("input format error\n");
        break;
    }
}

static void calculate_height_value(enum mpp_pixel_format fmt, uint32_t input_height, uint32_t height[])
{
    if ((fmt >= MPP_FMT_ARGB_8888) && (fmt <= MPP_FMT_BGRA_4444)) {
        height[0] = input_height;
        height[1] = 0;
        height[2] = 0;
        return;
    }

    switch (fmt) {
    case MPP_FMT_A8:
    case MPP_FMT_PALETTE8:
        height[0] = ALIGN_2B(input_height);
        height[1] = 0;
        height[2] = 0;
        break;
    case MPP_FMT_YUV420P:
        height[0] = ALIGN_2B(input_height);
        height[1] = ALIGN_2B(input_height / 2);
        height[2] = ALIGN_2B(input_height / 2);
        break;
    case MPP_FMT_NV21:
    case MPP_FMT_NV12:
        height[0] = ALIGN_2B(input_height);
        height[1] = ALIGN_2B(input_height / 2);
        height[2] = 0;
        break;
    case MPP_FMT_YUV422P:
        height[0] = ALIGN_2B(input_height);
        height[1] = ALIGN_2B(input_height);
        height[2] = ALIGN_2B(input_height);
        break;
    case MPP_FMT_NV16:
    case MPP_FMT_NV61:
        height[0] = ALIGN_2B(input_height);
        height[1] = ALIGN_2B(input_height);
        height[2] = 0;
        break;
    case MPP_FMT_YUYV:
    case MPP_FMT_YVYU:
    case MPP_FMT_UYVY:
    case MPP_FMT_VYUY:
        height[0] = ALIGN_2B((input_height));
        height[1] = 0;
        height[2] = 0;
        break;
    case MPP_FMT_YUV400:
        height[0] = ALIGN_2B(input_height);
        height[1] = 0;
        height[2] = 0;
        break;
    case MPP_FMT_YUV444P:
        height[0] = ALIGN_2B(input_height);
        height[1] = ALIGN_2B(input_height);
        height[2] = ALIGN_2B(input_height);
        break;
    default:
        artvg_log_err("input format error\n");
        break;
    }
}

static artvg_error_t get_phy_addr(artvg_t *vg, struct mpp_buf *buffer, uint32_t phy_addr[])
{
    struct dma_fd_entry *entry = NULL;
    bool found = false;

    if (!vg || !buffer || !phy_addr) {
        return ARTVG_INVALID_PARAM;
    }

    if (buffer->buf_type == MPP_DMA_BUF_FD) {
        artvg_os_mutex_lock(vg->os);
        mpp_list_for_each_entry(entry, &vg->fd_map_list, list) {
            for (int i = 0; i < 3; i++) {
                if (entry->fd == buffer->fd[i] && buffer->fd[i] >= 0) {
                    phy_addr[i] = entry->phy_addr;
                    found = true;
                }
            }
        }

        if (!found) {
            artvg_os_mutex_unlock(vg->os);
            artvg_log_err("Failed to find DMA buffer physical address\n");
            return ARTVG_INVALID_PARAM;
        }

        artvg_os_mutex_unlock(vg->os);
        return ARTVG_SUCCESS;
    } else if (buffer->buf_type == MPP_PHY_ADDR) {
        phy_addr[0] = buffer->phy_addr[0];
        phy_addr[1] = buffer->phy_addr[1];
        phy_addr[2] = buffer->phy_addr[2];
        return ARTVG_SUCCESS;
    }

    return ARTVG_INVALID_PARAM;
}

static artvg_error_t dma_fd_map(artvg_t *vg, int fd)
{
    struct dma_fd_entry *entry = NULL;
    uint32_t phy_addr = 0;

    if (!vg || fd < 0) {
        return ARTVG_INVALID_PARAM;
    }

    if (artvg_os_dma_buf_get_phy_addr(vg->os, fd, &phy_addr) != 0) {
        artvg_log_err("dma_fd_map: get phy addr failed for fd %d\n", fd);
        return ARTVG_GENERIC_IO;
    }

    entry = artvg_os_malloc(sizeof(struct dma_fd_entry));
    if (!entry) {
        return ARTVG_OUT_OF_MEMORY;
    }

    entry->fd = fd;
    entry->phy_addr = phy_addr;

    artvg_os_mutex_lock(vg->os);
    mpp_list_add_tail(&entry->list, &vg->fd_map_list);
    artvg_os_mutex_unlock(vg->os);

    artvg_log_debug("dma_fd_map: mapped fd %d to phy_addr 0x%x\n", fd, phy_addr);
    return ARTVG_SUCCESS;
}

static artvg_error_t dma_fd_unmap(artvg_t *vg, int fd)
{
    struct dma_fd_entry *entry = NULL;
    struct dma_fd_entry *node = NULL;

    if (!vg || fd < 0) {
        return ARTVG_INVALID_PARAM;
    }

    artvg_log_debug("dma_fd_unmap: unmapping fd %d\n", fd);

    artvg_os_mutex_lock(vg->os);
    mpp_list_for_each_entry_safe(entry, node, &vg->fd_map_list, list) {
        if (entry->fd == fd) {
            mpp_list_del(&entry->list);
            artvg_os_free(entry);
            break;
        }
    }

    artvg_os_mutex_unlock(vg->os);
    return ARTVG_SUCCESS;
}

static void clean_up_fd_map_list(struct mpp_list *list)
{
    struct dma_fd_entry *entry = NULL;
    struct dma_fd_entry *node = NULL;

    if (!list) {
        return;
    }

    mpp_list_for_each_entry_safe(entry, node, list, list) {
        mpp_list_del(&entry->list);
        artvg_os_free(entry);
    }
}

static void update_bounding_box(struct mpp_rect *box, artvg_point_t *point)
{
    // Expand bounding box to include the new point
    int32_t right = box->x + box->width;
    int32_t bottom = box->y + box->height;

    if (point->x < box->x) {
        box->width += (box->x - point->x);
        box->x = point->x;
    }
    if (point->y < box->y) {
        box->height += (box->y - point->y);
        box->y = point->y;
    }
    if (point->x > right) {
        box->width = point->x - box->x;
    }
    if (point->y > bottom) {
        box->height = point->y - box->y;
    }
}

artvg_error_t artvg_resolution_detect(artvg_t *vg, uint32_t width, uint32_t height, int vector_draw)
{
    if (!vg) {
        return ARTVG_INVALID_PARAM;
    }
    uint32_t max_width = 8192;
    uint32_t max_height = 8192;

    if (vector_draw)  {
        max_width = 2048;
        max_height = 2048;
    }

    if (width > max_width || height > max_height) {
        artvg_log_err("Exceeds maximum supported resolution: %ux%u\n", max_width, max_height);
        return ARTVG_NOT_SUPPORTED;
    }

    return ARTVG_SUCCESS;
}

artvg_error_t artvg_validate_dither_scan_order(artvg_ctrl_t *ctrl)
{
    if (!ctrl)
        return ARTVG_INVALID_PARAM;

    if (ctrl->output_dither != ARTVG_DITHER_OFF && ctrl->src_scan_order != ARTVG_SCAN_TB_LR) {
        artvg_log_err("dither mode %d not supported when src_scan_order is not ARTVG_SCAN_TB_LR\n",
                      ctrl->output_dither);
        return ARTVG_NOT_SUPPORTED;
    }
    return ARTVG_SUCCESS;
}

static int calculate_gradient_offset_and_step(float x1, float y1, float x2, float y2,
                                        int32_t *offset, int32_t *h_step, int32_t *v_step)
{
    float dx = x2 - x1;
    float dy = y2 - y1;
    float len = dx * dx + dy * dy;
    float offset_val;

    if (len < 1e-12) {
        dx = 1.0f;
        dy = 0.0f;
        len = 1.0f;
        artvg_log_warn("Gradient line is too small, using a default gradient line.\n");
    }

    dx = dx / len;
    dy = dy / len;
    offset_val = -dx * x1 - dy * y1;
    *offset = (int)(offset_val * 65536);
    *h_step = (int)(dx * 65536);
    *v_step = (int)(dy * 65536);
    return 0;
}

static void generate_gradient_color_lut(artvg_gradient_t *grad, uint32_t *lut, uint32_t lut_size)
{
    if (grad->count < 2) {
        artvg_log_err("error stop number:%u", grad->count);
        return;
    }

    int scaled_stops[VLC_MAX_GRADIENT_STOPS];
    for (uint32_t i = 0; i < grad->count; i++) {
        scaled_stops[i] = (grad->stops[i] * (lut_size - 1) + 127) / 255;
    }

    int start_idx = scaled_stops[0];
    for (int i = 0; i < start_idx; i++) lut[i] = grad->colors[0];

    for (uint32_t seg = 0; seg < grad->count - 1; seg++) {
        int seg_start = scaled_stops[seg];
        int seg_end   = scaled_stops[seg + 1];
        if (seg_start >= seg_end) continue;

        uint32_t color1 = grad->colors[seg];
        uint32_t color2 = grad->colors[seg + 1];
        uint8_t a1 = (color1 >> 24) & 0xFF, r1 = (color1 >> 16) & 0xFF, g1 = (color1 >> 8) & 0xFF, b1 = color1 & 0xFF;
        uint8_t a2 = (color2 >> 24) & 0xFF, r2 = (color2 >> 16) & 0xFF, g2 = (color2 >> 8) & 0xFF, b2 = color2 & 0xFF;

        int step = seg_end - seg_start;
        for (int idx = seg_start; idx < seg_end; idx++) {
            int ratio_num = idx - seg_start;
            uint8_t a = (uint8_t)((a1 * (step - ratio_num) + a2 * ratio_num + step / 2) / step);
            uint8_t r = (uint8_t)((r1 * (step - ratio_num) + r2 * ratio_num + step / 2) / step);
            uint8_t g = (uint8_t)((g1 * (step - ratio_num) + g2 * ratio_num + step / 2) / step);
            uint8_t b = (uint8_t)((b1 * (step - ratio_num) + b2 * ratio_num + step / 2) / step);
            lut[idx] = (a << 24) | (r << 16) | (g << 8) | b;
        }
    }

    int end_idx = scaled_stops[grad->count - 1];
    for (int i = end_idx; i < lut_size; i++)
        lut[i] = grad->colors[grad->count - 1];
}
