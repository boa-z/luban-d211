/*
 * Copyright (C) 2020-2026 ArtInChip Technology Co. Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Author: <qi.xu@artinchip.com>
 * Desc: h264 encoder interface
 */

#define LOG_TAG "h264_encoder"
#include <stdlib.h>
#include <string.h>

#include "h264_encoder.h"
#include "ve.h"
#include "mpp_mem.h"
#include "mpp_log.h"
#include "put_bits.h"
#include "mpp_encoder.h"
#include "h264_quality_metrics.h"

// H.264 profile and level
#define H264_PROFILE_BASELINE 66
#define H264_PROFILE_MAIN	 77
#define H264_PROFILE_HIGH	 100
#define H264_LEVEL_3_0		30
#define H264_LEVEL_3_1		31
#define H264_LEVEL_4_0		40
#define H264_LEVEL_4_1		41

static void write_startcode(u8 *buf)
{
	buf[0] = 0x00;
	buf[1] = 0x00;
	buf[2] = 0x00;
	buf[3] = 0x01;
}

// Exp-Golomb encoding for unsigned values (ue(v))
static void put_ue_golomb(struct put_bit_ctx *pb, int val)
{
	int len = 0;
	int tmp = val + 1;

	// calculate the length of the code
	while (tmp >>= 1)
		len++;

	// write leading zeros
	put_bits(pb, len, 0);

	// write the value + 1
	put_bits(pb, len + 1, val + 1);
}

// Exp-Golomb encoding for signed values (se(v))
static void put_se_golomb(struct put_bit_ctx *pb, int val)
{
	int uev;

	if (val <= 0)
		uev = -val * 2;
	else
		uev = val * 2 - 1;

	put_ue_golomb(pb, uev);
}

static int h264_mb_rc_params_init(struct h264_enc_ctx *s)
{
	s->mb_rc_params.mb_mad_threshold[0] = 60;
	s->mb_rc_params.mb_mad_threshold[1] = 100;
	s->mb_rc_params.mb_mad_threshold[2] = 140;
	s->mb_rc_params.mb_mad_threshold[3] = 180;
	s->mb_rc_params.mb_mad_threshold[4] = 200;
	s->mb_rc_params.mb_mad_threshold[5] = 230;

	s->mb_rc_params.mb_line_coeff = (double*)mpp_alloc(s->mb_height*sizeof(double));
	if (s->mb_rc_params.mb_line_coeff == NULL) {
		loge("Failed to alloc mb_rc_params.mb_line_coeff");
		return -1;
	}
	return 0;
}

static void h264_mb_rc_params_deinit(struct h264_enc_ctx *s)
{
	if (s->mb_rc_params.mb_line_coeff) {
		mpp_free(s->mb_rc_params.mb_line_coeff);
		s->mb_rc_params.mb_line_coeff = NULL;
	}
}

static void h264_mb_rc_start_frame(struct h264_enc_ctx *s)
{
	unsigned int *mb_line_target_buffer = (unsigned int*)s->rc_mb_line_target_thres_buf->vir_addr;
	unsigned int target_bits = s->rc_ctx.target_bits_per_frame;
	double mb_line_target_bits;
	int i;

	for (i=0; i<s->mb_height; i++) {
		mb_line_target_bits = s->mb_rc_params.mb_line_coeff[i] * target_bits;
		mb_line_target_buffer[6*i + 0] = (unsigned int)(mb_line_target_bits * 0.4f);
		mb_line_target_buffer[6*i + 1] = (unsigned int)(mb_line_target_bits * 0.6f);
		mb_line_target_buffer[6*i + 2] = (unsigned int)(mb_line_target_bits * 0.8f);
		mb_line_target_buffer[6*i + 3] = (unsigned int)(mb_line_target_bits * 1.2f);
		mb_line_target_buffer[6*i + 4] = (unsigned int)(mb_line_target_bits * 1.4f);
		mb_line_target_buffer[6*i + 5] = (unsigned int)(mb_line_target_bits * 1.6f);
	}
	ve_buffer_sync(s->rc_mb_line_target_thres_buf, CACHE_FLUSH);
}

static int h264_mb_rc_end_frame(struct h264_enc_ctx *s)
{
	unsigned short *ptr = NULL;
	int frame_mad_sum = 0;
	int mb_mad = s->rc_frame_mad / (s->mb_width * s->mb_height);
	int i;
	s->mb_rc_params.mb_mad_threshold[0] = 0.4 * mb_mad;
	s->mb_rc_params.mb_mad_threshold[1] = 0.6 * mb_mad;
	s->mb_rc_params.mb_mad_threshold[2] = 0.8 * mb_mad;
	s->mb_rc_params.mb_mad_threshold[3] = 1.2 * mb_mad;
	if (s->mb_rc_params.mb_mad_threshold[3] > 252)
		s->mb_rc_params.mb_mad_threshold[3] = 252;
	s->mb_rc_params.mb_mad_threshold[4] = 1.6 * mb_mad;
	if (s->mb_rc_params.mb_mad_threshold[4] > 253)
		s->mb_rc_params.mb_mad_threshold[4] = 253;
	s->mb_rc_params.mb_mad_threshold[5] = 2.0 * mb_mad;
	if (s->mb_rc_params.mb_mad_threshold[5] > 254)
		s->mb_rc_params.mb_mad_threshold[5] = 254;

	ve_buffer_sync(s->rc_mb_line_mad_buf, CACHE_INVALID);
	ptr = (unsigned short*)s->rc_mb_line_mad_buf->vir_addr;
	for (i=0; i<s->mb_height; i++) {
		frame_mad_sum += ptr[i];
	}

	for (i=0; i<s->mb_height; i++) {
		s->mb_rc_params.mb_line_coeff[i] = (double)ptr[i] / (double)frame_mad_sum;
	}

	return 0;
}

int h264_encode_sps(struct h264_enc_ctx *s, u8 *buf, int buf_size)
{
	struct put_bit_ctx pb;
	u8 *data = buf + 4; // skip startcode
	int max_size = buf_size - 4;

	write_startcode(buf);

	init_put_bits(&pb, data, max_size);

	// nal_unit_type = 7 (SPS)
	put_bits(&pb, 8, 0x67); // nal_ref_idc = 3, nal_unit_type = 7

	// profile_idc
	put_bits(&pb, 8, s->sps.profile_idc);

	// constraint_set flags + reserved_zero_5bits
	put_bits(&pb, 8, s->sps.constraint_set_flags);

	// level_idc
	put_bits(&pb, 8, s->sps.level_idc);

	// seq_parameter_set_id (ue(v))
	put_ue_golomb(&pb, 0);

	// log2_max_frame_num_minus4 (ue(v))
	put_ue_golomb(&pb, s->sps.log2_max_frame_num - 4);

	// pic_order_cnt_type (ue(v))
	put_ue_golomb(&pb, 2);

	// max_num_ref_frames (ue(v))
	put_ue_golomb(&pb, 1);

	// gaps_in_frame_num_value_allowed_flag
	put_bits(&pb, 1, 0);

	// pic_width_in_mbs_minus1 (ue(v))
	put_ue_golomb(&pb, s->sps.pic_width_in_mbs - 1);

	// pic_height_in_map_units_minus1 (ue(v))
	put_ue_golomb(&pb, s->sps.pic_height_in_mbs - 1);

	// frame_mbs_only_flag
	put_bits(&pb, 1, 1);

	// direct_8x8_inference_flag
	put_bits(&pb, 1, s->sps.direct_8x8_inference_flag);

	// frame_cropping_flag
	put_bits(&pb, 1, 0);

	// vui_parameters_present_flag
	put_bits(&pb, 1, 0);

	// rbsp_trailing_bits
	put_bits(&pb, 1, 1);
	put_bits(&pb, 7, 0);

	flush_put_bits(&pb);

	int size = put_bits_count(&pb) / 8;

	return size + 4;
}

int h264_encode_pps(struct h264_enc_ctx *s, u8 *buf, int buf_size)
{
	struct put_bit_ctx pb;
	u8 *data = buf + 4; // skip startcode
	int max_size = buf_size - 4;

	write_startcode(buf);
	init_put_bits(&pb, data, max_size);

	// nal_unit_type = 8 (PPS)
	put_bits(&pb, 8, 0x68); // nal_ref_idc = 3, nal_unit_type = 8

	// pic_parameter_set_id (ue(v))
	put_ue_golomb(&pb, 0);

	// seq_parameter_set_id (ue(v))
	put_ue_golomb(&pb, 0);

	// entropy_coding_mode_flag
	put_bits(&pb, 1, s->pps.entropy_coding_mode_flag);

	// bottom_field_pic_order_in_frame_present_flag
	put_bits(&pb, 1, 0);

	// num_slice_groups_minus1 (ue(v))
	put_ue_golomb(&pb, 0);

	// num_ref_idx_l0_default_active_minus1 (ue(v))
	put_ue_golomb(&pb, 0);

	// num_ref_idx_l1_default_active_minus1 (ue(v))
	put_ue_golomb(&pb, 0);

	// weighted_pred_flag
	put_bits(&pb, 1, 0);

	// weighted_bipred_idc
	put_bits(&pb, 2, 0);

	// pic_init_qp_minus26 (se(v)), default 0
	put_se_golomb(&pb, s->pps.pic_init_qp_minus26);

	// pic_init_qs_minus26 (se(v))
	put_se_golomb(&pb, 0);

	// chroma_qp_index_offset (se(v))
	put_se_golomb(&pb, s->pps.chroma_qp_index_offset);

	// deblocking_filter_control_present_flag
	put_bits(&pb, 1, s->pps.deblocking_filter_control_present_flag);

	// constrained_intra_pred_flag
	put_bits(&pb, 1, s->pps.constrained_intra_pred_flag);

	// redundant_pic_cnt_present_flag
	put_bits(&pb, 1, 0);

	// rbsp_trailing_bits
	put_bits(&pb, 1, 1);
	put_bits(&pb, 7, 0);

	flush_put_bits(&pb);

	int size = put_bits_count(&pb) / 8;


	return size + 4;
}

// Slice encode function is implemented in h264_enc_hal.c

static int __h264_encode_init(struct mpp_encoder *ctx, struct encode_config *config)
{
	logi("__h264_encode_init");
	struct h264_enc_ctx *s = (struct h264_enc_ctx *)ctx;
	struct enc_packet_manager_init_cfg pm_cfg;

	s->ve_buf_handle = ve_buffer_allocator_create(VE_BUFFER_TYPE_DMA);
	if (!s->ve_buf_handle) {
		loge("Failed to create ve buffer allocator");
		return -1;
	}

	pm_cfg.ve_buf_handle = s->ve_buf_handle;
	pm_cfg.buffer_size = config->packet_buffer_size;
	pm_cfg.packet_count = 4;
	s->encoder.pm = enc_pm_create(&pm_cfg);
	if (!s->encoder.pm) {
		loge("pm create failed");
		ve_buffer_allocator_destroy(s->ve_buf_handle);
		return -1;
	}

	s->qp = 24;
	s->frame_num = 0;
	s->idr_pic_id = 0;
	s->gop_frame_num = 30;
	s->rc_mode = H264_RC_MODE_FIX_QP; /* default: fix qp mode */

	// init SPS
	s->sps.profile_idc = H264_PROFILE_BASELINE;
	s->sps.level_idc = H264_LEVEL_4_0;
	s->sps.constraint_set_flags = 0;
	s->sps.chroma_format_idc = 1; // 4:2:0
	s->sps.log2_max_frame_num = 5;
	s->sps.pic_order_cnt_type = 2;
	s->sps.log2_max_pic_order_cnt_lsb = 4;
	s->sps.max_num_ref_frames = 1;
	s->sps.gaps_in_frame_num_value_allowed_flag = 0;
	s->sps.frame_mbs_only_flag = 1;
	s->sps.direct_8x8_inference_flag = 0;
	s->sps.frame_cropping_flag = 0;
	s->sps.vui_parameters_present_flag = 0;
	s->sps.pic_width_in_mbs = (s->width + 15) / 16;
	s->sps.pic_height_in_mbs = (s->height + 15) / 16;

	// init PPS
	s->pps.entropy_coding_mode_flag = 0; // CAVLC
	s->pps.bottom_field_pic_order_in_frame_present_flag = 0;
	s->pps.weighted_pred_flag = 0;
	s->pps.weighted_bipred_idc = 0;
	s->pps.deblocking_filter_control_present_flag = 1;
	s->pps.constrained_intra_pred_flag = 0;
	s->pps.redundant_pic_cnt_present_flag = 0;
	s->pps.transform_8x8_mode_flag = 0;
	s->pps.pic_init_qp_minus26 = 0;
	s->pps.chroma_qp_index_offset = 0;
	s->pps.second_chroma_qp_index_offset = 0;
	s->pps.num_ref_idx_l0_default_active = 1;
	s->pps.num_ref_idx_l1_default_active = 1;

	s->sh.disable_deblocking_filter_idc = 0;
	s->sh.dblk_alpha_offset_div2 = 6;
	s->sh.dblk_beta_offset_div2 = 2;

	s->intra_params.intra4x4_weight = 512;
	s->intra_params.intra_weight = 512;

	s->me_params.cime_en = 1;
	s->me_params.cime_search_range = 0;
	s->me_params.pmv_en = 1;
	s->me_params.fme_skip_en = 1;
	s->me_params.rime_pmv_weight = 20;
	s->me_params.cime_pmv_weight = 20;
	s->me_params.fme_pmv_weight = 20;
	s->me_params.fme_skip_weight = 20;
	s->me_params.lambda_en = 1;
	s->me_params.p16x8_weight = 16;
	s->me_params.p8x16_weight = 32;
	s->me_params.p8x8_weight = 64;

	logi("H264 encoder init: %dx%d, qp=%d", s->width, s->height, s->qp);

	return 0;
}

static void free_phy_buf(struct h264_enc_ctx *s)
{
	int i;
	for (i=0; i<MAX_FRAME_NUM; i++) {
		if (s->frames[i].buf[0])
			ve_buffer_free(s->ve_buf_handle, s->frames[i].buf[0]);
		if (s->frames[i].buf[1])
			ve_buffer_free(s->ve_buf_handle, s->frames[i].buf[1]);
		if (s->frames[i].buf[2])
			ve_buffer_free(s->ve_buf_handle, s->frames[i].buf[2]);
		if (s->frames[i].buf_subsample)
			ve_buffer_free(s->ve_buf_handle, s->frames[i].buf_subsample);
	}

	if (s->dblk_y_buf)
		ve_buffer_free(s->ve_buf_handle, s->dblk_y_buf);
	if (s->dblk_c_buf)
		ve_buffer_free(s->ve_buf_handle, s->dblk_c_buf);
	if (s->mb_col_buf)
		ve_buffer_free(s->ve_buf_handle, s->mb_col_buf);
	if (s->mb_recon_buf)
		ve_buffer_free(s->ve_buf_handle, s->mb_recon_buf);
	if (s->mb_neighbor_info_buf)
		ve_buffer_free(s->ve_buf_handle, s->mb_neighbor_info_buf);
	if (s->rc_mb_line_mad_buf)
		ve_buffer_free(s->ve_buf_handle, s->rc_mb_line_mad_buf);
	if (s->rc_mb_line_target_thres_buf)
		ve_buffer_free(s->ve_buf_handle, s->rc_mb_line_target_thres_buf);
}

static int alloc_phy_buf(struct h264_enc_ctx *s)
{
	int i = 0, j = 0;
	int mb_width = (s->width + 15) / 16;
	int mb_height = (s->height + 15) / 16;
	int subsample_size = (s->width + 31) / 32 * 8 * s->height / 2;
	int y_size = mb_width * 16 * mb_height * 16;
	int size[3] = {y_size, y_size / 4, y_size / 4};
	int comp = 3;
	int alloc_flag = 0;
	if (s->pix_fmt == MPP_FMT_NV12 || s->pix_fmt == MPP_FMT_NV21) {
		comp = 2;
		size[1] = y_size / 2;
	}

#ifdef H264_ENABLE_QUALITY_METRICS
	alloc_flag = ALLOC_NEED_VIR_ADDR;
#endif
	for (i=0; i<MAX_FRAME_NUM; i++) {
		for (j=0; j<comp; j++) {
			s->frames[i].buf[j] = ve_buffer_alloc(s->ve_buf_handle, size[j], alloc_flag);
			if (s->frames[i].buf[j] == NULL) {
				loge("Failed to alloc ve buffer");
				return -1;
			}
		}
		s->frames[i].buf_subsample = ve_buffer_alloc(s->ve_buf_handle, subsample_size, alloc_flag);
		if (s->frames[i].buf_subsample == NULL) {
			loge("Failed to alloc ve buffer");
			return -1;
		}
	}
	s->rec_frame_idx = s->ref_frame_idx = 0;
	s->src_frame_idx = MAX_FRAME_NUM;

	s->dblk_y_buf = ve_buffer_alloc(s->ve_buf_handle, mb_width*16*4, 0);
	if (s->dblk_y_buf == NULL) {
		loge("Failed to alloc ve buffer");
		return -1;
	}
	s->dblk_c_buf = ve_buffer_alloc(s->ve_buf_handle, mb_width*16*4, 0);
	if (s->dblk_c_buf == NULL) {
		loge("Failed to alloc ve buffer");
		return -1;
	}

	s->mb_col_buf = ve_buffer_alloc(s->ve_buf_handle, MB_COL_BUF_SIZE, 0);
	if (s->mb_col_buf == NULL) {
		loge("Failed to alloc ve buffer");
		return -1;
	}

	s->mb_neighbor_info_buf = ve_buffer_alloc(s->ve_buf_handle, MB_NEIGH_BUF_SIZE, 0);
	if (s->mb_neighbor_info_buf == NULL) {
		loge("Failed to alloc ve buffer");
		return -1;
	}

	s->mb_recon_buf = ve_buffer_alloc(s->ve_buf_handle, mb_width*32, 0);
	if (s->mb_recon_buf == NULL) {
		loge("Failed to alloc ve buffer");
		return -1;
	}

	s->rc_mb_line_mad_buf = ve_buffer_alloc(s->ve_buf_handle, mb_height * sizeof(int), ALLOC_NEED_VIR_ADDR);
	if (s->rc_mb_line_mad_buf == NULL) {
		loge("Failed to alloc ve buffer");
		return -1;
	}

	s->rc_mb_line_target_thres_buf = ve_buffer_alloc(s->ve_buf_handle, mb_height * sizeof(int) * 6, ALLOC_NEED_VIR_ADDR);
	if (s->rc_mb_line_target_thres_buf == NULL) {
		loge("Failed to alloc ve buffer");
		return -1;
	}
	return 0;
}

static int h264_enc_setup_frame(struct h264_enc_ctx *s, struct mpp_frame *frame)
{
	s->width = frame->buf.size.width;
	s->height = frame->buf.size.height;
	s->mb_width = (s->width + 15) / 16;
	s->mb_height = (s->height + 15) / 16;
	s->sps.pic_width_in_mbs = s->mb_width;
	s->sps.pic_height_in_mbs = s->mb_height;

	s->pix_fmt = frame->buf.format;
	switch (frame->buf.format) {
	case MPP_FMT_YUV420P:
		s->uv_alter = 0;
		s->uv_interleave = 0;
		break;
	case MPP_FMT_NV12:
		s->uv_alter = 0;
		s->uv_interleave = 1;
		break;
	case MPP_FMT_NV21:
		s->uv_alter = 1;
		s->uv_interleave = 1;
		break;
	default:
		loge("Unsupported pixel format: %d", frame->buf.format);
		return -1;
	}

	logd("frame: %dx%d, format: %d, uv_alter: %d, uv_interleave: %d",
		s->width, s->height, s->pix_fmt, s->uv_alter, s->uv_interleave);
	return 0;
}

static int h264_enc_init_first_frame(struct h264_enc_ctx *s)
{
	if (alloc_phy_buf(s))
		return -1;

	if (s->rc_mode != H264_RC_MODE_FIX_QP) {
		s->rc_params_cfg.width = s->width;
		s->rc_params_cfg.height = s->height;
		h264_rc_init(&s->rc_ctx, &s->rc_params_cfg);
		h264_mb_rc_params_init(s);
		logi("Rate control initialized: mode=%d, target_bps=%d, enabled=%d",
			s->rc_mode, s->rc_params_cfg.target_bps, s->rc_ctx.enabled);
	}
	s->init = 1;
	return 0;
}

static int h264_enc_write_sps_pps(struct h264_enc_ctx *s, struct enc_packet *packet)
{
	int ret;
	int offset = 0;
	u8 *buf = packet->data;
	int buf_size = packet->size;

	ret = h264_encode_sps(s, buf + offset, buf_size - offset);
	if (ret < 0) {
		loge("Failed to encode SPS");
		return ret;
	}
	offset += ret;

	ret = h264_encode_pps(s, buf + offset, buf_size - offset);
	if (ret < 0) {
		loge("Failed to encode PPS");
		return ret;
	}
	offset += ret;
	offset = (offset + 7) / 8 * 8;

	enc_packet_flush_cache(s->encoder.pm, packet->data, offset, CACHE_FLUSH);
	return offset;
}

static void h264_enc_rc_update(struct h264_enc_ctx *s, int ret)
{
	if (s->rc_mode != H264_RC_MODE_FIX_QP && h264_rc_is_enabled(&s->rc_ctx)) {
		int frame_bits = ret * 8;
		int mad = s->rc_frame_mad;
		h264_rc_end_frame(&s->rc_ctx, frame_bits, mad);
		h264_mb_rc_end_frame(s);
	}
}

#ifdef H264_ENABLE_QUALITY_METRICS
static void h264_enc_quality_metrics(struct h264_enc_ctx *s, struct mpp_frame *frame)
{
	unsigned char *orig_buf[3] = {NULL};
	unsigned char *recon_buf[3] = {NULL};
	double psnr_y = 0.0, psnr_u = 0.0, psnr_v = 0.0, psnr_avg = 0.0;
	double ssim = 0.0;
	int y_size = s->width * s->height;
	int uv_size = y_size / 4;

	if (frame->buf.buf_type != MPP_PHY_ADDR) {
		dmabuf_sync(frame->buf.fd[0], CACHE_INVALID);
		if (s->pix_fmt == MPP_FMT_YUV420P) {
			dmabuf_sync(frame->buf.fd[1], CACHE_INVALID);
			dmabuf_sync(frame->buf.fd[2], CACHE_INVALID);
		} else {
			dmabuf_sync(frame->buf.fd[1], CACHE_INVALID);
		}
	}

	ve_buffer_sync(s->frames[s->rec_frame_idx].buf[0], CACHE_INVALID);
	ve_buffer_sync(s->frames[s->rec_frame_idx].buf[1], CACHE_INVALID);
	ve_buffer_sync(s->frames[s->rec_frame_idx].buf[2], CACHE_INVALID);

	if (frame->buf.buf_type == MPP_PHY_ADDR) {
		orig_buf[0] = (unsigned char *)(uintptr_t)frame->buf.phy_addr[0];
		orig_buf[1] = (unsigned char *)(uintptr_t)frame->buf.phy_addr[1];
		orig_buf[2] = (unsigned char *)(uintptr_t)frame->buf.phy_addr[2];
	} else {
		orig_buf[0] = dmabuf_mmap(frame->buf.fd[0], y_size);
		if (s->pix_fmt == MPP_FMT_YUV420P) {
			orig_buf[1] = dmabuf_mmap(frame->buf.fd[1], uv_size);
			orig_buf[2] = dmabuf_mmap(frame->buf.fd[2], uv_size);
		} else {
			orig_buf[1] = dmabuf_mmap(frame->buf.fd[1], y_size / 2);
			orig_buf[2] = NULL;
		}
	}

	recon_buf[0] = s->frames[s->rec_frame_idx].buf[0]->vir_addr;
	recon_buf[1] = s->frames[s->rec_frame_idx].buf[1]->vir_addr;
	recon_buf[2] = s->frames[s->rec_frame_idx].buf[2]->vir_addr;

	logi("orig_buf[0]: %p, recon_buf[0]: %p", orig_buf[0], recon_buf[0]);
	if (orig_buf[0] && recon_buf[0]) {
		calculate_psnr(orig_buf, recon_buf, s->width, s->height,
				s->uv_interleave, &psnr_y, &psnr_u, &psnr_v, &psnr_avg);
		calculate_ssim(orig_buf, recon_buf, s->width, s->height, &ssim);
		print_quality_metrics(s->rc_ctx.frame_count, psnr_y, psnr_u,
					    psnr_v, psnr_avg, ssim);
	}

	if (frame->buf.buf_type != MPP_PHY_ADDR) {
		if (orig_buf[0])
			dmabuf_munmap(orig_buf[0], y_size);
		if (orig_buf[1]) {
			int unmap_size = (s->pix_fmt == MPP_FMT_YUV420P) ? uv_size : y_size / 2;
			dmabuf_munmap(orig_buf[1], unmap_size);
		}
		if (orig_buf[2])
			dmabuf_munmap(orig_buf[2], uv_size);
	}
}
#endif

static int __h264_encode_frame(struct mpp_encoder *ctx, struct mpp_frame *frame, struct enc_packet *packet)
{
	struct h264_enc_ctx *s = (struct h264_enc_ctx *)ctx;
	int ret;
	int offset = 0;
	int slice_type;
	int rc_qp = -1;

	logd("__h264_encode_frame");

	ret = h264_enc_setup_frame(s, frame);
	if (ret < 0)
		return ret;

	if (s->init == 0) {
		ret = h264_enc_init_first_frame(s);
		if (ret < 0)
			return ret;
	}

	slice_type = (s->frame_num == 0) ? H264_SLICE_I : H264_SLICE_P;
	s->sh.slice_type = slice_type;

	if (s->rc_mode != H264_RC_MODE_FIX_QP && h264_rc_is_enabled(&s->rc_ctx)) {
		rc_qp = h264_rc_start_frame(&s->rc_ctx, slice_type);
		if (rc_qp >= 0) {
			s->qp = rc_qp;
			logd("RC: frame %d, slice_type=%s, qp=%d",
				s->rc_ctx.frame_count,
				slice_type == H264_SLICE_I ? "I" : "P",
				s->qp);
		}
		h264_mb_rc_start_frame(s);
	}

	/* At high QP, reference frames are heavily distorted, which makes
	 * ME more expensive (larger MVs, fewer skips). Bias towards skip
	 * and coarser partitions to keep bitrate under control.
	 */
	if (s->qp >= 46 && s->sh.slice_type == H264_SLICE_P) {
		s->me_params.fme_skip_weight = 80;
		s->me_params.p8x8_weight = 128;
		s->me_params.p8x16_weight = 64;
		s->me_params.p16x8_weight = 32;
		s->intra_params.intra4x4_weight = 5120;
		s->intra_params.intra_weight = 2048;
	} else {
		s->me_params.fme_skip_weight = 20;
		s->me_params.p8x8_weight = 64;
		s->me_params.p8x16_weight = 32;
		s->me_params.p16x8_weight = 16;
	}

	if (s->frame_num == 0) {
		offset = h264_enc_write_sps_pps(s, packet);
		if (offset < 0)
			return offset;
	}

	ret = h264_encode_slice(s, frame, packet, offset);
	if (ret < 0) {
		loge("Failed to encode slice");
		return ret;
	}

	h264_enc_rc_update(s, ret);

#ifdef H264_ENABLE_QUALITY_METRICS
	h264_enc_quality_metrics(s, frame);
#endif

	s->frame_num++;
	packet->len = offset + ret;
	packet->pts = frame->pts;

	if (s->frame_num == s->gop_frame_num)
		s->frame_num = 0;

	s->ref_frame_idx = s->rec_frame_idx;
	s->rec_frame_idx = (s->rec_frame_idx + 1) % MAX_FRAME_NUM;
	logd("__h264_encode_frame end, size=%zu", packet->len);
	return 0;
}

static int __h264_encode_destroy(struct mpp_encoder *ctx)
{
	struct h264_enc_ctx *s = (struct h264_enc_ctx *)ctx;

	h264_mb_rc_params_deinit(s);
	free_phy_buf(s);
	if (s->ve_buf_handle)
		ve_buffer_allocator_destroy(s->ve_buf_handle);

	ve_close_device();
	mpp_free(s);

	return 0;
}

static int __h264_encode_get_parameter(struct mpp_encoder *ctx, enum mpp_enc_cmd_type cmd, void *param)
{
	struct h264_enc_ctx *s = (struct h264_enc_ctx *)ctx;

	switch (cmd) {
	case MPP_ENC_GET_QP:
		*(int *)param = s->qp;
		break;
	default:
		logw("Unknown command: %d", cmd);
		return -1;
	}

	return 0;
}

static int __h264_encode_set_parameter(struct mpp_encoder *ctx, enum mpp_enc_cmd_type cmd, void *param)
{
	struct h264_enc_ctx *s = (struct h264_enc_ctx *)ctx;
	struct mpp_enc_h264_rc_mode *rc_mode;

	switch (cmd) {
	case MPP_ENC_SET_QP:
		s->qp = *(int *)param;
		break;
	case ENC_CMD_H264_RC_MODE:
		rc_mode = (struct mpp_enc_h264_rc_mode *)param;
		if (rc_mode->rc_mode == MPP_ENC_RC_MODE_FIX_QP) {
			s->rc_mode = H264_RC_MODE_FIX_QP;
			h264_rc_destroy(&s->rc_ctx);
		} else {
			if (rc_mode->rc_mode == MPP_ENC_RC_MODE_CBR)
				s->rc_mode = H264_RC_MODE_CBR;
			else if (rc_mode->rc_mode == MPP_ENC_RC_MODE_VBR)
				s->rc_mode = H264_RC_MODE_VBR;

			s->rc_params_cfg.mode = s->rc_mode;
			s->rc_params_cfg.target_bps = rc_mode->target_bps;
			s->rc_params_cfg.max_bps = rc_mode->max_bps;
			s->rc_params_cfg.min_bps = rc_mode->min_bps;
			s->rc_params_cfg.qp_min = rc_mode->min_mb_qp;
			s->rc_params_cfg.qp_max = rc_mode->max_mb_qp;
			s->rc_params_cfg.frame_rate = s->gop_frame_num > 0 ? s->gop_frame_num : 30;
			s->rc_params_cfg.gop_size = s->gop_frame_num;
			/* Don't pass s->qp as qp_init - let RC calculate initial QP from bitrate.
			 * Passing a fixed default (24) prevents the RC from using its bpp-based
			 * calculation which would give a more appropriate QP for the target bitrate.
			 * Set to 0 so h264_rc_init will call calculate_initial_qp().
			 */
			s->rc_params_cfg.qp_init = 0;
			s->rc_params_cfg.qp_max_step = H264_RC_QP_STEP_MAX;

			// init rate control immediately if already initialized
			// otherwise it will be initialized in __h264_encode_frame
			if (s->init && s->width > 0 && s->height > 0) {
				s->rc_params_cfg.width = s->width;
				s->rc_params_cfg.height = s->height;
				h264_rc_init(&s->rc_ctx, &s->rc_params_cfg);
				logi("RC initialized immediately: enabled=%d", s->rc_ctx.enabled);
			}
		}
		logi("H264 RC mode: %d, target_bps: %d", s->rc_mode, rc_mode->target_bps);
		break;
	default:
		logw("Unknown command: %d", cmd);
		return -1;
	}

	return 0;
}

static int __h264_encode_reset(struct mpp_encoder *ctx)
{
	struct h264_enc_ctx *s = (struct h264_enc_ctx *)ctx;

	s->frame_num = 0;
	s->idr_pic_id = 0;

	return 0;
}

struct enc_ops h264_encoder = {
	.name		   = "h264",
	.init		   = __h264_encode_init,
	.destory		= __h264_encode_destroy,
	.encode		 = __h264_encode_frame,
	.get_parameter  = __h264_encode_get_parameter,
	.set_parameter  = __h264_encode_set_parameter,
	.reset		  = __h264_encode_reset,
};

struct mpp_encoder *create_h264_encoder()
{
	struct h264_enc_ctx *s = (struct h264_enc_ctx *)mpp_alloc(sizeof(struct h264_enc_ctx));
	if (s == NULL)
		return NULL;
	memset(s, 0, sizeof(struct h264_enc_ctx));

	s->encoder.ops = &h264_encoder;

	if (ve_open_device() < 0) {
		mpp_free(s);
		return NULL;
	}
	s->regs_base = ve_get_reg_base();
	logd("ve_reg_base: %lx", s->regs_base);

	return &s->encoder;
}
