/*
 * Copyright (C) 2020-2026 ArtInChip Technology Co. Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 *  author: <xiaodong.zhao@artinchip.com>
 *  Desc: mpeg12 register interface
 *
 */

#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include "mpeg_register.h"
#include "ve_top_register.h"
#include "mpeg4_decoder.h"
#include "avc_register_v2.h"
#include "mpp_log.h"
#include "ve.h"


// same with mpeg1/2
static void save_mb_info_data(struct mpeg4_ctx *s)
{
    int data_len = 0;
    int total_mbs = s->ref.mp4_state->mb_width * s->ref.mp4_state->mb_height;

    ve_buffer_sync(s->ref.mb_cfg_phy_addr, CACHE_CLEAN);

    for (int i = 0; i < total_mbs; i++) {
        data_len += s->ref.mb_cfg_info_list[i].len * 4;
        if (s->ref.mb_cfg_info_list[i].len == 0) {
            loge("mb_addr: %d, mb_cfg_data is 0", i);
        }
    }

#ifdef MPEG4_DUMP_ENABLE
    unsigned char *data = (unsigned char *)s->ref.mb_cfg_data;
    char fPath[1024];

    fprintf(s->fp_reg, "// write mb info\n");
    fprintf(s->fp_reg, "write_mem %08x %08x load_data_%d.txt\n", s->ref.mb_cfg_phy_addr->phy_addr, data_len, s->dec_frame_num);

    // careful, we should 256 byte align for gsim
    int align_size = (data_len + 255) / 256 * 256;
    fprintf(s->fp_reg, "// pat_dram_load(0x%02x, 0x%x, 0x%x);\n", s->dec_frame_num, s->ref.mb_cfg_phy_addr->phy_addr, align_size);

    snprintf(fPath, sizeof(fPath), "%s/dram_load%02x.bin", s->asic_path, s->dec_frame_num);
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
#endif
}

// same with mpeg1/2
static void config_vpu_reg(struct mpeg4_ctx *s)
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

// same with mpeg1/2
static void config_glb_reg(struct mpeg4_ctx *s, enum CODEC_TYPE type)
{
    if (s->fp_reg)
        fprintf(s->fp_reg, "// config codec type\n");

    write_reg(s->regs_base + GLB_CODEC_TYPE_REG, type, s->fp_reg);

    if (s->fp_reg)
        fprintf(s->fp_reg, "// config irq\n");

    write_reg(s->regs_base + GLB_INT_REG, 0x07, s->fp_reg);
    write_reg(s->regs_base + GLB_STATUS_REG, 0x37, s->fp_reg);
}

// same with mpeg1/2
static void config_pic_init(struct mpeg4_ctx *s)
{
    if (s->fp_reg)
        fprintf(s->fp_reg, "// config pic init\n");

    write_reg(s->regs_base + GLB_CTRL_REG, 1, s->fp_reg);
}

static void config_picture_size(struct mpeg4_ctx *s)
{
    if (s->fp_reg)
        fprintf(s->fp_reg, "// config picture size\n");

#ifndef AIC_1602
    struct reg_glb_pic_size pic_size = {0};

    pic_size.pic_x_size = s->ref.mp4_state->mb_width * 16;
    pic_size.pic_y_size = s->ref.mp4_state->mb_height * 16;

    uint32_t *pVal = (uint32_t *)&pic_size;
    write_reg(s->regs_base + GLB_PIC_SIZE_REG, *pVal, s->fp_reg);

    struct reg_glb_mb_size mb_size = {0};
    mb_size.pic_mb_x = s->ref.mp4_state->mb_width - 1;
    mb_size.pic_mb_y = s->ref.mp4_state->mb_height - 1;
    pVal = (uint32_t *)&mb_size;
    write_reg(s->regs_base + GLB_PIC_MB_SIZE_REG, *pVal, s->fp_reg);
#endif
}

static void config_mb_info_data(struct mpeg4_ctx *s)
{
#ifndef AIC_1602
    reg_mpeg_mb_info_cfg cfg = {0};
    uint32_t *pVal = (uint32_t *)&cfg;
    int data_len = 0;
    for (int i = 0; i < s->ref.mp4_state->mb_width * s->ref.mp4_state->mb_height; i++) {
        data_len += s->ref.mb_cfg_info_list[i].len * 4;
    }

    cfg.byte_len = data_len;
    cfg.requst = 1;

    if (s->fp_reg)
        fprintf(s->fp_reg, "// config mb_info\n");

    write_reg(s->regs_base + MPEG_MB_CFG_BASE_ADDR_REG, s->ref.mb_cfg_phy_addr->phy_addr, s->fp_reg);
    write_reg(s->regs_base + MPEG_MB_CFG_REG, *pVal, s->fp_reg);
#endif
}

// diffrent with mpeg1/2
static void config_header_info(struct mpeg4_ctx *s)
{
    uint32_t *pVal;

    if (s->fp_reg)
        fprintf(s->fp_reg, "// config picture type\n");

#ifndef AIC_1602
    struct reg_avc_slice_type slice_type = {0};
    // slice_type.bottom_field_flag = mpeg12->pic_code_extension.picture_structure == BOTTOM_FIELD;
    slice_type.field_pic_flag = 0; // s->ref.mp4_state->hdr.interlaced;
    pVal = (uint32_t *)&slice_type;
    write_reg(s->regs_base + AVC_SLICE_TYPE_REG, *pVal, s->fp_reg);

    if (s->fp_reg)
        fprintf(s->fp_reg, "// config first mb_addr\n");

    struct reg_fst_mb_addr fst_mb_addr = {0};
    fst_mb_addr.first_mb_x = 0;
    fst_mb_addr.first_mb_y = 0;
    fst_mb_addr.first_slice_in_pic = 1;
    pVal = (uint32_t *)&fst_mb_addr;
    write_reg(s->regs_base + AVC_FST_MB_ADDR_REG, *pVal, s->fp_reg);
#endif
}

// only in mpeg4
static void config_mc_register(struct mpeg4_ctx *s)
{
    if (s->fp_reg)
        fprintf(s->fp_reg, "// config mc register\n");

    // 1/4 precison
    struct mc_ctrl val = {0};
    val.round = s->ref.mp4_state->hdr.rounding_type;
    val.quart_interp = s->ref.mp4_state->hdr.quarter_pixel;
    uint32_t *pVal = (uint32_t *)&val;
    write_reg(s->regs_base + MC_CTRL_REG, *pVal, s->fp_reg);
}

static void config_dblk_register(struct mpeg4_ctx *s)
{
    if (s->fp_reg)
        fprintf(s->fp_reg, "// config dblk pic type register\n");

    int pic_xsize = s->ref.mp4_state->mb_width * 16;
    int pic_ysize = s->ref.mp4_state->mb_height * 16;
    uint32_t *pVal;

    struct reg_pic_type pic_type = {0};
    memset(&pic_type, 0, sizeof(int));
    pic_type.mbaff = 0;
    pic_type.bottom_field_flag = 0;
    pic_type.field = 0; // s->ref.mp4_state->hdr.interlaced;  // TODO: maybe error
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
    dec_config.dec_chroma_idc = 0; //! s->pic_code_extension.chroma_420_type;
    dec_config.dec_luma_only = 0;  //! s->pic_code_extension.chroma_420_type;
    dec_config.dec_range_en = 0;
    dec_config.dec_wr_en = 1;

    dec_config.endian = 0;

    dec_config.uv_interleave = 0;
    dec_config.uv_alternative = 0;
    pVal = (uint32_t *)&dec_config;

    if (s->fp_reg)
        fprintf(s->fp_reg, "// config dblk dec register\n");
    write_reg(s->regs_base + DEC_CONFIG_REG, *pVal, s->fp_reg);
    write_reg(s->regs_base + DEC_FRAME_IDX_REG, s->ref.cur_frame_id, s->fp_reg);
}

static void config_mpeg_quant_matrix(struct mpeg4_ctx *s)
{
#ifndef AIC_1602
    int i;
    uint32_t *pVal = NULL;
    reg_mpeg_quant_matrix qm_val = {0};

    if (s->fp_reg)
        fprintf(s->fp_reg, "// config intra quant matrix\n");
    qm_val.mpeg_quant_matrix_access = 1;
    qm_val.mpeg_quant_matrix_write = 1;
    for (i = 0; i < 64; i++) {
        qm_val.quant_matrix_addr = i;
        qm_val.quant_matrix_data = s->ref.mp4_state->hdr.intra_quant_matrix[i];
        pVal = (uint32_t *)&qm_val;
        write_reg(s->regs_base + MPEG_QUANT_MATRIX_REG, *pVal, s->fp_reg);
    }

    if (s->fp_reg)
        fprintf(s->fp_reg, "// config inter quant matrix\n");
    for (i = 0; i < 64; i++) {
        qm_val.quant_matrix_addr = 64 + i;
        qm_val.quant_matrix_data = s->ref.mp4_state->hdr.nonintra_quant_matrix[i];
        pVal = (uint32_t *)&qm_val;
        write_reg(s->regs_base + MPEG_QUANT_MATRIX_REG, *pVal, s->fp_reg);
    }
    if (s->fp_reg)
        fprintf(s->fp_reg, "// quant matrix release\n");
    write_reg(s->regs_base + MPEG_QUANT_MATRIX_REG, 0, s->fp_reg);
#endif
}

// different with mpeg1/2
static void config_picture_info_register(struct mpeg4_ctx *s)
{
    if (s->fp_reg)
        fprintf(s->fp_reg, "// config picture info register\n");

    uint32_t *pVal;
    unsigned int xsize = 0, ysize = 0, stride = 0;
    frame_reg_offset0 offset0;
    memset(&offset0, 0, sizeof(int));
    frame_reg_offset1 offset1;
    memset(&offset1, 0, sizeof(int));
    if ((s->ref.mp4_state->userdata_codec_version < 500) && (s->ref.mp4_state->userdata_codec_version != 0) &&
        (s->ref.mp4_state->userdata_codec_version != 311) && (s->ref.mp4_state->userdata_codec_version != 263)) {
        xsize = s->ref.mp4_state->horizontal_size;
        ysize = s->ref.mp4_state->vertical_size;
    } else {
        // divx311, divx5, h263
        xsize = s->ref.mp4_state->mb_width * 16;
        ysize = s->ref.mp4_state->mb_height * 16;
    }

    offset0.cbcr_interleaved = 0;
    offset0.frame_map = 0;
    offset0.color_mode = 0; // YUV420
    stride = s->ref.mp4_state->mb_width * 16; // stride

    for (int i = 0; i < MPEG4_FRAME_NUM; i++) {
        if (s->ref.frame[i]) {
            if (s->decoder.rotmir_flag && i == s->ref.cur_rotmir_id) {
                offset1.pic_xsize = s->ref.rotmir_h_stride;
                offset1.pic_ysize = s->ref.rotmir_v_stride;
                offset0.stride = s->ref.rotmir_h_stride;
            } else {
                offset1.pic_xsize = xsize;
                offset1.pic_ysize = ysize;
                offset0.stride = stride;
            }
            pVal = (uint32_t *)&offset0;
            write_reg(s->regs_base + PIC_INFO_START_REG + (5 * i) * 4, *pVal, s->fp_reg);
            pVal = (uint32_t *)&offset1;
            write_reg(s->regs_base + PIC_INFO_START_REG + (5 * i + 1) * 4, *pVal, s->fp_reg);
            write_reg(s->regs_base + PIC_INFO_START_REG + (5 * i + 2) * 4, s->ref.frame[i]->phy_addr[0], s->fp_reg);
            write_reg(s->regs_base + PIC_INFO_START_REG + (5 * i + 3) * 4, s->ref.frame[i]->phy_addr[1], s->fp_reg);
            write_reg(s->regs_base + PIC_INFO_START_REG + (5 * i + 4) * 4, s->ref.frame[i]->phy_addr[2], s->fp_reg);
        }
    }

    write_reg(s->regs_base + PIC_INFO_WRITE_END_REG, 0, s->fp_reg);
}

static void config_frame_buffer(struct mpeg4_ctx *s)
{
#ifndef AIC_1602
    uint32_t *pVal;

    if (s->fp_reg)
        fprintf(s->fp_reg, "// config cur frame idx\n");

    write_reg(s->regs_base + GLB_CUR_FRM_IDX_REG, s->ref.cur_frame_id, s->fp_reg);

    if (s->ref.mp4_state->hdr.prediction_type != I_VOP) {
        if (s->fp_reg)
            fprintf(s->fp_reg, "// config ref frame idx\n");

        reg_mpeg_ref_frm_idx ref_idx = {0};
        ref_idx.fwd_frm_idx = s->ref.fwd_frame_id;
        ref_idx.bwd_frm_idx = s->ref.bwd_frame_id;
        pVal = (uint32_t *)&ref_idx;
        write_reg(s->regs_base + MPEG_REF_BUF_IDX_REG, *pVal, s->fp_reg);
    }
#endif
}

static void config_pp_register(struct mpeg4_ctx *s)
{
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
    write_reg(s->regs_base + PP_CTRL_REG, *pVal, g_mpeg4_ctx->fp_reg);

    if (s->decoder.rotmir_flag && g_mpeg4_ctx->ref.cur_rotmir_id >= 0) {
        write_reg(s->regs_base + PP_FRAME_IDX_REG, g_mpeg4_ctx->ref.cur_rotmir_id, g_mpeg4_ctx->fp_reg);
    } else {
        write_reg(s->regs_base + PP_FRAME_IDX_REG, g_mpeg4_ctx->ref.cur_frame_id, g_mpeg4_ctx->fp_reg);
    }
}

static void config_slice_start(struct mpeg4_ctx *s)
{
    if (s->fp_reg)
        fprintf(s->fp_reg, "// config slice start\n");

    write_reg(s->regs_base + GLB_CTRL_REG, 2, s->fp_reg);
}

#ifdef MPEG4_COMP_RES
int compare_output_data(struct mpeg4_ctx *s, unsigned char *data, int size, int frm_num, int idx)
{
    unsigned char *comp_buf = NULL;
    char file_name[384] = {0};
    FILE *fp = NULL;

    comp_buf = (unsigned char*)malloc(size);
    if (comp_buf == NULL) {
        loge("malloc buf failed");
        return -1;
    }

    snprintf(file_name, sizeof(file_name), "/mnt/sdcard/mpeg4_cmp/ve_%s/comp%d%d.bin", s->pattern_name, frm_num, idx);
    fp = fopen(file_name, "rb");
    if (fp == NULL) {
        loge("open file:%s failed!\n", file_name);
        free(comp_buf);
        return -1;
    }
    fread(comp_buf, 1, size, fp);

    for (int i = 0; i < size; i++) {
        if (data[i] != comp_buf[i]) {
            loge("data err, pos: %d, hw:%x sw:%x", i, data[i], comp_buf[i]);
            return -1;
        }
    }
    fclose(fp);
    free(comp_buf);

    return 0;
}
#endif

void dump_output_data(struct mpeg4_ctx *s)
{
    unsigned char *hw_data[3] = {0};
    struct mpp_buf *video = NULL;
    int data_size[3] = {0};

    video = &s->ref.frame[s->ref.cur_frame_id]->mpp_frame.buf;
    if (NULL == video) {
        loge("video is null, cur_frame_id:%d", s->ref.cur_frame_id);
        return;
    }
    data_size[0] = video->size.height * video->stride[0];
    data_size[1] = data_size[2] = data_size[0] / 4;

    for (int i = 0; i < 3; i++) {
        dmabuf_sync(video->fd[i], CACHE_INVALID);
        hw_data[i] = mmap(NULL, data_size[i], PROT_READ, MAP_SHARED, video->fd[i], 0);
        if (hw_data[i] == MAP_FAILED) {
            loge("dmabuf alloc mmap failed!");
            break;
        }

#ifdef MPEG4_COMP_RES
        compare_output_data(s, hw_data[i], data_size[i], s->dec_frame_num, i);
#endif
#if MPEG4_DUMP_ENABLE
        char path[512] = {0};
        FILE *fp = NULL;
        snprintf(path, sizeof(path), "%s/comp%d%d.bin", s->asic_path, s->dec_frame_num, i);
        fp = fopen(path, "wb");
        if (fp) {
            fwrite(hw_data[i], 1, data_size[i], fp);
            fclose(fp);
        } else {
            loge("fopen file failed!");
        }
#endif
        munmap(hw_data[i], data_size[i]);
    }
}

void ve_mpeg4_decode_slice(void)
{
    struct mpeg4_ctx *s = g_mpeg4_ctx;
    unsigned int status;

    if (NULL == s) {
        loge("g_mpeg4_ctx is null");
        return;
    }

    save_mb_info_data(s);

    if (s->first_pic == 0) {
        s->first_pic = 1;
        ve_reset();
    }

    config_vpu_reg(s);
    config_glb_reg(s, MPEG4_DEC);

    config_pic_init(s);
    config_picture_size(s);
    config_mb_info_data(s);

    // 2.Configure header info
    config_header_info(s);
    config_mc_register(s);
    config_dblk_register(s);

    // 3. Configure quantization matrix
    config_mpeg_quant_matrix(s);

    config_picture_info_register(s);

    // Configure buffer addresses for reference and reconstructed frames
    config_frame_buffer(s);
    config_pp_register(s);

    // Configure trigger register to start decoding
    config_slice_start(s);

    if (ve_wait(&status) < 0) {
        loge("ve wait irq timeout");

        ve_reset();
    }

    // loge("status: %x", status);
    if (status & (1 << 1)) {
        loge("decode error, status: %x", status);
    }

    ve_put_client();

#if defined(MPEG4_DUMP_ENABLE) || defined(MPEG4_COMP_RES)
    dump_output_data(s);
#endif
}
