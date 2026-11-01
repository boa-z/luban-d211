/*
 * Copyright (C) 2020-2026 ArtInChip Technology Co. Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 *  author: <xiaodong.zhao@artinchip.com>
 *  Desc: mpeg4 vars interface
 *
 */

#ifndef _MP4_VARS_H_
#define _MP4_VARS_H_

#include <stdio.h>
#include <stdlib.h>
#include "mp4_decore.h"

#define mmax(a, b)     ((a) > (b) ? (a) : (b))
#define mmin(a, b)     ((a) < (b) ? (a) : (b))
#define mnint(a)       ((a) < 0 ? (int)(a - 0.5) : (int)(a + 0.5))
#define sign(a)        ((a) < 0 ? -1 : 1)
// #define abs(a)         (((a) > 0) ? (a) : -(a))
#define sign(a)        ((a) < 0 ? -1 : 1)
#define mnint(a)       ((a) < 0 ? (int)(a - 0.5) : (int)(a + 0.5))
#define _div_div(a, b) (a > 0) ? (a + (b >> 1)) / b : (a - (b >> 1)) / b

#ifndef MAX
#define MAX(a, b) (((a) > (b)) ? (a) : (b))
#endif

#ifndef MIN
#define MIN(a, b) (((a) < (b)) ? (a) : (b))
#endif

#ifndef CLIP
#define CLIP(a, i, s) (((a) > (s)) ? (s) : MAX(a, i))
#endif

#ifndef SIGN
#define SIGN(a) ((a) < 0 ? -1 : 1)
#endif

#ifdef WIN32
#pragma warning(disable : 4244) // conversion from 'x' to 'y'
#pragma warning(disable : 4100) // unreferenced formal parameter
#endif

/**
 * definitions
 **/
#define VO_START_CODE      0x8
#define VO_START_CODE_MIN  0x100
#define VO_START_CODE_MAX  0x11f

#define VOL_START_CODE     0x12
#define VOL_START_CODE_MIN 0x120
#define VOL_START_CODE_MAX 0x12f

#define VOS_START_CODE     0x1b0 // visual_object_sequence
#define USR_START_CODE     0x1b2 // user_data
#define GOP_START_CODE     0x1b3 // group of vop
#define VSO_START_CODE     0x1b5 // visual object
#define VOP_START_CODE     0x1b6 // visual object plane
#define STF_START_CODE     0x1c3 // stuffing_start_code
#define SHV_START_CODE     0x020
#define SHV_END_MARKER     0x03f

#define I_VOP              0
#define P_VOP              1
#define B_VOP              2
#define S_VOP              3

#define RECTANGULAR       0
#define BINARY            1
#define BINARY_SHAPE_ONLY 2
#define GRAY_SCALE        3

#define NOTUSE_SPRITE     0
#define STATIC_SPRITE     1
#define GMC_SPRITE        2
#define RESERVED_SPRITE   3

#define RESYNC_MARKER     1

#define NOT_VALID         -2
#define NOT_CODED         -1
#define INTER             0
#define INTER_Q           1
#define INTER4V           2
#define INTRA             3
#define INTRA_Q           4
#define STUFFING          7
#define B_MBLOCK          8 // used as a shortcut (overload) in mp4_recon_qpel
#define B_MBLOCK_DEST     9 // in this case, the destination is an array (mb)
#define B_BLOCK_DEST_4V   10

// b frames
#define MODB_1        0 // cbpb(default 0) & mb_type not present(default 'derect mode')
#define MODB_00       1 // cbpb and mb_type are all present
#define MODB_01       2 // cbpb not present, mb_type present

#define MB_TYPE_1     0 // direct mode
#define MB_TYPE_01    1 // bi-directional mode
#define MB_TYPE_001   2 // backward mode
#define MB_TYPE_0001  3 // forward mode
#define MB_TYPE_00001 4 // intra mode

#define DIRECT_MODE   MB_TYPE_1
#define BIDIR_MODE    MB_TYPE_01
#define BACKWARD_MODE MB_TYPE_001
#define FORWARD_MODE  MB_TYPE_0001
#define INTRA_MODE    MB_TYPE_00001

// video packet structure size
#define MAX_MB_PACKET 2560 // [Review] what is the maximum allowed?

#define DEC_MBC       132
#define DEC_MBR       132

extern char mb_type_name[12][16];

typedef struct {
    int val, len;
} tab_type;

typedef struct {
    int last;
    int run;
    int level;
} event_t;

/**
 * ATTENTION
 * The block[] MUST be aligned on 16byte boundaries.  Vector processors
 * will otherwise access improperly.  For example the PowerPC doesn't even use the
 * lower address lines!  So It'll actually load the wrong memory if not properly
 * aligned!
 *     ( J.Leiterman )
 */
typedef struct {
    // block data
    short block[64];

    // bit input
    FILE *infile;
    unsigned char rdbfr[2051];
    const unsigned char *startptr;
    const unsigned char *rdptr;
    unsigned char inbfr[16];
    int incnt;
    int bitcnt;

    // this int are supposed to be always aligned, I'm going to use always these two
    // mini-buffers to access to the stream. It's faster! (thanks to Lionel Ulmer)
    unsigned int bit_a, bit_b;
    unsigned int length;
} mp4_stream_t;

typedef struct _ac_dc {
    int *dc_store_lum; // dc_store_lum[2*rows+1][2*col+1]
    int *ac_left_lum;  // ac_left_lum[2*rows+1][2*col+1][7]
    int *ac_top_lum;   // ac_top_lum[2*rows+1][2*col+1][7]

    int *dc_store_chr[2]; // dc_store_chr[2][rows+1][col+1]
    int *ac_left_chr[2];  // ac_left_chr[2][rows+1][col+1][7]
    int *ac_top_chr[2];   // ac_top_chr[2][rows+1][col+1][7]

    int dc_store_lum_stride;
    int ac_left_lum_stride;
    int ac_top_lum_stride;
    int dc_store_chr_stride;
    int ac_left_chr_stride;
    int ac_top_chr_stride;

    int predict_dir;
    int *pAic_luma_top;
    int *pAic_chroma0_top;
    int *pAic_chroma1_top;
    int Aic_left[4][8];

} ac_dc;

typedef struct {
    int ileft;
    int iabove;
} t_edge;

struct mp4_state;

typedef struct _mp4_header {
    // video packet record
    int a_not_coded[MAX_MB_PACKET];
    int a_mcbpc[MAX_MB_PACKET];
    int a_derived_mb_type[MAX_MB_PACKET];
    int a_cbpc[MAX_MB_PACKET];
    int a_ac_pred_flag[MAX_MB_PACKET];
    int a_cbpy[MAX_MB_PACKET];
    int a_dquant[MAX_MB_PACKET];
    int a_cbp[MAX_MB_PACKET];
    int a_mcsel[MAX_MB_PACKET];
    short a_dc_store[MAX_MB_PACKET][6];
    // MPEG-2 quant matrices
    unsigned int intra_quant_matrix[64];
    unsigned int nonintra_quant_matrix[64];
    // vol
    int ident;
    int random_accessible_vol;
    int type_indication;
    int is_object_layer_identifier;
    int visual_object_layer_verid;
    int visual_object_layer_priority;
    int aspect_ratio_info;
    int par_width;
    int par_height;
    int vol_control_parameters;
    int chroma_format;
    int low_delay;
    int vbv_parameters;
    int first_half_bit_rate;
    int latter_half_bit_rate;
    int first_half_vbv_buffer_size;
    int latter_half_vbv_buffer_size;
    int first_half_vbv_occupancy;
    int latter_half_vbv_occupancy;
    int shape;
    int video_object_layer_shape_extension;
    int time_increment_resolution;
    int fixed_vop_rate;
    int fixed_vop_time_increment;
    int width;
    int height;
    int interlaced;
    int obmc_disable;
    int sprite_usage;
    int sprite_width;
    int sprite_height;
    int sprite_left_coordinate;
    int sprite_top_coordinate;

    int sadct_disable;
    int not_8_bit;
    int quant_precision;
    int bits_per_pixel;
    int quant_type; // q_scale_type
    int load_intra_quant_matrix;
    int load_nonintra_quant_matrix;
    int quarter_pixel;
    int complexity_estimation_disable;
    int resync_marker_disable;
    int data_partitioning;
    int reversible_vlc;
    int intra_acdc_pred_disable;
    int scalability;
    int quant_scale;
    // complexity estimation
    int estimation_method;
    int shape_complexity_estimation_disable;
    int opaque;
    int transparent;
    int intra_cae;
    int inter_cae;
    int no_update;
    int upsampling;
    int texture_complexity_estimation_set_1_disable;
    int intra_blocks;
    int inter_blocks;
    int inter4v_blocks;
    int not_coded_blocks;
    int texture_complexity_estimation_set_2_disable;
    int dct_coefs;
    int dct_lines;
    int vlc_symbols;
    int vlc_bits;
    int motion_compensation_complexity_disable;
    int apm;
    int npm;
    int interpolate_mc_q;
    int forw_back_mc_q;
    int halfpel2;
    int halfpel4;
    int version2_complexity_estimation_disable;
    int sadct;
    int quarterpel;
    int newpred_enable;
    int request_upstream_message_type;
    int newpred_segment_type;
    int reduced_resolution_vop_enable;
    int dcecs_opaque;
    int dcecs_transparent;
    int dcecs_intra_cae;
    int dcecs_inter_cae;
    int dcecs_no_update;
    int dcecs_upsampling;
    int dcecs_intra_blocks;
    int dcecs_not_coded_blocks;
    int dcecs_dct_coefs;
    int dcecs_dct_lines;
    int dcecs_vlc_symbols;
    int dcecs_vlc_bits;
    int dcecs_sadct;
    int dcecs_inter_blocks;
    int dcecs_inter4v_blocks;
    int dcecs_apm;
    int dcecs_npm;
    int dcecs_forw_back_mc_q;
    int dcecs_halfpel2;
    int dcecs_halfpel4;
    int dcecs_quarterpel;
    int dcecs_interpolate_mc_q;

    // svh
    int short_video_header;
    int temporal_reference;
    int split_screen_indicator;
    int document_camera_indicator;
    int full_picture_freeze_release;
    int source_format;
    int picture_coding_type;
    int four_reserved_zero_bits;
    int vop_quant;

    // gob
    int gob_number;
    int gob_frame_id;

    // gop
    int gob_header_emtpy;
    int time_code;
    int closed_gov;
    int broken_link;

    // vop
    int old_prediction_type;
    int last_coded_prediction_type;
    int prediction_type;
    int old_time_base;
    int time_base;
    int time_inc;
    int vop_coded;
    int rounding_type;
    int hor_spat_ref;
    int ver_spat_ref;
    int change_CR_disable;
    int constant_alpha;
    int constant_alpha_value;
    int intra_dc_vlc_thr;
    int use_intra_dc_vlc;
    int quantizer;
    int fcode_for;
    int fcode_back;
    int shape_coding_type;
    int trb; // temporal diff. between next and previous reference VOP
    int trd; // temporal diff. between current B-VOP and previous reference VOP
    int trbi;
    int trdi;
    int display_time_next;
    int display_time_prev;
    int tframe; // see paragraph 7.7.2.2

    // video packet
    int macroblock_number;
    int header_extension_code;

    int mb_intra;
    int mb_fwd_mc;
    int mb_bwd_mc;
    // macroblock
    int not_coded;
    int mcbpc;
    int derived_mb_type;
    int cbpc;
    int ac_pred_flag;
    int cbpy;
    int dquant;
    int cbp;
    //
    int modb;
    int mb_type;
    int cbpb;
    int dbquant;
    //
    int mcsel; // use GMC if mcsel = 1, or use local mv

    // extra/derived
    int mba_size;
    int mb_xsize;
    int mb_ysize;
    int picnum;
    int packetnum; // if ~0 indicates the presece of at least 1 video packet
    int gobnum;    // needed for short header decoding
    int mba;
    int mb_xpos;
    int mb_ypos;
    int dc_scaler;
    int num_mb_in_gob;   // shv
    int num_gobs_in_vop; // shv

    int mb_in_vop_length;
    int resync_length;
    int intrablock_rescaled; // indicates, when the quantizer changes inside a VOP, that

    int iEffectiveWarpingPoints;
    int no_of_sprite_warping_points;
    int warping_points[4][2];
    int sprite_warping_accuracy;
    int sprite_brightness_change;
    int sprite_brightness_change_factor;
    int low_latency_sprite_enable;
    // the ac rescaling have been applied to avoid to repeat it
    // 3.11 specific values
    short (*dc_chrom_table)(struct mp4_state *, mp4_stream_t *);
    short (*dc_lum_table)(struct mp4_state *, mp4_stream_t *);
    event_t (*ac_inter_table)(struct mp4_state *, mp4_stream_t *);
    event_t (*ac_intra_chrom_table)(struct mp4_state *mp4_state, mp4_stream_t *);
    event_t (*ac_intra_lum_table)(struct mp4_state *mp4_state, mp4_stream_t *);
    void (*mv_table)(mp4_stream_t *, int *, int *);
    short (*get_cbp)(mp4_stream_t *);
    int has_skips;
    int vol_mode; /* see comment in mp4_header_311.c */
    int switch_rounding;
    // the ac rescaling have been applied to avoid to repeat it

    // interlace
    int top_field_first;
    int alternate_vertical_scan_flag;
    int dct_type;
    int field_prediction;
    int forward_top_field_reference;
    int forward_bottom_field_reference;
    int backward_top_field_reference;
    int backward_bottom_field_reference;

    int h263_aic;
    int h263_aic_dir;
    int slice_structured;
    int modified_qantization;
    int umv;
    int UUI;
    int RPR;
    int RRU;
    int h263_ap;
    int deblocking;
    int rps;
    int isd;
    int aiv;
    t_edge gob_edge;
    int osvquant;
    int iratio0, iratio1;
    int quant_prev;
    int entropy_qp;

    // wmv
    int j_type;
    int per_mb_rl_table;
    int rl_chroma_table_index;
    int rl_table_index;
    int dc_table_index;
    int cbp_table_index;
    int mspel;
    int hshift;
    int per_mb_abt;
    int per_block_abt;
    int abt_type; // 0:8x8 //1:8x4, 2:4x8
    int abt_type_table[6];
    int mv_table_index;
    int inter_intra_pred;
    int esc3_level_length;
    int esc3_run_length;
    int skip_type;
    int *mb_skip;

    int first_slice_line;
    int slice_height;
    int bit_rate;
} mp4_header;

typedef struct _intra_x8_context {
    int *j_ac_vlc[4];
    int *j_dc_vlc[4];
    int *j_orient_vlc;

    unsigned char *dest[3];
    int use_quant_matrix;
    int dquant;
    int quant;
    int qsum;
    unsigned char *prediction_table; // 2*mb_w*2
    unsigned char edge_emu_buffer[42];
    // calculated per frame
    int quant_dc_chroma;
    int divide_quant_dc_luma;
    int divide_quant_dc_chroma;
    // changed per block
    int edges; // bit0:left, bit1:top, bit2 right
    int flat_dc;
    int predicted_dc;
    int raw_orient;
    int chroma_orient;
    int orient;
    int est_run;
    // function
    void (*x8_spatial_compensation[12])(unsigned char *src, unsigned char *dst, int linesize);
} intra_x8_context_t;

typedef struct {
    char corners[4];
    char top[DEC_MBC];
    char bottom[DEC_MBC];
    char left[DEC_MBR];
    char right[DEC_MBR];
} mp4_edge_info_t;

typedef struct {
    unsigned int *deviations[3];
    unsigned char *history1;
    unsigned char *history2;
} dering_info_t;

typedef struct {
    int64_t mmw_brightness;
    int64_t mmw_contrast;
    int64_t mmw_saturation;
    int brightness;
    int contrast;
    int saturation;
} gamma_adjustment_t;

typedef struct {
    int X0, Y0;
    int XX, YX, XY, YY;
    int rounder1, rounder2;
    int64_t shifter;
} affine_transform_t;

struct _reference;

typedef event_t(vld_proc)(mp4_stream_t *ld);
typedef vld_proc *vld_proc_ptr;

typedef void(recon_func)(struct mp4_state *mp4_state, unsigned char *src, unsigned char *dst, int lx,
                         int lx_dst, int x, int y, int dx, int dy, int interlaced);
typedef recon_func *recon_func_ptr;
typedef void(recon_block_func)(struct mp4_state *mp4_state, unsigned char *src, unsigned char *dst,
                               int lx, int lx_dst, int x, int y, int dx, int dy, int chrom, int interlaced);
typedef recon_block_func *recon_block_func_ptr;
typedef struct {
    int x, y;
} motion_vector_t;

typedef struct mp4_state {
    // quarter_pixel optimized version
    unsigned char mirrored_matrix[25 * 24]; // y:(17 + 6), x:(17 + 6) size matrix
    unsigned char half_hor_matrix[25 * 17]; // y:(16 + 6 + 1), x:(16) size matrix
    unsigned char half_ver_matrix[18 * 18];
    unsigned char half_horver_matrix[18 * 17];
    unsigned char quarter_e_matrix[25 * 17]; // y:(17+6), x:(16)
    unsigned char quarter_k_matrix[18 * 17];
    unsigned char tmp_f[3][256]; // temporal buffer for mb interpolation (forward and backward,
    unsigned char tmp_b[3][256]; // luma[0] and chroma[1][2] prediction buffers)

    short mpeg_coef_matrix_no[6];
    unsigned long mpeg_coef_matrix[6][64];
    int mp4_mv_data_buf[6][2];
    int mp4_mv_res_buf[6][2];
    int mp4_direct_mv[8][2]; // storing direct motion vectors
    int mp4_mv_data[2];
    int mp4_mv_res[2];

    long gmc_lum_mv_x;
    long gmc_lum_mv_y;
    long gmc_chrom_mv_x;
    long gmc_chrom_mv_y;

    mp4_header hdr;
    intra_x8_context_t x8hdr;

    int *modemap;              // modemap[DEC_MBR+1][DEC_MBC+2]
    int *codedmap;             // codedmap[DEC_MBR][DEC_MBC]
    short *cbp_store;          // cbp_store[DEC_MBR+1][DEC_MBC+1]
    char *quant_store;         // quant_store[DEC_MBR+1][DEC_MBC+1]
    motion_vector_t (*mv)[6];     // MV[DEC_MBR+1][DEC_MBC+2][6][2]
    motion_vector_t (*mv_back)[6]; // MV[DEC_MBR+1][DEC_MBC+2][6][2]

    int *fieldpredictedmap;
    int fieldpredictedmap_stride;
    char *fieldrefmap;
    int fieldrefmap_stride;

    motion_vector_t (*mv_field)[6];

    int modemap_stride;
    int codedmap_stride;
    int cbp_store_stride;
    int quant_store_stride;
    int MV_stride;

    // b-vop
    motion_vector_t mv_pfor[2]; // top and bottom field
    motion_vector_t mv_pback[2];
    int bvop_delay_activated;
    int bvop_delay_completed;
    unsigned char **frame_to_decode;
    unsigned char **frame_to_display; // must create a delay when encounter the first B-VOP
    unsigned char **output_frame;     // always matches the frame we use as input for color conversion
    int prefixed;                     // avoid delay caused by B-VOPs using prefixed information, no_delay_frame_flag is on
    int history_prefixed;             // there was a prefixed I-VOP, b_vop_grouping is on
    int preceding_vop_coding_type;

    unsigned char *mirrored_matrix_ref;
    unsigned char *half_hor_matrix_ref;
    unsigned char *quarter_e_matrix_ref;
    int mirrored_matrix_stride;
    int half_hor_matrix_stride;
    int half_ver_matrix_stride;
    int half_horver_matrix_stride;
    int quarter_e_matrix_stride;
    int quarter_k_matrix_stride;

    mp4_edge_info_t edge_info;
    ac_dc coeff_pred;

    int bpp;
    int flag_invert;

    int horizontal_size;
    int vertical_size;
    int edge_hor_start;
    int edge_ver_start;
    int mb_width;
    int mb_height;
    int coded_picture_width;
    int coded_picture_height;
    int chrom_width;
    int chrom_height;
    int coded_picture_width_field;
    int coded_picture_height_field;
    int chrom_width_field;
    int chrom_height_field;

    int post_flag;
    int pp_options;
    int postproc_level;

    int flag_sse; // indicate the support of sse intruction on this machine
    int flag_3dnow;
    int flag_keyframe; // indicates that the current frame is a keyframe
    int flag_disposable;
    int deblockingflag;
    int flag_smooth_playback; // delay one frame, copy B frames into private buffer, decode one frame at a time
    int flag_seek_bframe;
    int flag_buffered_bframe;
    char *buffered_bframe;
    int buffered_bframe_length;
    int disp_factor;
    /**
     * flag_skip_decoding: allows the decoder to ignore the data and stop decoding for a while
     * When the decoder receives skip_decoding set this flag to 1 and returns 0, when the decoder
     * has this flag set to 1, if the frame received is a keyframe set this flag to 0 and decodes, otherwise
     * returns 0.
     */
    int flag_skip_decoding;

    // user data info
    int msmpeg_version;
    int extra_size;
    int packet_format;
    int user_allocated;
    int userdata_codec_version;
    int if_flv_h263;
    int flvh263version;
    int userdata_build_number;
    int rm_codec_id;
    int if_rm_h263;
    int rv_version;
    int rm_low_delay;
    int *data_offset;
    short *data_valid;
    short packet_num;
    int format_plus;
    int umv;
    int h263_ap;
    int deblocking;
    int rps;
    int isd;
    int aiv;
    int PB;
    unsigned int spo_extra;
    unsigned int stream_version;
    unsigned int majors_tream_version;
    unsigned int minor_stream_version;
    unsigned int num_resampled_image_sizes;
    unsigned int encode_size;
    unsigned int largest_pels;
    unsigned int largest_lines;
    int num_rpr_sizes;
    int fid;
    int is_rv8;
    double m_picture_clock_frequency;
    int tr_wrap;

    int bad_header; /* Contains error returned when parsing last received VOL header */

    unsigned char *bmp;
    int stride;

    void (*set_gmc_mv)(struct mp4_state *);
    void (*reconstruct_skip)(struct _reference *);
    vld_proc_ptr vld_inter_fun;
    vld_proc_ptr vld_intra_fun;

    recon_func_ptr recon_16x16;
    recon_block_func_ptr recon_8x8_lum;
    recon_block_func_ptr recon_8x8_chr;

    int memory_cheat;
    void *old_pointer;
    int render_flag;

    int test_timeinc;
    int new_format;

    dering_info_t di;
    gamma_adjustment_t ga;

    affine_transform_t at_lum;
    affine_transform_t at_chrom;

    int do_mc;
    int do_mc_b;
    int do_add;
    int show_gmc;
    int show_type;

    // data partitioning extra stuff
    int draw_packet_boundary;
    int error_flag; // signal if the previous video packet was errouneous

    char *backup_buffer;

    int bsw_vld;

    // wmv
    int mspel_bit;
    int abt_flag;
    int j_type_bit;
    int top_left_mv_flag;
    int per_mb_rl_bit;
} mp4_state_t;

typedef struct mb_config_info {
    uint32_t *ptr;  // the mb info data ptr in mpeg12->mb_cfg_addr
    int len;        // the mb info length (including header)
} mb_config_info_t;

#define MPEG4_FRAME_NUM 5
typedef struct _reference {
    unsigned char *edged_ref[3];
    unsigned char *edged_for[3];
    unsigned char *edged_back[3];
    unsigned char *frame_ref[3];
    unsigned char *frame_for[3];
    unsigned char *frame_back[3];
    unsigned char *display_frame[3];

    struct frame *frame[MPEG4_FRAME_NUM];
    int fwd_frame_id;     // mp4_forw_buf_id
    int bwd_frame_id;     // mp4_back_buf_id
    int cur_frame_id;     // mp4_recon_buf_id
    int cur_rotmir_id;
    int last_rotmir_id;
    int need_get_frame;
    int find_top_field;
    int find_bot_field;

    int h_offset;
    int v_offset;
    int rotmir_h_stride;
    int rotmir_v_stride;
    int rotmir_h_real_size;
    int rotmir_v_real_size;

    // Chroma mv, fwd_mv_c[top/bottom][x/y]
    int fwd_mv_c[2][2];
    int bwd_mv_c[2][2];

    // mb_cfg_phy_addr: mb info used by hardware
    int mb_cfg_info_list_len;
    mb_config_info_t *mb_cfg_info_list; // all of the mb_infos in this picture
    struct ve_buffer *mb_cfg_phy_addr;           // physic addr of mb_cfg_buffer
    int mb_cfg_data_len;
    uint32_t *mb_cfg_data;
    uint32_t *cur_mb_cfg_ptr;

    struct mp4_state *mp4_state;
    mp4_stream_t *ld;

    void *(*alloc_fun)(size_t);
    void (*free_fun)(void *);
} reference_t;

/**
 * prototypes of main decore functions
 */
int decore_frame(reference_t *ref, unsigned char *stream, int length, unsigned char *bmp,
                 unsigned int stride, int render_flag, int just_vol_init, int skip_decoding);

int decore_alloc(reference_t *ref);
int decore_dealloc(reference_t *ref);
int decore_release(reference_t *ref);
int get_mp4picture(reference_t *ref, unsigned char *bmp, unsigned int stride, int render_flag);
int get_notcoded_mp4picture(reference_t *ref, unsigned char *bmp, unsigned int stride, int render_flag);

/**
 * cleanup functions
 */
typedef void(decore_cleanup_proc)();
typedef decore_cleanup_proc *decore_cleanup_proc_ptr;
extern decore_cleanup_proc_ptr decore_cleanup;

extern decore_cleanup_proc decore_cleanup_generic;
extern decore_cleanup_proc decore_cleanup_mmx;

#endif // _MP4_VARS_H_
