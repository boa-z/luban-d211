/*
 * Copyright (c) 2026, ArtInChip Technology Co., Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Authors:  zequan liang <zequan.liang@artinchip.com>
 */

#include "artvg_context.h"

static int artvg_get_mode_bit(artvg_projective_lut_mode_t lut_mode);
static int fill_lut_block(artvg_t *vg, int blk_x, int blk_y, int blk_size, void *lut, artvg_matrix_t *matrix, artvg_projective_lut_mode_t lut_mode);
static int invert_matrix_3x3(artvg_matrix_t *src, artvg_matrix_t *dst);
static int preprocess_matrix(artvg_matrix_t *original, artvg_matrix_t *output, uint8_t trans_type);

static artvg_error_t validate_src_output_resolution(artvg_t *vg, struct mpp_buf *src, struct mpp_buf *output)
{
    if (artvg_resolution_detect(vg, src->size.width, src->size.height, 0) != ARTVG_SUCCESS ||
        artvg_resolution_detect(vg, output->size.width, output->size.height, 0) != ARTVG_SUCCESS) {
        artvg_log_err("resolution not supported, width: %d, height: %d\n",
                      src->size.width, src->size.height);
        return ARTVG_NOT_SUPPORTED;
    }
    return ARTVG_SUCCESS;
}

/* Check if LUT mode uses packed format (X and Y packed into one 32-bit word) */
static int lut_mode_is_packed(artvg_projective_lut_mode_t lut_mode)
{
    /* Modes 0-4 (S10_6 to S14_2) use packed format: [Y(16bit) | X(16bit)] */
    /* Modes 5-7 (S16_8 to S20_4) use separate format: X and Y in separate 32-bit words */
    return (lut_mode >= ARTVG_PROJECTIVE_LUT_MOD_S10_6 &&
            lut_mode <= ARTVG_PROJECTIVE_LUT_MOD_S14_2);
}

artvg_error_t artvg_blit(artvg_t *vg,
                              struct mpp_buf *src,
                              struct mpp_buf *dst,
                              struct mpp_buf *mask,
                              artvg_ctrl_t *ctrl,
                              artvg_blit_ctl_t *blit_ctl)
{
    return artvg_blit2(vg, src, dst, dst, mask, ctrl, blit_ctl);
}

artvg_error_t artvg_affine(artvg_t *vg,
                                struct mpp_buf *src,
                                struct mpp_buf *dst,
                                struct mpp_buf *mask,
                                artvg_matrix_t *matrix,
                                artvg_ctrl_t *ctrl,
                                artvg_transform_edge_t *transform_edge)
{
    return artvg_affine2(vg, src, dst, dst, mask, matrix, ctrl, transform_edge);
}

artvg_error_t artvg_blit2(artvg_t *vg,
                               struct mpp_buf *src,
                               struct mpp_buf *dst,
                               struct mpp_buf *output,
                               struct mpp_buf *mask,
                               artvg_ctrl_t *ctrl,
                               artvg_blit_ctl_t *blit_ctl)
{
    artvg_blit_ctl_t base_ctl_default = {0};
    struct mpp_rect src_rect = {0};
    struct mpp_rect target_rect = {0};
    struct mpp_rect output_rect = {0};
    artvg_ctrl_t ctl_default = {0};
    artvg_error_t error = ARTVG_SUCCESS;

    if (!vg || !src || !dst || !output) {
        return ARTVG_INVALID_PARAM;
    }

    blit_ctl = blit_ctl == NULL ? &base_ctl_default : blit_ctl;
    ctrl = ctrl == NULL ? &ctl_default : ctrl;

    ARTVG_ERROR_HANDLER(validate_src_output_resolution(vg, src, output));
    ARTVG_ERROR_HANDLER(artvg_validate_dither_scan_order(ctrl));

    struct cmd_slot *slot = acquire_slot_for_thread(vg);
    if (!slot) {
        return ARTVG_OUT_OF_RESOURCES;
    }

    if (reserve_command_buffer_space(vg, slot) != ARTVG_SUCCESS) {
        return ARTVG_OUT_OF_RESOURCES;
    }

    uint32_t cmd_count_saved = slot->cmd_count;

    get_crop_rect(src, &src_rect);
    get_crop_rect(dst, &target_rect);
    get_crop_rect(output, &output_rect);

    int swap_size = (blit_ctl->rotate == ARTVG_ROTATE_90 || blit_ctl->rotate == ARTVG_ROTATE_270);
    if (swap_size ? (src_rect.width != output_rect.height || src_rect.height != output_rect.width)
                  : (src_rect.width != output_rect.width || src_rect.height != output_rect.height)) {
        artvg_log_err("src surface size (w=%d,h=%d) does not match output surface size (w=%d,h=%d) with rotation %d",
                     src_rect.width, src_rect.height, output_rect.width, output_rect.height, blit_ctl->rotate);
        return ARTVG_INVALID_PARAM;
    }

    uint32_t src_func_sel = 0;
    src_func_sel |= FUNC_SELECT(0);
    src_func_sel |= ctrl->src_recolor_en ? RECOLOR_EN : 0;
    src_func_sel |= ROTATION_CTRL(blit_ctl->rotate);
    if (blit_ctl->mirror) {
        switch (blit_ctl->mirror) {
        case ARTVG_MIRROR_HORIZONTAL:
            src_func_sel |= H_FLIP;
            break;
        case ARTVG_MIRROR_VERTICAL:
            src_func_sel |= V_FLIP;
            break;
        case ARTVG_MIRROR_BOTH:
            src_func_sel |= H_FLIP | V_FLIP;
            break;
        default:
            break;
        }
    }

    if (mask != NULL && mask->format == MPP_FMT_A8) {
        src_func_sel |= MASK_EN;
    }
    ARTVG_ERR_ROOLBACK(configure_src_surface(vg, slot, src, mask, ctrl, src_func_sel, true, &src_rect, output->format), slot, cmd_count_saved);

    ARTVG_ERR_ROOLBACK(configure_dst_surface(vg, slot, dst, ctrl, &target_rect, output->format), slot, cmd_count_saved);

    ARTVG_ERR_ROOLBACK(configure_output(vg, slot, output, ctrl->output_dither, &output_rect, 0), slot, cmd_count_saved);

    ARTVG_ERR_ROOLBACK(configure_blend_mode_finish(slot, ctrl), slot, cmd_count_saved);

    return ARTVG_SUCCESS;

ErrorHandler:
    return error;
}

artvg_error_t artvg_affine2(artvg_t *vg,
                                 struct mpp_buf *src,
                                 struct mpp_buf *dst,
                                 struct mpp_buf *output,
                                 struct mpp_buf *mask,
                                 artvg_matrix_t *matrix,
                                 artvg_ctrl_t *ctrl,
                                 artvg_transform_edge_t *transform_edge)
{
    if (!vg || !src || !dst || !output) {
        return ARTVG_INVALID_PARAM;
    }

    artvg_transform_edge_t affine_ctl_default = {0};
    artvg_matrix_t affine_matrix = {0};
    artvg_ctrl_t ctl_default = {0};
    artvg_error_t error = ARTVG_SUCCESS;

    artvg_matrix_identity(&affine_matrix);
    ctrl = ctrl == NULL ? &ctl_default : ctrl;
    matrix = matrix == NULL ? &affine_matrix : matrix;
    transform_edge = transform_edge == NULL ? &affine_ctl_default : transform_edge;

    ARTVG_ERROR_HANDLER(validate_src_output_resolution(vg, src, output));
    ARTVG_ERROR_HANDLER(artvg_validate_dither_scan_order(ctrl));

    if (matrix && (matrix->m[2][0] != 0 || matrix->m[2][1] != 0)) {
        artvg_log_err("affine is not supported for matrix with non-zero elements in last row except translation");
        return ARTVG_INVALID_PARAM;
    }

    struct cmd_slot *slot = acquire_slot_for_thread(vg);
    if (!slot) {
        return ARTVG_OUT_OF_RESOURCES;
    }

    if (reserve_command_buffer_space(vg, slot) != ARTVG_SUCCESS) {
        return ARTVG_OUT_OF_RESOURCES;
    }

    uint32_t cmd_count_saved = slot->cmd_count;
    struct mpp_rect src_rect = {0};
    struct mpp_rect dst_rect = {0};
    struct mpp_rect output_rect = {0};

    get_crop_rect(src, &src_rect);
    get_crop_rect(dst, &dst_rect);
    get_crop_rect(output, &output_rect);

    ARTVG_ERR_ROOLBACK(configure_affine_transform(slot, matrix, ARTVG_AFFINE_TRANS_IMAGE), slot, cmd_count_saved);

    ARTVG_ERR_ROOLBACK(configure_fill_edge_color(slot, transform_edge), slot, cmd_count_saved);

    uint32_t src_func_sel = 0;
    src_func_sel |= FUNC_SELECT(1);
    src_func_sel |= transform_edge->fill_en ? TRANS_FILL_BG_EN : 0;
    src_func_sel |= transform_edge->blend_en ? TRANS_BLEND_EDGE_EN : 0;
    src_func_sel |= ctrl->src_recolor_en ? RECOLOR_EN : 0;
    src_func_sel |= TRANS_PAD_MODE_EN(transform_edge->pad_mode);
    if (mask != NULL && mask->format == MPP_FMT_A8) {
        src_func_sel |= MASK_EN;
    }

    ARTVG_ERR_ROOLBACK(configure_src_surface(vg, slot, src, mask, ctrl, src_func_sel, true, &src_rect, output->format), slot, cmd_count_saved);

    ARTVG_ERR_ROOLBACK(configure_dst_surface(vg, slot, dst, ctrl, &dst_rect, output->format), slot, cmd_count_saved);

    ARTVG_ERR_ROOLBACK(configure_output(vg, slot, output, ctrl->output_dither, &output_rect, 0), slot, cmd_count_saved);

    ARTVG_ERR_ROOLBACK(configure_blend_mode_finish(slot, ctrl), slot, cmd_count_saved);

    return ARTVG_SUCCESS;

ErrorHandler:
    return error;
}

artvg_error_t artvg_projective(artvg_t *vg,
                               struct mpp_buf *src,
                               struct mpp_buf *dst,
                               artvg_ctrl_t *ctrl,
                               artvg_projective_t *projective)
{
    return artvg_projective2(vg, src, dst, dst, ctrl, projective);
}

artvg_error_t artvg_projective2(artvg_t *vg,
                                struct mpp_buf *src,
                                struct mpp_buf *dst,
                                struct mpp_buf *output,
                                artvg_ctrl_t *ctrl,
                                artvg_projective_t *projective)
{
    if (!vg || !src || !dst || !output || !projective) {
        return ARTVG_INVALID_PARAM;
    }

    artvg_projective_t projective_default = {0};
    artvg_ctrl_t ctl_default = {0};
    artvg_error_t error = ARTVG_SUCCESS;

    projective = projective == NULL ? &projective_default : projective;
    ctrl = ctrl == NULL ? &ctl_default : ctrl;

    ARTVG_ERROR_HANDLER(validate_src_output_resolution(vg, src, output));
    ARTVG_ERROR_HANDLER(artvg_validate_dither_scan_order(ctrl));

    struct cmd_slot *slot = acquire_slot_for_thread(vg);
    if (!slot) {
        return ARTVG_OUT_OF_RESOURCES;
    }

    if (reserve_command_buffer_space(vg, slot) != ARTVG_SUCCESS) {
        return ARTVG_OUT_OF_RESOURCES;
    }

    uint32_t cmd_count_saved = slot->cmd_count;
    struct mpp_rect src_rect = {0};
    struct mpp_rect dst_rect = {0};
    struct mpp_rect output_rect = {0};

    get_crop_rect(src, &src_rect);
    get_crop_rect(dst, &dst_rect);
    get_crop_rect(output, &output_rect);

    ARTVG_ERR_ROOLBACK(configure_projective_transform(slot, projective->lut_buffer.phy_addr), slot, cmd_count_saved);

    ARTVG_ERR_ROOLBACK(configure_fill_edge_color(slot, &projective->transform_edge), slot, cmd_count_saved);

    uint32_t src_func_sel = 0;
    src_func_sel |= FUNC_SELECT(2);
    src_func_sel |= projective->transform_edge.fill_en ? TRANS_FILL_BG_EN : 0;
    src_func_sel |= projective->transform_edge.blend_en ? TRANS_BLEND_EDGE_EN : 0;
    src_func_sel |= ctrl->src_recolor_en ? RECOLOR_EN : 0;
    src_func_sel |= TRANS_PAD_MODE_EN(projective->transform_edge.pad_mode);
    src_func_sel |= TRANS_LUT_MODE(projective->projective_lut_mode);
    src_func_sel |= TRANS_LUT_BLOCK_SIZE(projective->projective_lut_block_size);

    ARTVG_ERR_ROOLBACK(configure_src_surface(vg, slot, src, NULL, ctrl, src_func_sel, true, &src_rect, output->format), slot, cmd_count_saved);

    ARTVG_ERR_ROOLBACK(configure_dst_surface(vg, slot, dst, ctrl, &dst_rect, output->format), slot, cmd_count_saved);

    ARTVG_ERR_ROOLBACK(configure_output(vg, slot, output, ctrl->output_dither, &output_rect, projective->projective_out_blk_mode), slot, cmd_count_saved);

    ARTVG_ERR_ROOLBACK(configure_blend_mode_finish(slot, ctrl), slot, cmd_count_saved);

    return ARTVG_SUCCESS;

ErrorHandler:
    return error;
}

artvg_projective_t *artvg_projective_allocate(artvg_t *vg,
                                     artvg_matrix_t *matrix,
                                     artvg_transform_edge_t *transform_edge,
                                     artvg_projective_lut_mode_t projective_lut_mode,
                                     artvg_projective_lut_block_size_t projective_lut_block_size,
                                     artvg_projective_out_blk_mode_t trans_out_blk_mode,
                                     int dst_w, int dst_h)
{
    if (!vg)
        return NULL;

    artvg_projective_t *projective = (artvg_projective_t *)artvg_os_malloc(sizeof(artvg_projective_t));
    if (!projective) {
        artvg_log_err("Failed to allocate artvg_projective_t structure\n");
        return NULL;
    }

    memset(projective, 0, sizeof(artvg_projective_t));

    artvg_transform_edge_t transform_edge_default = {0};
    artvg_matrix_t identity_matrix;
    artvg_matrix_identity(&identity_matrix);

    transform_edge = transform_edge == NULL ? &transform_edge_default : transform_edge;
    matrix = matrix == NULL ? &identity_matrix : matrix;

    memcpy(&projective->transform_edge, transform_edge, sizeof(artvg_transform_edge_t));
    projective->projective_lut_mode = projective_lut_mode;
    projective->projective_lut_block_size = projective_lut_block_size;
    projective->projective_out_blk_mode = trans_out_blk_mode;

    int lut_blk_x = 0;
    int lut_blk_y = 0;
    int lut_cell_size;
    int lut_size = 0;
    int item_size;

    switch (projective_lut_block_size) {
    case ARTVG_PROJECTIVE_LUT_BLOCK_8X8:
        lut_cell_size = 8;
        lut_blk_x = ALIGN_16B(dst_w) / lut_cell_size + 1;
        lut_blk_y = ALIGN_16B(dst_h) / lut_cell_size + 1;
        break;
    case ARTVG_PROJECTIVE_LUT_BLOCK_16X16:
        lut_cell_size = 16;
        lut_blk_x = ALIGN_16B(dst_w) / lut_cell_size + 1;
        lut_blk_y = ALIGN_16B(dst_h) / lut_cell_size + 1;
        break;
    case ARTVG_PROJECTIVE_LUT_BLOCK_32X32:
        lut_cell_size = 32;
        lut_blk_x = ALIGN_32B(dst_w) / lut_cell_size + 1;
        lut_blk_y = ALIGN_32B(dst_h) / lut_cell_size + 1;
        break;
    case ARTVG_PROJECTIVE_LUT_BLOCK_64X64:
        lut_cell_size = 64;
        lut_blk_x = ALIGN_64B(dst_w) / lut_cell_size + 1;
        lut_blk_y = ALIGN_64B(dst_h) / lut_cell_size + 1;
        break;
    default:
        artvg_log_err("invalid LUT block size\n");
        artvg_os_free(projective);
        return NULL;
    }

    if (lut_mode_is_packed(projective_lut_mode)) {
        item_size = sizeof(uint32_t);
    } else {
        item_size = sizeof(artvg_point_t);
    }

    lut_size = lut_blk_x * lut_blk_y * item_size;

    uint32_t lut_alloc_size = (lut_size < DMA_BUF_MIN_SIZE) ? DMA_BUF_MIN_SIZE : lut_size;
    if (artvg_alloc_buffer_and_map(vg, lut_alloc_size, &projective->lut_buffer, (void **)&projective->lut_point) != 0) {
        artvg_log_err("map dma buffer failed (requested size=%u, alloc_size=%u)\n", lut_size, lut_alloc_size);
        artvg_os_free(projective);
        return NULL;
    }
    projective->size = lut_alloc_size;

    if (projective->lut_point == NULL) {
        artvg_log_err("allocated LUT pointer is NULL\n");
        artvg_free_buffer_and_unmap(vg, &projective->lut_buffer, lut_alloc_size, (void **)&projective->lut_point);
        artvg_os_free(projective);
        return NULL;
    }

    artvg_matrix_t inv_matrix;
    if (invert_matrix_3x3(matrix, &inv_matrix) != 0) {
        artvg_log_err("Failed to invert matrix for LUT generation\n");
        artvg_free_buffer_and_unmap(vg, &projective->lut_buffer, lut_alloc_size, (void **)&projective->lut_point);
        artvg_os_free(projective);
        return NULL;
    }

    if (fill_lut_block(vg, lut_blk_x, lut_blk_y, lut_cell_size, projective->lut_point, &inv_matrix, projective_lut_mode) != 0) {
        artvg_log_err("fill_lut_block failed, aborting LUT creation\n");
        artvg_free_buffer_and_unmap(vg, &projective->lut_buffer, lut_alloc_size, (void **)&projective->lut_point);
        artvg_os_free(projective);
        return NULL;
    }

    if (artvg_os_dma_buf_sync_range(vg->os, &projective->lut_buffer,
                                    projective->lut_point, ALIGN_CACHE_LINE(lut_size)) != 0) {
        artvg_log_warn("Failed to sync projective LUT cache (fd=%d)\n",
                       projective->lut_buffer.fd);
    }

    memcpy(&projective->matrix, matrix, sizeof(artvg_matrix_t));

    artvg_log_debug("create_projective_lut: mode=%d, packed=%d, projective=%p, lut_point=%p, size=%u, dma_fd=%d\n",
                    projective_lut_mode,
                    lut_mode_is_packed(projective_lut_mode),
                    projective,
                    projective->lut_point,
                    projective->size,
                    projective->lut_buffer.fd);

    return projective;
}

artvg_error_t artvg_projective_free(artvg_t *vg, artvg_projective_t *projective)
{
    if (vg == NULL || projective == NULL)
        return ARTVG_INVALID_PARAM;

    artvg_log_debug("destroy_projective_lut: projective = %p, lut_point=%p, size=%u, dma_fd=%d\n",
                    projective,
                    projective->lut_point,
                    projective->size,
                    projective->lut_buffer.fd);

    artvg_free_buffer_and_unmap(vg, &projective->lut_buffer, projective->size, (void **)&projective->lut_point);
    artvg_os_free(projective);
    return ARTVG_SUCCESS;
}

artvg_error_t configure_affine_transform(struct cmd_slot *slot, artvg_matrix_t *matrix, uint8_t trans_type)
{
    const float SCALE_16BIT = 65536.0f;
    const uint32_t MASK_26BIT = 0x3FFFFFF;   /* 26-bit mask for A11, A12, A21, A22 */
    const uint32_t MASK_29BIT = 0x1FFFFFFF;  /* 29-bit mask for A13, A23 */
    uint32_t affine_cmds[6];
    artvg_matrix_t valid_matrix;

    uint8_t fractional_bits = (trans_type == ARTVG_AFFINE_TRANS_VECTOR) ? 14 : 8;
    float scale_translation = (float)(1 << fractional_bits);

    if (preprocess_matrix(matrix, &valid_matrix, trans_type) < 0)
        return ARTVG_INVALID_PARAM;

    for (int i = 0; i < 2; i++) {
        for (int j = 0; j < 3; j++) {
            float value = valid_matrix.m[i][j];
            int32_t fixed_signed;
            uint32_t fixed_unsigned;

            if (j == 2) {
                fixed_signed = lroundf(value * scale_translation);  /* A13, A23 */
                fixed_unsigned = (uint32_t)fixed_signed & MASK_29BIT;
            } else {
                fixed_signed = lroundf(value * SCALE_16BIT);        /* A11, A12, A21, A22 */
                fixed_unsigned = (uint32_t)fixed_signed & MASK_26BIT;
            }

            int idx = i * 3 + j;
            switch (idx) {
            case 0: affine_cmds[0] = AFFINE_A11_SET(fixed_unsigned); break;
            case 1: affine_cmds[1] = AFFINE_A12_SET(fixed_unsigned); break;
            case 2: affine_cmds[2] = AFFINE_A13_SET(fixed_unsigned); break;
            case 3: affine_cmds[3] = AFFINE_A21_SET(fixed_unsigned); break;
            case 4: affine_cmds[4] = AFFINE_A22_SET(fixed_unsigned); break;
            case 5: affine_cmds[5] = AFFINE_A23_SET(fixed_unsigned); break;
            }
        }
    }

    command_queue_add_command(slot, AFFINE_A11, ARRAY_SIZE(affine_cmds), affine_cmds, false);

    return ARTVG_SUCCESS;
}

artvg_error_t configure_fill_edge_color(struct cmd_slot *slot, artvg_transform_edge_t *ctrl)
{
    if (ctrl->pad_mode == ARTVG_PAD_MODE_FIXED_COLOR)
        command_queue_add_command(slot, SRC_FILL_COLOR, 1, &ctrl->fill_color, false);


    return ARTVG_SUCCESS;
}

artvg_error_t configure_projective_transform(struct cmd_slot *slot, uint32_t phy_addr)
{
    artvg_log_debug("configure_projective_transform: phy_addr=0x%x\n", phy_addr);

    uint32_t trans_cmds[] = {
        phy_addr,
        0,
    };
    command_queue_add_command(slot, TRANS_LUT_LOW_ADDR, ARRAY_SIZE(trans_cmds), trans_cmds, false);

    return ARTVG_SUCCESS;
}

static int artvg_get_mode_bit(artvg_projective_lut_mode_t lut_mode)
{
    switch (lut_mode) {
    case ARTVG_PROJECTIVE_LUT_MOD_S10_6:
        return 6;
    case ARTVG_PROJECTIVE_LUT_MOD_S11_5:
        return 5;
    case ARTVG_PROJECTIVE_LUT_MOD_S12_4:
        return 4;
    case ARTVG_PROJECTIVE_LUT_MOD_S13_3:
        return 3;
    case ARTVG_PROJECTIVE_LUT_MOD_S14_2:
        return 2;
    case ARTVG_PROJECTIVE_LUT_MOD_S16_8:
        return 8;
    case ARTVG_PROJECTIVE_LUT_MOD_S18_6:
        return 6;
    case ARTVG_PROJECTIVE_LUT_MOD_S20_4:
        return 4;
    default:
        return 6;
    }
}

static int fill_lut_block(artvg_t *vg, int blk_x, int blk_y, int blk_size, void *lut, artvg_matrix_t *matrix, artvg_projective_lut_mode_t lut_mode)
{
    int y_step = blk_size;
    int x_step = blk_size;
    int y_cur = 0;
    int is_packed = lut_mode_is_packed(lut_mode);

    artvg_log_debug("fill_lut_block: blk_x=%d, blk_y=%d, blk_size=%d, lut_mode=%d, packed=%d\n",
                    blk_x, blk_y, blk_size, lut_mode, is_packed);

    for (int y = 0; y < blk_y; y++) {
        int x_cur = 0;
        for (int x = 0; x < blk_x; x++) {
            int pos = y * blk_x + x;
            int out_bit = 8;
            float denominator = matrix->m[2][0] * x_cur + matrix->m[2][1] * y_cur + matrix->m[2][2];

            /* Avoid division by zero */
            if (fabs(denominator) < 1e-6) {
                denominator = 1e-6 * (denominator >= 0 ? 1 : -1);
            }

            float cur_x = (matrix->m[0][0] * x_cur + matrix->m[0][1] * y_cur + matrix->m[0][2]) / denominator;
            float cur_y = (matrix->m[1][0] * x_cur + matrix->m[1][1] * y_cur + matrix->m[1][2]) / denominator;

            out_bit = artvg_get_mode_bit(lut_mode);

            long long out_x = (long long)((long long)(1 << out_bit) * cur_x);
            long long out_y = (long long)((long long)(1 << out_bit) * cur_y);

            if (is_packed) {
                /* Packed format (S10_6 to S14_2): X and Y packed into one 32-bit word */
                /* Format: [Y(16bit) | X(16bit)] */
                if (out_x < -32768 || out_x > 32767 ||
                    out_y < -32768 || out_y > 32767) {
                    artvg_log_err("LUT overflow: x=%lld, y=%lld (pos=%d, x_cur=%d, y_cur=%d)\n",
                              out_x, out_y, pos, x_cur, y_cur);
                    return -1;
                }
                int16_t clamped_x = (int16_t)CLAMP(out_x, -32768, 32767);
                int16_t clamped_y = (int16_t)CLAMP(out_y, -32768, 32767);
                uint32_t packed_value = ((uint32_t)(uint16_t)clamped_x) | (((uint32_t)(uint16_t)clamped_y) << 16);
                ((uint32_t *)lut)[pos] = packed_value;
            } else {
                /* Separate format (S16_8 to S20_4): X and Y in separate 32-bit words */
                if (out_x < -8388608 || out_x > 8388607 ||
                    out_y < -8388608 || out_y > 8388607) {
                    artvg_log_err("LUT overflow: x=%lld, y=%lld (pos=%d, x_cur=%d, y_cur=%d)\n",
                              out_x, out_y, pos, x_cur, y_cur);
                    return -1;
                }
                ((artvg_point_t *)lut)[pos].x = CLAMP(out_x, -8388608, 8388607);
                ((artvg_point_t *)lut)[pos].y = CLAMP(out_y, -8388608, 8388607);
            }
            x_cur = x_cur + x_step;
        }
        y_cur = y_cur + y_step;
    }

    return 0;
}

static int invert_matrix_3x3(artvg_matrix_t *src, artvg_matrix_t *dst)
{
    float det;

    /* Compute determinant */
    det = src->m[0][0] * (src->m[1][1] * src->m[2][2] - src->m[1][2] * src->m[2][1])
        - src->m[0][1] * (src->m[1][0] * src->m[2][2] - src->m[1][2] * src->m[2][0])
        + src->m[0][2] * (src->m[1][0] * src->m[2][1] - src->m[1][1] * src->m[2][0]);

    if (fabsf(det) < 1e-10f) {
        /* Matrix is singular, cannot be inverted */
        return -1;
    }

    /* Compute inverse matrix = adjugate / determinant */
    dst->m[0][0] = (src->m[1][1] * src->m[2][2] - src->m[1][2] * src->m[2][1]) / det;
    dst->m[0][1] = (src->m[0][2] * src->m[2][1] - src->m[0][1] * src->m[2][2]) / det;
    dst->m[0][2] = (src->m[0][1] * src->m[1][2] - src->m[0][2] * src->m[1][1]) / det;

    dst->m[1][0] = (src->m[1][2] * src->m[2][0] - src->m[1][0] * src->m[2][2]) / det;
    dst->m[1][1] = (src->m[0][0] * src->m[2][2] - src->m[0][2] * src->m[2][0]) / det;
    dst->m[1][2] = (src->m[0][2] * src->m[1][0] - src->m[0][0] * src->m[1][2]) / det;

    dst->m[2][0] = (src->m[1][0] * src->m[2][1] - src->m[1][1] * src->m[2][0]) / det;
    dst->m[2][1] = (src->m[0][1] * src->m[2][0] - src->m[0][0] * src->m[2][1]) / det;
    dst->m[2][2] = (src->m[0][0] * src->m[1][1] - src->m[0][1] * src->m[1][0]) / det;

    return 0;
}

static int preprocess_matrix(artvg_matrix_t *original, artvg_matrix_t *output, uint8_t trans_type)
{
    float max_coeff = 511.9999f;
    float max_trans = (trans_type == ARTVG_AFFINE_TRANS_VECTOR) ? 16383.999f : 1048575.0f;
    artvg_matrix_t tmp_matrix;

    if (trans_type == ARTVG_AFFINE_TRANS_IMAGE) {
        if (invert_matrix_3x3(original, &tmp_matrix) < 0) {
            return ARTVG_INVALID_PARAM;
        }
    } else {
        memcpy(&tmp_matrix, original, sizeof(artvg_matrix_t));
    }

    for (int i = 0; i < 2; i++) {
        for (int j = 0; j < 3; j++) {
            float value = tmp_matrix.m[i][j];
            if (j == 2) {
                if (value >= max_trans || value < -max_trans) {
                    artvg_log_warn("affine translation A%d%d=%f overflow, use artvg_projective instead\n", i + 1, j + 1, value);
                    tmp_matrix.m[i][j] = value > 0 ? max_trans : -max_trans;
                }
            } else {
                if (value >= max_coeff || value < -max_coeff) {
                    artvg_log_warn("affine coefficient A%d%d=%f overflow, use artvg_projective instead\n", i + 1, j + 1, value);
                    tmp_matrix.m[i][j] = value > 0 ? max_coeff : -max_coeff;
                }
            }
        }
    }

    memcpy(output, &tmp_matrix, sizeof(artvg_matrix_t));
    return ARTVG_SUCCESS;
}
