/*
 * Copyright (C) 2020-2026 ArtInChip Technology Co. Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 *  author: <xiaodong.zhao@artinchip.com>
 *  Desc: mpeg1/2/4 register interface
 *
 */

#ifndef MPEG_REGISTER_H
#define MPEG_REGISTER_H

#include "ve_top_register.h"
#include "avc_register_v2.h"

enum CODEC_TYPE {
    AVC_DEC = 0,
    MPEG1_DEC,
    MPEG2_DEC,
    MPEG4_DEC,
};

typedef struct {
    // 0- I mb; 1- P mb; 2- B mb;
    unsigned mb_type : 2;
    unsigned last_mb : 1;
    unsigned cbp_num : 3;            // number of non-zero 8x8 blocks
    unsigned data_length_bytes : 12; // bytes
    unsigned mb_x : 7;
    unsigned mb_y : 7;
} mb_info_header;

typedef struct {
    unsigned fwd_frm_idx : 3;
    unsigned r0 : 1;
    unsigned bwd_frm_idx : 3;
    unsigned r1 : 25;
} reg_mpeg_ref_frm_idx;
#define MPEG_REF_BUF_IDX_REG (AVC_TOP_BASE_ADDR + 0x80)

typedef struct {
    // TODO
} reg_mpeg_mv;
#define MPEG_MV_REG (AVC_TOP_BASE_ADDR + 0x90)

typedef struct {
    unsigned top_field_first : 1;
    unsigned pic_structure : 2;
    unsigned r : 29;
} reg_mpeg_pic_type;
#define MPEG_PIC_TYPE_REG (AVC_TOP_BASE_ADDR + 0x94)

// ====> MB reg
typedef struct // test
{
    /*
    ref_idx_blkx:
    [5:1]: ref_frame_idx;
    [0]: top/bot field
    */
    unsigned bot_field_blk0 : 1;
    unsigned ref_idx_blk0 : 5;
    unsigned r0 : 2;
    unsigned bot_field_blk1 : 1;
    unsigned ref_idx_blk1 : 5;
    unsigned r1 : 2;
    unsigned bot_field_blk2 : 1;
    unsigned ref_idx_blk2 : 5;
    unsigned r2 : 2;
    unsigned bot_field_blk3 : 1;
    unsigned ref_idx_blk3 : 5;
    unsigned r3 : 2;
} reg_ref_idx;
#define MB_BWD_REF_IDX_REG (AVC_MB_BASE_ADDR + 0x04)
#define MB_FWD_REF_IDX_REG (AVC_MB_BASE_ADDR + 0x08)

typedef struct {
    /*
    0: 16x16
    1: 16x8
    2: 8x16
    3: 8x8
    4: 8x4
    5: 4x8
    6: 4x4
    */
    unsigned pred_size_blk0 : 3;
    unsigned r0 : 1;
    unsigned pred_size_blk1 : 3;
    unsigned r1 : 1;
    unsigned pred_size_blk2 : 3;
    unsigned r2 : 1;
    unsigned pred_size_blk3 : 3;
    unsigned r3 : 17;
} reg_pred_size;
#define MB_PRED_SIZE_REG (AVC_MB_BASE_ADDR + 0x0C)

typedef struct {
    // 1:fwd-pred; 2:bwd-pred; 3:bi-pred
    unsigned pred_mode_blk0 : 2;
    unsigned r0 : 2;
    unsigned pred_mode_blk1 : 2;
    unsigned r1 : 2;
    unsigned pred_mode_blk2 : 2;
    unsigned r2 : 2;
    unsigned pred_mode_blk3 : 2;
    unsigned r3 : 18;
} reg_pred_mode;
#define MB_PRED_MODE_REG (AVC_MB_BASE_ADDR + 0x10)

typedef struct {
    unsigned mv_y : 16;
    unsigned mv_x : 16;
} reg_mb_mv;
#define MB_FWD_MV0_REG (AVC_MB_BASE_ADDR + 0x20)
#define MB_FWD_MV1_REG (AVC_MB_BASE_ADDR + 0x24)
#define MB_FWD_MV2_REG (AVC_MB_BASE_ADDR + 0x28)
#define MB_FWD_MV3_REG (AVC_MB_BASE_ADDR + 0x2C)
#define MB_BWD_MV0_REG (AVC_MB_BASE_ADDR + 0x30)
#define MB_BWD_MV1_REG (AVC_MB_BASE_ADDR + 0x34)
#define MB_BWD_MV2_REG (AVC_MB_BASE_ADDR + 0x38)
#define MB_BWD_MV3_REG (AVC_MB_BASE_ADDR + 0x3C)

typedef struct {
    unsigned mb_cbp : 6;
    unsigned r0 : 5;

    // 0: frame based DCT; 1: field based DCT
    unsigned dct_type : 1;

    // 0:frame-pred; 1:field-pred
    unsigned mb_field_mc_flag : 1;
    unsigned mpeg_intra : 1;
    unsigned mpeg_skip : 1;
    unsigned mpeg_gmc : 1;
    unsigned r : 16;
} reg_mpeg_mb_type;
#define MPEG_MB_TYPE_REG (AVC_MB_BASE_ADDR + 0x44)

typedef struct {
    unsigned quant_scale_code : 5;
    unsigned r0 : 3;
    unsigned q_scale_type : 1;
    unsigned r : 23;
} reg_quant_param;
#define MPEG_QUANT_PARAM_REG (AVC_MB_BASE_ADDR + 0x48)

typedef struct {
    unsigned byte_len : 28;
    unsigned r : 3;
    unsigned requst : 1;
} reg_mpeg_mb_info_cfg;
#define MPEG_MB_CFG_REG               (AVC_MB_BASE_ADDR + 0xA0)
#define MPEG_MB_CFG_BASE_ADDR_REG     (AVC_MB_BASE_ADDR + 0xA4)
#define MPEG_MB_CFG_BASE_ADDR_EXT_REG (AVC_MB_BASE_ADDR + 0xA8)
// =======> TQ reg
typedef struct {
    unsigned tq_coeff_buf_data : 16;
    unsigned tq_coeff_buf_addr : 9;
    unsigned r : 5;
    unsigned tq_coeff_buf_wr : 1;
    unsigned tq_coeff_buf_access : 1;
} reg_tq_coeff;
#define TQ_COEFF_REG (AVC_TQ_BASE_ADDR + 0x00)

typedef struct {
    unsigned quant_matrix_data : 9;
    unsigned r0 : 7;
    unsigned quant_matrix_addr : 8;
    unsigned r1 : 6;
    unsigned mpeg_quant_matrix_write : 1;
    unsigned mpeg_quant_matrix_access : 1;
} reg_mpeg_quant_matrix;
// the same with AVC_QUANT_MATRIX_REG
#define MPEG_QUANT_MATRIX_REG (AVC_TQ_BASE_ADDR + 0x10)


static inline void write_reg(unsigned long reg_addr, uint32_t val, FILE *fp)
{
    write_reg_u32(reg_addr, val);

    if (fp) {
        fprintf(fp, "write_reg %08x %08x\n", (uint32_t)reg_addr, val);
    }
}

static inline uint32_t read_reg(unsigned long reg_addr)
{
    return read_reg_u32(reg_addr);
}

#ifdef MPEG4_DECODER
void ve_mpeg4_decode_slice(void);
#endif

#endif
