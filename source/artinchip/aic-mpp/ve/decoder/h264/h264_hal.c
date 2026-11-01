/*
 * Copyright (C) 2020-2026 ArtInChip Technology Co. Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 *  author: <qi.xu@artinchip.com>
 *  Desc: h264 register config
 *
 */
#include <stdlib.h>
#include <string.h>

#include "ve_top_register.h"
#include "h264_decoder.h"
#include "ve.h"
#include "mpp_log.h"

#define MAX(a,b) ((a) > (b) ? (a) : (b))

static void write_reg(unsigned long reg_addr, u32 val, FILE* fp)
{
	write_reg_u32(reg_addr, val);

	if(fp) {
		fprintf(fp, "write_reg %08x %08x\n", (u32)reg_addr, val);
	}
}

static u32 read_reg(unsigned long reg_addr)
{
	return read_reg_u32(reg_addr);
}

static void ve_config_ve_top_reg(struct h264_dec_ctx *s)
{
	write_reg(s->regs_base + VE_CLK_REG, 1, s->fp_reg);
	write_reg(s->regs_base + VE_RST_REG, RST_AVC_PIC_MODULE, s->fp_reg);

	while(1) {
		u32 tmp = 0;
		tmp = read_reg(s->regs_base + VE_RST_REG);
		if ((tmp>>16) == 0)
			break;
	}

	write_reg(s->regs_base + VE_INIT_REG, 1, s->fp_reg);
	write_reg(s->regs_base + VE_IRQ_REG, 1, s->fp_reg);
	write_reg(s->regs_base + VE_AVC_EN_REG, 1, s->fp_reg);
}

static void ve_reset_pic_info_reg(struct h264_dec_ctx *s)
{
	write_reg(s->regs_base + VE_RST_REG, RST_PIC_MODULE, s->fp_reg);
	while (1) {
		u32 tmp = 0;
		tmp = read_reg(s->regs_base + VE_RST_REG);
		if ((tmp>>16) == 0)
			break;
	}
	write_reg(s->regs_base + VE_IRQ_REG, 1, s->fp_reg);
	write_reg(s->regs_base + VE_AVC_EN_REG, 1, s->fp_reg);
}

// enable irq here, because it will disable irq in irq_handle function
static void enable_irq(struct h264_dec_ctx *s)
{
	write_reg(s->regs_base + VE_IRQ_REG, 1, s->fp_reg);
}

static void config_sps(struct h264_dec_ctx *s)
{
#ifndef AIC_VE_DRV_V10
	// set pic init
	u32 first_mb = s->sh.first_mb_in_slice == 0;
	write_reg(s->regs_base + GLB_CTRL_REG, first_mb, s->fp_reg);
#endif
	if(s->fp_reg)
		fprintf(s->fp_reg, "// config sequence parameter set\n");

	struct h264_sps_info* cur_sps = s->sps_buffers[s->active_sps_id];
	struct reg_avc_sps avc_sps = {0};
	u32* pVal;

	pVal = (u32*)&avc_sps;

	avc_sps.direct_8x8_inference_flag = cur_sps->direct_8x8_inference_flag;
	avc_sps.mb_adaptive_frame_filed_flag = cur_sps->mbaff;
	avc_sps.frame_mbs_only_flag = cur_sps->frame_mbs_only_flag;
	avc_sps.pic_height_in_map_units_minus1 = cur_sps->pic_mb_height - 1;
	avc_sps.pic_width_in_mbs_minus1 = cur_sps->pic_mb_width - 1;
	avc_sps.chroma_format_idc = cur_sps->chroma_format_idc;
	avc_sps.uv_interleave = (s->pix_format == MPP_FMT_NV12 ||
		s->pix_format == MPP_FMT_NV21);
	avc_sps.uv_alter = s->pix_format == MPP_FMT_NV21;
#ifdef AIC_VE_DRV_V10
	avc_sps.pic_init = (s->sh.first_mb_in_slice == 0);
#endif
	write_reg(s->regs_base + AVC_SPS_REG, *pVal, s->fp_reg);

#ifndef AIC_VE_DRV_V10
	if (s->fp_reg)
		fprintf(s->fp_reg, "// config picture size\n");
	struct reg_glb_pic_size pic_size = { 0 };

	pic_size.pic_x_size = (cur_sps->pic_mb_width) * 16;
	pic_size.pic_y_size = (2 - cur_sps->frame_mbs_only_flag) *
		(cur_sps->pic_mb_height) * 16;

	pVal = (u32*)&pic_size;
	write_reg(s->regs_base + GLB_PIC_SIZE_REG, *pVal, s->fp_reg);

	struct reg_glb_mb_size mb_size = { 0 };
	mb_size.pic_mb_x = cur_sps->pic_mb_width - 1;
	mb_size.pic_mb_y = cur_sps->pic_mb_height - 1;
	pVal = (u32*)&mb_size;
	write_reg(s->regs_base + GLB_PIC_MB_SIZE_REG, *pVal, s->fp_reg);
#endif
}

static void config_avc_pps_register(struct h264_dec_ctx *s)
{
	struct h264_pps_info* cur_pps = s->pps_buffers[s->active_pps_id];
	struct reg_avc_pps avc_pps = {0};
	u32* pVal;

	if(s->fp_reg)
		fprintf(s->fp_reg, "// config picture parameter set\n");

	pVal = (u32*)(&avc_pps);

	avc_pps.constrained_intra_pred_flag = cur_pps->constrained_intra_pred_flag;
	avc_pps.entropy_coding_mode_flag = cur_pps->entropy_coding_flag;
	avc_pps.num_ref_idx_l0_active_minus1_pic = cur_pps->num_ref_idx[0] - 1;
	avc_pps.num_ref_idx_l1_active_minus1_pic = cur_pps->num_ref_idx[1] - 1;
	avc_pps.transform_8x8_mode_flag = cur_pps->transform_8x8_mode_flag;
	avc_pps.weighted_bipred_idc = cur_pps->weighted_bipred_flag;
	avc_pps.weighted_pred_flag = cur_pps->weighted_pred_flag;

	write_reg(s->regs_base + AVC_PPS_REG, *pVal, s->fp_reg);
}

#ifdef AIC_VE_DRV_V10
static void config_avc_sh_register_v10(struct h264_dec_ctx *s,
	struct h264_slice_header* sh,
	struct h264_pps_info* cur_pps,
	struct h264_sps_info* cur_sps,
	int mb_x, int mb_y)
{
	u32* pVal;
	struct reg_avc_sh1 avc_sh1 = {0};
	struct reg_avc_sh2 avc_sh2 = {0};
	struct reg_avc_shs_wp avc_wp = {0};
	struct reg_avc_shs_qp avc_qp = {0};

	pVal = (u32*)(&avc_sh1);
	avc_sh1.slice_type = sh->slice_type;
	avc_sh1.cabac_init_idc = sh->cabac_init_idc;
	avc_sh1.nal_ref_flag = (s->nal_ref_idc != 0);
	avc_sh1.direct_spatial_mv_pred_flag = sh->direct_spatial_mv_pred;
	avc_sh1.bottom_field_flag = sh->bottom_field_flag;
	avc_sh1.field_pic_flag = sh->field_pic_flag;
	avc_sh1.first_slice_in_pic = (sh->first_mb_in_slice == 0);
	avc_sh1.first_mb_x = mb_x;
	avc_sh1.first_mb_y = mb_y;
	write_reg(s->regs_base + AVC_SHS1_REG, *pVal, s->fp_reg);

	pVal = (u32*)(&avc_sh2);
	avc_sh2.slice_beta_offset_div2 = sh->slice_beta_offset_div2;
	avc_sh2.slice_alpha_offset_div2 = sh->slice_alpha_c0_offset_div2;
	avc_sh2.disable_deblocking_filter_idc = sh->deblocking_filter;
	avc_sh2.num_ref_idx_active_override_flag = sh->num_ref_idx_active_override;
	avc_sh2.num_ref_idx_l0_active_minus1 = sh->num_ref_idx[0] - 1;
	avc_sh2.num_ref_idx_l1_active_minus1 = sh->num_ref_idx[1] - 1;
	write_reg(s->regs_base + AVC_SHS2_REG, *pVal, s->fp_reg);

	pVal = (u32*)(&avc_wp);
	avc_wp.chroma_log2_weight_denom = sh->pwt.chroma_log2_weight_denom;
	avc_wp.luma_log2_weight_denom = sh->pwt.luma_log2_weight_denom;
	write_reg(s->regs_base + AVC_SHS_WP_REG, *pVal, s->fp_reg);

	pVal = (u32*)(&avc_qp);
	avc_qp.chroma_qp_idx_offset = cur_pps->chroma_qp_index_offset[0];
	avc_qp.second_chroma_qp_idx_offset = cur_pps->chroma_qp_index_offset[1];
	avc_qp.slice_qpy = sh->qp_y;
	write_reg(s->regs_base + AVC_SHS_QP_REG, *pVal, s->fp_reg);
}
#else
static void config_avc_sh_register_legacy(struct h264_dec_ctx *s,
	struct h264_slice_header* sh,
	struct h264_pps_info* cur_pps,
	struct h264_sps_info* cur_sps,
	int mb_x, int mb_y)
{
	u32* pVal;
	struct reg_avc_slice_type slice_type = { 0 };
	slice_type.bottom_field_flag = sh->bottom_field_flag;
	slice_type.field_pic_flag = sh->field_pic_flag;
	slice_type.slice_type = sh->slice_type;
	slice_type.nal_ref_flag = (s->nal_ref_idc != 0);
	pVal = (u32*)(&slice_type);
	write_reg(s->regs_base + AVC_SLICE_TYPE_REG, *pVal, s->fp_reg);

	struct reg_fst_mb_addr fst_mb_addr = { 0 };
	fst_mb_addr.first_mb_x = mb_x;
	fst_mb_addr.first_mb_y = mb_y;
	fst_mb_addr.first_slice_in_pic = (sh->first_mb_in_slice == 0);
	pVal = (u32*)(&fst_mb_addr);
	write_reg(s->regs_base + AVC_FST_MB_ADDR_REG, *pVal, s->fp_reg);

	write_reg(s->regs_base + AVC_MV_PRED_FLAG_REG, sh->direct_spatial_mv_pred,
		s->fp_reg);

	struct reg_avc_ref_idx_num ref_idx_val = {0};
	ref_idx_val.num_ref_idx_active_override_flag =
		sh->num_ref_idx_active_override;
	ref_idx_val.num_ref_idx_l0_active_minus1 = sh->num_ref_idx[0] - 1;
	ref_idx_val.num_ref_idx_l1_active_minus1 = sh->num_ref_idx[1] - 1;
	pVal = (u32*)(&ref_idx_val);
	write_reg(s->regs_base + AVC_REF_IDX_NUM_REG, *pVal, s->fp_reg);

	struct reg_avc_dblk_config dblk_cfg = { 0 };
	dblk_cfg.disable_deblocking_filter_idc = sh->deblocking_filter;
	dblk_cfg.slice_alpha_offset_div2 = sh->slice_alpha_c0_offset_div2;
	dblk_cfg.slice_beta_offset_div2 = sh->slice_beta_offset_div2;
	pVal = (u32*)(&dblk_cfg);
	write_reg(s->regs_base + AVC_DBLK_CFG_REG, *pVal, s->fp_reg);

	struct reg_avc_wp_cfg wp_cfg = { 0 };
	wp_cfg.chroma_log2_weight_denom = sh->pwt.chroma_log2_weight_denom;
	wp_cfg.luma_log2_weight_denom = sh->pwt.luma_log2_weight_denom;
	pVal = (u32*)(&wp_cfg);
	write_reg(s->regs_base + AVC_WP_CFG_REG, *pVal, s->fp_reg);

	struct reg_avc_slice_qp slice_qp = { 0 };
	slice_qp.chroma_qp_idx_offset = cur_pps->chroma_qp_index_offset[0];
	slice_qp.second_chroma_qp_idx_offset = cur_pps->chroma_qp_index_offset[1];
	slice_qp.slice_qpy = sh->qp_y;
	pVal = (u32*)(&slice_qp);
	write_reg(s->regs_base + AVC_SHS_QP_REG, *pVal, s->fp_reg);

	write_reg(s->regs_base + AVC_CABAC_CFG_REG, sh->cabac_init_idc, s->fp_reg);
}
#endif

static void config_avc_sh_register(struct h264_dec_ctx *s)
{
	struct h264_slice_header* sh = &s->sh;
	struct h264_pps_info* cur_pps = s->pps_buffers[s->active_pps_id];
	struct h264_sps_info* cur_sps = s->sps_buffers[s->active_sps_id];
	int mb_y = (sh->first_mb_in_slice / cur_sps->pic_mb_width) << cur_sps->mbaff;
	int mb_x = sh->first_mb_in_slice % cur_sps->pic_mb_width;

	if(s->fp_reg)
		fprintf(s->fp_reg, "// config slice header\n");

#ifdef AIC_VE_DRV_V10
	config_avc_sh_register_v10(s, sh, cur_pps, cur_sps, mb_x, mb_y);
#else
	config_avc_sh_register_legacy(s, sh, cur_pps, cur_sps, mb_x, mb_y);
#endif
}

static void init_decode_mb_number(struct h264_dec_ctx *s)
{
	if(s->sh.first_mb_in_slice == 0)
		write_reg(s->regs_base + CORRECT_DECODE_MB_NUMBER_REG,
			s->sh.first_mb_in_slice, s->fp_reg);
}

static void config_weight_pred_list(struct h264_dec_ctx *s,
	struct h264_slice_header* sh, int list, int comp)
{
	int i;
	struct reg_avc_weight_pred avc_wp = {0};
	u32* pVal;
	int base_addr = (list == 0) ? 0 : 96;
	int num_ref = sh->num_ref_idx[list];

	pVal = (u32*)&avc_wp;

	for (i = 0; i < 32; i++) {
#ifndef AIC_VE_DRV_V10
		avc_wp.avc_weight_pred_access = 1;
		avc_wp.avc_weight_pred_write_enable = 1;
#endif
		avc_wp.weight_pred_addr = base_addr + 32*comp + i;
		if (i < num_ref) {
			avc_wp.weight = sh->pwt.wp_weight[list][i][comp];
			avc_wp.offset = sh->pwt.wp_offset[list][i][comp];
		} else {
			avc_wp.weight = 1;
			avc_wp.offset = 0;
		}
		write_reg(s->regs_base + AVC_WEIGHT_PRED_REGISTER, *pVal, s->fp_reg);
	}
}

static void config_weight_pred_para(struct h264_dec_ctx *s)
{
	int comp;
	struct h264_slice_header* sh = &s->sh;

	if(s->fp_reg)
		fprintf(s->fp_reg, "// config weighted pred\n");

	for(comp = 0; comp < 3; comp++) {
		config_weight_pred_list(s, sh, 0, comp);
	}

	for(comp = 0; comp < 3; comp++) {
		config_weight_pred_list(s, sh, 1, comp);
	}
}

static void config_quant_matrix(struct h264_dec_ctx *s)
{
	struct h264_pps_info* cur_pps = s->pps_buffers[s->active_pps_id];
	struct reg_avc_scaling_matrix avc_scale_matrix = {0};
	u32* pVal;
	int i, j;

	if(s->fp_reg)
		fprintf(s->fp_reg, "// config quant matrix\n");

	pVal = (u32*)&avc_scale_matrix;

	avc_scale_matrix.matrix_access = 1;
	avc_scale_matrix.write_enable = 1;
	//4x4
	for (i = 0; i<6; i++) {
		avc_scale_matrix.matrix_addr = i;
		for (j = 0; j<16; j++) {
			avc_scale_matrix.matrix_addr = i * 16 + j;
			avc_scale_matrix.scaling_matrix_data =
				cur_pps->scaling_matrix4[i][j];
			write_reg(s->regs_base + AVC_SCALING_MATRIX_REG, *pVal, s->fp_reg);
		}
	}

	//8x8
	for (i = 0; i<2; i++) {
		avc_scale_matrix.matrix_addr = i + 6;
		for (j = 0; j<64; j++) {
			avc_scale_matrix.matrix_addr = 96 + i * 64 + j;
			avc_scale_matrix.scaling_matrix_data =
				cur_pps->scaling_matrix8[i][j];
			write_reg(s->regs_base + AVC_SCALING_MATRIX_REG, *pVal, s->fp_reg);
		}
	}

	write_reg(s->regs_base + AVC_SCALING_MATRIX_REG, 0, s->fp_reg);
}

static void config_reflist_entry(struct h264_dec_ctx *s,
	struct h264_slice_header* sh, int i)
{
	struct reg_avc_ref_list avc_ref_list = {0};
	u32* pVal;
	struct h264_picture* pic = NULL;

	pVal = (u32*)&avc_ref_list;

	avc_ref_list.ref_idx_rw = 1;
	avc_ref_list.ref_idx_we = 1;
	avc_ref_list.ref_idx = i;

	if (i < sh->num_ref_idx[0] &&
		(sh->slice_type == H264_SLICE_P || sh->slice_type == H264_SLICE_B)) {
		pic = s->frame_info.ref_list[0][i].parent;

		if (pic) {
			avc_ref_list.buf_idx_list0 = pic->buf_idx;
		} else {
			logw("ref idx maybe error");
			avc_ref_list.buf_idx_list0 = 0;
		}

		avc_ref_list.field_sel_list0 =
			(s->frame_info.ref_list[0][i].refrence == PICT_BOTTOM_FIELD);
	}

	if(i < sh->num_ref_idx[1] && sh->slice_type == H264_SLICE_B) {
		pic = s->frame_info.ref_list[1][i].parent;
		if (pic) {
			avc_ref_list.buf_idx_list1 = pic->buf_idx;
		} else {
			logw("ref idx maybe error");
			avc_ref_list.buf_idx_list1 = 0;
		}

		avc_ref_list.field_sel_list1 =
			(s->frame_info.ref_list[1][i].refrence == PICT_BOTTOM_FIELD);
	}

	write_reg(s->regs_base + AVC_REF_LIST_REGISTER, *pVal, s->fp_reg);
}

static void config_reflist(struct h264_dec_ctx *s)
{
	struct h264_slice_header* sh = &s->sh;
	int i;
	int max_ref = MAX(sh->num_ref_idx[0], sh->num_ref_idx[1]);

	if(s->fp_reg)
		fprintf(s->fp_reg, "// config reflist\n");

	for (i = 0; i < max_ref; i++) {
		config_reflist_entry(s, sh, i);
	}

	write_reg(s->regs_base + AVC_REF_LIST_REGISTER, 0, s->fp_reg);
}

#ifdef AIC_VE_DRV_V10
static void write_frame_info_v10(struct h264_dec_ctx *s,
	struct reg_avc_buf_info* avc_buf_info,
	u32 content)
{
	u32* buf_info = (u32*)avc_buf_info;
	write_reg(s->regs_base + AVC_BUF_INFO_REGISTER, *buf_info, s->fp_reg);
	write_reg(s->regs_base + AVC_BUF_INFO_CONTENT_REGISTER, content, s->fp_reg);
}
#else
static void write_frame_info_legacy(struct h264_dec_ctx *s,
	struct reg_avc_buf_info* avc_buf_info,
	u32 content)
{
	u32* buf_info = (u32*)avc_buf_info;
	write_reg(s->regs_base + AVC_BUF_INFO_CONTENT_REGISTER, content, s->fp_reg);
	write_reg(s->regs_base + AVC_BUF_INFO_REGISTER, *buf_info, s->fp_reg);
}
#endif

static void config_frame_poc(struct h264_dec_ctx *s,
	struct h264_picture* pic, int field)
{
	struct reg_avc_buf_info avc_buf_info = {0};
	u32 content = pic->field_poc[field];

	avc_buf_info.buf_idx = pic->buf_idx;
	avc_buf_info.content_select = field;
#ifdef AIC_VE_DRV_V10
	avc_buf_info.curr_frame_idx = s->frame_info.cur_pic_ptr->buf_idx;
	avc_buf_info.buf_info_rw = 1;
	write_frame_info_v10(s, &avc_buf_info, content);
#else
	avc_buf_info.buf_info_access = 1;
	avc_buf_info.buf_info_write_en = 1;
	write_frame_info_legacy(s, &avc_buf_info, content);
#endif
}

static void config_frame_struct(struct h264_dec_ctx *s,
	struct h264_picture* pic, struct frame_struct_ref_info* frm_info)
{
	struct reg_avc_buf_info avc_buf_info = {0};
	u32* ptr_tmp = (u32*)frm_info;

	avc_buf_info.buf_idx = pic->buf_idx;
	avc_buf_info.content_select = 2;

#ifdef AIC_VE_DRV_V10
	avc_buf_info.curr_frame_idx = s->frame_info.cur_pic_ptr->buf_idx;
	avc_buf_info.buf_info_rw = 1;
	write_frame_info_v10(s, &avc_buf_info, *ptr_tmp);
#else
	avc_buf_info.buf_info_access = 1;
	avc_buf_info.buf_info_write_en = 1;
	write_frame_info_legacy(s, &avc_buf_info, *ptr_tmp);
#endif
}

static void config_col_addr(struct h264_dec_ctx *s,
	struct h264_picture* pic, int field)
{
	struct reg_avc_buf_info avc_buf_info = {0};
	u32 content = (field == 0) ? pic->top_field_col_addr : pic->bot_field_col_addr;
	int select = (field == 0) ? 5 : 6;

	avc_buf_info.content_select = select;
	avc_buf_info.buf_idx = pic->buf_idx;
#ifdef AIC_VE_DRV_V10
	avc_buf_info.curr_frame_idx = s->frame_info.cur_pic_ptr->buf_idx;
	avc_buf_info.buf_info_rw = 1;
	write_frame_info_v10(s, &avc_buf_info, content);
#else
	avc_buf_info.buf_info_access = 1;
	avc_buf_info.buf_info_write_en = 1;
	write_frame_info_legacy(s, &avc_buf_info, content);
#endif
}

static void config_frame_info_frame(struct h264_dec_ctx *s,
	struct h264_picture* pic)
{
	struct frame_struct_ref_info frm_info = {0};

	config_frame_poc(s, pic, 0);
	config_frame_poc(s, pic, 1);

	frm_info.frm_struct = s->sh.mbaff_frame ? 2 : 0;
	frm_info.top_ref_type = frm_info.bot_ref_type = s->nal_ref_idc ? 0: 2;
	config_frame_struct(s, pic, &frm_info);
}

static void config_frame_info_top_field(struct h264_dec_ctx *s,
	struct h264_picture* pic)
{
	struct frame_struct_ref_info frm_info = {0};

	config_frame_poc(s, pic, 0);

	frm_info.top_ref_type = s->nal_ref_idc ? 0: 2;
	frm_info.frm_struct = 1;
	config_frame_struct(s, pic, &frm_info);
}

static void config_frame_info_bot_field(struct h264_dec_ctx *s,
	struct h264_picture* pic)
{
	struct frame_struct_ref_info frm_info = {0};

	config_frame_poc(s, pic, 1);

	frm_info.bot_ref_type = s->nal_ref_idc ? 0: 2;
	frm_info.frm_struct = 1;
	config_frame_struct(s, pic, &frm_info);
}

static void config_frame_info(struct h264_dec_ctx *s, struct h264_picture* pic)
{
	logd("s->picture_structure: %d", s->picture_structure);

	if(s->picture_structure == PICT_FRAME) {
		config_frame_info_frame(s, pic);
	} else if(s->picture_structure == PICT_TOP_FIELD) {
		config_frame_info_top_field(s, pic);
	} else if(s->picture_structure == PICT_BOTTOM_FIELD) {
		config_frame_info_bot_field(s, pic);
	}

	config_col_addr(s, pic, 0);
	config_col_addr(s, pic, 1);
}

struct frame_info {
	int used;
	int buf_idx;
	int field_poc[2];
	int frame_stucture;
	int top_ref_type;
	int bot_ref_type;
};

static int get_ref_type(struct h264_picture* pic, int is_bottom_field)
{
	if(pic->nal_ref_idc[is_bottom_field] == 0)
		return 2;
	return pic->long_ref ? 1: 0;
}

static void collect_ref_frame_info(struct h264_dec_ctx *s,
	struct frame_info* frame_info, int* valid_frame, int* valid_frame_num)
{
	int i, list;
	struct h264_picture* pic = NULL;

	memset(frame_info, 0, 32 * sizeof(struct frame_info));
	*valid_frame_num = 0;

	logd("list_count: %d, num_ref: %d", s->sh.list_count, s->sh.num_ref_idx[0]);

	for(list = 0; list < s->sh.list_count; list++) {
		for(i = 0; i < s->sh.num_ref_idx[list]; i++) {
			int buf_idx = 0;

			pic = s->frame_info.ref_list[list][i].parent;
			if (pic) {
				buf_idx = pic->buf_idx;
				frame_info[buf_idx].buf_idx = buf_idx;

				if(pic->refrence == PICT_FRAME) {
					frame_info[buf_idx].field_poc[0] = pic->field_poc[0];
					frame_info[buf_idx].field_poc[1] = pic->field_poc[1];
					frame_info[buf_idx].frame_stucture = 1;

					if(pic->picture_structure == PICT_FRAME) {
						frame_info[buf_idx].frame_stucture =
							pic->mbaff_frame ? 2: 0;
					}

					frame_info[buf_idx].top_ref_type = get_ref_type(pic, 0);
					frame_info[buf_idx].bot_ref_type = get_ref_type(pic, 1);
				} else if(pic->refrence == PICT_TOP_FIELD) {
					frame_info[buf_idx].field_poc[0] = pic->field_poc[0];
					frame_info[buf_idx].frame_stucture = 1;
					frame_info[buf_idx].top_ref_type = get_ref_type(pic, 0);
				} else if(pic->refrence == PICT_BOTTOM_FIELD) {
					frame_info[buf_idx].field_poc[1] = pic->field_poc[1];
					frame_info[buf_idx].frame_stucture = 1;
					frame_info[buf_idx].top_ref_type = get_ref_type(pic, 1);
				}
			} else {
				logw("ref list maybe error");
				frame_info[0].field_poc[0] = 0;
				frame_info[0].field_poc[1] = 0;
				frame_info[0].frame_stucture = 0;
				frame_info[0].top_ref_type = 0;
				frame_info[0].bot_ref_type = 0;
			}

			valid_frame[*valid_frame_num] = buf_idx;
			if(frame_info[buf_idx].used == 0)
				(*valid_frame_num)++;
			frame_info[buf_idx].used = 1;
		}
	}
}

static void config_single_ref_frame(struct h264_dec_ctx *s,
	struct frame_info* frame_info, int buf_idx)
{
	struct frame_struct_ref_info frm_info = {0};
	struct h264_picture* pic = &s->frame_info.picture[buf_idx];

	config_frame_poc(s, pic, 0);
	config_frame_poc(s, pic, 1);

	frm_info.frm_struct = frame_info[buf_idx].frame_stucture;
	frm_info.top_ref_type = frame_info[buf_idx].top_ref_type;
	frm_info.bot_ref_type = frame_info[buf_idx].bot_ref_type;
	config_frame_struct(s, pic, &frm_info);

	config_col_addr(s, pic, 0);
	config_col_addr(s, pic, 1);
}

static void config_refrence_frame_info(struct h264_dec_ctx *s)
{
	int i;
	struct frame_info frame_info[32];
	int valid_frame[32] = {0};
	int valid_frame_num = 0;

	collect_ref_frame_info(s, frame_info, valid_frame, &valid_frame_num);

	logd("valid_frame_num: %d", valid_frame_num);

	for(i = 0; i < valid_frame_num; i++) {
		config_single_ref_frame(s, frame_info, valid_frame[i]);
	}
}

/* ************** framebuffer data struct ********
    |-------------------------------------------------|
    |  top field poc/ frame poc (32bit)               |
    |-------------------------------------------------|
    |  bottom field poc (32 bit)                      |
    |-------------------------------------------------|
    |    frame_struct_ref_info (32bit)                |
    |-------------------------------------------------|
    | top filed/frame motion vector collocated info   |
    |-------------------------------------------------|
    | bottom field motion vector collocated info      |
    |-------------------------------------------------|
*/
static void config_framebuffer_info(struct h264_dec_ctx *s)
{
	if(s->fp_reg)
		fprintf(s->fp_reg, "// config framebuffer info\n");

	config_frame_info(s, s->frame_info.cur_pic_ptr);
	config_refrence_frame_info(s);
}

static void config_picture_info(struct h264_dec_ctx *s)
{
	int i;
	int width = 0, height = 0, stride = 0;
	u32* ptr_tmp = NULL;
	u32 phy_addr_offset[3] = {0, 0, 0};
	struct h264_picture* pic = NULL;
	struct h264_picture* cur_pic = s->frame_info.cur_pic_ptr;
	struct frame_format_reg frm_format = {0};
	struct frame_size_reg frm_size = {0};

	width = s->width;
	height = s->height;

	if(s->fp_reg)
		fprintf(s->fp_reg, "// config picture info\n");

	if(s->pix_format == MPP_FMT_YUV420P ||
		s->pix_format == MPP_FMT_NV12 ||
		s->pix_format == MPP_FMT_NV21)
		frm_format.color_mode = 0;
	else
		frm_format.color_mode = 4;

	if (s->max_width)
		stride = s->width;
	else
		stride = cur_pic->frame->mpp_frame.buf.stride[0];

	frm_format.cbcr_interleaved = (s->pix_format == MPP_FMT_NV12) ||
		(s->pix_format == MPP_FMT_NV21);

	phy_addr_offset[0] = s->decoder.output_y *
		cur_pic->frame->mpp_frame.buf.stride[0] + s->decoder.output_x;

	if ((s->pix_format == MPP_FMT_NV12) || (s->pix_format == MPP_FMT_NV21)) {
		phy_addr_offset[1] = s->decoder.output_y *
			cur_pic->frame->mpp_frame.buf.stride[1] / 2 +
			s->decoder.output_x;
	} else {
		phy_addr_offset[1] = s->decoder.output_y *
			cur_pic->frame->mpp_frame.buf.stride[1] / 2 +
			s->decoder.output_x / 2;
		phy_addr_offset[2] = s->decoder.output_y *
			cur_pic->frame->mpp_frame.buf.stride[2] / 2 +
			s->decoder.output_x / 2;
	}

	for(i = 0; i < s->frame_info.max_valid_frame_num; i++) {
		pic = &s->frame_info.picture[i];
		if(pic->frame) {
			if (s->decoder.rotmir_flag && i == cur_pic->rotmir_idx) {
				frm_size.pic_xsize = s->rotmir_width;
				frm_size.pic_ysize = s->rotmir_height;
				frm_format.stride = s->rotmir_width;
			} else {
				frm_size.pic_xsize = width;
				frm_size.pic_ysize = height;
				frm_format.stride = stride;
			}
			ptr_tmp = (u32*)&frm_format;
			write_reg(s->regs_base + FRAME_FORMAT_REG(i), *ptr_tmp, s->fp_reg);
			ptr_tmp = (u32*)&frm_size;
			write_reg(s->regs_base + FRAME_SIZE_REG(i), *ptr_tmp, s->fp_reg);
			write_reg(s->regs_base + FRAME_YADDR_REG(i),
				pic->frame->phy_addr[0] + phy_addr_offset[0], s->fp_reg);
			write_reg(s->regs_base + FRAME_CBADDR_REG(i),
				pic->frame->phy_addr[1] + phy_addr_offset[1], s->fp_reg);
			write_reg(s->regs_base + FRAME_CRADDR_REG(i),
				pic->frame->phy_addr[2] + phy_addr_offset[2], s->fp_reg);
		}
	}

#ifndef AIC_VE_DRV_V10
	int cur_frame_idx = s->frame_info.cur_pic_ptr->buf_idx;
	write_reg(s->regs_base + PIC_INFO_WRITE_END_REG, 0, s->fp_reg);
	write_reg(s->regs_base + GLB_CUR_FRM_IDX_REG, cur_frame_idx, s->fp_reg);
#endif
}

static void config_mc_register(struct h264_dec_ctx *s)
{
	int logwd_y, logwd_c;
	struct h264_pps_info* cur_pps = s->pps_buffers[s->active_pps_id];

	if(s->fp_reg)
		fprintf(s->fp_reg, "// config mc reg\n");

	write_reg(s->regs_base + MC_DMA_MODE_REG, 0, s->fp_reg);

	int implicited_en = (s->sh.slice_type == H264_SLICE_B) &&
		(cur_pps->weighted_bipred_flag == 2);
	int weighted_pred_en = (s->sh.slice_type == H264_SLICE_P &&
		cur_pps->weighted_pred_flag) ||
		((s->sh.slice_type == H264_SLICE_B) &&
		cur_pps->weighted_bipred_flag);

	write_reg(s->regs_base + MC_WP_EN_REG,
		weighted_pred_en << 1 | implicited_en, s->fp_reg);

	if((s->sh.slice_type == H264_SLICE_B) &&
		(cur_pps->weighted_bipred_flag == 2)) {
		logwd_c = logwd_y = 5;
	} else {
		logwd_c = s->sh.pwt.chroma_log2_weight_denom;
		logwd_y = s->sh.pwt.luma_log2_weight_denom;
	}
	write_reg(s->regs_base + MC_WP_LOGWD_REG, logwd_c << 3 | logwd_y,
		s->fp_reg);
}

static void config_dblk_register(struct h264_dec_ctx *s)
{
	struct h264_sps_info* cur_sps = s->sps_buffers[s->active_sps_id];
	struct h264_picture* cur_pic = s->frame_info.cur_pic_ptr;
	u32 *ptr_tmp = NULL;
	struct reg_dec_config dec_config = {0};

	if(s->fp_reg)
		fprintf(s->fp_reg, "// config dblk reg\n");

	int tmp = (s->sh.mbaff_frame << 2) |
		(s->sh.field_pic_flag << 1) |
		s->sh.bottom_field_flag;
	write_reg(s->regs_base + DBLK_PIC_TYPE_REG, tmp, s->fp_reg);

	write_reg(s->regs_base + DBLK_PIC_SIZE_REG,
		(s->width << 16) | s->height, s->fp_reg);
	write_reg(s->regs_base + DBLK_EN_REG, 1, s->fp_reg);
	write_reg(s->regs_base + DBLK_BUF_Y_REG,
		s->frame_info.dblk_y_buf->phy_addr, s->fp_reg);
	write_reg(s->regs_base + DBLK_BUF_C_REG,
		s->frame_info.dblk_c_buf->phy_addr, s->fp_reg);

	dec_config.dec_chroma_idc = !cur_sps->chroma_format_idc;
	dec_config.dec_luma_only = cur_sps->chroma_format_idc == 0;
	dec_config.dec_wr_en = 1;
	dec_config.uv_interleave = (s->pix_format == MPP_FMT_NV12) ||
		(s->pix_format == MPP_FMT_NV21);
	dec_config.uv_alternative = s->pix_format == MPP_FMT_NV21;
	ptr_tmp = (u32*)&dec_config;
	write_reg(s->regs_base + DEC_CONFIG_REG, *ptr_tmp, s->fp_reg);

	write_reg(s->regs_base + DEC_FRAME_IDX_REG, cur_pic->buf_idx, s->fp_reg);
}

static void avc_reset(struct h264_dec_ctx *s)
{
#ifdef AIC_VE_DRV_V10
	if(s->fp_reg)
		fprintf(s->fp_reg, "// reset avc\n");

	write_reg(s->regs_base + AVC_RESET_REG, 1, s->fp_reg);

	// wait until reset finish
	while(read_reg(s->regs_base + AVC_RESET_REG)) {
	};
#endif
}

static void config_bitstream(struct h264_dec_ctx *s)
{
	u32 end_addr = s->curr_packet->phy_base + s->curr_packet->phy_size - 1;
	u32 bit_offset = s->curr_packet->phy_offset * 8 + s->bit_offset;
	u32 bit_length = s->curr_packet->size * 8 - s->bit_offset;
#ifdef SW_SEARCH_START_CODE_EN
	bit_length = s->cur_slice_bit_len;
#endif

	if (s->fp_reg)
		fprintf(s->fp_reg, "// config bitstream\n");
#ifndef AIC_VE_DRV_V10
	write_reg(s->regs_base + VLD_CTRL_REG, 3, s->fp_reg);
#endif
	write_reg(s->regs_base + VLD_DATA_START_ADDR_REG,
		s->curr_packet->phy_base, s->fp_reg);
	write_reg(s->regs_base + VLD_DATA_END_ADDR_REG, end_addr, s->fp_reg);
	write_reg(s->regs_base + VLD_BIT_OFFSET_REG, bit_offset, s->fp_reg);
	write_reg(s->regs_base + VLD_DATA_LEN_REG, bit_length, s->fp_reg);
	write_reg(s->regs_base + VLD_DATA_VALID_REG, 0x80000003, s->fp_reg);
}

static void clear_interrupt_register(struct h264_dec_ctx *s)
{
#ifdef AIC_VE_DRV_V10
	write_reg(s->regs_base + VE_STATUS_REG, 7 << 16, s->fp_reg);
#else
	write_reg(s->regs_base + GLB_INT_REG, 0x07, s->fp_reg);
	write_reg(s->regs_base + GLB_STATUS_REG, 0x37, s->fp_reg);
#endif
}

static void config_internal_memory(struct h264_dec_ctx *s)
{
	if (s->fp_reg)
		fprintf(s->fp_reg, "// config internal memory\n");
	write_reg(s->regs_base + MB_COL_BUF_ADDR_REG,
		s->frame_info.mb_col_info_buf->phy_addr, s->fp_reg);
	write_reg(s->regs_base + MBINFO_BUF_ADDR_REG,
		s->frame_info.mb_info_buf->phy_addr, s->fp_reg);
	write_reg(s->regs_base + MB_RECON_BASE_ADDR_REG,
		s->frame_info.intrap_buf->phy_addr, s->fp_reg);
}


static void config_pp_register(struct h264_dec_ctx *s)
{
#ifndef AIC_VE_DRV_V10
	int rotate = MPP_ROTATION_GET(s->decoder.rotmir_flag);
	int flip_v = MPP_FLIP_V_GET(s->decoder.rotmir_flag);
	int flip_h = MPP_FLIP_H_GET(s->decoder.rotmir_flag);
	struct reg_pp_ctrl pp_ctrl = {0};
	uint32_t *pVal;

	if (s->fp_reg) {
		fprintf(s->fp_reg, "// config pp frame idx\n");
	}

	if (rotate != MPP_ROTATION_0) {
		pp_ctrl.rotmir_en = 1;
		if (rotate == MPP_ROTATION_0) {
		    pp_ctrl.rotate = 0;
		} else if (rotate == MPP_ROTATION_90) { // left rotate 90
		    pp_ctrl.rotate = 1;
		} else if (rotate == MPP_ROTATION_180) {
		    pp_ctrl.rotate = 2;
		} else if (rotate == MPP_ROTATION_270) {
		    pp_ctrl.rotate = 3;
		}
	}

	pp_ctrl.mirror = (flip_v ? 1 : 0) | (flip_h ? 2 : 0);

	pVal = (uint32_t *)&pp_ctrl;
	write_reg(s->regs_base + PP_CTRL_REG, *pVal, s->fp_reg);

	if (s->decoder.rotmir_flag && s->frame_info.cur_pic_ptr->rotmir_idx >= 0) {
		write_reg(s->regs_base + PP_FRAME_IDX_REG, s->frame_info.cur_pic_ptr->rotmir_idx, s->fp_reg);
	} else {
		write_reg(s->regs_base + PP_FRAME_IDX_REG, s->frame_info.cur_pic_ptr->buf_idx, s->fp_reg);
	}
#endif
}


static void config_avc_ctrl(struct h264_dec_ctx *s)
{
#ifdef AIC_VE_DRV_V10
	struct reg_avc_ctrl avc_ctrl = {0};
	u32 *pval;

	if (s->fp_reg)
		fprintf(s->fp_reg, "// slice start\n");
	avc_ctrl.stcd_detect_en = 1;
	avc_ctrl.eptb_detection = 1;
	avc_ctrl.bit_request_int_enable = 1;
	avc_ctrl.dec_error_int_enable = 1;
	avc_ctrl.dec_finish_int_enable = 1;
	avc_ctrl.slice_start = 1;
	pval = (u32*)(&avc_ctrl);
	write_reg(s->regs_base + AVC_CTRL_REG, *pval, s->fp_reg);
#else
	write_reg(s->regs_base + GLB_INT_REG, 0x07, s->fp_reg);
	write_reg(s->regs_base + GLB_CTRL_REG, 0x2, s->fp_reg);
#endif
}

void config_cache(struct h264_dec_ctx *s)
{
#ifndef AIC_VE_DRV_V10
	u32 *pVal = NULL;
	struct h264_picture* pic = NULL;
	struct reg_cache_config cache_cfg = { 0 };

	if (s->sh.slice_type != H264_SLICE_P && s->sh.slice_type != H264_SLICE_B)
		return;

	// not support cache in old version
	if (s->ve_version < 0x501 || s->ve_version == 0x30000)
		return;
	// cache refresh
	write_reg(s->regs_base + CACHE_CTRL_REG, 0, s->fp_reg);
	while(read_reg(s->regs_base + CACHE_CTRL_REG) & 0x10) {
		usleep(10);
	}

	// config cache
	if (s->sh.slice_type == H264_SLICE_P) {
		write_reg(s->regs_base + CACHE_SIZE_REG, SINGLE_CACHE_SIZE, s->fp_reg);
	} else {
		write_reg(s->regs_base + CACHE_SIZE_REG, DUAL_CACHE_SIZE, s->fp_reg);
	}
	write_reg(s->regs_base + CACHE_BUF_SIZE_REG, CACHE_BUF_SIZE, s->fp_reg);

	if ((s->pix_format == MPP_FMT_NV12 || s->pix_format == MPP_FMT_NV21))
		cache_cfg.cache_cbcr_inter = 1;
	cache_cfg.cache_page_merge = CACHE_MERGE;
	cache_cfg.cache_dual_mode = (s->sh.slice_type == H264_SLICE_B);

	pic = s->frame_info.ref_list[0][0].parent;
	cache_cfg.cache_frame_idx0 = pic->buf_idx;
	if (s->sh.slice_type == H264_SLICE_B) {
		pic = s->frame_info.ref_list[1][0].parent;
		cache_cfg.cache_frame_idx1 = pic->buf_idx;
	}

	pVal = (u32*)&cache_cfg;
	write_reg(s->regs_base + CACHE_CFG_REG, *pVal, s->fp_reg);
	write_reg(s->regs_base + CACHE_CTRL_REG, 1, s->fp_reg);
	while((read_reg(s->regs_base + CACHE_CTRL_REG) & 0x20) != 0x20) {
		usleep(10);
	}
#endif
}

static int check_decode_status(struct h264_dec_ctx *s, u32 status)
{
	const char name[3][64] = {"P", "B", "I"};
	struct h264_picture* cur_pic = s->frame_info.cur_pic_ptr;

	logi("%p, %s slice, cycle: %d", s, name[s->sh.slice_type],
		read_reg(s->regs_base + CYCLES_REG));
	s->decode_mb_num = read_reg(s->regs_base + CORRECT_DECODE_MB_NUMBER_REG);

	if (status & AVC_ERROR) {
		loge("%s slice(poc: %d, slice_num: %d) decode error, status: %x",
			name[s->sh.slice_type], cur_pic->field_poc[0], s->cur_slice_num, status);
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
		logi("finish, cur pic top poc: %d, bot poc: %d, buf_idx: %d, "
			"nal_ref_idc: %d",
			cur_pic->field_poc[0], cur_pic->field_poc[1],
			cur_pic->buf_idx, s->nal_ref_idc);
		return 0;
	}

	logw("unkown error, status: %x", status);
	return -1;
}

static void get_ve_version(struct h264_dec_ctx *s)
{
	s->ve_version = read_reg(s->regs_base + 0xfc);
}

int decode_slice(struct h264_dec_ctx *s)
{
	int ret = 0;
	u32 status = 0;
	struct h264_picture* cur_pic = s->frame_info.cur_pic_ptr;
	struct h264_pps_info* cur_pps = s->pps_buffers[s->active_pps_id];

	ve_get_client();
	if(s->avc_start) {
		ve_config_ve_top_reg(s);
		avc_reset(s);
		s->avc_start = 0;
	} else {
		ve_reset_pic_info_reg(s);
	}
	logi("s->cur_slice_num: %d", s->cur_slice_num);
	get_ve_version(s);
	init_decode_mb_number(s);
	enable_irq(s);
	clear_interrupt_register(s);

	config_sps(s);
	config_avc_pps_register(s);
	config_avc_sh_register(s);

	if((s->sh.slice_type == H264_SLICE_P && cur_pps->weighted_pred_flag) ||
		(s->sh.slice_type == H264_SLICE_B && cur_pps->weighted_bipred_flag))
		config_weight_pred_para(s);

	config_quant_matrix(s);
	config_reflist(s);
	config_framebuffer_info(s);
	config_picture_info(s);
	config_dblk_register(s);
	config_mc_register(s);
	config_internal_memory(s);
	config_cache(s);
	config_pp_register(s);
	config_avc_ctrl(s);
	config_bitstream(s);

	if (ve_wait(&status) < 0) {
		loge("ve timeout, avc status: %x", status);
		logi("correct number: %d",
			read_reg(s->regs_base + CORRECT_DECODE_MB_NUMBER_REG));
		ret = -1;
		goto error;
	}

	s->slice_end_offset = read_reg(s->regs_base + VLD_BIT_OFFSET_REG) / 8 - s->curr_packet->phy_offset;
	ret = check_decode_status(s, status);
	if (ret == 0) {
		ve_put_client();
		return 0;
	}

error:
	cur_pic->frame->mpp_frame.flags |= FRAME_FLAG_ERROR;
	ve_put_client();
	s->avc_start = 1;
	s->error = H264_DECODER_ERROR_HARD;
	return ret;
}
