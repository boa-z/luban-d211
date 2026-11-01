/*
 * Copyright (C) 2020-2026 ArtInChip Technology Co. Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Author: <qi.xu@artinchip.com>
 * Desc: h264 encoder context
 */

#ifndef _H264_ENCODER_H_
#define _H264_ENCODER_H_

#include "mpp_codec.h"
#include "ve_buffer.h"
#include "mpp_dec_type.h"
#include "put_bits.h"
#include "h264_rc.h"

#define SPS_MAX_NUM 32
#define PPS_MAX_NUM 256

#define H264_ENC_DEBUG (1)

#define MB_COL_BUF_SIZE   (272*1024)
#define MB_NEIGH_BUF_SIZE (64*1024)
// NAL type
enum NAL_TYPE {
	NAL_TYPE_SLICE = 1,
	NAL_TYPE_DPA = 2,
	NAL_TYPE_DPB = 3,
	NAL_TYPE_DPC = 4,
	NAL_TYPE_IDR = 5,
	NAL_TYPE_SEI = 6,
	NAL_TYPE_SPS = 7,
	NAL_TYPE_PPS = 8,
	NAL_TYPE_AUD = 9,
	NAL_TYPE_EOSEQ = 10,
	NAL_TYPE_EOSTREAM = 11,
	NAL_TYPE_FILL = 12,
};

// slice type
#define H264_SLICE_P  0
#define H264_SLICE_B  1
#define H264_SLICE_I  2
#define H264_SLICE_SP 3
#define H264_SLICE_SI 4

struct h264_sps_info {
	u8 profile_idc;
	u8 level_idc;
	u8 constraint_set_flags;
	u8 chroma_format_idc;
	u8 log2_max_frame_num;
	u8 pic_order_cnt_type;
	u8 log2_max_pic_order_cnt_lsb;
	u8 max_num_ref_frames;
	u8 gaps_in_frame_num_value_allowed_flag;
	u8 frame_mbs_only_flag;
	u8 direct_8x8_inference_flag;
	u8 frame_cropping_flag;
	u8 vui_parameters_present_flag;

	int pic_width_in_mbs;
	int pic_height_in_mbs;
	int frame_cropping_rect_left_offset;
	int frame_cropping_rect_right_offset;
	int frame_cropping_rect_top_offset;
	int frame_cropping_rect_bottom_offset;
};

struct h264_pps_info {
	u8 entropy_coding_mode_flag;
	u8 bottom_field_pic_order_in_frame_present_flag;
	u8 weighted_pred_flag;
	u8 weighted_bipred_idc;
	u8 deblocking_filter_control_present_flag;
	u8 constrained_intra_pred_flag;
	u8 redundant_pic_cnt_present_flag;
	u8 transform_8x8_mode_flag;

	int pic_init_qp_minus26;
	int chroma_qp_index_offset;
	int second_chroma_qp_index_offset;
	int num_ref_idx_l0_default_active;
	int num_ref_idx_l1_default_active;
};

struct h264_slice_header {
	u32 first_mb_in_slice;
	u32 slice_type;
	u32 pps_id;
	int frame_num;
	int idr_pic_id;
	int poc_lsb;
	int qp_delta;
	int disable_deblocking_filter_idc;
	int dblk_alpha_offset_div2;
	int dblk_beta_offset_div2;
};

#define MAX_FRAME_NUM 2
struct enc_frame {
	struct ve_buffer *buf[3];
	struct ve_buffer *buf_subsample;
};

struct intra_params {
	int intra4x4_weight;
	int intra_weight;
};

struct me_params {
	int cime_en;
	int cime_search_range;
	int rime_search_range;
	int pmv_en;
	int cime_pmv_weight;
	int rime_pmv_weight;
	int fme_pmv_weight;
	int fme_skip_en;
	int fme_skip_weight;
	int lambda_en;

	int p16x8_weight;
	int p8x16_weight;
	int p8x8_weight;
};

struct h264_mb_rc_params {
	int mb_mad_threshold[6];
	// the coeff of mb line
	double *mb_line_coeff;
};

struct h264_enc_ctx {
	struct mpp_encoder encoder;
	unsigned long regs_base;

	struct h264_sps_info sps;
	struct h264_pps_info pps;
	struct h264_slice_header sh;

	struct ve_buffer_allocator *ve_buf_handle;

	int src_frame_idx; // it is fixed, src_frame_idx = MAX_FRAME_NUM
	int ref_frame_idx;
	int rec_frame_idx;
	struct enc_frame frames[MAX_FRAME_NUM];
	struct ve_buffer *dblk_y_buf;
	struct ve_buffer *dblk_c_buf;
	struct ve_buffer *mb_recon_buf;
	struct ve_buffer *mb_col_buf;
	struct ve_buffer *mb_neighbor_info_buf;
	struct ve_buffer *rc_mb_line_target_thres_buf;
	struct ve_buffer *rc_mb_line_mad_buf;
	int init;
	unsigned int src_frame_phy_addr[3];

	struct intra_params intra_params;
	struct me_params me_params;
	struct h264_mb_rc_params mb_rc_params;

	/* rate control */
	struct h264_rc_ctx rc_ctx;
	struct h264_rc_params rc_params_cfg;
	int rc_mode; /* 0: fix qp, 1: vbr, 2: cbr */
	unsigned int rc_frame_mad;

	int gop_frame_num;
	int width;
	int height;
	int mb_width;
	int mb_height;
	int frame_num;
	int idr_pic_id;
	int qp;
	int uv_alter;
	int uv_interleave;
	enum mpp_pixel_format pix_fmt;
};

// SPS/PPS encode functions
int h264_encode_sps(struct h264_enc_ctx *s, u8 *buf, int buf_size);
int h264_encode_pps(struct h264_enc_ctx *s, u8 *buf, int buf_size);

// Slice encode function (hardware implementation)
int h264_encode_slice(struct h264_enc_ctx *s, struct mpp_frame *frame,
	struct enc_packet *packet, int offset);

#endif
