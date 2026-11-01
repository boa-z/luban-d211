/*
 * Copyright (C) 2020-2026 ArtInChip Technology Co. Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 *  author: <xiaodong.zhao@artinchip.com>
 *  Desc: mpeg12 register setting interface
 *
 */

#include "mpeg_register.h"
#include "avc_register_v2.h"
#include "mpeg12_decoder.h"
#include "ve.h"
#include "ve_top_register.h"
#include <stdio.h>
#include <string.h>

typedef struct mpeg12_register_list {
    reg_mpeg_ref_frm_idx mpeg_ref_idx;
    reg_mpeg_pic_type mpeg_pic_type;
    reg_mpeg_mb_type mpeg_mb_type;
    reg_tq_coeff tq_coeff;
    reg_mpeg_quant_matrix mpeg_quant_matrix;
} mpeg12_register_list;

static mpeg12_register_list g_mpeg_reg_list = {0};


void config_glb_reg(struct mpeg12_dec_ctx *s, enum CODEC_TYPE type)
{
    if (s->fp_reg)
        fprintf(s->fp_reg, "// config codec type\n");

    write_reg(s->regs_base + GLB_CODEC_TYPE_REG, type, s->fp_reg);

    if (s->fp_reg)
        fprintf(s->fp_reg, "// config irq\n");

    write_reg(s->regs_base + GLB_INT_REG, 0x07, s->fp_reg);
    write_reg(s->regs_base + GLB_STATUS_REG, 0x37, s->fp_reg);
}

void config_pic_init(struct mpeg12_dec_ctx *s)
{
    if (s->fp_reg)
        fprintf(s->fp_reg, "// config pic init\n");

    write_reg(s->regs_base + GLB_CTRL_REG, 1, s->fp_reg);
}

void config_slice_start(struct mpeg12_dec_ctx *s)
{
    if (s->fp_reg)
        fprintf(s->fp_reg, "// config slice start\n");

    write_reg(s->regs_base + GLB_CTRL_REG, 2, s->fp_reg);
}

static void config_vpu_reg(struct mpeg12_dec_ctx *s)
{
    write_reg(s->regs_base + VE_CLK_REG, 1, s->fp_reg);
    write_reg(s->regs_base + VE_RST_REG, 0, s->fp_reg);

    while (1) {
        u32 tmp = 0;
        tmp = read_reg(s->regs_base + VE_RST_REG);
        if ((tmp >> 16) == 0)
            break;
    }

    write_reg(s->regs_base + VE_INIT_REG, 1, s->fp_reg);
    write_reg(s->regs_base + VE_IRQ_REG, 1, s->fp_reg);
    write_reg(s->regs_base + VE_AVC_EN_REG, 1, s->fp_reg);
}

void config_mpeg12_mb_coeff(struct mpeg12_dec_ctx *s, int block_num)
{
    int len, addr;
    uint32_t *pVal, *ptr;

    len = s->mb_cfg_info_list[s->cur_mblk_addr].len;
    ptr = s->mb_cfg_info_list[s->cur_mblk_addr].ptr;

    g_mpeg_reg_list.tq_coeff.tq_coeff_buf_access = 1;
    g_mpeg_reg_list.tq_coeff.tq_coeff_buf_wr = 1;
    addr = 64 * block_num;
    for (int i = 0; i < 64; i++) {
        g_mpeg_reg_list.tq_coeff.tq_coeff_buf_addr = addr + i;
        if (s->mb_info.mb_intra && i == 0) {
            // DC val of intra mb
            int dc = s->mb_info.recon[block_num][i];
            dc = dc << (3 - s->pic_code_extension.intra_dc_precision);
            dc = (dc > 2047) ? 2047 : ((dc < -2048) ? 2048 : dc);
            g_mpeg_reg_list.tq_coeff.tq_coeff_buf_data = dc;
            pVal = (uint32_t *)&g_mpeg_reg_list.tq_coeff;
            ptr[len++] = *pVal;
        } else if (s->mb_info.recon[block_num][i]) {
            g_mpeg_reg_list.tq_coeff.tq_coeff_buf_data = s->mb_info.recon[block_num][i];
            pVal = (uint32_t *)&g_mpeg_reg_list.tq_coeff;
            ptr[len++] = *pVal;
        }
    }
    s->mb_cfg_info_list[s->cur_mblk_addr].len = len;
}

static void check_mb_data(struct mpeg12_dec_ctx *s)
{
    // 1. check cur_mb_addr
    if (s->cur_mblk_addr != s->mb_cfg_addr) {
        loge("error, cur_mb_addr: %d, s->mc_cfg_addr: %d", s->cur_mblk_addr, s->mb_cfg_addr);
    }

    // 2. check mb_x/mb_y
    int mb_x = s->mb_cfg_addr % s->mb_width;
    int mb_y = s->mb_cfg_addr / s->mb_width;
    if (mb_x != s->mb_x || mb_y != s->rsim_mb_y) {
        loge("error, mb: %d, %d, s->mbx(%d %d)", mb_x, mb_y, s->mb_x, s->rsim_mb_y);
    }

    s->mb_cfg_addr++;
}

int is_last_mb(struct mpeg12_dec_ctx *s)
{
    if (s->pic_code_extension.picture_structure == FRAME)
        return (s->mb_x == (s->mb_width - 1)) && (s->mb_y == (s->mb_height - 1));
    else
        return (s->mb_x == (s->mb_width - 1)) && (s->rsim_mb_y == (s->mb_height / 2 - 1));
}

void write_mb_info_header(struct mpeg12_dec_ctx *s)
{
    // write mb_info header
    mb_info_header header = {0};

    header.cbp_num = ((s->mb_info.cbp >> 5) & 1) + ((s->mb_info.cbp >> 4) & 1) + ((s->mb_info.cbp >> 3) & 1) + ((s->mb_info.cbp >> 2) & 1) +
                     ((s->mb_info.cbp >> 1) & 1) + ((s->mb_info.cbp) & 1);

    if (s->mb_info.mb_intra) {
        header.mb_type = 0;
    } else if (s->mb_info.motion_backward == 0) {
        header.mb_type = 1;
    } else {
        header.mb_type = 2;
    }
    header.mb_x = s->mb_x;
    header.mb_y = s->rsim_mb_y;

    if (s->mb_info.motion_type == DUAL_PRIME && !s->mb_info.mb_intra) {
        header.mb_type = 2;
    }

    header.last_mb = is_last_mb(s);

    header.data_length_bytes = 4 * (s->mb_cfg_info_list[s->cur_mblk_addr].len - 1);

    uint32_t *pVal = (uint32_t *)&header;
    s->mb_cfg_info_list[s->cur_mblk_addr].ptr[0] = *pVal;
    s->cur_mb_cfg_ptr += s->mb_cfg_info_list[s->cur_mblk_addr].len;

    check_mb_data(s);

#ifdef MPEG12_DUMP_ENABLE
    fprintf(s->fp_mb_cfg_data, "mb_type: %d, last: %d, header.data_length_bytes: %d\n", header.mb_type, header.last_mb,
            header.data_length_bytes);
    fprintf(s->fp_mb_cfg_data, "s->cur_mblk_addr: %d, cfg_data offset: 0x%lx, len: %d\n", s->cur_mblk_addr,
            (s->mb_cfg_info_list[s->cur_mblk_addr].ptr - s->mb_cfg_data) * 4, s->mb_cfg_info_list[s->cur_mblk_addr].len);
#endif
}

static uint32_t mc_get_blk_num(struct mpeg12_dec_ctx *s)
{
    uint32_t blk_num = 1; // 16x16

    if (s->pic_code_extension.picture_structure == FRAME && s->mb_info.motion_type == FIELD_BASED) {
        blk_num = 2; // 16x8
    }
    if (s->pic_code_extension.picture_structure != FRAME && s->mb_info.motion_type == SIXTEEN_BY_EIGHT_MC) {
        blk_num = 2;
    }
    if (s->pic_code_extension.picture_structure == FRAME && s->mb_info.motion_type == DUAL_PRIME) {
        blk_num = 2;
    }

    return blk_num;
}

uint32_t mc_get_pred_size(struct mpeg12_dec_ctx *s)
{
    uint32_t ret = 0;

    // [2:0] pred_size: 0-16x16; 1-16x8
    reg_pred_size val = {0};
    if (mc_get_blk_num(s) == 2) {
        val.pred_size_blk0 = 1;
        val.pred_size_blk2 = 1;
    }

    if (s->mb_info.motion_type == DUAL_PRIME && s->pic_code_extension.picture_structure == FRAME) {
        val.pred_size_blk0 = 1;
        val.pred_size_blk2 = 1;
    }

    ret = *(uint32_t *)&val;

    return ret;
}

uint32_t mc_get_pred_mode(struct mpeg12_dec_ctx *s)
{
    // [1:0] pred_mode: 0-none; 1-fwd; 2-bwd; 3-bi
    uint32_t pred_mode = 0, ret;

    if (s->mb_info.motion_forward)
        pred_mode += 1;
    if (s->mb_info.motion_backward)
        pred_mode += 2;

    reg_pred_mode val = {0};
    val.pred_mode_blk0 = pred_mode;
    val.pred_mode_blk1 = pred_mode;
    val.pred_mode_blk2 = pred_mode;
    val.pred_mode_blk3 = pred_mode;
    if (mc_get_blk_num(s) == 2) // 16x8
        val.pred_mode_blk2 = pred_mode;

    if (s->mb_info.motion_type == DUAL_PRIME) {
        val.pred_mode_blk0 = 3; // bi-pred
        val.pred_mode_blk1 = 3;
        val.pred_mode_blk2 = 3;
        val.pred_mode_blk3 = 3;

        // 16x8 block, bi-pred
        if (s->pic_code_extension.picture_structure == FRAME)
            val.pred_mode_blk2 = 3;
    }

    ret = *(uint32_t *)&val;

    return ret;
}

struct mc_info {
    uint32_t fwd_idx;
    uint32_t bwd_idx;
    uint32_t pred_size;
    uint32_t pred_mode;
    uint32_t fwd_mv[4];
    uint32_t fwd_mv_c[4];
    uint32_t bwd_mv[4];
    uint32_t bwd_mv_c[4];
};

static void mc_get_frame_pred_frame_info(struct mpeg12_dec_ctx *s, struct mc_info *info)
{
    uint32_t *pVal = NULL;

    if (s->mb_info.motion_forward) {
        reg_ref_idx ref_idx = {0};
        ref_idx.ref_idx_blk0 = s->last_pic_idx;

        pVal = (uint32_t *)&ref_idx;
        info->fwd_idx = *pVal;
        info->fwd_mv[0] = ((s->mb_info.mv[0][0][0] << 16) & 0xffff0000) | (s->mb_info.mv[0][0][1] & 0xffff);
        info->fwd_mv_c[0] = ((s->mb_info.mv[0][0][0] / 2 << 16) & 0xffff0000) | (s->mb_info.mv[0][0][1] / 2 & 0xffff);
    }

    if (s->mb_info.motion_backward) {
        reg_ref_idx ref_idx = {0};
        ref_idx.ref_idx_blk0 = s->next_pic_idx;
        pVal = (uint32_t *)&ref_idx;

        info->bwd_idx = *pVal;
        info->bwd_mv[0] = ((s->mb_info.mv[0][1][0] << 16) & 0xffff0000) | (s->mb_info.mv[0][1][1] & 0xffff);
        info->bwd_mv_c[0] = ((s->mb_info.mv[0][1][0] / 2 << 16) & 0xffff0000) | (s->mb_info.mv[0][1][1] / 2 & 0xffff);
    }
}

static void mc_get_16x8_field_pred_info(struct mpeg12_dec_ctx *s, struct mc_info *info)
{
    int i;
    uint32_t *pVal = NULL;

    if (s->mb_info.motion_forward) {
        int ref = 0;
        reg_ref_idx ref_idx = {0};
        /* i=0: upper 16x8; i=1: lower 16x8 */
        for (i = 0; i < 2; i++) {
            // ref field select for filed-pred ISO/IEC 13818-2:  7.6.2.1
            if (s->pic.picture_coding_type == MP2_B_PICTURE || s->first_field ||
                s->pic_code_extension.picture_structure == s->mb_info.motion_vertical_field_select[i][0] + 1) {
                ref = s->last_pic_idx;
            } else {
                // ref field is the first filed of current frame
                ref = s->cur_pic_idx;
            }

            if (i == 0) {
                ref_idx.ref_idx_blk0 = ref;
                ref_idx.bot_field_blk0 = s->mb_info.motion_vertical_field_select[i][0];
            } else {
                ref_idx.ref_idx_blk2 = ref;
                ref_idx.bot_field_blk2 = s->mb_info.motion_vertical_field_select[i][0];
            }

            info->fwd_mv[2 * i] = ((s->mb_info.mv[i][0][0] << 16) & 0xffff0000) | (s->mb_info.mv[i][0][1] & 0xffff);
            info->fwd_mv_c[2 * i] = ((s->mb_info.mv[i][0][0] / 2 << 16) & 0xffff0000) | (s->mb_info.mv[i][0][1] / 2 & 0xffff);
        }
        pVal = (uint32_t *)&ref_idx;
        info->fwd_idx = *pVal;
    }

    if (s->mb_info.motion_backward) {
        int ref = 0;
        reg_ref_idx ref_idx = {0};

        /* i=0: upper 16x8; i=1: lower 16x8 */
        for (i = 0; i < 2; i++) {
            // ref field select for filed-pred ISO/IEC 13818-2:  7.6.2.1
            if (s->pic.picture_coding_type == MP2_B_PICTURE || s->first_field ||
                s->pic_code_extension.picture_structure == s->mb_info.motion_vertical_field_select[i][1] + 1) {
                ref = s->next_pic_idx;
            } else {
                // ref field is the first filed of current frame
                ref = s->cur_pic_idx;
            }

            if (i == 0) {
                ref_idx.ref_idx_blk0 = ref;
                ref_idx.bot_field_blk0 = s->mb_info.motion_vertical_field_select[i][1];
            } else {
                ref_idx.ref_idx_blk2 = ref;
                ref_idx.bot_field_blk2 = s->mb_info.motion_vertical_field_select[i][1];
            }

            info->bwd_mv[2 * i] = ((s->mb_info.mv[i][1][0] << 16) & 0xffff0000) | (s->mb_info.mv[i][1][1] & 0xffff);
            info->bwd_mv_c[2 * i] = ((s->mb_info.mv[i][1][0] / 2 << 16) & 0xffff0000) | (s->mb_info.mv[i][1][1] / 2 & 0xffff);
        }
        pVal = (uint32_t *)&ref_idx;
        info->bwd_idx = *pVal;
    }
}

static void mc_get_field_pred_frame_info(struct mpeg12_dec_ctx *s, struct mc_info *info)
{
    uint32_t *pVal = NULL;

    if (s->mb_info.motion_forward) {
        /* top field */
        logd("======== forward top field ========");
        reg_ref_idx ref_idx = {0};
        ref_idx.ref_idx_blk0 = s->last_pic_idx;
        ref_idx.bot_field_blk0 = s->mb_info.motion_vertical_field_select[0][0];
        ref_idx.ref_idx_blk2 = s->last_pic_idx;
        ref_idx.bot_field_blk2 = s->mb_info.motion_vertical_field_select[1][0];
        pVal = (uint32_t *)&ref_idx;
        info->fwd_idx = *pVal;

        info->fwd_mv[0] = ((s->mb_info.mv[0][0][0] << 16) & 0xffff0000) | (s->mb_info.mv[0][0][1] & 0xffff);
        info->fwd_mv[2] = ((s->mb_info.mv[1][0][0] << 16) & 0xffff0000) | (s->mb_info.mv[1][0][1] & 0xffff);
        info->fwd_mv_c[0] = ((s->mb_info.mv[0][0][0] / 2 << 16) & 0xffff0000) | (s->mb_info.mv[0][0][1] / 2 & 0xffff);
        info->fwd_mv_c[2] = ((s->mb_info.mv[1][0][0] / 2 << 16) & 0xffff0000) | (s->mb_info.mv[1][0][1] / 2 & 0xffff);
    }

    if (s->mb_info.motion_backward) {
        /* bottom field */
        logd("======== backward bottom field ========");
        reg_ref_idx ref_idx = {0};
        ref_idx.ref_idx_blk0 = s->next_pic_idx;
        ref_idx.bot_field_blk0 = s->mb_info.motion_vertical_field_select[0][1];
        ref_idx.ref_idx_blk2 = s->next_pic_idx;
        ref_idx.bot_field_blk2 = s->mb_info.motion_vertical_field_select[1][1];
        pVal = (uint32_t *)&ref_idx;
        info->bwd_idx = *pVal;

        info->bwd_mv[0] = ((s->mb_info.mv[0][1][0] << 16) & 0xffff0000) | (s->mb_info.mv[0][1][1] & 0xffff);
        info->bwd_mv[2] = ((s->mb_info.mv[1][1][0] << 16) & 0xffff0000) | (s->mb_info.mv[1][1][1] & 0xffff);
        info->bwd_mv_c[0] = ((s->mb_info.mv[0][1][0] / 2 << 16) & 0xffff0000) | (s->mb_info.mv[0][1][1] / 2 & 0xffff);
        info->bwd_mv_c[2] = ((s->mb_info.mv[1][1][0] / 2 << 16) & 0xffff0000) | (s->mb_info.mv[1][1][1] / 2 & 0xffff);
    }
}

static void mc_get_field_pred_field_info(struct mpeg12_dec_ctx *s, struct mc_info *info)
{
    uint32_t *pVal = NULL;

    if (s->mb_info.motion_forward) {
        reg_ref_idx ref_idx = {0};
        ref_idx.bot_field_blk0 = s->mb_info.motion_vertical_field_select[0][0];

        // update ref picture
        if (s->pic_code_extension.picture_structure != s->mb_info.motion_vertical_field_select[0][0] + 1 &&
            s->pic.picture_coding_type != MP2_B_PICTURE && !s->first_field)
            ref_idx.ref_idx_blk0 = s->cur_pic_idx;
        else
            ref_idx.ref_idx_blk0 = s->last_pic_idx;

        pVal = (uint32_t *)&ref_idx;
        info->fwd_idx = *pVal;

        info->fwd_mv[0] = ((s->mb_info.mv[0][0][0] << 16) & 0xffff0000) | (s->mb_info.mv[0][0][1] & 0xffff);
        info->fwd_mv_c[0] = ((s->mb_info.mv[0][0][0] / 2 << 16) & 0xffff0000) | (s->mb_info.mv[0][0][1] / 2 & 0xffff);
    }

    if (s->mb_info.motion_backward) {
        reg_ref_idx ref_idx = {0};
        ref_idx.bot_field_blk0 = s->mb_info.motion_vertical_field_select[0][1];

        // update ref picture
        if (s->pic_code_extension.picture_structure != s->mb_info.motion_vertical_field_select[0][1] + 1 &&
            s->pic.picture_coding_type != MP2_B_PICTURE && !s->first_field)
            ref_idx.ref_idx_blk0 = s->cur_pic_idx;
        else
            ref_idx.ref_idx_blk0 = s->next_pic_idx;

        pVal = (uint32_t *)&ref_idx;
        info->bwd_idx = *pVal;
        info->bwd_mv[0] = ((s->mb_info.mv[0][1][0] << 16) & 0xffff0000) | (s->mb_info.mv[0][1][1] & 0xffff);
        info->bwd_mv_c[0] = ((s->mb_info.mv[0][1][0] / 2 << 16) & 0xffff0000) | (s->mb_info.mv[0][1][1] / 2 & 0xffff);
    }
}

static void mc_get_dual_prime_frame_info(struct mpeg12_dec_ctx *s, struct mc_info *info)
{
    uint32_t *pVal = NULL;
    reg_ref_idx ref_idx = {0};

    /**
     * the pred block of top-field is the average of top-field with mv[0] pred and bottom-field with mv[2]
     * the pred block of bottom-field is the average of top-field with mv[1] and bottom-field with mv[3]

     * j is indicates whether current filed is bottom filed;
     * i^j is indicates filed_select
     * i=0;j=0: current filed is top-filed, the reference is the top filed, mv[0] (fwd-pred)
     * i=0;j=1: current filed is bot-filed, the reference is the bot filed, mv[1]
     * i=1;j=0: current filed is top-filed, the reference is the bot filed, mv[2] (bwd-pred)
     * i=1;j=1: current filed is bot-filed, the reference is the top filed, mv[3]
     */
    ref_idx.ref_idx_blk0 = s->last_pic_idx;
    ref_idx.bot_field_blk0 = 0;
    ref_idx.ref_idx_blk2 = s->last_pic_idx;
    ref_idx.bot_field_blk2 = 1;
    pVal = (uint32_t *)&ref_idx;
    info->fwd_idx = *pVal;

    ref_idx.bot_field_blk0 = 1;
    ref_idx.bot_field_blk2 = 0;
    pVal = (uint32_t *)&ref_idx;
    info->bwd_idx = *pVal;

    // top-field 16x8, fwd-pred refrence top-field, mv[0]; bwd-pred refrence bot-field, mv[2]
    info->fwd_mv[0] = ((s->mb_info.mv[0][0][0] << 16) & 0xffff0000) | (s->mb_info.mv[0][0][1] & 0xffff);
    info->bwd_mv[0] = ((s->mb_info.mv[2][0][0] << 16) & 0xffff0000) | (s->mb_info.mv[2][0][1] & 0xffff);
    info->fwd_mv_c[0] = ((s->mb_info.mv[0][0][0] / 2 << 16) & 0xffff0000) | (s->mb_info.mv[0][0][1] / 2 & 0xffff);
    info->bwd_mv_c[0] = ((s->mb_info.mv[2][0][0] / 2 << 16) & 0xffff0000) | (s->mb_info.mv[2][0][1] / 2 & 0xffff);

    // bot-field 16x8, fwd-pred refrence bot-field, mv[1]; bwd-pred refrence top field, mv[3]
    info->fwd_mv[2] = ((s->mb_info.mv[1][0][0] << 16) & 0xffff0000) | (s->mb_info.mv[1][0][1] & 0xffff);
    info->bwd_mv[2] = ((s->mb_info.mv[3][0][0] << 16) & 0xffff0000) | (s->mb_info.mv[3][0][1] & 0xffff);
    info->fwd_mv_c[2] = ((s->mb_info.mv[1][0][0] / 2 << 16) & 0xffff0000) | (s->mb_info.mv[1][0][1] / 2 & 0xffff);
    info->bwd_mv_c[2] = ((s->mb_info.mv[3][0][0] / 2 << 16) & 0xffff0000) | (s->mb_info.mv[3][0][1] / 2 & 0xffff);
}

static void mc_get_dual_prime_field_info(struct mpeg12_dec_ctx *s, struct mc_info *info)
{
    uint32_t *pVal = NULL;
    reg_ref_idx ref_idx = {0};

    ref_idx.ref_idx_blk0 = s->last_pic_idx;
    ref_idx.bot_field_blk0 = (s->pic_code_extension.picture_structure != TOP_FIELD);

    pVal = (uint32_t *)&ref_idx;
    info->fwd_idx = *pVal;

    info->fwd_mv[0] = ((s->mb_info.mv[0][0][0] << 16) & 0xffff0000) | (s->mb_info.mv[0][0][1] & 0xffff);
    info->fwd_mv_c[0] = ((s->mb_info.mv[0][0][0] / 2 << 16) & 0xffff0000) | (s->mb_info.mv[0][0][1] / 2 & 0xffff);

    if (!s->first_field) {
        ref_idx.ref_idx_blk0 = s->cur_pic_idx;
    } else {
        ref_idx.ref_idx_blk0 = s->last_pic_idx;
    }
    ref_idx.bot_field_blk0 = s->pic_code_extension.picture_structure != BOTTOM_FIELD;

    pVal = (uint32_t *)&ref_idx;
    info->bwd_idx = *pVal;

    info->bwd_mv[0] = ((s->mb_info.mv[2][0][0] << 16) & 0xffff0000) | (s->mb_info.mv[2][0][1] & 0xffff);
    info->bwd_mv_c[0] = ((s->mb_info.mv[2][0][0] / 2 << 16) & 0xffff0000) | (s->mb_info.mv[2][0][1] / 2 & 0xffff);
}
static void get_mc_info(struct mpeg12_dec_ctx *s, struct mc_info *info)
{
    info->pred_mode = mc_get_pred_mode(s);
    info->pred_size = mc_get_pred_size(s);
    info->fwd_idx = 0;
    info->bwd_idx = 0;

    if (s->pic_code_extension.picture_structure == FRAME && s->mb_info.motion_type == FRAME_BASED) {
        // 1. frame-pred for frame picture
        mc_get_frame_pred_frame_info(s, info);
    } else if (s->pic_code_extension.picture_structure != FRAME && s->mb_info.motion_type == SIXTEEN_BY_EIGHT_MC) {
        // 2. 16x8 filed pred
        mc_get_16x8_field_pred_info(s, info);
    } else if (s->mb_info.motion_type == FIELD_BASED) {
        // 3. field-pred for frame picture structure
        logi("motion_type: %ld, forward: %ld, backward: %ld", s->mb_info.motion_type,
        	s->mb_info.motion_forward, s->mb_info.motion_backward);
        if (s->pic_code_extension.picture_structure == FRAME) {
            mc_get_field_pred_frame_info(s, info);
        } else {
            mc_get_field_pred_field_info(s, info);
        }
    } else if (s->mb_info.motion_type == DUAL_PRIME) {
        // 4. dual-prime
        if (s->pic_code_extension.picture_structure == FRAME) {
            mc_get_dual_prime_frame_info(s, info);
        } else { // field
            // vector[0][0][1:0] whole field, from same parity, forward
            // vector[2][0][1:0] whole field, from opposite parity, forward
            mc_get_dual_prime_field_info(s, info);
        }
    }
}

void config_mpeg12_mb_info(struct mpeg12_dec_ctx *s)
{
    uint32_t *pVal = NULL;
    int len = 1;

    g_mpeg_reg_list.mpeg_mb_type.mb_cbp = s->mb_info.cbp;
    if (s->mb_info.mb_intra) {
        g_mpeg_reg_list.mpeg_mb_type.mpeg_intra = 1;
    } else {
        g_mpeg_reg_list.mpeg_mb_type.mpeg_intra = 0;
    }

    if (s->pic_code_extension.picture_structure != FRAME && s->mb_info.motion_type == SIXTEEN_BY_EIGHT_MC) {
        g_mpeg_reg_list.mpeg_mb_type.mb_field_mc_flag = 1;
    } else if (s->mb_info.motion_type == FRAME_BASED || s->mb_info.mb_intra) {
        g_mpeg_reg_list.mpeg_mb_type.mb_field_mc_flag = 0;
    } else {
        g_mpeg_reg_list.mpeg_mb_type.mb_field_mc_flag = 1;
    }

    g_mpeg_reg_list.mpeg_mb_type.mpeg_gmc = 0;
    g_mpeg_reg_list.mpeg_mb_type.mpeg_skip = 0;
    g_mpeg_reg_list.mpeg_mb_type.dct_type = s->mb_info.dct_type;
    pVal = (uint32_t *)&g_mpeg_reg_list.mpeg_mb_type;
    // MPEG_MB_TYPE_REG
    s->cur_mb_cfg_ptr[len++] = *pVal;

#ifdef MPEG12_DUMP_ENABLE
    fprintf(s->fp_mb_cfg_data, "mb(%d, %d)\n", s->mb_x, s->rsim_mb_y);
    fprintf(s->fp_mb_cfg_data, "MB_TYPE_REG: %08x\n", *pVal);
#endif

    // MB_QSCALE
    reg_quant_param q_scale = {0};
    q_scale.quant_scale_code = s->mb_info.quantiser_scale_code;
    q_scale.q_scale_type = s->pic_code_extension.q_scale_type;
    pVal = (uint32_t *)&q_scale;
    s->cur_mb_cfg_ptr[len++] = *pVal;

#ifdef MPEG12_DUMP_ENABLE
    fprintf(s->fp_mb_cfg_data, "MB_QSCALE_REG: %08x\n", *pVal);
#endif

    if (s->mb_info.mb_intra) {
        s->mb_cfg_info_list[s->cur_mblk_addr].len = len;
    } else {
        struct mc_info info = {0};
        get_mc_info(s, &info);

        if (s->mb_info.motion_type == DUAL_PRIME || s->mb_info.motion_backward) {
            // MB_FWD_IDX
            s->cur_mb_cfg_ptr[len++] = info.fwd_idx;
            // MB_BWD_IDX
            s->cur_mb_cfg_ptr[len++] = info.bwd_idx;
            // PRED_SIZE
            s->cur_mb_cfg_ptr[len++] = info.pred_size;
            // PRED_MODE
            s->cur_mb_cfg_ptr[len++] = info.pred_mode;

#ifdef MPEG12_DUMP_ENABLE
            fprintf(s->fp_mb_cfg_data, "MB_FWD_IDX: %08x\n", info.fwd_idx);
            fprintf(s->fp_mb_cfg_data, "MB_BWD_IDX: %08x\n", info.bwd_idx);
            fprintf(s->fp_mb_cfg_data, "PRED_SIZE: %08x\n", info.pred_size);
            fprintf(s->fp_mb_cfg_data, "PRED_MODE: %08x\n", info.pred_mode);
#endif

            // MB_REF_IDX
            for (int i = 0; i < 5; i++) {
                s->cur_mb_cfg_ptr[len++] = i;

#ifdef MPEG12_DUMP_ENABLE
                fprintf(s->fp_mb_cfg_data, "FRAME_IDX: %08x\n", i);
#endif
            }
            // FWD MV
            s->cur_mb_cfg_ptr[len++] = info.fwd_mv[0];
            s->cur_mb_cfg_ptr[len++] = info.fwd_mv[1];
            s->cur_mb_cfg_ptr[len++] = info.fwd_mv[2];
            s->cur_mb_cfg_ptr[len++] = info.fwd_mv[3];
            s->cur_mb_cfg_ptr[len++] = info.fwd_mv_c[0];
            s->cur_mb_cfg_ptr[len++] = info.fwd_mv_c[1];
            s->cur_mb_cfg_ptr[len++] = info.fwd_mv_c[2];
            s->cur_mb_cfg_ptr[len++] = info.fwd_mv_c[3];

            // BWD MV
            s->cur_mb_cfg_ptr[len++] = info.bwd_mv[0];
            s->cur_mb_cfg_ptr[len++] = info.bwd_mv[1];
            s->cur_mb_cfg_ptr[len++] = info.bwd_mv[2];
            s->cur_mb_cfg_ptr[len++] = info.bwd_mv[3];
            s->cur_mb_cfg_ptr[len++] = info.bwd_mv_c[0];
            s->cur_mb_cfg_ptr[len++] = info.bwd_mv_c[1];
            s->cur_mb_cfg_ptr[len++] = info.bwd_mv_c[2];
            s->cur_mb_cfg_ptr[len++] = info.bwd_mv_c[3];
            s->mb_cfg_info_list[s->cur_mblk_addr].len = len;

#ifdef MPEG12_DUMP_ENABLE
            fprintf(s->fp_mb_cfg_data, "FWD MV0: %08x\n", info.fwd_mv[0]);
            fprintf(s->fp_mb_cfg_data, "FWD MV2: %08x\n", info.fwd_mv[2]);
            fprintf(s->fp_mb_cfg_data, "BWD MV0: %08x\n", info.bwd_mv[0]);
            fprintf(s->fp_mb_cfg_data, "BWD MV2: %08x\n", info.bwd_mv[2]);
#endif

        } else {
            // MB_FWD_IDX
            s->cur_mb_cfg_ptr[len++] = info.fwd_idx;
            // PRED_SIZE
            s->cur_mb_cfg_ptr[len++] = info.pred_size;
            // PRED_MODE
            s->cur_mb_cfg_ptr[len++] = info.pred_mode;

#ifdef MPEG12_DUMP_ENABLE
            fprintf(s->fp_mb_cfg_data, "MB_FWD_IDX: %08x\n", info.fwd_idx);
            fprintf(s->fp_mb_cfg_data, "PRED_SIZE: %08x\n", info.pred_size);
            fprintf(s->fp_mb_cfg_data, "PRED_MODE: %08x\n", info.pred_mode);
#endif

            // MB_REF_IDX
            for (int i = 0; i < 5; i++) {
                s->cur_mb_cfg_ptr[len++] = i;

#ifdef MPEG12_DUMP_ENABLE
                fprintf(s->fp_mb_cfg_data, "FRAME_IDX: %08x\n", i);
#endif
            }
            // Y_MV
            s->cur_mb_cfg_ptr[len++] = info.fwd_mv[0];
            s->cur_mb_cfg_ptr[len++] = info.fwd_mv[1];
            s->cur_mb_cfg_ptr[len++] = info.fwd_mv[2];
            s->cur_mb_cfg_ptr[len++] = info.fwd_mv[3];
            // C_MV
            s->cur_mb_cfg_ptr[len++] = info.fwd_mv_c[0];
            s->cur_mb_cfg_ptr[len++] = info.fwd_mv_c[1];
            s->cur_mb_cfg_ptr[len++] = info.fwd_mv_c[2];
            s->cur_mb_cfg_ptr[len++] = info.fwd_mv_c[3];
            s->mb_cfg_info_list[s->cur_mblk_addr].len = len;

#ifdef MPEG12_DUMP_ENABLE
            fprintf(s->fp_mb_cfg_data, "FWD MV0: %08x\n", info.fwd_mv[0]);
            fprintf(s->fp_mb_cfg_data, "FWD MV2: %08x\n", info.fwd_mv[2]);
#endif
        }
    }
}

void config_header_info(struct mpeg12_dec_ctx *s)
{
    uint32_t *pVal;

    if (s->fp_reg)
        fprintf(s->fp_reg, "// config picture type\n");

    struct reg_avc_slice_type slice_type = {0};
    slice_type.bottom_field_flag = s->pic_code_extension.picture_structure == BOTTOM_FIELD;
    slice_type.field_pic_flag = s->pic_code_extension.picture_structure != FRAME;
    pVal = (uint32_t *)&slice_type;
    write_reg(s->regs_base + AVC_SLICE_TYPE_REG, *pVal, s->fp_reg);

    if (s->fp_reg)
        fprintf(s->fp_reg, "// config first mb_addr\n");
    write_reg(s->regs_base + AVC_FST_MB_ADDR_REG, 0x80000000, s->fp_reg);
}

static void config_mpeg_quant_matrix(struct mpeg12_dec_ctx *s)
{
    int i;
    uint32_t *pVal = (uint32_t *)(&g_mpeg_reg_list.mpeg_quant_matrix);

    if (s->fp_reg)
        fprintf(s->fp_reg, "// config intra quant matrix\n");
    g_mpeg_reg_list.mpeg_quant_matrix.mpeg_quant_matrix_access = 1;
    g_mpeg_reg_list.mpeg_quant_matrix.mpeg_quant_matrix_write = 1;
    for (i = 0; i < 64; i++) {
        g_mpeg_reg_list.mpeg_quant_matrix.quant_matrix_addr = i;
        g_mpeg_reg_list.mpeg_quant_matrix.quant_matrix_data = s->seq_header.intra_quantiser_matrix[i];

        write_reg(s->regs_base + MPEG_QUANT_MATRIX_REG, *pVal, s->fp_reg);
    }

    if (s->fp_reg)
        fprintf(s->fp_reg, "// config inter quant matrix\n");
    for (i = 0; i < 64; i++) {
        g_mpeg_reg_list.mpeg_quant_matrix.quant_matrix_addr = 64 + i;
        g_mpeg_reg_list.mpeg_quant_matrix.quant_matrix_data = s->seq_header.non_intra_quantiser_matrix[i];
        write_reg(s->regs_base + MPEG_QUANT_MATRIX_REG, *pVal, s->fp_reg);
    }
    if (s->fp_reg)
        fprintf(s->fp_reg, "// quant matrix release\n");
    write_reg(s->regs_base + MPEG_QUANT_MATRIX_REG, 0, s->fp_reg);
}

static void save_mb_info_data(struct mpeg12_dec_ctx *s)
{
    ve_buffer_sync(s->mb_cfg_phy_addr, CACHE_CLEAN);

    int total_mbs = s->mb_width * s->mb_height;
    int data_len = 0;

    if (s->pic_code_extension.picture_structure != FRAME)
        total_mbs /= 2;

    for (int i = 0; i < total_mbs; i++) {
        data_len += s->mb_cfg_info_list[i].len * 4;
        if (s->mb_cfg_info_list[i].len == 0) {
            loge("mb_addr: %d, mb_cfg_data is 0", i);
        }
    }

#ifdef MPEG12_DUMP_ENABLE
    unsigned char *data = (unsigned char *)s->mb_cfg_data;
    unsigned int addr = 0;
    char fPath[1024];
    int len = 0;
    snprintf(fPath, sizeof(fPath), "%s/load_data_%d.txt", s->ip_path, s->pic_num);
    FILE *fp = fopen(fPath, "w+");
    if (fp == NULL) {
        loge("Couldn't open nalu save file \n");
        return;
    }

    while (len < (data_len + 15) / 16 * 16) {
        fprintf(fp, "%08x: ", addr);
        for (int i = 0; i < 4; i++) {
            for (int j = 3; j >= 0; j--) {
                if (len + 4 * i + j >= data_len)
                    fprintf(fp, "ff");
                else
                    fprintf(fp, "%02x", data[len + 4 * i + j]);
            }
            fprintf(fp, " ");
        }
        fprintf(fp, "\n");

        len += 16;
        addr += 16;
    }

    fprintf(s->fp_reg, "// write mb info\n");
    fprintf(s->fp_reg, "write_mem %08x %08x load_data_%d.txt\n", s->mb_cfg_phy_addr->phy_addr, data_len, s->pic_num);

    // careful, we should 256 byte align for gsim
    int align_size = (data_len + 255) / 256 * 256;
    fprintf(s->fp_reg, "// pat_dram_load(0x%02x, 0x%x, 0x%x);\n", s->pic_num, s->mb_cfg_phy_addr->phy_addr, align_size);

    snprintf(fPath, sizeof(fPath), "%s/dram_load%02x.bin", s->asic_path, s->pic_num);
    FILE *fp_bin = fopen(fPath, "wb");
    if (fp_bin == NULL) {
        loge("Couldn't open nalu save file \n");
        return;
    }
    fwrite(data, 1, data_len, fp_bin);

    if (align_size != data_len) {
        logd("===================> data_len: %d, align_size: %d", data_len, align_size);
        unsigned char a[256] = {0};
        fwrite(a, 1, align_size - data_len, fp_bin);
    }
    fclose(fp_bin);

    snprintf(fPath, sizeof(fPath), "%s/tmp_log/input.txt", s->ip_path);
    FILE *fp_t = fopen(fPath, "wb");
    for (int i = 0; i < data_len / 4; i++) {
        fprintf(fp_t, "%08x,", s->mb_cfg_data[i]);
        if (i && (i % 10) == 0) {
            fprintf(fp_t, "\n");
        }
    }
    fclose(fp_t);
#endif
}

static void config_mb_info_data(struct mpeg12_dec_ctx *s)
{
    reg_mpeg_mb_info_cfg cfg = {0};
    uint32_t *pVal = (uint32_t *)&cfg;
    int data_len = 0;
    for (int i = 0; i < s->mb_width * s->mb_height; i++) {
        data_len += s->mb_cfg_info_list[i].len * 4;
    }

    cfg.byte_len = data_len;
    cfg.requst = 1;

    if (s->fp_reg)
        fprintf(s->fp_reg, "// config mb_info\n");

    write_reg(s->regs_base + MPEG_MB_CFG_BASE_ADDR_REG, s->mb_cfg_phy_addr->phy_addr, s->fp_reg);
    write_reg(s->regs_base + MPEG_MB_CFG_REG, *pVal, s->fp_reg);
}

static void config_frame_buffer(struct mpeg12_dec_ctx *s)
{
    uint32_t *pVal;

    if (s->fp_reg)
        fprintf(s->fp_reg, "// config cur frame idx\n");
    write_reg(s->regs_base + GLB_CUR_FRM_IDX_REG, s->cur_pic_idx, s->fp_reg);

    if (s->pic.picture_coding_type != MP2_I_PICTURE) {
        if (s->fp_reg)
            fprintf(s->fp_reg, "// config ref frame idx\n");

        pVal = (uint32_t *)&g_mpeg_reg_list.mpeg_ref_idx;
        g_mpeg_reg_list.mpeg_ref_idx.fwd_frm_idx = s->last_pic_idx;
        g_mpeg_reg_list.mpeg_ref_idx.bwd_frm_idx = s->next_pic_idx;
        write_reg(s->regs_base + MPEG_REF_BUF_IDX_REG, *pVal, s->fp_reg);
    }
}

static void config_picture_info_register(struct mpeg12_dec_ctx *s)
{
    if (s->fp_reg)
        fprintf(s->fp_reg, "// config picture info register\n");

    uint32_t *pVal;
    frame_reg_offset0 offset0;
    memset(&offset0, 0, sizeof(int));
    frame_reg_offset1 offset1;
    memset(&offset1, 0, sizeof(int));

    offset0.cbcr_interleaved = 0;
    offset0.frame_map = 0;
    offset0.color_mode = 0;            // YUV420

    for (int i = 0; i < FRAME_BUFFER_NUM; i++) {
        if (s->frame_buf[i].frame) {
            if (s->decoder.rotmir_flag && i == s->cur_rotmir_idx) {
                offset1.pic_xsize = s->rotmir_h_stride;
                offset1.pic_ysize = s->rotmir_v_stride;
                offset0.stride = s->rotmir_h_stride;
            } else {
                offset1.pic_xsize = s->mb_width * 16;
                offset1.pic_ysize = s->mb_height * 16;
                offset0.stride = s->mb_width * 16;
            }
            pVal = (uint32_t *)&offset0;
            write_reg(s->regs_base + PIC_INFO_START_REG + (5 * i) * 4, *pVal, s->fp_reg);
            pVal = (uint32_t *)&offset1;
            write_reg(s->regs_base + PIC_INFO_START_REG + (5 * i + 1) * 4, *pVal, s->fp_reg);
            write_reg(s->regs_base + PIC_INFO_START_REG + (5 * i + 2) * 4, s->frame_buf[i].frame->phy_addr[0], s->fp_reg);
            write_reg(s->regs_base + PIC_INFO_START_REG + (5 * i + 3) * 4, s->frame_buf[i].frame->phy_addr[1], s->fp_reg);
            write_reg(s->regs_base + PIC_INFO_START_REG + (5 * i + 4) * 4, s->frame_buf[i].frame->phy_addr[2], s->fp_reg);
        }
    }

    write_reg(s->regs_base + PIC_INFO_WRITE_END_REG, 0, s->fp_reg);
}

static void config_picture_size(struct mpeg12_dec_ctx *s)
{
    if (s->fp_reg)
        fprintf(s->fp_reg, "// config picture size\n");

    struct reg_glb_pic_size pic_size = {0};
    pic_size.pic_x_size = s->mb_width * 16;
    pic_size.pic_y_size = s->mb_height * 16;
    if (s->pic_code_extension.picture_structure != FRAME)
        pic_size.pic_y_size *= 2;

    uint32_t *pVal = (uint32_t *)&pic_size;
    write_reg(s->regs_base + GLB_PIC_SIZE_REG, *pVal, s->fp_reg);

    struct reg_glb_mb_size mb_size = {0};
    mb_size.pic_mb_x = s->mb_width - 1;
    mb_size.pic_mb_y = s->mb_height - 1;
    pVal = (uint32_t *)&mb_size;
    write_reg(s->regs_base + GLB_PIC_MB_SIZE_REG, *pVal, s->fp_reg);
}

static void config_dblk_register(struct mpeg12_dec_ctx *s)
{
    if (s->fp_reg)
        fprintf(s->fp_reg, "// config dblk pic type register\n");
    int pic_xsize = s->mb_width * 16;
    int pic_ysize = s->mb_height * 16;
    uint32_t *pVal;

    struct reg_pic_type pic_type = {0};
    memset(&pic_type, 0, sizeof(int));
    pic_type.mbaff = 0;
    pic_type.bottom_field_flag = s->pic_code_extension.picture_structure == BOTTOM_FIELD;
    pic_type.field = s->pic_code_extension.picture_structure != FRAME;
    pVal = (uint32_t *)&pic_type;
    write_reg(s->regs_base + DBLK_PIC_TYPE_REG, *pVal, s->fp_reg);

    struct reg_pic_size pic_size = {0};
    pic_size.pic_xsize = pic_xsize;
    pic_size.pic_ysize = pic_ysize;
    if (s->fp_reg)
        fprintf(s->fp_reg, "// config dblk pic size register\n");
    pVal = (uint32_t *)&pic_size;
    write_reg(s->regs_base + DBLK_PIC_SIZE_REG, *pVal, s->fp_reg);

    if (s->fp_reg)
        fprintf(s->fp_reg, "// config dblk en register\n");
    write_reg(s->regs_base + DBLK_EN_REG, 0, s->fp_reg);

    struct reg_dec_config dec_config = {0};
    // careful, 004_003_017_IP_32x32 chroma_420_type parse from bitstream is 0,
    // but the output is YUV420
    dec_config.dec_chroma_idc = 0;
    dec_config.dec_luma_only = 0;
    dec_config.dec_range_en = 0;
    dec_config.dec_wr_en = 1;

    dec_config.endian = 0;

    dec_config.uv_interleave = 0;
    dec_config.uv_alternative = 0;
    pVal = (uint32_t *)&dec_config;

    if (s->fp_reg)
        fprintf(s->fp_reg, "// config dblk dec register\n");
    write_reg(s->regs_base + DEC_CONFIG_REG, *pVal, s->fp_reg);
    write_reg(s->regs_base + DEC_FRAME_IDX_REG, s->cur_pic_idx, s->fp_reg);
}

static void config_pp_register(struct mpeg12_dec_ctx *s)
{
    int rotate = MPP_ROTATION_GET(s->decoder.rotmir_flag);
    int flip_v = MPP_FLIP_V_GET(s->decoder.rotmir_flag);
    int flip_h = MPP_FLIP_H_GET(s->decoder.rotmir_flag);
    struct reg_pp_ctrl pp_ctrl = {0};
    uint32_t pVal;

    if (s->fp_reg)
        fprintf(s->fp_reg, "// config pp frame idx\n");

    if (rotate != MPP_ROTATION_0) {
        pp_ctrl.rotmir_en = 1;
        if (rotate == MPP_ROTATION_0)
            pp_ctrl.rotate = 0;
        else if (rotate == MPP_ROTATION_90) // left rotate 90
            pp_ctrl.rotate = 1;
        else if (rotate == MPP_ROTATION_180)
            pp_ctrl.rotate = 2;
        else if (rotate == MPP_ROTATION_270)
            pp_ctrl.rotate = 3;
    }

    pp_ctrl.mirror = (flip_v ? 1 : 0) | (flip_h ? 2 : 0);

    pVal = *((uint32_t *)(&pp_ctrl));
    write_reg(s->regs_base + PP_CTRL_REG, pVal, s->fp_reg);
    if (s->decoder.rotmir_flag && s->cur_rotmir_idx >= 0) {
        write_reg(s->regs_base + PP_FRAME_IDX_REG, s->cur_rotmir_idx, s->fp_reg);
    } else {
        write_reg(s->regs_base + PP_FRAME_IDX_REG, s->cur_pic_idx, s->fp_reg);
    }
}

#include <time.h>
int64_t cur_time = 0;
extern int64_t g_time;
void ve_decode_slice(struct mpeg12_dec_ctx *s)
{
    unsigned int status;

    save_mb_info_data(s);

    if (s->first_pic == 0) {
        s->first_pic = 1;
        ve_reset();
    }

    config_vpu_reg(s);
    config_glb_reg(s, s->is_mpeg2 ? MPEG2_DEC : MPEG1_DEC);

    config_pic_init(s);
    config_picture_size(s);
    config_mb_info_data(s);

    // configure header info
    config_header_info(s);

    config_dblk_register(s);

    // configure quant
    config_mpeg_quant_matrix(s);

    config_picture_info_register(s);

    // config buffer (ref-frame and recon frame)
    config_frame_buffer(s);
    config_pp_register(s);

    //  start decode
    config_slice_start(s);

    if (ve_wait(&status) < 0) {
        loge("ve wait irq timeout");

        ve_reset();
    }

    if (status & (1 << 1)) {
        loge("decode error, status: %x", status);
    }

    ve_put_client();
}
