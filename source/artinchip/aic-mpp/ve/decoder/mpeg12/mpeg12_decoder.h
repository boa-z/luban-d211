/*
 * Copyright (C) 2020-2026 ArtInChip Technology Co. Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 *   author: <xiaodong.zhao@artinchip.com>
 *  Desc: mpeg decoder context
 *
 */
#ifndef _MPEG12_DECODER_H_
#define _MPEG12_DECODER_H_

#include "mpp_codec.h"
#include "mpp_dec_type.h"
#include "read_bits.h"
#include "ve_buffer.h"

// #define MPEG12_DUMP_ENABLE 1

#define FRAME_BUFFER_NUM      (5)

#define MB_INFO_SIZE          (384 + 64)

// motion type
#define EIGHT_BY_EIGHT_MC     0
#define FRAME_BASED           2
#define FIELD_BASED           1
#define DUAL_PRIME            3
#define SIXTEEN_BY_EIGHT_MC   2
#define MOTION_TYPE_NOT_EXIST 0xff
#define DCT_TYPE_NOT_EXIST    0xff

#define MP2_I_PICTURE         0x1
#define MP2_P_PICTURE         0x2
#define MP2_B_PICTURE         0x3
#define MP2_D_PICTURE         0x4

enum PIC_STRUCTURE {
    TOP_FIELD = 1,
    BOTTOM_FIELD,
    FRAME,
};

struct sequence_header {
    int horizontal_size; // 12bit
    int vertical_size;   // 12bit

    int aspect_ratio;
    int frame_rate;

    int bitrate;
    int vbv_buffer_size;

    int constrained_param;
    int load_intra_quantiser_matrix;
    int load_non_intra_quantiser_matrix;

    uint8_t intra_quantiser_matrix[64];
    uint8_t non_intra_quantiser_matrix[64];
};

struct gop {
    int time_code;   // 25bit
    int close_gop;   // 1bit
    int broken_link; // 1bit
};

struct picture {
    int temporal_reference;      // picture display order, 10bit
    int picture_coding_type;     // 001: I, 010: P, 011: B, 3bit
    int vbv_delay;               // 16bit
    int full_pel_forward_vector; // 1bit
    int forward_f_code;          // 3bit
    int full_pel_backward_vector;
    int backward_f_code;
};

struct slice_header {
    unsigned long slice_start_code;
    unsigned long vertical_position;
    unsigned long vertical_position_extension;
    unsigned long priority_breakpoint;
    unsigned long quantiser_scale;      // MPEG1
    unsigned long quantiser_scale_code; // MPEG2
    unsigned long intra_slice;
};

enum MV_FORMAT { MV_FORMAT_FRAME, MV_FORMAT_FIELD };

struct macroblock_info {
    unsigned long mb_type;
    unsigned long mb_quant;
    unsigned long motion_forward;
    unsigned long motion_backward;
    unsigned long mb_pattern;
    unsigned long mb_intra;
    unsigned long coded_block_pattern;
    unsigned long cbp;
    unsigned long macroblock_address_increment;
    unsigned int quantiser_scale;
    unsigned int quant_scale_code; // used for hw

    int predict_mode;

    // mpeg1
    // forward
    unsigned int motion_horizontal_forward_r;
    int motion_horizontal_forward_code;
    unsigned int motion_vertical_forward_r;
    int motion_vertical_forward_code;
    // backward
    int motion_horizontal_backward_code;
    unsigned int motion_horizontal_backward_r;
    int motion_vertical_backward_code;
    unsigned int motion_vertical_backward_r;

    int mv_x_for; // mv_x_fwd
    int mv_y_for; // mv_y_fwd
    int mv_x_bwd; // mv_x_bwd
    int mv_y_bwd; // mv_y_bwd
    int mv_x_for_prev;
    int mv_y_for_prev;
    int mv_x_bwd_prev;
    int mv_y_bwd_prev;

    long right_for_y;
    long down_for_y;
    long right_half_for_y;
    long down_half_for_y;
    long right_for_c;
    long down_for_c;
    long right_half_for_c;
    long down_half_for_c;
    long right_back_y;
    long down_back_y;
    long right_half_back_y;
    long down_half_back_y;
    long right_back_c;
    long down_back_c;
    long right_half_back_c;
    long down_half_back_c;

    // MPEG2
    unsigned long spatial_temporal_weight_code_flag;
    unsigned long spatial_temporal_weight_code_table_index;
    unsigned long spatial_temporal_weight_code;

    unsigned long motion_type;
    unsigned long dct_type;
    unsigned long quantiser_scale_code;
    unsigned long dmv;
    unsigned long mv_format;
    unsigned long motion_vector_count;

    // motion_vertical_field_select [r][s]
    // r: First/Second motion vector in Macroblock;
    // s: Forward/Backwards motion Vector
    unsigned long motion_vertical_field_select[2][2];

    // motion_code[r][s][t]
    // r: First/Second motion vector in Macroblock;
    // s: Forward/Backwards motion Vector
    // t: Horizontal/Vertical Component
    //    NOTE - r also takes the values 2 and 3 for derived motion vectors used with dualprime
    //    prediction.Since these motion vectors are derived they do not
    //    themselves have motion vector predictors.
    int motion_code[2][2][2];
    unsigned long motion_residual[2][2][2];
    long dmvector[2];
    int mv[4][2][2];
    int pmv[4][2][2];

    // reconstruct pixel for 6 blocks
    // 4 blocks for luma, 2 blocks for chrom
    short recon[6][64];
};

struct sequence_extension_info {
    unsigned long extension_start_code_identifier;
    unsigned long profile_and_level_indication;
    unsigned long progressive_sequence;
    unsigned long chroma_format;
    unsigned long horizontal_size_extension;
    unsigned long vertical_size_extension;
    unsigned long bit_rate_extension;
    unsigned long vbv_buffer_size_extension;
    unsigned long low_delay;
    unsigned long frame_rate_extension_n;
    unsigned long frame_rate_extension_d;
};

struct pic_coding_extension_info {
    unsigned long extension_start_code_identifier;
    unsigned long f_code[2][2];
    unsigned long intra_dc_precision;
    unsigned long picture_structure;
    unsigned long top_field_first;
    unsigned long frame_pred_frame_dct;

    unsigned long concealment_motion_vectors;
    unsigned long q_scale_type;
    unsigned long intra_vlc_format;
    unsigned long alternate_scan;
    unsigned long repeat_first_field;
    unsigned long chroma_420_type;
    unsigned long progressive_frame;
    unsigned long composite_display_flag;
    unsigned long v_axis;
    unsigned long field_sequence;
    unsigned long sub_carrier;
    unsigned long burst_amplitude;
    unsigned long sub_carrier_phase;
};

struct mpeg_frame_buffer_info {
    // uint8_t buffer[4000 * 3000 * 3 / 2];
    int display_count;
    int decoded_count;
    int is_ref; // is refrence frame
    struct frame *frame;
    // struct ve_buffer *phy_addr[3];
};

struct mb_config_info {
    uint32_t *ptr; // the mb info data ptr in s->mb_cfg_addr
    int len;       // the mb info length (including header)
};

struct mpeg12_dec_ctx {
    struct mpp_decoder decoder;
    unsigned long regs_base;

    struct ve_buffer_allocator *ve_buf_handle;
    struct packet *curr_packet;
    struct frame *curr_frame;
    enum mpp_pixel_format pix_format; // output pixel format
    int eos;

    int extra_frame_num;

    struct sequence_header seq_header;
    struct sequence_extension_info seq_extension;
    struct pic_coding_extension_info pic_code_extension;
    struct gop group_of_pic;
    struct picture pic;
    struct slice_header sh;
    struct read_bit_context gb;
    struct macroblock_info mb_info;

    int mb_width;
    int mb_height;
    int pic_num;

    int is_mpeg2;

    uint8_t *scan_table;

    int cur_mblk_addr;
    int prev_mblk_addr;
    int mb_cfg_addr;

    int last_dc[3];

    int mb_x;
    int mb_y;      // mb_y += 2, if it is field picture
    int rsim_mb_y; // for rsim, mb_y always mb_y += 1

    int gop_pic_num;
    int decoded_pic_num;
    int first_field;

    // it cannot be skipped mb if it is the first mb of slice
    int first_mb_slice;

    int last_temporal_reference;
    int update_pic;

    int display_num_base;
    int display_pic_num; // display_pic_num = display_num_base + pic.temporal_ref

    // mb_cfg_phy_addr: mb info used by hardware
    struct mb_config_info *mb_cfg_info_list; // all of the mb_infos in this picture
    struct ve_buffer *mb_cfg_phy_addr;       // physic addr of mb_cfg_buffer
    uint32_t *mb_cfg_data;                   // mb_cfg_data is mb_cfg_phy_addr virtual address
    uint32_t *cur_mb_cfg_ptr;
    // simple buffer manager, two ref picture
    struct mpeg_frame_buffer_info frame_buf[FRAME_BUFFER_NUM];
    int cur_pic_idx;  //
    int last_pic_idx; // forward ref idx
    int next_pic_idx; // backward ref idx
    int cur_rotmir_idx;
    int last_rotmir_idx;

    int picture_end;

    int max_width;
    int max_height;
    int first_pic;

    int need_get_frame;

    int h_offset;
    int v_offset;
    int rotmir_h_stride;
    int rotmir_v_stride;
    int rotmir_h_real_size;
    int rotmir_v_real_size;

    FILE *fp_reg;
#ifdef MPEG12_DUMP_ENABLE
    char ip_path[128];
    char asic_path[128];
    FILE *fp_es;
    FILE *fp_mb_cfg_data;
    FILE *fp_mb_coeff;
#endif
};

// api
int mpeg1_calc_mv(struct mpeg12_dec_ctx *s);
void mpeg2_update_qscale(struct mpeg12_dec_ctx *s);
void mpeg1_decode_skipped_mb(struct mpeg12_dec_ctx *s, int mb_idx);
void mpeg2_decode_skipped_mb(struct mpeg12_dec_ctx *s, int mb_idx);

// mpeg12_mv.cpp
int mpeg2_motion_vectors(struct mpeg12_dec_ctx *s, int n);

void ve_decode_slice(struct mpeg12_dec_ctx *s);
void write_mb_info_header(struct mpeg12_dec_ctx *s);
void config_mpeg12_mb_info(struct mpeg12_dec_ctx *s);
void config_mpeg12_mb_coeff(struct mpeg12_dec_ctx *s, int block_num);

#endif
