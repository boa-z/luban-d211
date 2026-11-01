/*
 * Copyright (C) 2020-2026 ArtInChip Technology Co. Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 *  author: <xiaodong.zhao@artinchip.com>
 *  Desc: avc register interface
 */

#ifndef AVC_REGISTER_V2_H
#define AVC_REGISTER_V2_H


/**************** AVC common reg (DBLK)***************/
struct reg_dblk_status {
    unsigned r : 1;         // [0]
    unsigned dblk_busy : 1; //
    unsigned r1 : 30;
};

#define DBLK_STATUS_REG (AVC_DBLK_BASE_ADDR + 0x08)

struct reg_pic_type {
    unsigned bottom_field_flag : 1; // 1 indicates bottom field, 0 indicates top field
    unsigned field : 1;             // 1 indicates field mode, 0 indicates frame mode
    unsigned mbaff : 1;             // mbaffFrameFlag
    unsigned pic_type : 2;          // for rv
    unsigned r0 : 2;
    unsigned codec_std : 3; // 0:avc; 1:mpeg1; 2:mpeg2; 3:mpeg4
    unsigned codec_type : 1; // 0:decoder; 1:encoder
    unsigned r1 : 21;
};
#define DBLK_PIC_TYPE_REG (AVC_DBLK_BASE_ADDR + 0x10)

struct reg_pic_size {
    // Picture height, when in field mode this value is half of frame height
    unsigned pic_ysize : 12; // [11:0]
    unsigned r0 : 4;
    unsigned pic_xsize : 12; // [27:16]
    unsigned r1 : 4;
};
#define DBLK_PIC_SIZE_REG (AVC_DBLK_BASE_ADDR + 0x14)

struct reg_dblk_type {
    unsigned dblk_enable : 1;
    unsigned force_dblk_start : 1;
    unsigned r1 : 30;
};
#define DBLK_EN_REG    (AVC_DBLK_BASE_ADDR + 0x40)

// Used for second AXI, VE requires
#define DBLK_BUF_Y_REG (AVC_DBLK_BASE_ADDR + 0x44)
#define DBLK_BUF_C_REG (AVC_DBLK_BASE_ADDR + 0x48)

struct reg_dec_config {
    unsigned endian : 2;            //
    unsigned uv_interleave : 1;     // Whether uv is interleaved
    unsigned uv_alternative : 1;    // 0-cbcr 1-crcb
    unsigned dec_chroma_idc : 2;    // 00-420; 01-400
    unsigned dec_luma_only : 1;
    unsigned dec_range_en : 1;      // Write 0
    unsigned dec_wr_en : 1;         // Write 1
    unsigned r : 23;
};
#define DEC_CONFIG_REG    (AVC_DBLK_BASE_ADDR + 0x4C)
#define DEC_FRAME_IDX_REG (AVC_DBLK_BASE_ADDR + 0x50)

struct reg_disp_config {
    unsigned endian : 2;
    unsigned uv_interleave : 1;
    unsigned uv_alternative : 1; // cbcr 1-crcb
    unsigned dec_chroma_idc : 2; // 00-420; 01-400
    unsigned dec_luma_only : 1;
    unsigned dec_range_en : 1;
    unsigned dec_wr_en : 1;
    unsigned r : 23;
};
#define DISP_CONFIG_REG    (AVC_DBLK_BASE_ADDR + 0x54)
#define DISP_FRAME_IDX_REG (AVC_DBLK_BASE_ADDR + 0x58)

#define DBLK_SUBSAMPLE_DISABLE      (AVC_DBLK_BASE_ADDR+0x70)
#define DBLK_SUBSAMPLE_ADDR_REG     (AVC_DBLK_BASE_ADDR+0x74)
/************ AVC MC ************************/
struct mc_ctrl {
    unsigned round : 1;
    unsigned quart_interp : 2; // 0: 1/2; 1: 1/4

    unsigned r : 29;
};
#define MC_CTRL_REG (AVC_MC_BASE_ADDR + 0x0C)

struct mc_dma_mode {
    unsigned mode : 1;
    unsigned r : 31;
};
#define MC_DMA_MODE_REG (AVC_MC_BASE_ADDR + 0x48)

struct mc_wp_en {
    unsigned implicited_en : 1;
    unsigned weighted_en : 1;
    unsigned r : 30;
};
#define MC_WP_EN_REG (AVC_MC_BASE_ADDR + 0x80)

struct mc_wp_logwd {
    unsigned logwd_y : 3;
    unsigned logwd_c : 3;
    unsigned r : 26;
};
#define MC_WP_LOGWD_REG (AVC_MC_BASE_ADDR + 0x84)

struct mc_wp_weight {
    unsigned imp_weight : 32;
};
#define MC_WP_WEIGHT_REG (AVC_MC_BASE_ADDR + 0x88)

/**
 * @brief Write protect region
 *
 * If VE writes DRAM out of this region, the VE module (h264/jpeg/png) status
 * will finish right now and generate an interrupt of PICTURE_INFO module.
 */
typedef struct {
    unsigned burst_len : 4;
    unsigned id : 4;
    unsigned r : 1;    // always 0
    unsigned flag : 1; // [9] :error flag
    unsigned r1 : 22;
} wr_proc_err_status;
#define WR_PROC_ERR_STATUS_REG (PIC_INFO_REG_BASE_ADDR + 0x38)
#define WR_PROC_ERR_ADDR_REG   (PIC_INFO_REG_BASE_ADDR + 0x3C)

/**
 * @brief Picture information configuration registers
 *
 * Memory range 0xA80-0xbfc is used for configuring picture information.
 * Each picture requires 5 consecutive registers:
 * - OFFSET+0: {10'b0, frame_map[1:0], color_mode[2:0], cbcr_interleaved[0], stride[15:0]}
 * - OFFSET+1: {pic_xsize[15:0], pic_ysize[15:0]}
 * - OFFSET+2: {luma_base[31:0]} (luma buffer base address)
 * - OFFSET+3: {cb_base[31:0]} (chroma blue buffer base address)
 * - OFFSET+4: {cr_base[31:0]} (chroma red buffer base address)
 */
typedef struct frame_reg_offset0 {
    unsigned stride : 16;          //[15:0], stride 16bytes align
    unsigned cbcr_interleaved : 1; // uv interleave
    unsigned color_mode : 3;       // 0-YUV420; 1-YUV422; 2-224; 3-YUV444; 4-YUV400
    unsigned frame_map : 2;        // 0- line map; 2- tile map
    unsigned r2 : 10;
} frame_reg_offset0;

typedef struct frame_reg_offset1 {
    unsigned pic_ysize : 16;
    unsigned pic_xsize : 16;
} frame_reg_offset1;

// ======== TOP ===============
struct reg_codec_type {
    unsigned codec_std : 3; // 0-avc; 1-mpeg1; 2-mpeg2; 3-mpeg4
    unsigned r0 : 5;
    unsigned codec_type : 1; // 0-decoder; 1-encoder
    unsigned r1 : 23;
};
#define GLB_CODEC_TYPE_REG (AVC_TOP_BASE_ADDR + 0x00)

struct reg_glb_ctrl {
    unsigned dec_finish_int_enable : 1;  // [0]: finish irq
    unsigned dec_error_int_enable : 1;   // [1]: decode error
    unsigned bit_request_int_enable : 1; // [2]: bit request
    unsigned r : 29;
};
#define GLB_INT_REG (AVC_TOP_BASE_ADDR + 0x04)

struct reg_glb_status {
    unsigned ve_finish : 1;   //
    unsigned ve_error : 1;    //
    unsigned bit_request : 1; //
    unsigned r : 29;
};
#define GLB_STATUS_REG (AVC_TOP_BASE_ADDR + 0x08)

struct reg_cur_frm_idx {
    unsigned cur_frm_idx : 5;
    unsigned r : 27;
};
#define GLB_CUR_FRM_IDX_REG (AVC_TOP_BASE_ADDR + 0x0C)

/**
 * [1]: slice start
 * [0]: pic_init
 */
#define GLB_CTRL_REG        (AVC_TOP_BASE_ADDR + 0x10)

struct reg_glb_pic_size {
    unsigned pic_x_size : 12; // [11:0]
    unsigned r0 : 4;
    unsigned pic_y_size : 12;
    unsigned r1 : 4;
};
#define GLB_PIC_SIZE_REG (AVC_TOP_BASE_ADDR + 0x14)

struct reg_glb_mb_size {
    unsigned pic_mb_y : 8; //[6:0]
    unsigned pic_mb_x : 8; // [14:8]
    unsigned r1 : 16;
};
#define GLB_PIC_MB_SIZE_REG (AVC_TOP_BASE_ADDR + 0x18)

struct reg_avc_sps {
    unsigned pic_height_in_map_units_minus1 : 8;    //[6:0]
    unsigned pic_width_in_mbs_minus1 : 8;           // [14:8]
    unsigned direct_8x8_inference_flag : 1;         // [16]: B_skip,B_Direct MV mode
    unsigned mb_adaptive_frame_filed_flag : 1;      // [17]: 0: no mbaff
    unsigned frame_mbs_only_flag : 1;               // [18]: 0: frame and field maybe in sequence
    unsigned uv_interleave : 1;                     // [19]: uv interleave, it must be same as dblk register 0x24c
    unsigned uv_alter : 1;                          //[20]: 0-cbcr; 1-crcb
    unsigned chroma_format_idc : 2;                 //[22:21]:
    unsigned r : 9;
};
#define AVC_SPS_REG (AVC_TOP_BASE_ADDR + 0x20)

struct reg_avc_pps {
    unsigned transform_8x8_mode_flag : 1;           // use 8x8 transform
    unsigned constrained_intra_pred_flag : 1;
    unsigned weighted_bipred_idc : 2;               // [3:2]: 00: default; 01: explicit mode; 10 : implicit mode(only use for B slice)
    unsigned weighted_pred_flag : 1;                // [4]:
    unsigned entropy_coding_mode_flag : 1;          // [5]:  0: CAVLC;  1: CABAC
    unsigned r0 : 2;
    unsigned num_ref_idx_l1_active_minus1_pic : 5;  // [12:8]: max num of ref-list1
    unsigned r1 : 3;
    unsigned num_ref_idx_l0_active_minus1_pic : 5;  // [20:16]: max num of ref-list0
    unsigned r2 : 11;
};
#define AVC_PPS_REG (AVC_TOP_BASE_ADDR + 0x24)

struct reg_avc_slice_type {
    unsigned bottom_field_flag : 1; // [0]: top or bot field
    unsigned field_pic_flag : 1;    // [1]: 0: frame; 1: field
    unsigned r0 : 6;
    unsigned slice_type : 4;        // [11:8]: slice type
    unsigned nal_ref_flag : 1;      // [12]:nal_ref_idc, used for reference
    unsigned r1 : 19;
};
#define AVC_SLICE_TYPE_REG (AVC_TOP_BASE_ADDR + 0x28)

struct reg_fst_mb_addr {
    unsigned first_mb_y : 8; // [0:6]
    unsigned r1 : 8;
    unsigned first_mb_x : 8; // [16:22]
    unsigned r2 : 7;
    unsigned first_slice_in_pic : 1;
};
#define AVC_FST_MB_ADDR_REG  (AVC_TOP_BASE_ADDR + 0x2C)

// direct_spatial_mv_pred_flag
#define AVC_MV_PRED_FLAG_REG (AVC_TOP_BASE_ADDR + 0x30)

struct reg_avc_ref_idx_num {
    unsigned num_ref_idx_active_override_flag : 1;
    unsigned r1 : 7;

    // [12:8]: max num of ref idx list1 -1
    unsigned num_ref_idx_l1_active_minus1 : 5;
    unsigned r2 : 3;
    // [20:16]: max num of ref idx list0 -1
    unsigned num_ref_idx_l0_active_minus1 : 5;
    unsigned r3 : 11;
};
#define AVC_REF_IDX_NUM_REG (AVC_TOP_BASE_ADDR + 0x34)

struct reg_avc_dblk_config {
    // [3:0]: FilterOffsetB = slice_beta_offset_div2 << 1
    unsigned slice_beta_offset_div2 : 4;

    // [7:4]: FilterOffsetA = slice_alpha_c0_offset_div2 << 1
    unsigned slice_alpha_offset_div2 : 4;
    unsigned disable_deblocking_filter_idc : 2;
    unsigned r : 22;
};
#define AVC_DBLK_CFG_REG (AVC_TOP_BASE_ADDR + 0x38)

struct reg_avc_wp_cfg{
    unsigned chroma_log2_weight_denom : 3; // [2:0]:
    unsigned r0 : 1;
    unsigned luma_log2_weight_denom : 3; // [6:4]:
    unsigned r1 : 25;
};
#define AVC_WP_CFG_REG (AVC_TOP_BASE_ADDR + 0x3C)

struct reg_avc_weight_pred {
    unsigned offset : 8; // [7:0]:offset
    unsigned weight : 9; // [16:8]:weight
    unsigned r0 : 3;
    /**
     * 0x00-0x1F Luma_L0 address
     * 0x20-0x3F Chroma_Cb_L0 address
     * 0x40-0x5F Chroma_Cr_L0 address
     * 0x60-0x7F Luma_L1 address
     * 0x80-0x9F Chroma_Cb_L1 address
     * 0xA0-0xBF Chroma_Cr_L1 address
     */
    unsigned weight_pred_addr : 8;
    unsigned r1 : 2;
    unsigned avc_weight_pred_write_enable : 1;
    unsigned avc_weight_pred_access : 1;
};
#define AVC_WEIGHT_PRED_REGISTER (AVC_TOP_BASE_ADDR + 0x40)

struct reg_avc_slice_qp {
    unsigned slice_qpy : 6; // [5:0]: Slice qp_y, [0,51]
    unsigned r0 : 2;
    unsigned chroma_qp_idx_offset : 6; // [13:8]: Cb qp offset, [-12,12]
    unsigned r1 : 2;
    unsigned second_chroma_qp_idx_offset : 6; // [21:16]: Cr qp offset [-12,12]
    unsigned r2 : 2;
    unsigned pic_init_qp_minus26 : 6; // [29:24]: pps_init_qp [-26,25]
    unsigned r3 : 2;
};
#define AVC_SHS_QP_REG (AVC_TOP_BASE_ADDR + 0x44)

typedef struct frame_struct_ref_info {
    unsigned top_ref_type : 2;  // top field refrence type: 00 (short-term); 01 (long-term); 10 (non-refrence); 11(reserve)
    unsigned r0 : 2;
    unsigned bot_ref_type : 2;  // bottom field refrence type: 00 (short-term); 01 (long-term); 10 (non-refrence); 11(reserve)
    unsigned r1 : 2;
    unsigned frm_struct : 2;    // picture type: 00 (frame); 01(field); 10(mbaff)
    unsigned r2 : 22;
} Frame_Struct_Ref_Info;

struct reg_avc_buf_info{
    /**
     * frame buffer content
     * 000 top poc
     * 001 bottom poc
     * 010 pic info, see Frame_Struct_Ref_Info
     * 011 luma buffer start address in DRAM
     * 100 chroma buffer start address in DRAM
     * 101 top field/frame mv collocated info start address in DRAM
     * 110 bottpm field/frame mv collocated info start address in DRAM
     */
    unsigned buf_idx : 5;           // [4:0]
    unsigned r1 : 3;
    unsigned content_select : 3;    // [10:8]
    unsigned r2 : 19;
    unsigned buf_info_write_en : 1; // [30]:
    unsigned buf_info_access : 1;   // [30]:
};
#define AVC_BUF_INFO_REGISTER (AVC_TOP_BASE_ADDR + 0x48)

#define AVC_BUF_INFO_CONTENT_REGISTER (AVC_TOP_BASE_ADDR + 0x4C)

struct reg_avc_ref_list {
    unsigned field_sel_list0 : 1;   // [0]: 0: top field; 1: bot field
    unsigned buf_idx_list0 : 5;     // [5:1]: forward ref buf_idx
    unsigned r0 : 2;
    unsigned field_sel_list1 : 1;   // [8]: 0: top field; 1: bot field
    unsigned buf_idx_list1 : 5;     // [13:9]: backward ref buf_idx
    unsigned r1 : 2;
    unsigned ref_idx : 5;           // [20:16]: ref idx, 0-31.
    unsigned r2 : 9;
    unsigned ref_idx_we : 1;
    unsigned ref_idx_rw : 1;        // [31]: 0:read from sram; 1: read config val
};
#define AVC_REF_LIST_REGISTER (AVC_TOP_BASE_ADDR + 0x54)

struct reg_ve_mbcol_addr {
    /**
     * MB_COL_BUF_ADDR, used to store colocated mb info for direct pred in B Slice.
     * 1)mb_field_flag: every MB in picture need 1bit, max pic number is 17,
     *      we need alloc max size buffer, 34K byte
     * 2)mb_non_inter_flag: every MB in picture need 1bit, max pic number is 17,
     *      we need alloc max size buffer, 34K byte
     * total 68K
     */

    unsigned addr : 32; // [31:10]
};
#define MB_COL_BUF_ADDR_REG     (AVC_TOP_BASE_ADDR + 0x58)

#define MB_COL_BUF_ADDR_EXT_REG (AVC_TOP_BASE_ADDR + 0x5C)

struct reg_ve_mbinfo_addr {
    /**
     * mb_header: 4K
     * mvd: 4K
     * mvp_intra: 4K
     * we need total 12K bytes
     */
    unsigned addr : 32; // [31: 10], MBINFO_BUF_ADDR
};
#define MBINFO_BUF_ADDR_REG        (AVC_TOP_BASE_ADDR + 0x60)
#define MBINFO_BUF_ADDR_EXT_REG    (AVC_TOP_BASE_ADDR + 0x64)

// store last MB line data for intra pred, we need (stride * 2) byte
#define MB_RECON_BASE_ADDR_REG     (AVC_TOP_BASE_ADDR + 0x68)
#define MB_RECON_BASE_ADDR_EXT_REG (AVC_TOP_BASE_ADDR + 0x6C)

// ================== post process ==================
struct reg_pp_ctrl {
    unsigned rotmir_en : 1;
    unsigned r0 : 3;
    unsigned rotate : 2;
    unsigned mirror : 2;        // 01 - VM;10 - HM; 11 - HM&VM
    unsigned endian : 2;
    unsigned uv_interleave : 1; // 0: seperate, 1: interleave
    unsigned uv_alter : 1;      // 0:cbcr; 1:crcb
    unsigned r1 : 20;
};
#define PP_CTRL_REG      (AVC_TOP_BASE_ADDR + 0x70)

#define PP_FRAME_IDX_REG (AVC_TOP_BASE_ADDR + 0x74)

// =================== cache =======================
struct reg_cache_ctrl
{
	unsigned cache_cmd_mode : 2; // 0: refresh mode; 1: load cache param
	unsigned r0 : 2;
	unsigned refresh_status : 1; // [RO]
	unsigned cache_cmd_valid : 1; // [RO]
	unsigned r1 : 26;
};
#define CACHE_CTRL_REG (AVC_TOP_BASE_ADDR+0xC0)

struct reg_cache_size
{
	unsigned cache_y_c : 3;
	unsigned r0 : 1;
	unsigned cache_x_c : 3;
	unsigned r1 : 1;
	unsigned page_y_c : 3;
	unsigned r2 : 1;
	unsigned page_x_c : 3;
	unsigned r3 : 1;
	unsigned cache_y : 3; // page num of y dir
	unsigned r4 : 1;
	unsigned cache_x : 3; // page num of x dir
	unsigned r5 : 1;
	unsigned page_y : 3; // y size of page, 1 << (page_y) bytes
	unsigned r6 : 1;
	unsigned page_x : 3;  // x size of page, 1 << (page_x+3) bytes
	unsigned r7 : 1;
};

#define SINGLE_CACHE_SIZE 0x42143213
#define DUAL_CACHE_SIZE 0x42133212
#define CACHE_MERGE 1

#define CACHE_SIZE_REG (AVC_TOP_BASE_ADDR+0xC4)

struct reg_cache_buf_size {
	unsigned cache_cr_size : 8;
	unsigned cache_cb_size : 8;
	unsigned cache_y_size : 8;
	unsigned r : 8;
};
#define CACHE_BUF_SIZE 0x00401010
#define CACHE_BUF_SIZE_REG (AVC_TOP_BASE_ADDR+0xC8)

struct reg_cache_config {
	unsigned cache_bypass : 1;
	unsigned cache_dual_mode : 1;
	unsigned cache_cbcr_inter : 1;
	unsigned r0 : 1;
	unsigned cache_page_merge : 2; // 0: no merge; 1: hor merge; 2: ver merge
	unsigned r1 : 10;
	unsigned cache_frame_idx0 : 5;
	unsigned r2 : 3;
	unsigned cache_frame_idx1 : 5;
	unsigned r3 : 3;
};
#define CACHE_CFG_REG (AVC_TOP_BASE_ADDR+0xCC)

struct reg_avc_status {
    unsigned vld_rdma_busy : 1; // [0]: (RO)
    unsigned vld_is_busy : 1;   // [1]: (RO)
    unsigned intram_busy : 1;   // [2]: (RO)
    unsigned mvp_busy : 1;      // [3]: (RO)
    unsigned iqidct_busy : 1;   // [4]: (RO)
    unsigned intrap_busy : 1;   // [5]: (RO)
    unsigned mcr_busy : 1;      // [6]: (RO)
    unsigned irec_busy : 1;     // [7]: (RO)
    unsigned dblk_busy : 1;     // [8]: (RO)
    unsigned r0 : 19;

    unsigned mb_prefix_error : 1;
    unsigned mb_header_error : 1;
    unsigned coeff_error : 1;
    unsigned bitstream_error : 1;
};
#define AVC_STATUS_REG (AVC_TOP_BASE_ADDR + 0x1A0)

#define MP2_STATUS_REG (AVC_TOP_BASE_ADDR + 0x1A4)

#define MP4_STATUS_REG (AVC_TOP_BASE_ADDR + 0x1A8)

#define CQ_STATUS_REG  (AVC_TOP_BASE_ADDR + 0x1AC)

#define CYCLES_REG (AVC_TOP_BASE_ADDR + 0x1E0)

// ======================= VLD register
struct reg_vld_ctrl {
    unsigned eptb_detection_bypass : 1;
    unsigned startcode_detect_en : 1;
    unsigned r : 30;
};
#define VLD_CTRL_REG (AVC_VLD_BASE_ADDR + 0x00)

// [31:0]: start address, 16 bytes align
#define VLD_DATA_START_ADDR_REG (AVC_VLD_BASE_ADDR + 0x04)

struct reg_vld_data_start_addr_ext {
    unsigned addr : 8;
    unsigned r : 24;
};
#define VLD_DATA_START_ADDR_EXT_REG (AVC_VLD_BASE_ADDR + 0x08)

struct reg_vld_data_end_addr {
    unsigned addr : 32;
};
#define VLD_DATA_END_ADDR_REG (AVC_VLD_BASE_ADDR + 0x0C)

struct reg_vld_data_end_addr_ext {
    unsigned addr : 8;
    unsigned r : 24;
};
#define VLD_DATA_END_ADDR_EXT_REG (AVC_VLD_BASE_ADDR + 0x10)

// [29:0]: bit offset
#define VLD_BIT_OFFSET_REG (AVC_VLD_BASE_ADDR + 0x14)

// [29:0]: valid length
#define VLD_DATA_LEN_REG (AVC_VLD_BASE_ADDR + 0x18)

struct reg_vld_data_valid {
    unsigned data_first : 1; // [0]:
    unsigned data_last : 1;  // [1]:
    unsigned r : 29;
    unsigned data_valid : 1; // [31]
};
#define VLD_DATA_VALID_REG (AVC_VLD_BASE_ADDR + 0x1C)

struct reg_avc_entropy {
    unsigned cabac_init_idc : 2;
    unsigned r : 30;
};
#define AVC_CABAC_CFG_REG (AVC_VLD_BASE_ADDR + 0x30)

// ======== MB register (0x400 ~ )
struct reg_mb_addr {
    unsigned mb_y : 8;
    unsigned r0 : 8;
    unsigned mb_x : 8;
    unsigned r1 : 8;
};
#define MB_ADDR_REG (AVC_MB_BASE_ADDR + 0x00)

// bit 0-23: MB/MCU number
#define CORRECT_DECODE_MB_NUMBER_REG (AVC_MB_BASE_ADDR + 0xE0)

// ===== TQ register (0x600 ~)
struct reg_avc_scaling_matrix {
    unsigned scaling_matrix_data : 9; // [8:0],
    unsigned r0 : 7;
    /**
     * - 000: S1_4x4_intra_Y
     * - 001: S1_4x4_intra_Cb
     * - 010: S1_4x4_intra_Cr
     * - 011: S1_4x4_inter_Y
     * - 100: S1_4x4_inter_Cb
     * - 101: S1_4x4_inter_Cr
     * - 110: S1_8x8_intra_Y
     * - 111: S1_8x8_inter_Y
     */
    unsigned matrix_addr : 8;   // [23:16]
    unsigned r1 : 6;
    unsigned write_enable : 1;  // [30]
    unsigned matrix_access : 1; // [31]
};
#define AVC_SCALING_MATRIX_REG (AVC_TQ_BASE_ADDR + 0x10)


// ================== Encoder =======================
struct reg_enc_ctrl {
	unsigned imd_en : 1;
	unsigned cimd_en : 1;
	unsigned uv_alter : 1; // 0-uv, 1-vu
	unsigned uv_interleave : 1;// 1-interleave
	unsigned r : 28;
};
#define ENC_CTRL_REG  (AVC_ENC_BASE_ADDR+0x00)

struct reg_enc_frame_idx {
	unsigned src_frame_idx : 5;
	unsigned r0 : 3;
	unsigned ref_frame_idx : 5;
	unsigned r : 19;
};
#define ENC_FRM_IDX_REG  (AVC_ENC_BASE_ADDR+0x04)

struct reg_enc_slice_header {
	unsigned frame_num : 5;
	unsigned r0 : 3;
	unsigned nal_ref_idc : 2;
	unsigned r1 : 6;
	unsigned idr_pic_id : 8;
	unsigned ref_pic_list_modification_flag : 1;
	unsigned r2 : 7;
};
#define ENC_SLICE_HEADER_REG  (AVC_ENC_BASE_ADDR+0x08)

// start addr must be 8 bytes align
#define ENC_VLE_BUF_START_REG  (AVC_ENC_BASE_ADDR+0x0C)
#define ENC_VLE_BUF_END_REG  (AVC_ENC_BASE_ADDR+0x10)
#define ENC_VLE_BUF_SIZE_REG  (AVC_ENC_BASE_ADDR+0x14)

struct reg_enc_imd_thr
{
	unsigned imd_threshold_0 : 16;
	unsigned imd_threshold_1 : 16;  // intra4x4 weight
};
#define ENC_IMD_THR_REG         (AVC_ENC_BASE_ADDR+0x18)

// intra weight, to choose inter mb
// [19:0]
#define ENC_COST_WEIGHT_REG     (AVC_ENC_BASE_ADDR+0x1C)


struct reg_enc_intra_refresh {
	// enable intra-refresh
	unsigned refresh_en : 1;

	// 0: col-refresh; 1: row-refresh
	unsigned refresh_mode : 1;
	unsigned r0 : 14;
	unsigned intra_refresh_mbx : 8;
	unsigned intra_refresh_mby : 8;
};
#define ENC_INTRA_REFRESH_REG  (AVC_ENC_BASE_ADDR+0x34)


// ====== ME =====
struct reg_enc_me_ctrl
{
	// 0: hor(-128~127), ver(-64~63)
	// 1: hor(-64~63),   ver(-32~31)
	// 2: hor(-32~31),   ver(-16~15)
	// 3: hor(-16~15),   ver(-16~15)
	unsigned cime_search_range : 2;

	// 0: (-5~5)
	// 1: (-1~1)
	unsigned rime_search_range : 1;

	// for RIME, default val 0
	// if enable, search range will change +/-1 sometimes
	unsigned dynamic_search_range_en : 1;

	// default 0
	unsigned cime_disable : 1;

	// default 0
	// fme can exceed picture boundary
	unsigned umc_flag : 1;

	// default 0, lambda = 1
	// if enable, lambda change with QP
	unsigned lambda_en : 1;

	unsigned r3 : 25;
};
#define ENC_ME_CTRL_REG    (AVC_ME_BASE_ADDR+0x00)

struct reg_enc_me_pweight {
	unsigned cime_pweight : 10; // default 20

	unsigned rime_pweight : 10; // default 20
	unsigned fme_pweight : 10;  // default 20

	unsigned r : 2;
};
#define ENC_ME_PWEIGHT_REG    (AVC_ME_BASE_ADDR+0x04)

struct reg_enc_me_zweight {
	unsigned cime_zweight : 10; // default 20
	unsigned rime_zweight : 10; // default 20
	unsigned fme_zweight : 10;  // default 20

	unsigned r : 2;
};
#define ENC_ME_ZWEIGHT_REG    (AVC_ME_BASE_ADDR+0x08)

struct reg_enc_me_skip {
	// use skip mv as fme mv force, if skip cost < skip_threshold
	unsigned skip_threshold : 16; // default 20
	unsigned skip_weight : 16;    // default 20
};
#define ENC_ME_SKIP_REG    (AVC_ME_BASE_ADDR+0x0C)

struct reg_enc_me_p16x8_weight {
	unsigned p16x8_weight : 16; // default 16
	unsigned r : 16;
};
#define ENC_ME_P16x8_REG    (AVC_ME_BASE_ADDR+0x10)

struct reg_enc_me_p8x16_weight {
	unsigned p8x16_weight : 16; // default 32
	unsigned r : 16;
};
#define ENC_ME_P8x16_REG    (AVC_ME_BASE_ADDR+0x14)

struct reg_enc_me_p8x8_weight {
	unsigned p8x8_weight : 16; // default 64
	unsigned r : 16;
};
#define ENC_ME_P8x8_REG    (AVC_ME_BASE_ADDR+0x18)

struct reg_enc_me_pmv {
	unsigned cime_pmv_en : 1; // default 1
	unsigned rime_pmv_en : 1; // default 1
	unsigned fme_pmv_en : 1;  // default 1
	unsigned r0 : 1;
	unsigned skip_en : 1;
	unsigned r : 27;
};
#define ENC_ME_PMV_EN_REG    (AVC_ME_BASE_ADDR+0x28)

// cime subsample buffer address
#define ENC_ME_REF_ADDR_REG    (AVC_ME_BASE_ADDR+0x30)
#define ENC_ME_REF_FRM_IDX_REG    (AVC_ME_BASE_ADDR+0x34)

struct reg_enc_block_en {
	unsigned p16x16_en : 1;
	unsigned p16x8_en : 1;
	unsigned p8x16_en : 1;
	unsigned p8x8_en : 1;
	unsigned r : 28;
};
#define ENC_ME_BLOCK_EN_REG    (AVC_ME_BASE_ADDR+0x38)
// =========== RC ==============

struct reg_enc_rate_control {
	unsigned rate_control : 1;
	unsigned r2 : 31;
};
#define ENC_RATE_CONTROL_REG               (AVC_RC_BASE_ADDR+0x00)

struct reg_enc_res_mad_sum {
	unsigned res_mad_sum : 32;
};
#define ENC_FRAME_RES_MAD_SUM_REG          (AVC_RC_BASE_ADDR+0x04)

// the start address of mad buffer,
// mad sum of every mb line, VE write
#define ENC_MB_LINE_MAD_ADDR_REG_L         (AVC_RC_BASE_ADDR+0x08)
#define ENC_MB_LINE_MAD_ADDR_REG_H         (AVC_RC_BASE_ADDR+0x0C)

//
#define ENC_MB_LINE_BITS_THRES_ADDR_L_REG  (AVC_RC_BASE_ADDR+0x10)
#define ENC_MB_LINE_BITS_THRES_ADDR_H_REG  (AVC_RC_BASE_ADDR+0x14)

#define ENC_FRAME_MAD_SUM_REG              (AVC_RC_BASE_ADDR+0x18)

struct reg_enc_mad_param {
	unsigned mb_mad_th0 : 16;
	unsigned mb_mad_th1 : 16;
};
#define ENC_MB_MAD_PARAM0_REG              (AVC_RC_BASE_ADDR+0x1C)
#define ENC_MB_MAD_PARAM1_REG              (AVC_RC_BASE_ADDR+0x20)
#define ENC_MB_MAD_PARAM2_REG              (AVC_RC_BASE_ADDR+0x24)

struct reg_enc_qp_range {
	unsigned qp_min : 6;
	unsigned r0 : 2;
	unsigned qp_max : 6;
	unsigned r1 : 18;
};
#define ENC_QP_RANGE_REG                   (AVC_RC_BASE_ADDR+0x28)

enum AVC_STATUS
{
	AVC_FINISH = 1,
	AVC_BIT_REQ = 2,
	AVC_ERROR = 4,
};
#endif
