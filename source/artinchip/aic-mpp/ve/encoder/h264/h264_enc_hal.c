/*
 * Copyright (C) 2020-2026 ArtInChip Technology Co. Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Author: <qi.xu@artinchip.com>
 * Desc: h264 encoder hardware abstraction layer
 */

#define LOG_TAG "h264_enc_hal"
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "h264_encoder.h"
#include "ve.h"
#include "ve_top_register.h"
#include "mpp_mem.h"
#include "mpp_log.h"

#define ALIGN_16B(x) (((x) + (15)) & ~(15))
#define ALIGN_8B(x) (((x) + (7)) & ~(7))

// register write/read helper
#define ve_write_reg(offset, val) ({write_reg_u32(s->regs_base + (offset), (val));})

#define ve_read_reg(offset) (read_reg_u32(s->regs_base + (offset)))

// VPU/VE status bits
#define VE_FINISH	(1 << 0)
#define VE_ERROR	(1 << 1)
#define BIT_REQ		(1 << 2)

static void ve_config_vpu_reg(struct h264_enc_ctx *s)
{
	// enable VPU clock
	ve_write_reg(VE_CLK_REG, 1);

	// reset AVC module
	ve_write_reg(VE_RST_REG, 0);

	// init VPU
	ve_write_reg(VE_INIT_REG, 1);

	// enable interrupt
	ve_write_reg(VE_IRQ_REG, 1);

	// enable AVC
	ve_write_reg(VE_AVC_EN_REG, 1);
}

static void config_top_register(struct h264_enc_ctx *s)
{
	uint32_t val;

	// clear irq
	ve_write_reg(GLB_STATUS_REG, 0x07);

	// config codec type: AVC encoder
	struct reg_codec_type codec_type = { 0 };
	codec_type.codec_std = 0;
	codec_type.codec_type = 1;
	val = *(uint32_t *)&codec_type;
	ve_write_reg(GLB_CODEC_TYPE_REG, val);

	// config picture size
	struct reg_glb_pic_size pic_size = { 0 };
	pic_size.pic_x_size = s->width;
	pic_size.pic_y_size = s->height;
	val = *(uint32_t *)&pic_size;
	ve_write_reg(GLB_PIC_SIZE_REG, val);

	// config SPS
	struct reg_avc_sps sps = { 0 };
	sps.direct_8x8_inference_flag = 0;
	sps.mb_adaptive_frame_filed_flag = 0;
	sps.frame_mbs_only_flag = 1;
	sps.pic_height_in_map_units_minus1 = s->sps.pic_height_in_mbs - 1;
	sps.pic_width_in_mbs_minus1 = s->sps.pic_width_in_mbs - 1;
	sps.chroma_format_idc = s->sps.chroma_format_idc;
	sps.uv_interleave = s->uv_interleave;
	sps.uv_alter = s->uv_alter;
	val = *(uint32_t *)&sps;
	ve_write_reg(AVC_SPS_REG, val);

	// init pic
	ve_write_reg(GLB_CTRL_REG, 1);

	// config mb size
	struct reg_glb_mb_size mb_size = { 0 };
	mb_size.pic_mb_x = s->sps.pic_width_in_mbs - 1;
	mb_size.pic_mb_y = s->sps.pic_height_in_mbs - 1;
	val = *(uint32_t *)&mb_size;
	ve_write_reg(GLB_PIC_MB_SIZE_REG, val);
}

static void config_picture_info_register(struct h264_enc_ctx *s, struct mpp_frame *frame)
{
	int i;
	uint32_t val;
	struct frame_format_reg frm_format = {0};
	struct frame_size_reg frm_size = {0};

	frm_size.pic_xsize = s->width;
	frm_size.pic_ysize = s->height;

	frm_format.cbcr_interleaved = s->uv_interleave;
	frm_format.stride = s->width;

	// color mode based on pixel format
	switch (s->pix_fmt) {
	case MPP_FMT_YUV420P:
	case MPP_FMT_NV12:
	case MPP_FMT_NV21:
		frm_format.color_mode = 0;
		break;
	case MPP_FMT_YUV400:
		frm_format.color_mode = 4;
		break;
	default:
		loge("format(%d) not support now", s->pix_fmt);
		frm_format.color_mode = 0;
		break;
	}
	for (i=0; i<MAX_FRAME_NUM; i++) {
		// config current frame (index 0)
		val = *(uint32_t *)&frm_format;
		ve_write_reg(PIC_INFO_START_REG + 20*i, val);
		val = *(uint32_t *)&frm_size;
		ve_write_reg(PIC_INFO_START_REG + 20*i + 4, val);
		ve_write_reg(PIC_INFO_START_REG + 20*i + 8, s->frames[i].buf[0]->phy_addr);
		ve_write_reg(PIC_INFO_START_REG + 20*i + 12, s->frames[i].buf[1]->phy_addr);
		ve_write_reg(PIC_INFO_START_REG + 20*i + 16, s->frames[i].buf[2]->phy_addr);
	}

	// src frame
	val = *(uint32_t *)&frm_format;
	ve_write_reg(PIC_INFO_START_REG + 20*MAX_FRAME_NUM, val);
	val = *(uint32_t *)&frm_size;
	ve_write_reg(PIC_INFO_START_REG + 20*MAX_FRAME_NUM + 4, val);

	if (frame->buf.buf_type == MPP_PHY_ADDR) {
		ve_write_reg(PIC_INFO_START_REG + 20*MAX_FRAME_NUM + 8, frame->buf.phy_addr[0]);
		ve_write_reg(PIC_INFO_START_REG + 20*MAX_FRAME_NUM + 12, frame->buf.phy_addr[1]);
		ve_write_reg(PIC_INFO_START_REG + 20*MAX_FRAME_NUM + 16, frame->buf.phy_addr[2]);
	} else {
		ve_add_dma_buf(frame->buf.fd[0], &s->src_frame_phy_addr[0]);
		if (frame->buf.fd[1])
			ve_add_dma_buf(frame->buf.fd[1], &s->src_frame_phy_addr[1]);
		if (frame->buf.fd[2])
			ve_add_dma_buf(frame->buf.fd[2], &s->src_frame_phy_addr[2]);
		ve_write_reg(PIC_INFO_START_REG + 20*MAX_FRAME_NUM + 8, s->src_frame_phy_addr[0]);
		ve_write_reg(PIC_INFO_START_REG + 20*MAX_FRAME_NUM + 12, s->src_frame_phy_addr[1]);
		ve_write_reg(PIC_INFO_START_REG + 20*MAX_FRAME_NUM + 16, s->src_frame_phy_addr[2]);
	}

	ve_write_reg(PIC_INFO_WRITE_END_REG, 0);

	// config cur frame index
	ve_write_reg(GLB_CUR_FRM_IDX_REG, s->rec_frame_idx);

	// config source frame idx
	struct reg_enc_frame_idx frame_idx = { 0 };
	frame_idx.ref_frame_idx = s->ref_frame_idx;
	frame_idx.src_frame_idx = s->src_frame_idx;
	val = *(uint32_t *)&frame_idx;
	ve_write_reg(ENC_FRM_IDX_REG, val);
}

static void ve_config_bitstream_register(struct h264_enc_ctx *s, struct enc_packet *packet, int offset)
{
	// use physical address for hardware
	uint32_t start_addr = packet->phy_base + packet->phy_offset + offset;
	uint32_t end_addr = start_addr + packet->size - 1;

	ve_write_reg(ENC_VLE_BUF_START_REG, start_addr & 0xfffffff8);
	ve_write_reg(ENC_VLE_BUF_END_REG, end_addr);
}

static void start_encode(struct h264_enc_ctx *s)
{
	// enable irq
	ve_write_reg(GLB_INT_REG, 0x07);

	// start encode
	ve_write_reg(GLB_CTRL_REG, 2);
}

static void config_slice_header(struct h264_enc_ctx *s)
{
	uint32_t val;

	// config slice type
	struct reg_avc_slice_type slice_type = { 0 };
	slice_type.bottom_field_flag = 0;
	slice_type.field_pic_flag = 0;
	slice_type.slice_type = s->sh.slice_type;
	val = *(uint32_t *)&slice_type;
	ve_write_reg(AVC_SLICE_TYPE_REG, val);

	// config first mb
	struct reg_fst_mb_addr mb_addr = { 0 };
	mb_addr.first_mb_x = 0;
	mb_addr.first_mb_y = 0;
	mb_addr.first_slice_in_pic = 1;
	val = *(uint32_t *)&mb_addr;
	ve_write_reg(AVC_FST_MB_ADDR_REG, val);

	// config ref idx num
	ve_write_reg(AVC_REF_IDX_NUM_REG, 0);

	// config deblock
	struct reg_avc_dblk_config dblk_cfg = { 0 };
	dblk_cfg.disable_deblocking_filter_idc = s->sh.disable_deblocking_filter_idc;
	dblk_cfg.slice_alpha_offset_div2 = s->sh.dblk_alpha_offset_div2;
	dblk_cfg.slice_beta_offset_div2 = s->sh.dblk_beta_offset_div2;
	val = *(uint32_t *)&dblk_cfg;
	ve_write_reg(AVC_DBLK_CFG_REG, val);

	// config slice QP
	struct reg_avc_slice_qp slice_qp = { 0 };
	slice_qp.chroma_qp_idx_offset = s->pps.chroma_qp_index_offset;
	slice_qp.second_chroma_qp_idx_offset = s->pps.second_chroma_qp_index_offset;
	slice_qp.slice_qpy = s->qp;
	slice_qp.pic_init_qp_minus26 = 0;
	val = *(uint32_t *)&slice_qp;
	ve_write_reg(AVC_SHS_QP_REG, val);

	// config pps
	struct reg_avc_pps pps = { 0 };
	pps.constrained_intra_pred_flag = s->pps.constrained_intra_pred_flag;
	pps.entropy_coding_mode_flag = s->pps.entropy_coding_mode_flag;
	pps.transform_8x8_mode_flag = s->pps.transform_8x8_mode_flag;
	pps.weighted_bipred_idc = s->pps.weighted_bipred_idc;
	pps.weighted_pred_flag = s->pps.weighted_pred_flag;
	val = *(uint32_t *)&pps;
	ve_write_reg(AVC_PPS_REG, val);

	// config slice header
	struct reg_enc_slice_header slice_header = { 0 };
	slice_header.frame_num = s->frame_num;
	slice_header.nal_ref_idc = 3;
	slice_header.ref_pic_list_modification_flag = 0;
	slice_header.idr_pic_id = s->idr_pic_id;
	val = *(uint32_t *)&slice_header;
	ve_write_reg(ENC_SLICE_HEADER_REG, val);
}

void config_quant_matrix(struct h264_enc_ctx *s)
{
	int i, j;
	u32* pVal;
	struct reg_avc_scaling_matrix q_matrix = { 0 };
	pVal = (u32*)&q_matrix;

	q_matrix.matrix_access = 1;
	q_matrix.write_enable = 1;
	//using default matrix or flat matrix, anyway not flat matrix, so we should config them
	for (i = 0; i < 8; i++) {
		q_matrix.matrix_addr = i;
		if (i < 6) {   //4x4
			for (j = 0; j < 16; j++) {
				int addr = i * 16 + j;
				q_matrix.matrix_addr = addr;
				q_matrix.scaling_matrix_data = 16;
				ve_write_reg(AVC_SCALING_MATRIX_REG, *pVal);
			}
		}
	}

	ve_write_reg(AVC_SCALING_MATRIX_REG, 0);
}

static void config_intra_pred(struct h264_enc_ctx *s)
{
	uint32_t val;
	ve_write_reg(MB_RECON_BASE_ADDR_REG, s->mb_recon_buf->phy_addr);

	// config encoder ctrl
	struct reg_enc_ctrl enc_ctrl = { 0 };
	enc_ctrl.imd_en = 1;
	enc_ctrl.cimd_en = 1;
	enc_ctrl.uv_interleave = s->uv_interleave;
	enc_ctrl.uv_alter = s->uv_alter;
	val = *(uint32_t *)&enc_ctrl;
	ve_write_reg(ENC_CTRL_REG, val);

	// config imd threshold
	struct reg_enc_imd_thr imd_thr = { 0 };
	imd_thr.imd_threshold_0 = 0;
	imd_thr.imd_threshold_1 = s->intra_params.intra4x4_weight;
	val = *(uint32_t *)&imd_thr;
	ve_write_reg(ENC_IMD_THR_REG, val);

	// config intra weight
	ve_write_reg(ENC_COST_WEIGHT_REG, s->intra_params.intra_weight);
}

static void config_me(struct h264_enc_ctx *s)
{
	uint32_t val;
	logi("config me register");
	// config me ctrl
	struct reg_enc_me_ctrl enc_me = { 0 };
	enc_me.cime_disable = !s->me_params.cime_en;
	enc_me.umc_flag = 0;
	enc_me.dynamic_search_range_en = 0;
	enc_me.cime_search_range = s->me_params.cime_search_range;
	enc_me.rime_search_range = 0;
	enc_me.lambda_en = s->me_params.lambda_en;
	val = *(uint32_t *)&enc_me;
	ve_write_reg(ENC_ME_CTRL_REG, val);

	// config pweight
	struct reg_enc_me_pweight enc_pweight = { 0 };
	enc_pweight.cime_pweight = s->me_params.cime_pmv_weight;
	enc_pweight.rime_pweight = s->me_params.rime_pmv_weight;
	enc_pweight.fme_pweight = s->me_params.fme_pmv_weight;
	val = *(uint32_t *)&enc_pweight;
	ve_write_reg(ENC_ME_PWEIGHT_REG, val);

	// config zweight
	struct reg_enc_me_zweight enc_zweight = { 0 };
	enc_zweight.cime_zweight = s->me_params.cime_pmv_weight;
	enc_zweight.rime_zweight = s->me_params.rime_pmv_weight;
	enc_zweight.fme_zweight = s->me_params.fme_pmv_weight;
	val = *(uint32_t *)&enc_zweight;
	ve_write_reg(ENC_ME_ZWEIGHT_REG, val);

	// config skip
	struct reg_enc_me_skip enc_skip = { 0 };
	enc_skip.skip_threshold = 0;
	enc_skip.skip_weight = s->me_params.fme_skip_weight;
	val = *(uint32_t *)&enc_skip;
	ve_write_reg(ENC_ME_SKIP_REG, val);

	// config block weights
	ve_write_reg(ENC_ME_P16x8_REG, s->me_params.p16x8_weight);
	ve_write_reg(ENC_ME_P8x16_REG, s->me_params.p8x16_weight);
	ve_write_reg(ENC_ME_P8x8_REG, s->me_params.p8x8_weight);

	// config pmv en
	struct reg_enc_me_pmv me_pmv = { 0 };
	me_pmv.cime_pmv_en = 1;
	me_pmv.rime_pmv_en = 1;
	me_pmv.fme_pmv_en = 1;
	me_pmv.skip_en = 1;
	val = *(uint32_t *)&me_pmv;
	ve_write_reg(ENC_ME_PMV_EN_REG, val);

	ve_write_reg(ENC_ME_REF_ADDR_REG, s->frames[s->ref_frame_idx].buf_subsample->phy_addr);

	// config block en
	struct reg_enc_block_en block_en = { 0 };
	block_en.p16x16_en = 1;
	block_en.p16x8_en = 1;
	block_en.p8x16_en = 1;
	block_en.p8x8_en = 1;
	val = *(uint32_t *)&block_en;
	ve_write_reg(ENC_ME_BLOCK_EN_REG, val);
}

static void config_dblk(struct h264_enc_ctx *s)
{
	uint32_t val;
	int pic_xsize = s->width;
	int pic_ysize = s->height;
	logi("config dblk register");

	// config dblk pic type
	struct reg_pic_type pic_type = { 0 };
	pic_type.mbaff = 0;
	pic_type.bottom_field_flag = 0;
	pic_type.field = 0;
	pic_type.codec_std = 0;
	pic_type.codec_type = 1; // encoder
	val = *(uint32_t *)&pic_type;
	ve_write_reg(DBLK_PIC_TYPE_REG, val);

	// config dblk pic size
	struct reg_pic_size pic_size = { 0 };
	pic_size.pic_xsize = pic_xsize;
	pic_size.pic_ysize = pic_ysize;
	val = *(uint32_t *)&pic_size;
	ve_write_reg(DBLK_PIC_SIZE_REG, val);

	// config dblk en
	ve_write_reg(DBLK_EN_REG, !(s->sh.disable_deblocking_filter_idc == 1));

	// config dblk tmp buf (use packet data area temporarily)
	ve_write_reg(DBLK_BUF_Y_REG, s->dblk_y_buf->phy_addr);
	ve_write_reg(DBLK_BUF_C_REG, s->dblk_c_buf->phy_addr);

	struct reg_dec_config dec_config = { 0 };
	dec_config.dec_chroma_idc = 0;  // !chroma_idc
	dec_config.dec_luma_only = 0;
	dec_config.dec_range_en = 0;
	dec_config.dec_wr_en = 1;
	dec_config.endian = 0;
	dec_config.uv_interleave = s->uv_interleave;
	dec_config.uv_alternative = s->uv_alter;
	val = *(uint32_t *)&dec_config;
	ve_write_reg(DEC_CONFIG_REG, val);

	// config dblk frame idx
	ve_write_reg(DEC_FRAME_IDX_REG, s->rec_frame_idx);

	if (s->me_params.cime_en) {
		ve_write_reg(DBLK_SUBSAMPLE_DISABLE, 0);
		ve_write_reg(DBLK_SUBSAMPLE_ADDR_REG, s->frames[s->rec_frame_idx].buf_subsample->phy_addr);
	} else {
		ve_write_reg(DBLK_SUBSAMPLE_DISABLE, 1);
	}
}

static int wait_encode_finish(struct h264_enc_ctx *s)
{
	uint32_t status;
	if (ve_wait(&status) < 0) {
		loge("ve timeout, avc status: %x", status);
		return -1;
	}

	if (status & AVC_ERROR) {
		loge("decode error, status: %x", status);
		return -1;
	} else if (status & AVC_BIT_REQ) {
		logw("bit request");
		return -1;
	} else if (status & AVC_FINISH) {
		if (status & (~AVC_FINISH)) {
			// the status will be 0x10100 in multi-slice case.
			// it is not error, so we cannot reset avc module here.
			// if the data is not enough to decode the whole picture,
			// we can justice frame error from first_mb in next slice header.
		}
		logd("finish");
		return 0;
	}

	loge("wait ve finish timeout, status: 0x%x", status);
	return -1;
}

static void config_rc(struct h264_enc_ctx *s)
{
	u32 *pVal = NULL;
	ve_write_reg(ENC_RATE_CONTROL_REG, h264_rc_is_enabled(&s->rc_ctx));
	ve_write_reg(ENC_MB_LINE_MAD_ADDR_REG_L, s->rc_mb_line_mad_buf->phy_addr);
	ve_write_reg(ENC_MB_LINE_BITS_THRES_ADDR_L_REG, s->rc_mb_line_target_thres_buf->phy_addr);

	struct reg_enc_mad_param param = { 0 };
	param.mb_mad_th0 = s->mb_rc_params.mb_mad_threshold[0];
	param.mb_mad_th1 = s->mb_rc_params.mb_mad_threshold[1];
	pVal = (u32*)&param;
	ve_write_reg(ENC_MB_MAD_PARAM0_REG, *pVal);

	param.mb_mad_th0 = s->mb_rc_params.mb_mad_threshold[2];
	param.mb_mad_th1 = s->mb_rc_params.mb_mad_threshold[3];
	pVal = (u32*)&param;
	ve_write_reg(ENC_MB_MAD_PARAM1_REG, *pVal);

	param.mb_mad_th0 = s->mb_rc_params.mb_mad_threshold[4];
	param.mb_mad_th1 = s->mb_rc_params.mb_mad_threshold[5];
	pVal = (u32*)&param;
	ve_write_reg(ENC_MB_MAD_PARAM2_REG, *pVal);

	struct reg_enc_qp_range qp_range = { 0 };
	qp_range.qp_min = s->rc_ctx.params.qp_min;
	qp_range.qp_max = s->rc_ctx.params.qp_max;
	pVal = (u32*)&qp_range;
	ve_write_reg(ENC_QP_RANGE_REG, *pVal);
}

static void get_frame_mad(struct h264_enc_ctx *s)
{
	s->rc_frame_mad = ve_read_reg(ENC_FRAME_MAD_SUM_REG);
}

static int get_encoded_data_size(struct h264_enc_ctx *s)
{
	return ve_read_reg(ENC_VLE_BUF_SIZE_REG);
}

static void config_cache(struct h264_enc_ctx *s)
{
#ifndef AIC_VE_DRV_V10
	u32 version = 0;
	u32 *pVal = NULL;
	struct reg_cache_config cache_cfg = { 0 };
	version = ve_read_reg(0xfc);

	if (s->sh.slice_type != H264_SLICE_P && s->sh.slice_type != H264_SLICE_B)
		return;

	// not support cache in old version
	if (version < 0x501 || version == 0x30000)
		return;
	// cache refresh
	ve_write_reg(CACHE_CTRL_REG, 0);
	while(ve_read_reg(CACHE_CTRL_REG) & 0x10) {
		usleep(10);
	}

	// config cache
	if (s->sh.slice_type == H264_SLICE_P) {
		ve_write_reg(CACHE_SIZE_REG, SINGLE_CACHE_SIZE);
	} else {
		ve_write_reg(CACHE_SIZE_REG, DUAL_CACHE_SIZE);
	}
	ve_write_reg(CACHE_BUF_SIZE_REG, CACHE_BUF_SIZE);

	if ((s->pix_fmt == MPP_FMT_NV12 || s->pix_fmt == MPP_FMT_NV21))
		cache_cfg.cache_cbcr_inter = 1;
	cache_cfg.cache_page_merge = CACHE_MERGE;
	cache_cfg.cache_dual_mode = 0;

	cache_cfg.cache_frame_idx0 = s->ref_frame_idx;
	pVal = (u32*)&cache_cfg;
	ve_write_reg(CACHE_CFG_REG, *pVal);
	ve_write_reg(CACHE_CTRL_REG, 1);
	while((ve_read_reg(CACHE_CTRL_REG) & 0x20) != 0x20) {
		usleep(10);
	}
#endif
}

// Slice encode function (hardware implementation)
int h264_encode_slice(struct h264_enc_ctx *s, struct mpp_frame *frame, struct enc_packet *packet, int offset)
{
	int ret;

	logd("h264_encode_slice: start hardware encoding");

	if (!s || !frame || !packet) {
		loge("Invalid parameters");
		return -1;
	}

	ve_get_client();
	// config VPU registers
	ve_config_vpu_reg(s);

	// config top registers
	config_top_register(s);

	// config picture info
	config_picture_info_register(s, frame);

	// config slice header
	config_slice_header(s);

	config_quant_matrix(s);
	// config intra prediction
	config_intra_pred(s);

	// config motion estimation for P frame
	if (s->sh.slice_type == H264_SLICE_P) {
		config_me(s);
		config_cache(s);
	}

	logd("h264_encode_slice: rc_enabled=%d", h264_rc_is_enabled(&s->rc_ctx));
	if (h264_rc_is_enabled(&s->rc_ctx)) {
		logi("config_rc called");
		config_rc(s);
	}

	// config deblock
	config_dblk(s);

	// config bitstream buffer
	ve_config_bitstream_register(s, packet, offset);

	// start encoding
	start_encode(s);

	// wait for encode finish
	ret = wait_encode_finish(s);
	if (ret < 0) {
		loge("Encode failed");
		ve_put_client();
		return ret;
	}

	get_frame_mad(s);
	// get encoded data size
	int encoded_size = get_encoded_data_size(s);
	logd("h264_encode_slice: encoded size = %d", encoded_size);
	packet->len = encoded_size;
	ve_put_client();

	return encoded_size;
}
