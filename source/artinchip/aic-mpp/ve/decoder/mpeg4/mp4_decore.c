/*
 * Copyright (C) 2020-2026 ArtInChip Technology Co. Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 *  author: <xiaodong.zhao@artinchip.com>
 *  Desc: mpeg4 decore interface
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

#include "mp4_decore.h"
#include "mp4_getbits.h"
#include "mp4_header.h"
#include "mp4_recon.h"
#include "mp4_vars.h"
#include "mp4_vld.h"
#include "mp4_vld_r.h"
#include "mp4_global.h"
#include "mp4_tables.h"
#include "mpeg_register.h"
#include "mpeg4_decoder.h"
#include "rotation_config.h"

#define HD_FORMAT_OUTPUT

#define II_BITRATE   128 * 1024
#define MBAC_BITRATE 50 * 1024

extern struct mpeg4_ctx *g_mpeg4_ctx;
static int decore_frame_311(reference_t *_ref, unsigned char *stream, int length, unsigned char *bmp, unsigned int stride, int render_flag);
extern void get_mp4picture_311(reference_t *ref, unsigned char *bmp, unsigned int stride, int render_flag);

extern void init_platform(int *flag_sse, int *flag_3dnow);
extern void init_platform_vld(struct mp4_state *mp4_state);

#define SU_MALLOC_KEY 0x5c
extern int flvh263version;

void mp4_save_notcoded_flag(struct mp4_state *_mp4_state);
void mp4_set_vop_info(struct mp4_state *_mp4_state);
void mp4_set_display_buf(struct mp4_state *_mp4_state);
void mp4_set_mb_info(struct mp4_state *_mp4_state);
void mp4_set_ivop_mbinfo(struct mp4_state *_mp4_state);
void mp4_set_pvop_mbinfo(struct mp4_state *_mp4_state);
void mp4_set_bvop_mbinfo(struct mp4_state *_mp4_state);
void mp4_set_svop_mbinfo(struct mp4_state *_mp4_state);
void mp4_set_packet_info(struct mp4_state *_mp4_state);
void mp4_set_gob_info(struct mp4_state *_mp4_state);
void mp4_backup_mbh_info(struct mp4_state *_mp4_state);
void mp4_save_sw_recon_result(reference_t *ref);
// int mp4_compare_result(reference_t *ref);
void mp4_update_info(struct mp4_state *_mp4_state);
extern int hor_avg_pixel(unsigned char *buf, int y, int x, int height, int width, int linesize, int mode);

int wmv2_decode_picture_header(mp4_stream_t *ld, struct mp4_state *mp4_state);
int msmpeg4_decode_picture_header(mp4_stream_t *ld, struct mp4_state *mp4_state);
extern short get_dc_311_0_lum(struct mp4_state *, mp4_stream_t *ld);
extern short get_dc_311_1_lum(struct mp4_state *, mp4_stream_t *ld);
extern short get_dc_311_0_chrom(struct mp4_state *, mp4_stream_t *ld);
extern short get_dc_311_1_chrom(struct mp4_state *, mp4_stream_t *ld);
extern void get_mv_data_311_0(mp4_stream_t *ld, int *mv_x, int *mv_y);
extern void get_mv_data_311_1(mp4_stream_t *ld, int *mv_x, int *mv_y);

int mp4_b_mb_not_coded; // not coded flag in B-VOP

void mp4_get_rotmir_offset(reference_t *ref)
{
    struct mp4_state *mp4_state = ref->mp4_state;

    int rotate = MPP_ROTATION_GET(g_mpeg4_ctx->decoder.rotmir_flag);
    int flip_v = MPP_FLIP_V_GET(g_mpeg4_ctx->decoder.rotmir_flag);
    int flip_h = MPP_FLIP_H_GET(g_mpeg4_ctx->decoder.rotmir_flag);

    ref->h_offset = 0;
    ref->v_offset = 0;
    ref->rotmir_h_stride = mp4_state->mb_width * 16;
    ref->rotmir_v_stride = mp4_state->mb_height * 16;
    ref->rotmir_h_real_size = mp4_state->horizontal_size;
    ref->rotmir_v_real_size = mp4_state->vertical_size;

    const rotation_config *config = find_rotation_config(rotate, flip_h, flip_v);
    if (config != NULL) {
        if (config->set_h_offset) {
            ref->h_offset = ref->rotmir_h_stride - ref->rotmir_h_real_size;
        }

        if (config->set_v_offset) {
            ref->v_offset = ref->rotmir_v_stride - ref->rotmir_v_real_size;
        }

        if (config->h_v_switch) {
            swap_val(&ref->h_offset, &ref->v_offset);
            swap_val(&ref->rotmir_h_real_size, &ref->rotmir_v_real_size);
            swap_val(&ref->rotmir_h_stride, &ref->rotmir_v_stride);
        }

        logi("Found matching : rotate=%d, flip_h=%d, flip_v=%d",
             config->rotate, config->flip_h, config->flip_v);
    }
}

void *dsu_malloc(size_t size)
{
    void *retblock;
    retblock = malloc(size);
    if (retblock == NULL) {
        loge("SU_malloc: error allocing");
        return NULL;
    }

    return retblock;
}

void dsu_free(void *memblock)
{
    if (memblock)
        free(memblock);
}

decore_cleanup_proc_ptr decore_cleanup;

void decore_cleanup_generic()
{

}

void mp4_state_alloc(reference_t *ref, struct mp4_state *mp4_state, int hor_size, int ver_size);

void convert_touser(const unsigned char *puc_y, int stride_y, const unsigned char *puc_u, const unsigned char *puc_v, int stride_uv,
                    unsigned char *bmp, int width_y, int height_y, unsigned int stride_out, const gamma_adjustment_t *ga)
{
    // do absolutely nothing, user does not want us to do color conversion
}

int decore_init(reference_t *ref, int codec_version, int smooth_playback, int disp_factor, int w, int h);

extern void decore_show_type(unsigned char *bmp, int stride, int prediction_type);

extern unsigned int bitpos(mp4_stream_t *_ld);

/**
 * Handle DEC_OPT_INIT option
 * Initialize decoder with provided parameters
 */
static int decore_init_handler(struct mpeg4_ctx* s, const dec_init_t *pinit)
{
    int err;
    reference_t *ref = &s->ref;

    ref->alloc_fun = (pinit && pinit->alloc) ? pinit->alloc : dsu_malloc;
    ref->free_fun = (pinit && pinit->alloc) ? pinit->free : dsu_free;

    if (pinit) {
        err = decore_init(ref, pinit->codec_version, pinit->smooth_playback, pinit->disp_factor, pinit->width, pinit->height);
        if (err != DEC_OK) {
            decore_release(ref);
            return err;
        }
    }

    return DEC_OK;
}

/**
 * Handle DEC_OPT_RELEASE option
 * Release all allocated decoder resources
 */
static int decore_release_handler(struct mpeg4_ctx* s)
{
    reference_t *ref = &s->ref;
    decore_dealloc(ref);
    decore_release(ref);
    return DEC_OK;
}

/**
 * Setup RealMedia frame parameters
 * Configure RM-specific decoding parameters
 */
static void decore_setup_rm_frame_params(struct mp4_state *mp4_state, const dec_frame_t *dec_frame)
{
    mp4_state->packet_format = dec_frame->packet_format;
    mp4_state->if_rm_h263 = dec_frame->if_rm_h263;
    mp4_state->rm_codec_id = dec_frame->rm_codec_id;
    mp4_state->rv_version = dec_frame->rv_version;
    mp4_state->rm_low_delay = dec_frame->rm_low_delay;
    mp4_state->data_offset = dec_frame->data_offset;
    mp4_state->data_valid = dec_frame->data_valid;
    mp4_state->packet_num = dec_frame->packet_num;
    mp4_state->hdr.width = dec_frame->width;
    mp4_state->hdr.height = dec_frame->height;
    mp4_state->format_plus = dec_frame->format_plus;
    mp4_state->hdr.h263_aic = dec_frame->h263_aic;
    mp4_state->hdr.slice_structured = dec_frame->slice_structured;
    mp4_state->hdr.modified_qantization = dec_frame->modified_qantization;
    mp4_state->umv = dec_frame->umv;
    mp4_state->h263_ap = dec_frame->h263_ap;
    mp4_state->deblocking = dec_frame->deblocking;
    mp4_state->rps = dec_frame->rps;
    mp4_state->isd = dec_frame->isd;
    mp4_state->aiv = dec_frame->aiv;
    mp4_state->spo_extra = dec_frame->spo_extra;
    mp4_state->stream_version = dec_frame->stream_version;
    mp4_state->majors_tream_version = dec_frame->majors_tream_version;
    mp4_state->minor_stream_version = dec_frame->minor_stream_version;
    mp4_state->num_resampled_image_sizes = dec_frame->num_resampled_image_sizes;
    mp4_state->encode_size = dec_frame->encode_size;
    mp4_state->largest_pels = dec_frame->largest_pels;
    mp4_state->largest_lines = dec_frame->largest_lines;
    mp4_state->num_rpr_sizes = dec_frame->num_rpr_sizes;
    mp4_state->fid = dec_frame->fid;
    mp4_state->is_rv8 = dec_frame->is_rv8;
    mp4_state->m_picture_clock_frequency = dec_frame->m_picture_clock_frequency;
    mp4_state->tr_wrap = dec_frame->tr_wrap;
}


/**
 * Fill frame information after successful decoding
 * Populate dec_frame_info structure with decoded frame data
 */
static void decore_fill_frame_info(dec_frame_info_t *dec_frame_info, struct mp4_state *mp4_state, mp4_stream_t *ld)
{
    dec_frame_info->prediction_type = mp4_state->hdr.prediction_type;
    dec_frame_info->quant_store = &mp4_state->quant_store[mp4_state->quant_store_stride + 1];
    dec_frame_info->quant_stride = mp4_state->quant_store_stride;
    dec_frame_info->frame_length = bitpos(ld);
    dec_frame_info->frame_num = mp4_state->hdr.picnum;
    dec_frame_info->vop_coded = mp4_state->hdr.vop_coded;

    if (mp4_state->output_frame) {
        dec_frame_info->y = mp4_state->output_frame[0];
        dec_frame_info->u = mp4_state->output_frame[1];
        dec_frame_info->v = mp4_state->output_frame[2];
    } else {
        dec_frame_info->y = 0;
        dec_frame_info->u = 0;
        dec_frame_info->v = 0;
    }

    dec_frame_info->stride_y = mp4_state->coded_picture_width;
    dec_frame_info->stride_uv = mp4_state->chrom_width;
}

/**
 * Fill VOL information after initialization
 * Populate dec_vol_info structure with video object layer data
 */
static void decore_fill_vol_info(dec_vol_info_t *dec_vol_info, struct mp4_state *mp4_state)
{
    dec_vol_info->x_dim = mp4_state->hdr.width;
    dec_vol_info->y_dim = mp4_state->hdr.height;
    dec_vol_info->time_incr = mp4_state->hdr.time_increment_resolution;
    dec_vol_info->codec_version = mp4_state->userdata_codec_version;
    dec_vol_info->build_number = mp4_state->userdata_build_number;
    dec_vol_info->prefixed = mp4_state->prefixed;
}

/**
 * Handle frame decoding options
 * Process video frames for decoding
 */
static int decore_frame_handler(struct mpeg4_ctx* s, int dec_opt, dec_frame_t *dec_frame, dec_frame_info_t *dec_frame_info, dec_vol_info_t *dec_vol_info)
{
    reference_t *ref = &s->ref;
    struct mp4_state *mp4_state;
    int success;
    int stride;

    if (dec_frame->length == 0)
        return DEC_OK;

    mp4_state = ref->mp4_state;

    ref->mp4_state->new_format = 0;
    ref->mp4_state->render_flag = dec_frame->render_flag;
    mp4_state->if_flv_h263 = dec_frame->if_flv_h263;
    mp4_state->msmpeg_version = dec_frame->msmpeg_version;
    mp4_state->extra_size = dec_frame->extra_size;

    // Setup RealMedia frame parameters
    if (dec_opt == DEC_OPT_RM_FRAME) {
        decore_setup_rm_frame_params(mp4_state, dec_frame);
    }

    stride = dec_frame->stride ? dec_frame->stride : mp4_state->hdr.width;
    if ((dec_opt == DEC_OPT_FRAME) && (stride < mp4_state->hdr.width)) {
        loge("Invalid argument");
        return DEC_INVALID_ARGUMENT;
    }

    // Decode based on codec version
    if (mp4_state->userdata_codec_version == 311 || mp4_state->msmpeg_version) {
        success = decore_frame_311(ref, (unsigned char *)dec_frame->bitstream, dec_frame->length,
                                  (unsigned char *)dec_frame->bmp, stride, dec_frame->render_flag);
    } else {
        success = decore_frame(ref, (unsigned char *)dec_frame->bitstream, dec_frame->length, (unsigned char *)dec_frame->bmp, stride,
                               dec_frame->render_flag, (dec_opt == DEC_OPT_INIT_VOL), dec_frame->skip_decoding);
    }

    if (success == 0) {
        // Fill frame info for regular frame decoding
        if ((dec_opt == DEC_OPT_FRAME) && dec_frame_info) {
            decore_fill_frame_info(dec_frame_info, mp4_state, ref->ld);
        }

        // Fill VOL info for initialization
        if ((dec_opt == DEC_OPT_INIT_VOL) && dec_vol_info) {
            decore_fill_vol_info(dec_vol_info, mp4_state);
        }

        decore_cleanup(); // just does emms if needed
        return DEC_OK;
    }

    if (success > 0)
        return success;

    return DEC_BAD_FORMAT;
}

int decore(struct mpeg4_ctx* s, int dec_opt, void *param1, void *param2)
{
    if (!s) {
        loge("invalid parameter [%p]", s);
        return -1;
    }

    switch (dec_opt) {
    case DEC_OPT_INIT:
        return decore_init_handler(s, (const dec_init_t *)param1);

    case DEC_OPT_RELEASE:
        return decore_release_handler(s);

    case DEC_OPT_SETOUT:
        break;

    case DEC_OPT_ADJUST:
        break;

    case DEC_OPT_FRAME:
    case DEC_OPT_FLV_FRAME:
    case DEC_OPT_RM_FRAME:
    case DEC_OPT_WMV_FRAME:
    case DEC_OPT_INIT_VOL:
        return decore_frame_handler(s, dec_opt, (dec_frame_t *)param1, (dec_frame_info_t *)param2, (dec_vol_info_t *)param2);

    case DEC_OPT_VERSION:
        return DECORE_VERSION;

    case DEC_OPT_SETDEBUG:
        break;

    case DEC_OPT_CONVERTYUV:
    case DEC_OPT_CONVERTYV12:
        break;

    case DEC_OPT_FLUSH:
        return DEC_OK;

    default:
        return DEC_INVALID_ARGUMENT;
    }

    return DEC_BAD_FORMAT; // quiet compiler
}

void init_platform_vld(struct mp4_state *mp4_state)
{
    // Mask using RVLC for B-VOP by Robert Yuan,Oct 5,2003
    if (!mp4_state->hdr.reversible_vlc || mp4_state->hdr.prediction_type == B_VOP) {
        if (!mp4_state->hdr.short_video_header) {
            mp4_state->vld_intra_fun = vld_intra_dct;
            mp4_state->vld_inter_fun = vld_inter_dct;
        } else {
            if (mp4_state->if_flv_h263)
                flvh263version = mp4_state->flvh263version;
            else
                flvh263version = 0;
            if (mp4_state->if_rm_h263) {
                if (mp4_state->hdr.h263_aic && mp4_state->hdr.prediction_type == I_VOP)
                    mp4_state->vld_intra_fun = vld_intra_aic_dct;
                else
                    mp4_state->vld_intra_fun = vld_rmg2_intra_dct;
                if (mp4_state->hdr.modified_qantization)
                    mp4_state->vld_inter_fun = vld_inter_mq_dct;
                else
                    mp4_state->vld_inter_fun = vld_inter_dct;
            } else {
                mp4_state->vld_intra_fun = vld_shv_dct;
                mp4_state->vld_inter_fun = vld_shv_dct;
            }
        }
    } else {
        mp4_state->vld_intra_fun = rvld_intra_dct;
        mp4_state->vld_inter_fun = rvld_inter_dct;
    }
}

// if this function is changed to return error code in any case, it must de-allocate all allocated resources first
int decore_init(reference_t *ref, int codec_version, int smooth_playback, int disp_factor, int w, int h)
{
    struct mp4_state *mp4_state;

    ref->mp4_state = (struct mp4_state *)ref->alloc_fun(sizeof(struct mp4_state));
    ref->ld = (mp4_stream_t *)ref->alloc_fun(sizeof(mp4_stream_t));

    ref->frame_back[0] = 0;
    ref->frame_for[0] = 0;
    ref->frame_ref[0] = 0;

    memset(ref->mp4_state, 0, sizeof(struct mp4_state));

    mp4_state = (struct mp4_state *)ref->mp4_state;
    mp4_state->user_allocated = 0;

    // default value for user data fields
    mp4_state->userdata_codec_version = codec_version;
    mp4_state->userdata_build_number = 0;

    mp4_state->bad_header = DEC_BAD_FORMAT;

    mp4_state->post_flag = 0;
    mp4_state->hdr.width = w;
    mp4_state->hdr.height = h;

    mp4_state->hdr.quant_precision = 5;
    mp4_state->hdr.bits_per_pixel = 8;

    mp4_state->hdr.quant_type = 0;
    mp4_state->hdr.time_increment_resolution = 15;
    mp4_state->test_timeinc = 16;
    mp4_state->hdr.complexity_estimation_disable = 1;

    mp4_state->old_pointer = 0;
    mp4_state->hdr.switch_rounding = 1;
    mp4_state->do_add = 1;
    mp4_state->do_mc = 1;
    mp4_state->do_mc_b = 1;

    // quarter pixel
    memset(mp4_state->mirrored_matrix, 0, sizeof(mp4_state->mirrored_matrix));
    memset(mp4_state->half_hor_matrix, 0, sizeof(mp4_state->half_hor_matrix));
    memset(mp4_state->half_ver_matrix, 0, sizeof(mp4_state->half_ver_matrix));
    memset(mp4_state->half_horver_matrix, 0, sizeof(mp4_state->half_horver_matrix));
    memset(mp4_state->quarter_e_matrix, 0, sizeof(mp4_state->quarter_e_matrix));
    memset(mp4_state->quarter_k_matrix, 0, sizeof(mp4_state->quarter_k_matrix));

    mp4_state->mirrored_matrix_stride = 17 + 6 + 1;
    mp4_state->half_hor_matrix_stride = 16 + 1;
    mp4_state->half_ver_matrix_stride = 17 + 1;
    mp4_state->half_horver_matrix_stride = 16 + 1;
    mp4_state->quarter_e_matrix_stride = 16 + 1;
    mp4_state->quarter_k_matrix_stride = 16 + 1;

    mp4_state->mirrored_matrix_ref = &mp4_state->mirrored_matrix[3 * mp4_state->mirrored_matrix_stride + 3];
    mp4_state->half_hor_matrix_ref = &mp4_state->half_hor_matrix[3 * mp4_state->half_hor_matrix_stride];
    mp4_state->quarter_e_matrix_ref = &mp4_state->quarter_e_matrix[3 * mp4_state->quarter_e_matrix_stride];

    mp4_state->flag_sse = 0;
    mp4_state->flag_3dnow = 0;
    init_platform(&mp4_state->flag_sse, &mp4_state->flag_3dnow);

    mp4_state->flag_buffered_bframe = 0;
    mp4_state->flag_smooth_playback = smooth_playback; // [v503]

    mp4_state->disp_factor = disp_factor;

    // default color conversion mode is convert_touser
    mp4_state->bpp = -1;

    return DEC_OK;
}

static void alloc_phy_buffer(reference_t *ref)
{
    #define MB_INFO_SIZE (384 + 64)
    struct mp4_state *mp4_state = ref->mp4_state;

    ref->mb_cfg_data_len = mp4_state->mb_width * mp4_state->mb_height * MB_INFO_SIZE * sizeof(uint32_t);

    ref->mb_cfg_phy_addr = ve_buffer_alloc(g_mpeg4_ctx->ve_buf_handle, ref->mb_cfg_data_len, ALLOC_NEED_VIR_ADDR);// alloc_physic_buffer(ref->mb_cfg_data_len);
    if (NULL == ref->mb_cfg_phy_addr) {
        loge("alloc_phy_buffer failed");
        return;
    }

    ref->mb_cfg_data = (uint32_t *)ref->mb_cfg_phy_addr->vir_addr;
    memset(ref->mb_cfg_data, 0, ref->mb_cfg_data_len);
    ref->cur_mb_cfg_ptr = ref->mb_cfg_data;

    ref->mb_cfg_info_list_len = mp4_state->mb_width * mp4_state->mb_height * sizeof(mb_config_info_t);
    ref->mb_cfg_info_list = (mb_config_info_t *)malloc(ref->mb_cfg_info_list_len);
    memset(ref->mb_cfg_info_list, 0, ref->mb_cfg_info_list_len);
}

static void free_phy_buffer(reference_t *ref)
{
    struct mpeg4_ctx *s = g_mpeg4_ctx;

    if (s->ref.mb_cfg_phy_addr) {
        ve_buffer_free(s->ve_buf_handle, s->ref.mb_cfg_phy_addr);
        s->ref.mb_cfg_phy_addr = NULL;
    }
    s->ref.mb_cfg_data = NULL; // mb_cfg_data is mb_cfg_phy_addr virtual address

    if (s->ref.mb_cfg_info_list) {
        free(s->ref.mb_cfg_info_list);
        s->ref.mb_cfg_info_list = NULL;
    }

    if (s->decoder.fm) {
        fm_destory(s->decoder.fm);
        s->decoder.fm = NULL;
    }
}

/**
 * Initialize basic frame dimensions and properties
 * @param ref Reference structure containing decoder state
 */
static void init_frame_dimensions(reference_t *ref)
{
    struct mp4_state *mp4_state = ref->mp4_state;

    mp4_state->hdr.picnum = 0;
    mp4_state->hdr.mb_xsize = (mp4_state->hdr.width + 15) / 16;
    mp4_state->hdr.mb_ysize = (mp4_state->hdr.height + 15) / 16;
    mp4_state->hdr.mba_size = mp4_state->hdr.mb_xsize * mp4_state->hdr.mb_ysize;
    mp4_state->hdr.mb_in_vop_length = log2ceil(mp4_state->hdr.mba_size);

    // set picture dimension global vars
    mp4_state->horizontal_size = mp4_state->hdr.width;
    mp4_state->vertical_size = mp4_state->hdr.height;

    mp4_state->mb_width = (mp4_state->horizontal_size + 15) / 16;
    mp4_state->mb_height = (mp4_state->vertical_size + 15) / 16;

    if (g_mpeg4_ctx->decoder.rotmir_flag) {
        mp4_get_rotmir_offset(ref);
    }

    mp4_state->coded_picture_width = mp4_state->mb_width * 16 + 64;
    mp4_state->coded_picture_height = mp4_state->mb_height * 16 + 64;
    mp4_state->chrom_width = mp4_state->coded_picture_width >> 1;
    mp4_state->chrom_height = mp4_state->coded_picture_height >> 1;

    if ((mp4_state->userdata_codec_version < 500) && (mp4_state->userdata_codec_version != 0) &&
        (mp4_state->userdata_codec_version != 311)) {
        mp4_state->edge_hor_start = mp4_state->horizontal_size;
        mp4_state->edge_ver_start = mp4_state->vertical_size;
    } else {
        mp4_state->edge_hor_start = mp4_state->mb_width * 16;
        mp4_state->edge_ver_start = mp4_state->mb_height * 16;
    }
}

/**
 * Initialize AC/DC prediction borders
 * @param ref Reference structure containing decoder state
 */
static void init_prediction_borders(reference_t *ref)
{
    struct mp4_state *mp4_state = ref->mp4_state;
    ac_dc *coeff_pred = &mp4_state->coeff_pred;
    int i, j;

    /* dc prediction border */
    for (i = 0; i < (2 * mp4_state->mb_width + 1); i++) {
        coeff_pred->dc_store_lum[i] = 1024;
    }

    for (i = 1; i < (2 * mp4_state->mb_height + 1); i++) {
        coeff_pred->dc_store_lum[i * coeff_pred->dc_store_lum_stride] = 1024;
    }

    for (i = 0; i < (mp4_state->mb_width + 1); i++) {
        coeff_pred->dc_store_chr[0][i] = 1024;
        coeff_pred->dc_store_chr[1][i] = 1024;
    }

    for (i = 1; i < (mp4_state->mb_height + 1); i++) {
        coeff_pred->dc_store_chr[0][i * coeff_pred->dc_store_chr_stride] = 1024;
        coeff_pred->dc_store_chr[1][i * coeff_pred->dc_store_chr_stride] = 1024;
    }

    /* ac prediction border */
    for (i = 0; i < (2 * mp4_state->mb_width + 1); i++) {
        for (j = 0; j < 7; j++) {
            coeff_pred->ac_left_lum[i * 7 + j] = 0;
            coeff_pred->ac_top_lum[i * 7 + j] = 0;
        }
    }

    for (i = 1; i < (2 * mp4_state->mb_height + 1); i++) {
        for (j = 0; j < 7; j++) {
            coeff_pred->ac_left_lum[i * coeff_pred->ac_left_lum_stride + j] = 0;
            coeff_pred->ac_top_lum[i * coeff_pred->ac_top_lum_stride + j] = 0;
        }
    }

    for (i = 0; i < (mp4_state->mb_width /*DEC_MBC*/ + 1); i++) {
        for (j = 0; j < 7; j++) {
            coeff_pred->ac_left_chr[0][i * 7 + j] = 0;
            coeff_pred->ac_top_chr[0][i * 7 + j] = 0;
            coeff_pred->ac_left_chr[1][i * 7 + j] = 0;
            coeff_pred->ac_top_chr[1][i * 7 + j] = 0;
        }
    }

    for (i = 1; i < (mp4_state->mb_height /*DEC_MBR*/ + 1); i++) {
        for (j = 0; j < 7; j++) {
            coeff_pred->ac_left_chr[0][i * coeff_pred->ac_left_chr_stride + j] = 0;
            coeff_pred->ac_top_chr[0][i * coeff_pred->ac_top_chr_stride + j] = 0;
            coeff_pred->ac_left_chr[1][i * coeff_pred->ac_left_chr_stride + j] = 0;
            coeff_pred->ac_top_chr[1][i * coeff_pred->ac_top_chr_stride + j] = 0;
        }
    }
}

/**
 * Initialize mode borders
 * @param ref Reference structure containing decoder state
 */
static void init_mode_borders(reference_t *ref)
{
    struct mp4_state *mp4_state = ref->mp4_state;
    int i;

    /* mode border */
    for (i = 0; i < mp4_state->mb_width + 1; i++) {
        mp4_state->modemap[i] = NOT_VALID; // INTRA; // [Review]
    }
    for (i = 0; i < mp4_state->mb_height + 1; i++) {
        mp4_state->modemap[i * mp4_state->modemap_stride] = NOT_VALID;                           // INTRA;
        mp4_state->modemap[i * mp4_state->modemap_stride + mp4_state->mb_width + 1] = NOT_VALID; // INTRA;
    }
}

/**
 * Setup frame buffer pointers
 * @param ref Reference structure containing decoder state
 * @param mp4_edged_ref_buffers Reference frame buffer
 * @param mp4_edged_for_buffers Forward frame buffer
 * @param mp4_edged_back_buffers Backward frame buffer
 */
static void setup_frame_buffers(reference_t *ref, void *mp4_edged_ref_buffers,
                                void *mp4_edged_for_buffers, void *mp4_edged_back_buffers)
{
    struct mp4_state *mp4_state = ref->mp4_state;
    int cc;

    // edged forward and reference frame
    for (cc = 0; cc < 3; cc++) {
        if (cc == 0) {
            ref->edged_ref[cc] = (uint8_t *)mp4_edged_ref_buffers;
            if (!ref->edged_ref[cc]) {
                loge("ref->edged_ref[cc] == 0");
                return;
            }

            ref->edged_for[cc] = (uint8_t *)mp4_edged_for_buffers;
            if (!ref->edged_for[cc]) {
                loge("ref->edged_for[cc] == 0");
                return;
            }

            ref->edged_back[cc] = (uint8_t *)mp4_edged_back_buffers;
            if (!ref->edged_back[cc]) {
                loge("ref->edged_back[cc] == 0");
                return;
            }

            ref->frame_ref[cc] = ref->edged_ref[cc] + mp4_state->coded_picture_width * 32 + 32;
            ref->frame_for[cc] = ref->edged_for[cc] + mp4_state->coded_picture_width * 32 + 32;
            ref->frame_back[cc] = ref->edged_back[cc] + mp4_state->coded_picture_width * 32 + 32;
        } else {
            unsigned int height_luma = mp4_state->coded_picture_height;
            unsigned int height_chr = mp4_state->chrom_height;

            unsigned int offset = (cc == 1) ? mp4_state->coded_picture_width * height_luma
                                            : (mp4_state->coded_picture_width * height_luma + mp4_state->chrom_width * height_chr);

            ref->edged_ref[cc] = (uint8_t *)mp4_edged_ref_buffers + offset;
            if (!ref->edged_ref[cc]) {
                loge("ref->edged_ref[cc] == 0");
                return;
            }

            ref->edged_for[cc] = (uint8_t *)mp4_edged_for_buffers + offset;
            if (!ref->edged_for[cc]) {
                loge("ref->edged_for[cc] == 0");
                return;
            }

            ref->edged_back[cc] = (uint8_t *)mp4_edged_back_buffers + offset;
            if (!ref->edged_back[cc]) {
                loge("ref->edged_back[cc] == 0");
                return;
            }

            ref->frame_ref[cc] = ref->edged_ref[cc] + mp4_state->chrom_width * 16 + 16;
            ref->frame_for[cc] = ref->edged_for[cc] + mp4_state->chrom_width * 16 + 16;
            ref->frame_back[cc] = ref->edged_back[cc] + mp4_state->chrom_width * 16 + 16;
        }
    }
}

/**
 * Setup display frame buffers
 * @param ref Reference structure containing decoder state
 * @param mp4_display_buffers Display frame buffer
 */
static void setup_display_frame_buffers(reference_t *ref, void *mp4_display_buffers)
{
    struct mp4_state *mp4_state = ref->mp4_state;
    int cc;

    // display frame
    for (cc = 0; cc < 3; cc++) {
        unsigned int offset;

        switch (cc) {
        case 0:
        default:
            offset = 0;
            break;
        case 1:
            offset = mp4_state->coded_picture_width * mp4_state->coded_picture_height;
            break;
        case 2:
            offset = (mp4_state->coded_picture_width * mp4_state->coded_picture_height) +
                     ((mp4_state->coded_picture_width * mp4_state->coded_picture_height) >> 2);
            break;
        }

        ref->display_frame[cc] = (uint8_t *)mp4_display_buffers + offset; //+ mp4_state->coded_picture_width * 32 + 32;
        if (!ref->display_frame[cc]) {
            loge("ref->display_frame[cc] == 0");
            return;
        }
    }
}

/**
 * Initialize frame buffer IDs and other state variables
 * @param ref Reference structure containing decoder state
 */
static void init_frame_ids_and_state(reference_t *ref)
{
    ref->fwd_frame_id = -1;
    ref->bwd_frame_id = -1;
    ref->cur_frame_id = -1;
    ref->cur_rotmir_id = -1;
    ref->last_rotmir_id = -1;
    ref->mp4_state->bsw_vld = 0;
    ref->need_get_frame = 1;
    ref->find_top_field = 0;
    ref->find_bot_field = 0;
    mpeg_test4_init();
}

int decore_alloc(reference_t *ref)
{
    struct mp4_state *mp4_state = ref->mp4_state;
    int mbxcount = (ref->mp4_state->hdr.width + 15) / 16;
    int mbycount = (ref->mp4_state->hdr.height + 15) / 16;
    int coded_y_size = (mbxcount * 16 + 64) * (mbycount * 16 + 64);
    int coded_c_size = coded_y_size / 4;
    int edged_size = coded_y_size + (2 * coded_c_size);

    void *mp4_edged_ref_buffers = ref->alloc_fun(edged_size);
    void *mp4_edged_for_buffers = ref->alloc_fun(edged_size);
    void *mp4_edged_back_buffers = ref->alloc_fun(edged_size);
    void *mp4_display_buffers = ref->alloc_fun(edged_size);

    if ((!mp4_edged_ref_buffers) || (!mp4_edged_for_buffers) || (!mp4_edged_back_buffers) || (!mp4_display_buffers)) {
        if (mp4_edged_ref_buffers)
            ref->free_fun(mp4_edged_ref_buffers);
        if (mp4_edged_for_buffers)
            ref->free_fun(mp4_edged_for_buffers);
        if (mp4_edged_back_buffers)
            ref->free_fun(mp4_edged_back_buffers);
        if (mp4_display_buffers)
            ref->free_fun(mp4_display_buffers);
        return DEC_MEMORY;
    }

    // Initialize frame dimensions and properties
    init_frame_dimensions(ref);

    // Allocate MP4 state
    mp4_state_alloc(ref, mp4_state, mp4_state->horizontal_size, mp4_state->vertical_size);

    // Initialize AC/DC prediction borders
    init_prediction_borders(ref);

    // Initialize mode borders
    init_mode_borders(ref);

    // Setup frame buffers
    setup_frame_buffers(ref, mp4_edged_ref_buffers, mp4_edged_for_buffers, mp4_edged_back_buffers);

    // Setup display frame buffers
    setup_display_frame_buffers(ref, mp4_display_buffers);

    // Initialize frame IDs and state
    init_frame_ids_and_state(ref);

    // Allocate physical buffer
    alloc_phy_buffer(ref);

    return 0;
}

typedef motion_vector_t (*mv_type)[6];

void mp4_state_alloc(reference_t *ref, struct mp4_state *mp4_state, int hor_size, int ver_size)
{
    int mb_hor_size = hor_size / 16 + 1;
    int mb_ver_size = ver_size / 16 + 1;
    int i, size;
    // motion_vector_t *vect;

    mp4_state->codedmap_stride = mb_hor_size + 1;
    mp4_state->modemap_stride = mb_hor_size + 2;
    mp4_state->cbp_store_stride = mb_hor_size + 1;
    mp4_state->quant_store_stride = mb_hor_size + 1;
    mp4_state->MV_stride = mb_hor_size + 2;
    mp4_state->fieldpredictedmap_stride = mb_hor_size + 1;
    mp4_state->fieldrefmap_stride = 2 * mb_hor_size;

    size = (mb_ver_size + 1) * (mb_hor_size + 1) * sizeof(int);
    mp4_state->codedmap = (int *)ref->alloc_fun(size); // fixme: check for nulls
    memset(mp4_state->codedmap, -1, size);

    size = (mb_ver_size + 1) * (mb_hor_size + 2) * sizeof(int);
    mp4_state->modemap = (int *)ref->alloc_fun(size);
    memset(mp4_state->modemap, -1, size);

    size = (mb_ver_size + 1) * (mb_hor_size + 1) * sizeof(short);
    mp4_state->cbp_store = (short *)ref->alloc_fun(size);
    memset(mp4_state->cbp_store, 0, size);

    size = (mb_ver_size + 1) * (mb_hor_size + 1) * sizeof(char);
    mp4_state->quant_store = (char *)ref->alloc_fun(size);
    memset(mp4_state->quant_store, 0, size);

    size = (mb_ver_size + 1) * (mb_hor_size + 1) * sizeof(int);
    mp4_state->fieldpredictedmap = (int *)ref->alloc_fun(size);
    memset(mp4_state->fieldpredictedmap, 0, size);

    size = 2 * mb_ver_size * mb_hor_size * sizeof(char);
    mp4_state->fieldrefmap = (char *)ref->alloc_fun(size);
    memset(mp4_state->fieldrefmap, 0, size);

    size = 6 * (mb_ver_size + 1) * (mb_hor_size + 2) * sizeof(motion_vector_t);
    mp4_state->mv = (mv_type)ref->alloc_fun(size);
    mp4_state->mv_field = (mv_type)ref->alloc_fun(size);
    mp4_state->mv_back = (mv_type)ref->alloc_fun(size);
    memset(mp4_state->mv, 0, size);
    memset(mp4_state->mv_field, 0, size);
    memset(mp4_state->mv_back, 0, size);

    size = mp4_state->mb_width * mp4_state->mb_height * sizeof(int);
    mp4_state->hdr.mb_skip = (int *)ref->alloc_fun(size);
    if (NULL == mp4_state->hdr.mb_skip) {
        loge("malloc mp4_state->hdr.mb_skip failed");
        return;
    }
    memset(mp4_state->hdr.mb_skip, 0, size);

    // ac_dc
    mp4_state->coeff_pred.dc_store_lum = (int *)ref->alloc_fun((2 * mb_ver_size + 1) * (2 * mb_hor_size + 1) * sizeof(int));
    mp4_state->coeff_pred.ac_left_lum = (int *)ref->alloc_fun((2 * mb_ver_size + 1) * (2 * mb_hor_size + 1) * 7 * sizeof(int));
    mp4_state->coeff_pred.ac_top_lum = (int *)ref->alloc_fun((2 * mb_ver_size + 1) * (2 * mb_hor_size + 1) * 7 * sizeof(int));
    mp4_state->coeff_pred.pAic_luma_top = (int *)ref->alloc_fun((2 * mb_hor_size + 1) * 8 * sizeof(int));
    mp4_state->coeff_pred.pAic_chroma0_top = (int *)ref->alloc_fun((mb_hor_size + 1) * 8 * sizeof(int));
    mp4_state->coeff_pred.pAic_chroma1_top = (int *)ref->alloc_fun((mb_hor_size + 1) * 8 * sizeof(int));

    for (i = 0; i < 2; i++) {
        mp4_state->coeff_pred.dc_store_chr[i] = (int *)ref->alloc_fun((mb_ver_size + 1) * (mb_hor_size + 1) * sizeof(int));
        mp4_state->coeff_pred.ac_left_chr[i] = (int *)ref->alloc_fun((mb_ver_size + 1) * (mb_hor_size + 1) * 7 * sizeof(int));
        mp4_state->coeff_pred.ac_top_chr[i] = (int *)ref->alloc_fun((mb_ver_size + 1) * (mb_hor_size + 1) * 7 * sizeof(int));
    }

    mp4_state->coeff_pred.dc_store_lum_stride = 2 * mb_hor_size + 1;
    mp4_state->coeff_pred.ac_left_lum_stride = (2 * mb_hor_size + 1) * 7;
    mp4_state->coeff_pred.ac_top_lum_stride = (2 * mb_hor_size + 1) * 7;
    mp4_state->coeff_pred.dc_store_chr_stride = mb_hor_size + 1;
    mp4_state->coeff_pred.ac_left_chr_stride = (mb_hor_size + 1) * 7;
    mp4_state->coeff_pred.ac_top_chr_stride = (mb_hor_size + 1) * 7;
}

void mp4_state_free(reference_t *ref, struct mp4_state *mp4_state)
{
    int i;

    ref->free_fun(mp4_state->modemap);
    ref->free_fun(mp4_state->codedmap);
    ref->free_fun(mp4_state->cbp_store);
    ref->free_fun(mp4_state->quant_store);
    ref->free_fun(mp4_state->fieldpredictedmap);
    ref->free_fun(mp4_state->fieldrefmap);

    ref->free_fun(mp4_state->mv);
    ref->free_fun(mp4_state->mv_field);
    ref->free_fun(mp4_state->mv_back);

    ref->free_fun(mp4_state->coeff_pred.dc_store_lum);
    ref->free_fun(mp4_state->coeff_pred.ac_left_lum);
    ref->free_fun(mp4_state->coeff_pred.ac_top_lum);
    ref->free_fun(mp4_state->coeff_pred.pAic_luma_top);
    ref->free_fun(mp4_state->coeff_pred.pAic_chroma0_top);
    ref->free_fun(mp4_state->coeff_pred.pAic_chroma1_top);

    for (i = 0; i < 2; i++) {
        ref->free_fun(mp4_state->coeff_pred.dc_store_chr[i]);
        ref->free_fun(mp4_state->coeff_pred.ac_left_chr[i]);
        ref->free_fun(mp4_state->coeff_pred.ac_top_chr[i]);
    }

    if (mp4_state->hdr.mb_skip) {
        ref->free_fun(mp4_state->hdr.mb_skip);
        mp4_state->hdr.mb_skip = NULL;
    }
}

int try_adjust_time_increment(mp4_stream_t *ld, struct mp4_state *mp4_state);
extern void getusrhdr(mp4_stream_t *ld, struct mp4_state *mp4_state, int just_vol_init);

// If in this chunk there's a second frame, copy it in the buffered_bframe buffer
// so that it can be decoded and displayed at the next decoder call
static int decode_bufferize_bframe(reference_t *_ref)
{
    struct mp4_state *mp4_state = _ref->mp4_state;
    mp4_stream_t *ld = _ref->ld;
    int bitshift_counter = 0;

    //  return 1; // [Ag][smooth]

    loge("[smooth] Bufferizing B-Frame\n");
    while (showbits(ld, 32) != (int)VOP_START_CODE) {
        bitshift_counter++;
        next_start_code(ld, mp4_state);
        if (bitshift_counter > 100)
            return -1;
    }

    mp4_state->flag_buffered_bframe = 1;
    mp4_state->flag_seek_bframe = -1; // I can restart only once

    mp4_state->buffered_bframe_length = ld->length - (ld->rdptr - 8 - ld->startptr); // [Review]
    if (mp4_state->buffered_bframe_length < 0) {
        loge("mp4_state->buffered_bframe_length < 0");
        return -1;
    }
    if (mp4_state->buffered_bframe)
        _ref->free_fun(mp4_state->buffered_bframe);
    mp4_state->buffered_bframe = (char *)_ref->alloc_fun(mp4_state->buffered_bframe_length + 4);
    memcpy(mp4_state->buffered_bframe, ld->rdptr - 8,
           mp4_state->buffered_bframe_length + 1); // [smooth][Review] calculation of buffer lenght

    return 0;
}

int get_current_field_type(reference_t *ref, struct mp4_state *mp4_state) {
    if (mp4_state->hdr.interlaced && mp4_state->hdr.field_prediction) {
        if (mp4_state->hdr.top_field_first) {
            return (mp4_state->hdr.temporal_reference % 2 == 0) ? TOP_FIELD : BOTTOM_FIELD;
        } else {
            return (mp4_state->hdr.temporal_reference % 2 == 0) ? BOTTOM_FIELD : TOP_FIELD;
        }
    }
    return FRAME;
}

int check_frame_render_condition(reference_t *ref, struct mp4_state *mp4_state) {
    int field_type = get_current_field_type(ref, mp4_state);
    int need_render_frame = 0;
    if (field_type == TOP_FIELD) {
        if (ref->find_bot_field) {
            need_render_frame = 1;
            ref->find_bot_field = 0;
        } else {
            ref->find_top_field = 1;
        }
    } else if (field_type == BOTTOM_FIELD) {
        if (ref->find_top_field) {
            need_render_frame = 1;
            ref->find_top_field = 0;
        } else {
            ref->find_bot_field = 1;
        }
    } else if (field_type == FRAME) {
        need_render_frame = 1;
        ref->find_top_field = 0;
        ref->find_bot_field = 0;
    }
    return need_render_frame;
}

/**
 * @brief Creation of final image process
 *
 * Creation of final image is done in several steps, which are supposed to achieve reasonable
 * cache efficiency.
 *
 * 1. Each decoded macroblock is written during MC into mp4_state->frame_to_display in IYUV format
 *    with stride = width + 64.
 * 2. For each macroblock we call mp4_state->MacroblockDisplay(). This function does
 *    different things depending on level of postprocessing.
 * 2.1. In all cases, if user wants to do color conversion by himself ( DEC_USER ), color conversion is omitted.
 * 2.2. If no postprocessing is needed, macroblock will be immediately processed
 * with YUV->RGB or YUV->YUY2 and written into user memory area.
 * 2.3. If postproc is on, we copy macroblock from mp4_state->frame_to_display
 * to ref->display_frame.
 * 3. If only horizontal deblocking is on,  do deblocking on ref->display_frame and color conversion
 * on left neighbor MB.
 * 4. If only deblocking is on, do deblocking on ref->display_frame and color conversion on upper neighbor
 * MB.
 * 5. If deringing is also on, do deblocking and calculate thresholds for deringing. Deringing itself together with
 * color conversion will be done during separate pass.
 */
int decore_frame(reference_t *ref, unsigned char *stream, int length, unsigned char *bmp,
                 unsigned int stride, int render_flag, int just_vol_init, int skip_decoding)
{
    struct mp4_state *mp4_state = ref->mp4_state;
    mp4_stream_t *ld = ref->ld;
    int backup_render_flag;
    int err;

    if (stride == 0)
        stride = mp4_state->hdr.width;
    mp4_state->output_frame = 0;

    if (skip_decoding == 1) {
        mp4_state->flag_skip_decoding = 1;
        return 0;
    }

    mp4_state->bmp = bmp;
    mp4_state->stride = stride;

    if (mp4_state->old_pointer == bmp)
        mp4_state->memory_cheat = 1;
    else
        mp4_state->memory_cheat = 0;
    mp4_state->old_pointer = render_flag ? bmp : 0;

restart_decode:
    if (mp4_state->if_flv_h263) {
        if ((stream[0] & 0x0f) == 4) {
            // stream[0]: UB[4] FrameType, UB[4] codecID
            // stream[1]: UB[4] HorizontalAdjust, UB[4] VerticalAdjust
            if (!mp4_frame_ctr) {
                ref->fwd_frame_id = -1;
                ref->bwd_frame_id = -1;
                ref->cur_frame_id = -1;
                ref->cur_rotmir_id = -1;
                ref->last_rotmir_id = -1;
                ref->need_get_frame = 1;
                ref->find_top_field = 0;
                ref->find_bot_field = 0;
            }

            goto mp4_pic_decoded;
        }
    } else if (mp4_state->userdata_codec_version == 12) {
        loge("not support");
    }

    initbits(ld, stream, length);

    // [prefix] stuffing chunk, indicated that the encoder is buffering
    if ((length <= 2) && (stream[0] == 0x7f)) {
        loge("Null Frame, Rendering Last Decoded Frame\n");
        get_notcoded_mp4picture(ref, bmp, stride, render_flag);
        return 0;
    }

    mp4_state->prefixed = 0; // will avoid the delay caused by B frames
    mp4_state->flag_keyframe = 0;

    unsigned int code32 = showbits(ld, 32);
    while (1) {
        code32 = showbits(ld, 32);
        if ((VO_START_CODE_MIN <= code32 && code32 <= VO_START_CODE_MAX) || (VOS_START_CODE <=code32 && code32 <= VOP_START_CODE) ||
            showbits(ld, 28) == VOL_START_CODE) {
            break;
        }
        flushbits(ld, 8);
    }

    if (!mp4_state->if_flv_h263 && !mp4_state->if_rm_h263) {
        getvoshdr(ld, mp4_state, just_vol_init);

        // parse VisualObject()
        if (getvsohdr(ld, mp4_state)) {
            if (bytealign(ld) == 8) {
                // next_start_code()
                mp4_state->prefixed = 1;
            }

            while (showbits(ld, 32) == USR_START_CODE)
                getusrhdr(ld, mp4_state, just_vol_init);
        }

        // interlace, q_type, q_matrix
        err = getvolhdr(ld, mp4_state, just_vol_init);
        if (err == 0) {
            mp4_state->bad_header = 0;
            mp4_state->flag_buffered_bframe = 0;
        }
        if (err > 0) {
            mp4_state->bad_header = err;
            return err; // fixme: save this result so we return it again next time
        }
        if ((err < 0) && (mp4_state->bad_header != 0))
            return mp4_state->bad_header;
    }

    // decoder has not been allocated yet
    if (!ref->frame_back[0]) {
        if ((mp4_state->hdr.width < 0) || (mp4_state->hdr.width > 3840) || (mp4_state->hdr.height < 0) || (mp4_state->hdr.height > 2160))
            return DEC_BAD_FORMAT;

        err = decore_alloc(ref);
        if (err != DEC_OK)
            return err;
    }

    if (mp4_state->if_flv_h263) {
        get_flv_pic_hdr(ld, mp4_state);
        if (mp4_state->flag_disposable)
            return 0;
    } else {
        getshvhdr(ld, mp4_state);
    }
    if ((mp4_state->hdr.data_partitioning == 1) || (mp4_state->hdr.reversible_vlc == 1)) {
        mp4_state->bsw_vld = 1;
    }

    if ((mp4_state->preceding_vop_coding_type == B_VOP || mp4_state->preceding_vop_coding_type == S_VOP ||
         mp4_state->hdr.shape != RECTANGULAR) &&
        showbits(ld, 32) == STF_START_CODE) {
        getbits(ld, 32); // stuffing_start_code
        while (showbits(ld, 24) != 0x01)
            getbits(ld, 8);
    }

    if (!mp4_state->if_flv_h263)
        getgophdr(ld, mp4_state);

    if (just_vol_init)
        return 0;

    mp4_state->flag_seek_bframe = -1;

decode_next_frame:
    // [smooth]
    mp4_state->flag_seek_bframe++;
    if (mp4_state->flag_buffered_bframe == 1) {
        loge("[smooth] Retrieving Buffered B-Frame\n"); // [Ag][smooth]
        initbits(ld, (const unsigned char *)mp4_state->buffered_bframe, mp4_state->buffered_bframe_length);
        mp4_state->flag_buffered_bframe = 0;
    }

    if (!mp4_state->hdr.short_video_header) {
        // [smooth]
        if ((mp4_state->flag_seek_bframe == 1) && (mp4_state->flag_smooth_playback)) {
            // To try to achieve a smooth playback, I'm going to copy the second frame in a buffer,
            // signal that there's a frame there (for the next decoder call) and return
            loge("[smooth] Copying frame to internal buffer\n");
            decode_bufferize_bframe(ref);
            return 0; // even if we didn't find the B-frame
        } else {
            int err = getvophdr(ld, mp4_state);
            if (err != 0) {
                int val = try_adjust_time_increment(ld, mp4_state);
                loge("Warning: Wrong time_increment or other error encountered in VOP header, decoding restarts\n");
                if (val)
                    goto restart_decode;
                else if (err > 0)
                    return err;
                else
                    return DEC_BAD_FORMAT;
            }

            if (g_mpeg4_ctx->decoder.fm == NULL) {
                struct frame_manager_init_cfg cfg;
                cfg.frame_count = g_mpeg4_ctx->extra_frame_num + 3;
                if (g_mpeg4_ctx->decoder.rotmir_flag && cfg.frame_count < 5) {
                    cfg.frame_count += 1;
                }
                cfg.width = mp4_state->mb_width * 16;
                cfg.height = mp4_state->mb_height * 16;
                cfg.stride = mp4_state->mb_width * 16;
                cfg.height_align = mp4_state->mb_height * 16;
                cfg.pixel_format = MPP_FMT_YUV420P;
                cfg.allocator = g_mpeg4_ctx->decoder.allocator;
                g_mpeg4_ctx->decoder.fm = fm_create(&cfg);
                if (NULL == g_mpeg4_ctx->decoder.fm) {
                    loge("fm_create failed");
                    return -1;
                }
            }

            if (ref->need_get_frame) {
                struct frame *f = fm_decoder_get_frame(g_mpeg4_ctx->decoder.fm);
                if (f == NULL) {
                    loge("fm_decoder_get_frame failed!");
                    pm_reclaim_ready_packet(g_mpeg4_ctx->decoder.pm, g_mpeg4_ctx->curr_packet);
                    return DEC_NO_EMPTY_FRAME;
                }
                ref->cur_frame_id = f->mpp_frame.id;
                ref->frame[f->mpp_frame.id] = f;

                if (g_mpeg4_ctx->decoder.rotmir_flag) {
                    struct frame *rotmir_f = fm_decoder_get_frame(g_mpeg4_ctx->decoder.fm);
                    if (rotmir_f == NULL) {
                        loge("fm_decoder_get_frame failed for rotmir_f!");
                        pm_reclaim_ready_packet(g_mpeg4_ctx->decoder.pm, g_mpeg4_ctx->curr_packet);
                        return DEC_NO_EMPTY_FRAME;
                    }
                    ref->cur_rotmir_id = rotmir_f->mpp_frame.id;
                    ref->frame[rotmir_f->mpp_frame.id] = rotmir_f;
                }
            }

            if (ref->cur_frame_id < 0 || ref->cur_frame_id >= MPEG4_FRAME_NUM) {
                loge("Invalid cur_frame_id: %d", ref->cur_frame_id);
                return -1;
            }
        }
    }

    // [prefix]
    if (mp4_state->hdr.prediction_type == I_VOP) {
        mp4_state->history_prefixed = mp4_state->prefixed ? 1 : 0;
    }

    init_platform_vld(mp4_state);

    backup_render_flag = render_flag;
    if ((mp4_state->userdata_codec_version >= 500) && mp4_state->history_prefixed &&
        ((mp4_state->hdr.prediction_type == P_VOP) || (mp4_state->hdr.prediction_type == S_VOP)) && (mp4_state->hdr.vop_coded == 1) &&
        (!mp4_state->flag_smooth_playback))
        render_flag = 0; // it will decide later if this frame has to be displayed or not (B frame postfixed in this case)

    if ((mp4_state->flag_smooth_playback) && (mp4_state->history_prefixed) && (mp4_state->hdr.prediction_type != B_VOP) &&
        (mp4_state->hdr.picnum != 0)) {
        loge("[smooth] Rendering Last Decoded P-Frame\n");
        mp4_state->frame_to_display = ref->frame_for;
        get_notcoded_mp4picture(ref, bmp, stride, render_flag);
        render_flag = 0;
    }

    if (mp4_state->flag_skip_decoding == 1) {
        if (mp4_state->hdr.picture_coding_type == I_VOP)
            mp4_state->flag_skip_decoding = 0;
        else
            return 0;
    }

    // 1. set header info regs
    if (mp4_state->hdr.vop_coded) {
        logi("[smooth] Decoding Frame\n");
        mp4_set_vop_info(mp4_state);
        if (get_mp4picture(ref, bmp, stride, render_flag) != 0) {
            goto restart_decode;
        }

        if (check_frame_render_condition(ref, mp4_state)) {
            if (g_mpeg4_ctx->decoder.rotmir_flag) {
                if (ref->cur_rotmir_id < 0) {
                    loge("cur_rotmir_id shall not be less than zero!");
                    goto restart_decode;
                }

                fm_decoder_frame_to_render(g_mpeg4_ctx->decoder.fm, ref->frame[ref->cur_rotmir_id], 1);
                fm_decoder_frame_to_render(g_mpeg4_ctx->decoder.fm, ref->frame[ref->cur_frame_id], 0);
            } else {
                fm_decoder_frame_to_render(g_mpeg4_ctx->decoder.fm, ref->frame[ref->cur_frame_id], 1);
            }

            ref->need_get_frame = 1;
            if ((mp4_state->hdr.prediction_type == B_VOP)) {
                fm_decoder_put_frame(g_mpeg4_ctx->decoder.fm, ref->frame[ref->cur_frame_id]);
            }
        } else {
            ref->need_get_frame = 0;
        }
    } else {
        loge("[smooth] Rendering Last Frame (not coded)\n");
        get_notcoded_mp4picture(ref, bmp, stride, render_flag);
    }

mp4_pic_decoded:
    render_flag = backup_render_flag;
    mp4_state->preceding_vop_coding_type = mp4_state->hdr.prediction_type;

    if (mp4_state->show_type)
        decore_show_type(bmp, stride, mp4_state->hdr.prediction_type);

    mp4_state->hdr.picnum++;
    mp4_frame_ctr++;

    // [prefix]
    if ((mp4_state->userdata_codec_version >= 500) && mp4_state->history_prefixed) {
        while (8 * ld->length - bitpos(ld) >= 32) {
            if (showbits(ld, 32) == VOP_START_CODE) {
                goto decode_next_frame;
            } else {
                getbits(ld, 1);
            }
            if ((mp4_state->hdr.prediction_type == I_VOP) || (ld->length > (unsigned int)(mp4_state->hdr.width * mp4_state->hdr.height)))
                break; // stream length information wrong (buf fix for smart tree upstream filter)
        }
        if (((mp4_state->hdr.prediction_type == P_VOP) || (mp4_state->hdr.prediction_type == S_VOP)) && (mp4_state->hdr.vop_coded == 1) &&
            (!mp4_state->flag_smooth_playback))
            get_notcoded_mp4picture(ref, bmp, stride, render_flag);
    }

    return 0;
}

static int decore_frame_311(reference_t *_ref, unsigned char *stream, int length, unsigned char *bmp, unsigned int stride, int render_flag)
{
    mp4_stream_t *ld = _ref->ld;
    struct mp4_state *mp4_state = _ref->mp4_state;

    mp4_state->bmp = bmp;
    mp4_state->stride = stride;
    mp4_state->output_frame = 0;
    if (!mp4_frame_ctr && mp4_state->msmpeg_version >= 4 && mp4_state->extra_size) {
        int code;
        // decode_ext_header
        initbits(ld, stream, mp4_state->extra_size);
        getbits(ld, 5);     // fps
        getbits(ld, 11);     // bit_rate = getbits(ld, 11) * 1024
        mp4_state->mspel_bit = getbits1(ld);
        mp4_state->deblockingflag = getbits1(ld);
        mp4_state->abt_flag = getbits1(ld);
        mp4_state->j_type_bit = getbits1(ld);
        mp4_state->top_left_mv_flag = getbits1(ld);
        mp4_state->per_mb_rl_bit = getbits1(ld);
        code = getbits(ld, 3);

        if (code == 0)
            return -1;

        mp4_state->hdr.slice_height = ((mp4_state->hdr.height + 15) >> 4) / code;
    }

    initbits(ld, stream + mp4_state->extra_size, length - mp4_state->extra_size);
    if (!_ref->frame_back[0]) // decoder has not been allocated yet
    {
        int err;
        if ((mp4_state->hdr.width < 0) || (mp4_state->hdr.width > 2048) || (mp4_state->hdr.height < 0) || (mp4_state->hdr.height > 2048))
            return DEC_BAD_FORMAT;
        err = decore_alloc(_ref);
        if (err != 0)
            return err;
    }

    mp4_state->bsw_vld = 0;
    if (!mp4_state->msmpeg_version) {
        getvophdr_311(ld, mp4_state); // read vop header
    } else if (mp4_state->msmpeg_version == 5) {
        wmv2_decode_picture_header(ld, mp4_state);
    } else {
        msmpeg4_decode_picture_header(ld, mp4_state);
    }

    mp4_set_display_buf(mp4_state);

    if (mp4_state->old_pointer == bmp)
        mp4_state->memory_cheat = 1;
    else
        mp4_state->memory_cheat = 0;
    mp4_state->old_pointer = render_flag ? bmp : 0;

    get_mp4picture_311(_ref, bmp, stride, render_flag); // decode vop

    mp4_state->hdr.picnum++;
    mp4_frame_ctr++;

    return 0;
}

// frees dimension dependent buffers
int decore_dealloc(reference_t *ref)
{
    if (!ref->frame_back[0])
        return DEC_INVALID_ARGUMENT;

    ref->free_fun(ref->edged_ref[0]);
    ref->free_fun(ref->edged_for[0]);
    ref->free_fun(ref->edged_back[0]);
    ref->free_fun(ref->display_frame[0]);
    mp4_state_free(ref, ref->mp4_state);
    free_phy_buffer(ref);

    return DEC_OK;
}

// frees dimension independent buffers
int decore_release(reference_t *ref)
{
#ifdef POWERPC
    term_platform();
#endif
    ref->free_fun(ref->mp4_state);
    ref->free_fun(ref->ld);
    // ref->free_fun(ref);

    return DEC_OK;
}

#define FOURCC(A, B, C, D) (((uint8_t)A) | (((uint8_t)B) << 8) | (((uint8_t)C) << 16) | (((uint8_t)D) << 24))

extern int do_checksumming_decore;
extern int last_checksum_y_decore, last_checksum_uv_decore;

int decore_backdoor(void *handle, int enc_opt, void *param1, void *param2)
{
    switch (enc_opt) {
    case 0:
        do_checksumming_decore = *(int *)param1;
        break;
    case 1:
        ((int *)param1)[0] = last_checksum_y_decore;
        ((int *)param1)[1] = last_checksum_uv_decore;
        break;
    }
    return 1;
}

// save mp4 not_code flag to file for RSIM
void mp4_save_notcoded_flag(struct mp4_state *_mp4_state)
{

}

void mp4_set_vop_info(struct mp4_state *_mp4_state)
{
    mp4_set_display_buf(_mp4_state);
}

void mp4_set_display_buf(struct mp4_state *_mp4_state)
{
    struct mp4_state *mp4_state = _mp4_state;
    reference_t *ref = &g_mpeg4_ctx->ref;

    if (get_current_field_type(ref, mp4_state) != FRAME) {
        return;
    }

    if (mp4_state->hdr.prediction_type == B_VOP || mp4_state->hdr.prediction_type == S_VOP) {
        return;
    } else if (mp4_state->hdr.prediction_type == I_VOP) {
        if (ref->fwd_frame_id >= 0) {
            fm_decoder_put_frame(g_mpeg4_ctx->decoder.fm, ref->frame[ref->fwd_frame_id]);
        }
        if (ref->bwd_frame_id >= 0) {
            fm_decoder_put_frame(g_mpeg4_ctx->decoder.fm, ref->frame[ref->bwd_frame_id]);
            ref->bwd_frame_id = -1;
        }
    } else if (mp4_state->hdr.prediction_type == P_VOP) {
        if (ref->fwd_frame_id >= 0) {
            fm_decoder_put_frame(g_mpeg4_ctx->decoder.fm, ref->frame[ref->fwd_frame_id]);
        }
    }
    ref->fwd_frame_id= ref->bwd_frame_id;
    ref->bwd_frame_id = ref->cur_frame_id;

    if (g_mpeg4_ctx->decoder.rotmir_flag) {
        if(ref->last_rotmir_id >= 0) {
            fm_decoder_put_frame(g_mpeg4_ctx->decoder.fm, ref->frame[ref->last_rotmir_id]);
        }
        ref->last_rotmir_id = ref->cur_rotmir_id;
    }
}

static void config_mb_coeffs(struct mp4_state *mp4_state)
{
    int len = g_mpeg4_ctx->ref.mb_cfg_info_list[mp4_state->hdr.mba].len;
    uint32_t *ptr = g_mpeg4_ctx->ref.mb_cfg_info_list[mp4_state->hdr.mba].ptr;
    uint32_t *pVal = NULL;

    reg_tq_coeff coeff_data = {0};
    coeff_data.tq_coeff_buf_access = 1;
    coeff_data.tq_coeff_buf_wr = 1;
    for (int i = 0; i < 6; i++) {
        int addr = 64 * i;
        int coded = mp4_state->hdr.cbp & (1 << (5 - i));
        if (coded) {
            for (int j = 0; j < 64; j++) {
                if (mp4_state->mpeg_coef_matrix[i][j]) {
                    coeff_data.tq_coeff_buf_addr = addr + j;
                    coeff_data.tq_coeff_buf_data = mp4_state->mpeg_coef_matrix[i][j];
                    pVal = (uint32_t *)&coeff_data;
                    ptr[len++] = *pVal;
                }
            }
        }
    }
    g_mpeg4_ctx->ref.mb_cfg_info_list[mp4_state->hdr.mba].len = len;
}

static int is_last_mb(struct mp4_state *mp4_state, int mb_x, int mb_y)
{
    if (mb_x == (mp4_state->hdr.mb_xsize - 1) && mb_y == (mp4_state->hdr.mb_ysize - 1))
        return 1;

    return 0;
}

static void write_mb_info_header(struct mp4_state *mp4_state, int mb_x, int mb_y)
{
    reference_t *ref = &g_mpeg4_ctx->ref;
    int cbp = mp4_state->hdr.cbp;
    uint32_t *pVal;

    mb_info_header header = {0};
    header.cbp_num = ((cbp >> 5) & 1) + ((cbp >> 4) & 1) + ((cbp >> 3) & 1) + ((cbp >> 2) & 1) + ((cbp >> 1) & 1) + ((cbp >> 0) & 1);

    if (mp4_state->hdr.mb_intra)
        header.mb_type = 0;
    else if (mp4_state->hdr.mb_fwd_mc && !mp4_state->hdr.mb_bwd_mc)
        header.mb_type = 1;
    else
        header.mb_type = 2;

    header.mb_x = mb_x;
    header.mb_y = mb_y;

    header.last_mb = is_last_mb(mp4_state, mb_x, mb_y);
    header.data_length_bytes = 4 * (ref->mb_cfg_info_list[mp4_state->hdr.mba].len - 1);
    pVal = (uint32_t *)&header;
    ref->mb_cfg_info_list[mp4_state->hdr.mba].ptr[0] = *pVal;
    ref->cur_mb_cfg_ptr += ref->mb_cfg_info_list[mp4_state->hdr.mba].len;

#ifdef MPEG4_DUMP_ENABLE
    fprintf(g_mpeg4_ctx->fp_mb_cfg_data, "intra: %d, fwd_mc: %d, bwd_mc: %d\n", mp4_state->hdr.mb_intra, mp4_state->hdr.mb_fwd_mc,
            mp4_state->hdr.mb_bwd_mc);
    fprintf(g_mpeg4_ctx->fp_mb_cfg_data, "mb_type: %d, last: %d, header.data_length_bytes: %d\n", header.mb_type, header.last_mb,
            header.data_length_bytes);

    fprintf(g_mpeg4_ctx->fp_mb_cfg_data, "cur_mb_addr: %d, cfg_data offset: 0x%lx, len: %d\n", mp4_state->hdr.mba,
            (g_mpeg4_ctx->ref.mb_cfg_info_list[mp4_state->hdr.mba].ptr - g_mpeg4_ctx->ref.mb_cfg_data) * 4,
            g_mpeg4_ctx->ref.mb_cfg_info_list[mp4_state->hdr.mba].len);

    fprintf(g_mpeg4_ctx->fp_mb_info, "cur_mb_addr: %d, cfg_data offset: 0x%lx, len: %d\n", mp4_state->hdr.mba,
            (g_mpeg4_ctx->ref.mb_cfg_info_list[mp4_state->hdr.mba].ptr - g_mpeg4_ctx->ref.mb_cfg_data) * 4,
            g_mpeg4_ctx->ref.mb_cfg_info_list[mp4_state->hdr.mba].len);
#endif
}

// there is a DC coeff in intra 8x8 block at least,
// so cbp must be 0x3f in this cases
void change_cbp_intra(struct mp4_state *mp4_state)
{
    if (((mp4_state->hdr.derived_mb_type == INTRA) || (mp4_state->hdr.derived_mb_type == INTRA_Q)) &&
        !mp4_state->hdr.h263_aic) { // see 6.2.1 14496-2
        if (mp4_state->hdr.short_video_header == 1)
            mp4_state->hdr.cbp = 0x3f;
        else if (mp4_state->hdr.use_intra_dc_vlc == 1)
            mp4_state->hdr.cbp = 0x3f;
        if (mp4_state->userdata_codec_version == 311 || mp4_state->msmpeg_version >= 4)
            mp4_state->hdr.cbp = 0x3f;

        if (!mp4_state->hdr.short_video_header)
            mp4_state->hdr.cbp = 0x3f;
    }
}
// set macroblock level information
void mp4_set_mb_info(struct mp4_state *_mp4_state)
{
    int mb_x, mb_y;
    struct mp4_state *mp4_state = _mp4_state;

    // calaculat MB coordinate
    mb_x = mp4_state->hdr.mba % mp4_state->hdr.mb_xsize;
    mb_y = mp4_state->hdr.mba / mp4_state->hdr.mb_xsize;

    // reserve 1 data space for mb_cfg header
    g_mpeg4_ctx->ref.mb_cfg_info_list[mp4_state->hdr.mba].ptr = g_mpeg4_ctx->ref.cur_mb_cfg_ptr;
    g_mpeg4_ctx->ref.mb_cfg_info_list[mp4_state->hdr.mba].len = 1;

    // 1. save mb info regs to mb_cfg data
    switch (mp4_state->hdr.prediction_type) {
    case I_VOP:
        mp4_set_ivop_mbinfo(mp4_state);
        break;
    case P_VOP:
        mp4_set_pvop_mbinfo(mp4_state);
        break;
    case B_VOP:
        mp4_set_bvop_mbinfo(mp4_state);
        break;
    case S_VOP:
        mp4_set_svop_mbinfo(mp4_state);
        break;
    }

#ifdef MPEG4_DUMP_ENABLE
    char name[4][16] = {"I", "P", "B", "S"};
    fprintf(g_mpeg4_ctx->fp_mb_info, "  VOP type: %s\n", name[mp4_state->hdr.prediction_type]);

    fprintf(g_mpeg4_ctx->fp_mb_info, "  not_coded: %d, dct_type: %d, cbp: 0x%x\n", mp4_state->hdr.not_coded, mp4_state->hdr.dct_type,
            mp4_state->hdr.cbp);

    fprintf(g_mpeg4_ctx->fp_mb_info, "  q_scale: %d, q_scale_type: %d\n", mp4_state->hdr.quantizer, mp4_state->hdr.quant_type);

    if (!mp4_state->hdr.not_coded) {
        // derived_mb_type information
        fprintf(g_mpeg4_ctx->fp_mb_info, "  mb_type: %s\n", mb_type_name[mp4_state->hdr.derived_mb_type]);
    }
#endif

    // 2. config mb coeffs
    config_mb_coeffs(mp4_state);

    // 3. mb_cfg header
    write_mb_info_header(mp4_state, mb_x, mb_y);
}

void mp4_set_ivop_mbinfo(struct mp4_state *_mp4_state)
{
    struct mp4_state *mp4_state = _mp4_state;
    uint32_t *pVal = NULL;
    int len = 1;

    if ((mp4_state->hdr.derived_mb_type == INTRA) || (mp4_state->hdr.derived_mb_type == INTRA_Q)) {
        mp4_state->hdr.mb_intra = 1;
        mp4_state->hdr.mb_fwd_mc = 0;
        mp4_state->hdr.mb_bwd_mc = 0;

        change_cbp_intra(mp4_state);

        reg_mpeg_mb_type val = {0};
        val.dct_type = mp4_state->hdr.dct_type;
        val.mb_cbp = mp4_state->hdr.cbp;
        val.mpeg_intra = 1;
        pVal = (uint32_t *)&val;
        g_mpeg4_ctx->ref.cur_mb_cfg_ptr[len++] = *pVal;

        reg_quant_param q_scale = {0};
        q_scale.quant_scale_code = mp4_state->hdr.quantizer;
        q_scale.q_scale_type = mp4_state->hdr.quant_type;
        pVal = (uint32_t *)&q_scale;
        g_mpeg4_ctx->ref.cur_mb_cfg_ptr[len++] = *pVal;

        g_mpeg4_ctx->ref.mb_cfg_info_list[mp4_state->hdr.mba].len = len;
    } else {
        loge("====> stuffing mb, how to deal with it");

        mp4_state->hdr.mb_intra = 1;
        mp4_state->hdr.mb_fwd_mc = 0;
        mp4_state->hdr.mb_bwd_mc = 0;

        reg_mpeg_mb_type val = {0};
        val.dct_type = mp4_state->hdr.dct_type;
        // only have cbp for c component
        val.mb_cbp = mp4_state->hdr.cbpc;
        val.mpeg_intra = 1;
        pVal = (uint32_t *)&val;
        g_mpeg4_ctx->ref.cur_mb_cfg_ptr[len++] = *pVal;

        reg_quant_param q_scale = {0};
        q_scale.quant_scale_code = mp4_state->hdr.quantizer;
        q_scale.q_scale_type = mp4_state->hdr.quant_type;
        pVal = (uint32_t *)&q_scale;
        g_mpeg4_ctx->ref.cur_mb_cfg_ptr[len++] = *pVal;

        g_mpeg4_ctx->ref.mb_cfg_info_list[mp4_state->hdr.mba].len = len;

    }
}

void mp4_set_pvop_mbinfo(struct mp4_state *_mp4_state)
{
    reference_t *ref = &g_mpeg4_ctx->ref;
    struct mp4_state *mp4_state = _mp4_state;
    int mb_xpos = mp4_state->hdr.mba % mp4_state->mb_width;
    int mb_ypos = mp4_state->hdr.mba / mp4_state->mb_width;
    int len = 1;
    uint32_t *pVal;
    reg_mpeg_mb_type mb_type = {0};
    reg_quant_param q_scale = {0};
    reg_ref_idx ref_idx = {0};
    reg_mb_mv mb_mv0 = {0};
    reg_mb_mv mb_mv1 = {0};
    reg_mb_mv mb_mv2 = {0};
    reg_mb_mv mb_mv3 = {0};
    reg_mb_mv mb_mv0_c = {0};
    reg_mb_mv mb_mv1_c = {0};
    reg_mb_mv mb_mv2_c = {0};
    reg_mb_mv mb_mv3_c = {0};
    reg_pred_size pred_size = {0};
    reg_pred_mode pred_mode = {0};

    if (!mp4_state->hdr.not_coded) {
        if ((mp4_state->hdr.derived_mb_type == INTRA) || (mp4_state->hdr.derived_mb_type == INTRA_Q)) { // 3, 4
            mp4_state->hdr.mb_intra = 1;
            mp4_state->hdr.mb_fwd_mc = 0;
            mp4_state->hdr.mb_bwd_mc = 0;

            change_cbp_intra(mp4_state);

            mb_type.mpeg_intra = 1;
            mb_type.dct_type = mp4_state->hdr.dct_type;
            mb_type.mb_cbp = mp4_state->hdr.cbp;
            mb_type.mb_field_mc_flag = 0;

#ifdef MPEG4_DUMP_ENABLE
            fprintf(g_mpeg4_ctx->fp_mb_info, "  %s, intra\n", mb_type_name[mp4_state->hdr.derived_mb_type]);
#endif
        } else if ((mp4_state->hdr.derived_mb_type == INTER) || (mp4_state->hdr.derived_mb_type == INTER_Q)) { // 0, 1
            mp4_state->hdr.mb_intra = 0;
            mp4_state->hdr.mb_fwd_mc = 1;
            mp4_state->hdr.mb_bwd_mc = 0;

            if ((mp4_state->hdr.interlaced == 1) && (mp4_state->hdr.cbp != 0))
                mb_type.dct_type = mp4_state->hdr.dct_type;
            else
                mb_type.dct_type = 0;
            mb_type.mb_cbp = mp4_state->hdr.cbp;
            mb_type.mb_field_mc_flag = 0;

            mb_mv0.mv_x = mp4_state->mv[(mb_ypos + 1) * mp4_state->MV_stride + mb_xpos + 1][0].x;
            mb_mv0.mv_y = mp4_state->mv[(mb_ypos + 1) * mp4_state->MV_stride + mb_xpos + 1][0].y;
            mb_mv0_c.mv_x = ref->fwd_mv_c[0][0];
            mb_mv0_c.mv_y = ref->fwd_mv_c[0][1];

            pred_mode.pred_mode_blk0 = 1;
            pred_mode.pred_mode_blk1 = 1;
            pred_mode.pred_mode_blk2 = 1;
            pred_mode.pred_mode_blk3 = 1;

            if ((mp4_state->hdr.interlaced == 1) && (mp4_state->hdr.field_prediction == 1)) {
                mb_type.mb_field_mc_flag = 1;
                // field mv should div2 (right shift 1 bit) here
                mb_mv0.mv_x = mp4_state->mv[(mb_ypos + 1) * mp4_state->MV_stride + mb_xpos + 1][0].x;
                mb_mv0.mv_y = mp4_state->mv[(mb_ypos + 1) * mp4_state->MV_stride + mb_xpos + 1][0].y >> 1;
                mb_mv0_c.mv_x = ref->fwd_mv_c[0][0];
                mb_mv0_c.mv_y = ref->fwd_mv_c[0][1] >> 1;
                mb_mv2.mv_x = mp4_state->mv_field[(mb_ypos + 1) * mp4_state->MV_stride + mb_xpos + 1][0].x;
                mb_mv2.mv_y = mp4_state->mv_field[(mb_ypos + 1) * mp4_state->MV_stride + mb_xpos + 1][0].y >> 1;
                mb_mv2_c.mv_x = ref->fwd_mv_c[1][0];
                mb_mv2_c.mv_y = ref->fwd_mv_c[1][1] >> 1;

                ref_idx.ref_idx_blk0 = ref->fwd_frame_id;
                ref_idx.ref_idx_blk2 = ref->fwd_frame_id;
                ref_idx.bot_field_blk0 = mp4_state->hdr.forward_top_field_reference;
                ref_idx.bot_field_blk2 = mp4_state->hdr.forward_bottom_field_reference;

                pred_size.pred_size_blk0 = 1;
                pred_size.pred_size_blk2 = 1;
#ifdef MPEG4_DUMP_ENABLE
                fprintf(g_mpeg4_ctx->fp_mb_info, "  field_pred, 16x8 pred size\n");
                fprintf(g_mpeg4_ctx->fp_mb_info, "  ref_idx: %d\n", ref->fwd_frame_id);
                fprintf(g_mpeg4_ctx->fp_mb_info, "  mb(%d %d), mv0(%x, %x)\n", mb_xpos, mb_ypos, mb_mv0.mv_x, mb_mv0.mv_y);
                fprintf(g_mpeg4_ctx->fp_mb_info, "  mv2(%x, %x)\n", mb_mv2.mv_x, mb_mv2.mv_y);
#endif
            } else {
#ifdef MPEG4_DUMP_ENABLE
                    fprintf(g_mpeg4_ctx->fp_mb_info, "  frame_pred\n");
                    fprintf(g_mpeg4_ctx->fp_mb_info, "  ref_idx: %d\n", ref->fwd_frame_id);
                    fprintf(g_mpeg4_ctx->fp_mb_info, "  mv0(%x, %x)\n", mb_mv0.mv_x, mb_mv0.mv_y);
#endif
                mb_type.mb_field_mc_flag = 0;
                ref_idx.ref_idx_blk0 = ref->fwd_frame_id;
                pred_size.pred_size_blk0 = 0;
            }
        } else if (mp4_state->hdr.derived_mb_type == INTER4V) { // 2
            mp4_state->hdr.mb_intra = 0;
            mp4_state->hdr.mb_fwd_mc = 1;
            mp4_state->hdr.mb_bwd_mc = 0;

            pred_mode.pred_mode_blk0 = 1;
            pred_mode.pred_mode_blk1 = 1;
            pred_mode.pred_mode_blk2 = 1;
            pred_mode.pred_mode_blk3 = 1;

            pred_size.pred_size_blk0 = 3;
            pred_size.pred_size_blk1 = 3;
            pred_size.pred_size_blk2 = 3;
            pred_size.pred_size_blk3 = 3;

            mb_type.mb_field_mc_flag = 0;
            mb_type.mb_cbp = mp4_state->hdr.cbp;
            if ((mp4_state->hdr.interlaced == 1) && (mp4_state->hdr.cbp != 0))
                mb_type.dct_type = mp4_state->hdr.dct_type;
            else
                mb_type.dct_type = 0;

            ref_idx.ref_idx_blk0 = ref->fwd_frame_id;
            ref_idx.ref_idx_blk1 = ref->fwd_frame_id;
            ref_idx.ref_idx_blk2 = ref->fwd_frame_id;
            ref_idx.ref_idx_blk3 = ref->fwd_frame_id;
            // set 4 MVs
            // MV1 (Y0 block MV)
            mb_mv0.mv_x = mp4_state->mv[(mb_ypos + 1) * mp4_state->MV_stride + mb_xpos + 1][0].x;
            mb_mv0.mv_y = mp4_state->mv[(mb_ypos + 1) * mp4_state->MV_stride + mb_xpos + 1][0].y;
            mb_mv0_c.mv_x = ref->fwd_mv_c[0][0];
            mb_mv0_c.mv_y = ref->fwd_mv_c[0][1];
            // MV2 (Y1 block MV)
            mb_mv1.mv_x = (mp4_state->mv[(mb_ypos + 1) * mp4_state->MV_stride + mb_xpos + 1][1].x);
            mb_mv1.mv_y = (mp4_state->mv[(mb_ypos + 1) * mp4_state->MV_stride + mb_xpos + 1][1].y);
            mb_mv1_c.mv_x = ref->fwd_mv_c[0][0];
            mb_mv1_c.mv_y = ref->fwd_mv_c[0][1];
            // MV3 (Y2 block MV)
            mb_mv2.mv_x = (mp4_state->mv[(mb_ypos + 1) * mp4_state->MV_stride + mb_xpos + 1][2].x);
            mb_mv2.mv_y = (mp4_state->mv[(mb_ypos + 1) * mp4_state->MV_stride + mb_xpos + 1][2].y);
            mb_mv2_c.mv_x = ref->fwd_mv_c[0][0];
            mb_mv2_c.mv_y = ref->fwd_mv_c[0][1];
            // MV4 (Y3 block MV)
            mb_mv3.mv_x = (mp4_state->mv[(mb_ypos + 1) * mp4_state->MV_stride + mb_xpos + 1][3].x);
            mb_mv3.mv_y = (mp4_state->mv[(mb_ypos + 1) * mp4_state->MV_stride + mb_xpos + 1][3].y);
            mb_mv3_c.mv_x = ref->fwd_mv_c[0][0];
            mb_mv3_c.mv_y = ref->fwd_mv_c[0][1];

#ifdef MPEG4_DUMP_ENABLE
            fprintf(g_mpeg4_ctx->fp_mb_info, "  %s, fwd-pred, inter4v\n", mb_type_name[mp4_state->hdr.derived_mb_type]);
            fprintf(g_mpeg4_ctx->fp_mb_info, "  cbp: %x, dct_type: %d\n", mb_type.mb_cbp, mb_type.dct_type);
            fprintf(g_mpeg4_ctx->fp_mb_info, "  ref_idx: %d\n", ref->fwd_frame_id);
            fprintf(g_mpeg4_ctx->fp_mb_info, "  mv0(%x, %x)\n", mb_mv0.mv_x, mb_mv0.mv_y);
            fprintf(g_mpeg4_ctx->fp_mb_info, "  mv1(%x, %x)\n", mb_mv1.mv_x, mb_mv1.mv_y);
            fprintf(g_mpeg4_ctx->fp_mb_info, "  mv2(%x, %x)\n", mb_mv2.mv_x, mb_mv2.mv_y);
            fprintf(g_mpeg4_ctx->fp_mb_info, "  mv3(%x, %x)\n", mb_mv3.mv_x, mb_mv3.mv_y);
#endif
        } else if (mp4_state->hdr.derived_mb_type == 5) { // H.263 inter4v+q
            // TBD
        }
    } else { // not_coded MB in P-VOP
        mp4_state->hdr.mb_intra = 0;
        mp4_state->hdr.mb_fwd_mc = 1;
        mp4_state->hdr.mb_bwd_mc = 0;

        mb_type.mb_field_mc_flag = 0;
        mb_type.mb_cbp = 0;
        mb_type.dct_type = 0;

        pred_mode.pred_mode_blk0 = 1;
        pred_mode.pred_mode_blk1 = 1;
        pred_mode.pred_mode_blk2 = 1;
        pred_mode.pred_mode_blk3 = 1;

        ref_idx.ref_idx_blk0 = ref->fwd_frame_id;

        // MVs should be zero for not_coded MB
        mb_mv0.mv_x = 0;
        mb_mv0.mv_y = 0;
#ifdef MPEG4_DUMP_ENABLE
        fprintf(g_mpeg4_ctx->fp_mb_info, "  not_coded MB, fwd-pred, mv(0, 0)\n");
#endif
    }

    // MPEG_MB_TYPE_REG
    pVal = (uint32_t *)&mb_type;
    g_mpeg4_ctx->ref.cur_mb_cfg_ptr[len++] = *pVal;

    // MB_QSCALE
    q_scale.quant_scale_code = mp4_state->hdr.quantizer;
    q_scale.q_scale_type = mp4_state->hdr.quant_type;

    pVal = (uint32_t *)&q_scale;
    g_mpeg4_ctx->ref.cur_mb_cfg_ptr[len++] = *pVal;

    if (mp4_state->hdr.mb_fwd_mc) {
        // MB_FWD_IDX
        pVal = (uint32_t *)&ref_idx;
        g_mpeg4_ctx->ref.cur_mb_cfg_ptr[len++] = *pVal;
        // PRED_SIZE
        pVal = (uint32_t *)&pred_size;
        g_mpeg4_ctx->ref.cur_mb_cfg_ptr[len++] = *pVal;
        // PRED_MODE
        pVal = (uint32_t *)&pred_mode;
        g_mpeg4_ctx->ref.cur_mb_cfg_ptr[len++] = *pVal;
#ifdef MPEG4_DUMP_ENABLE
        fprintf(g_mpeg4_ctx->fp_mb_info, "  MB_FWD_IDX: %08x\n", *((uint32_t *)&ref_idx));
        fprintf(g_mpeg4_ctx->fp_mb_info, "  PRED_SIZE: %08x\n", *((uint32_t *)&pred_size));
        fprintf(g_mpeg4_ctx->fp_mb_info, "  PRED_MODE: %08x\n", *((uint32_t *)&pred_mode));
#endif
        // MB_REF_IDX
        for (int i = 0; i < 5; i++) {
            g_mpeg4_ctx->ref.cur_mb_cfg_ptr[len++] = i;
#ifdef MPEG4_DUMP_ENABLE
            fprintf(g_mpeg4_ctx->fp_mb_info, "  FRAME_IDX: %08x\n", i);
#endif
        }
        // Y_MV
        pVal = (uint32_t *)&mb_mv0;
        g_mpeg4_ctx->ref.cur_mb_cfg_ptr[len++] = *pVal;
        pVal = (uint32_t *)&mb_mv1;
        g_mpeg4_ctx->ref.cur_mb_cfg_ptr[len++] = *pVal;
        pVal = (uint32_t *)&mb_mv2;
        g_mpeg4_ctx->ref.cur_mb_cfg_ptr[len++] = *pVal;
        pVal = (uint32_t *)&mb_mv3;
        g_mpeg4_ctx->ref.cur_mb_cfg_ptr[len++] = *pVal;
        // C_MV
        pVal = (uint32_t *)&mb_mv0_c;
        g_mpeg4_ctx->ref.cur_mb_cfg_ptr[len++] = *pVal;
        pVal = (uint32_t *)&mb_mv1_c;
        g_mpeg4_ctx->ref.cur_mb_cfg_ptr[len++] = *pVal;
        pVal = (uint32_t *)&mb_mv2_c;
        g_mpeg4_ctx->ref.cur_mb_cfg_ptr[len++] = *pVal;
        pVal = (uint32_t *)&mb_mv3_c;
        g_mpeg4_ctx->ref.cur_mb_cfg_ptr[len++] = *pVal;
#ifdef MPEG4_DUMP_ENABLE
        fprintf(g_mpeg4_ctx->fp_mb_info, "  FWD MV0: %08x\n", *((uint32_t *)&mb_mv0));
        fprintf(g_mpeg4_ctx->fp_mb_info, "  FWD MV2: %08x\n", *((uint32_t *)&mb_mv2));
#endif
    }

    g_mpeg4_ctx->ref.mb_cfg_info_list[mp4_state->hdr.mba].len = len;
}

void mp4_set_bvop_mbinfo(struct mp4_state *_mp4_state)
{
    reference_t *ref = &g_mpeg4_ctx->ref;
    struct mp4_state *mp4_state = _mp4_state;
    int mb_xpos = mp4_state->hdr.mba % mp4_state->mb_width;
    int mb_ypos = mp4_state->hdr.mba / mp4_state->mb_width;
    reg_mpeg_mb_type mb_type = {0};
    reg_quant_param q_scale = {0};
    reg_ref_idx fwd_ref_idx = {0};
    reg_ref_idx bwd_ref_idx = {0};
    reg_mb_mv mb_fwd_mv[4] = {{0}, {0}, {0}, {0}};
    reg_mb_mv mb_bwd_mv[4] = {{0}, {0}, {0}, {0}};
    reg_mb_mv mb_fwd_mv_c[4] = {{0}, {0}, {0}, {0}};
    reg_mb_mv mb_bwd_mv_c[4] = {{0}, {0}, {0}, {0}};
    reg_pred_size pred_size = {0};
    reg_pred_mode pred_mode = {0};
    int len = 1;
    uint32_t *pVal;

    pred_mode.pred_mode_blk0 = 3;
    pred_mode.pred_mode_blk1 = 3;
    pred_mode.pred_mode_blk2 = 3;
    pred_mode.pred_mode_blk3 = 3;

    mb_type.mb_cbp = mp4_state->hdr.cbp;
    if (mp4_state->hdr.interlaced == 1)
        mb_type.dct_type = mp4_state->hdr.dct_type;
    else
        mb_type.dct_type = 0;

    mp4_b_mb_not_coded = 0;
    if ((mp4_state->codedmap[mb_ypos * mp4_state->codedmap_stride + mb_xpos] != 1) || // co_located_not_coded
        (mp4_state->hdr.old_prediction_type == S_VOP) || mp4_state->if_rm_h263) {
        if (mp4_state->hdr.modb != MODB_1) {
            switch (mp4_state->hdr.mb_type) {
            case MB_TYPE_1: // direct
                mp4_state->hdr.mb_intra = 0;
                mp4_state->hdr.mb_fwd_mc = 1;
                mp4_state->hdr.mb_bwd_mc = 1;

#ifdef MPEG4_DUMP_ENABLE
                fprintf(g_mpeg4_ctx->fp_mb_info, "  direct mode, bi-pred\n");
                fprintf(g_mpeg4_ctx->fp_mb_info, "  cbp: %x, dct_type: %d\n", mp4_state->hdr.cbp, (int)mb_type.dct_type);
                for (int i = 0; i < 8; i++) {
                    fprintf(g_mpeg4_ctx->fp_mb_info, "  direct_mv%d(%d, %d)\n", i, mp4_state->mp4_direct_mv[i][0], mp4_state->mp4_direct_mv[i][1]);
                }
#endif
                // in DIRECT mode, prediction mode ( interlaced/frame or progressive/field ) is determined
                // by prediction mode of co-located macroblock.
                if ((mp4_state->hdr.interlaced) &&
                    (mp4_state->fieldpredictedmap[(mb_ypos + 1) * mp4_state->fieldpredictedmap_stride + mb_xpos + 1] == 1)) {
                    // 16x8 block size
                    pred_size.pred_size_blk0 = 1;
                    pred_size.pred_size_blk2 = 1;

                    mb_type.mb_field_mc_flag = 1;
                    // forward top field
                    mb_fwd_mv[0].mv_x = mp4_state->mp4_direct_mv[0][0];
                    mb_fwd_mv[0].mv_y = mp4_state->mp4_direct_mv[0][1] >> 1;
                    mb_fwd_mv_c[0].mv_x = ref->fwd_mv_c[0][0];
                    mb_fwd_mv_c[0].mv_y = ref->fwd_mv_c[0][1] >> 1;

                    // forward bottom field
                    mb_fwd_mv[2].mv_x = mp4_state->mp4_direct_mv[2][0];
                    mb_fwd_mv[2].mv_y = mp4_state->mp4_direct_mv[2][1] >> 1;
                    mb_fwd_mv_c[2].mv_x = ref->fwd_mv_c[1][0];
                    mb_fwd_mv_c[2].mv_y = ref->fwd_mv_c[1][1] >> 1;
                    // backward top field
                    mb_bwd_mv[0].mv_x = mp4_state->mp4_direct_mv[1][0];
                    mb_bwd_mv[0].mv_y = mp4_state->mp4_direct_mv[1][1] >> 1;
                    mb_bwd_mv_c[0].mv_x = ref->bwd_mv_c[0][0];
                    mb_bwd_mv_c[0].mv_y = ref->bwd_mv_c[0][1] >> 1;
                    // backward bottom field
                    mb_bwd_mv[2].mv_x = mp4_state->mp4_direct_mv[3][0];
                    mb_bwd_mv[2].mv_y = mp4_state->mp4_direct_mv[3][1] >> 1;
                    mb_bwd_mv_c[2].mv_x = ref->bwd_mv_c[1][0];
                    mb_bwd_mv_c[2].mv_y = ref->bwd_mv_c[1][1] >> 1;

                    // forward ref idx
                    fwd_ref_idx.ref_idx_blk0 = ref->fwd_frame_id;
                    fwd_ref_idx.bot_field_blk0 = mp4_state->fieldrefmap[2 * mb_xpos + mb_ypos * mp4_state->fieldrefmap_stride];
                    fwd_ref_idx.ref_idx_blk2 = ref->fwd_frame_id;
                    fwd_ref_idx.bot_field_blk2 = mp4_state->fieldrefmap[2 * mb_xpos + mb_ypos * mp4_state->fieldrefmap_stride + 1];
                    // backward ref idx
                    bwd_ref_idx.ref_idx_blk0 = ref->bwd_frame_id;
                    bwd_ref_idx.bot_field_blk0 = 0;
                    bwd_ref_idx.ref_idx_blk2 = ref->bwd_frame_id;
                    bwd_ref_idx.bot_field_blk2 = 1;
                } else {
                    // 8x8 block size
                    pred_size.pred_size_blk0 = 3;
                    pred_size.pred_size_blk1 = 3;
                    pred_size.pred_size_blk2 = 3;
                    pred_size.pred_size_blk3 = 3;

                    // forward ref idx
                    fwd_ref_idx.ref_idx_blk0 = ref->fwd_frame_id;
                    fwd_ref_idx.ref_idx_blk1 = ref->fwd_frame_id;
                    fwd_ref_idx.ref_idx_blk2 = ref->fwd_frame_id;
                    fwd_ref_idx.ref_idx_blk3 = ref->fwd_frame_id;

                    // backward ref idx
                    bwd_ref_idx.ref_idx_blk0 = ref->bwd_frame_id;
                    bwd_ref_idx.ref_idx_blk1 = ref->bwd_frame_id;
                    bwd_ref_idx.ref_idx_blk2 = ref->bwd_frame_id;
                    bwd_ref_idx.ref_idx_blk3 = ref->bwd_frame_id;

                    // Y0, Y1, Y2, Y3 blocks' forward motion vectors
                    mb_fwd_mv[0].mv_x = mp4_state->mp4_direct_mv[0][0];
                    mb_fwd_mv[0].mv_y = mp4_state->mp4_direct_mv[0][1];
                    mb_fwd_mv[1].mv_x = mp4_state->mp4_direct_mv[1][0];
                    mb_fwd_mv[1].mv_y = mp4_state->mp4_direct_mv[1][1];
                    mb_fwd_mv[2].mv_x = mp4_state->mp4_direct_mv[2][0];
                    mb_fwd_mv[2].mv_y = mp4_state->mp4_direct_mv[2][1];
                    mb_fwd_mv[3].mv_x = mp4_state->mp4_direct_mv[3][0];
                    mb_fwd_mv[3].mv_y = mp4_state->mp4_direct_mv[3][1];

                    // C use the same mv
                    mb_fwd_mv_c[0].mv_x = mb_fwd_mv_c[1].mv_x = mb_fwd_mv_c[2].mv_x = mb_fwd_mv_c[3].mv_x = ref->fwd_mv_c[0][0];
                    mb_fwd_mv_c[0].mv_y = mb_fwd_mv_c[1].mv_y = mb_fwd_mv_c[2].mv_y = mb_fwd_mv_c[3].mv_y = ref->fwd_mv_c[0][1];

                    // Y0, Y1, Y2, Y3 blocks' backward motion vectors
                    mb_bwd_mv[0].mv_x = mp4_state->mp4_direct_mv[4][0];
                    mb_bwd_mv[0].mv_y = mp4_state->mp4_direct_mv[4][1];
                    mb_bwd_mv[1].mv_x = mp4_state->mp4_direct_mv[5][0];
                    mb_bwd_mv[1].mv_y = mp4_state->mp4_direct_mv[5][1];
                    mb_bwd_mv[2].mv_x = mp4_state->mp4_direct_mv[6][0];
                    mb_bwd_mv[2].mv_y = mp4_state->mp4_direct_mv[6][1];
                    mb_bwd_mv[3].mv_x = mp4_state->mp4_direct_mv[7][0];
                    mb_bwd_mv[3].mv_y = mp4_state->mp4_direct_mv[7][1];

                    mb_bwd_mv_c[0].mv_x = mb_bwd_mv_c[1].mv_x = mb_bwd_mv_c[2].mv_x = mb_bwd_mv_c[3].mv_x = ref->bwd_mv_c[0][0];
                    mb_bwd_mv_c[0].mv_y = mb_bwd_mv_c[1].mv_y = mb_bwd_mv_c[2].mv_y = mb_bwd_mv_c[3].mv_y = ref->bwd_mv_c[0][1];
                }
                break;
            case MB_TYPE_01: // interpolate mc+q
                mp4_state->hdr.mb_intra = 0;
                mp4_state->hdr.mb_fwd_mc = 1;
                mp4_state->hdr.mb_bwd_mc = 1;

                // set first forward MV
                mb_fwd_mv[0].mv_x = mp4_state->mv[(mb_ypos + 1) * mp4_state->MV_stride + mb_xpos + 1][4].x;
                mb_fwd_mv[0].mv_y = mp4_state->mv[(mb_ypos + 1) * mp4_state->MV_stride + mb_xpos + 1][4].y;
                mb_fwd_mv_c[0].mv_x = ref->fwd_mv_c[0][0];
                mb_fwd_mv_c[0].mv_y = ref->fwd_mv_c[0][1];
                // set first backward MB
                mb_bwd_mv[0].mv_x = mp4_state->mv[(mb_ypos + 1) * mp4_state->MV_stride + mb_xpos + 1][5].x;
                mb_bwd_mv[0].mv_y = mp4_state->mv[(mb_ypos + 1) * mp4_state->MV_stride + mb_xpos + 1][5].y;
                mb_bwd_mv_c[0].mv_x = ref->bwd_mv_c[0][0];
                mb_bwd_mv_c[0].mv_y = ref->bwd_mv_c[0][1];

                if ((mp4_state->hdr.interlaced == 1) && (mp4_state->hdr.field_prediction == 1)) {
                    // 16x8 field mc
                    mb_type.mb_field_mc_flag = 1;

                    mb_fwd_mv[0].mv_y = mp4_state->mv[(mb_ypos + 1) * mp4_state->MV_stride + mb_xpos + 1][4].y >> 1;
                    mb_fwd_mv_c[0].mv_y = ref->fwd_mv_c[0][1] >> 1;
                    mb_bwd_mv[0].mv_y = mp4_state->mv[(mb_ypos + 1) * mp4_state->MV_stride + mb_xpos + 1][5].y >> 1;
                    mb_bwd_mv_c[0].mv_y = ref->bwd_mv_c[0][1] >> 1;

                    // pred_size
                    pred_size.pred_size_blk0 = 1;
                    pred_size.pred_size_blk2 = 1;
                    // second forward MV
                    mb_fwd_mv[2].mv_x = mp4_state->mv_field[(mb_ypos + 1) * mp4_state->MV_stride + mb_xpos + 1][4].x;
                    mb_fwd_mv[2].mv_y = mp4_state->mv_field[(mb_ypos + 1) * mp4_state->MV_stride + mb_xpos + 1][4].y >> 1;
                    mb_fwd_mv_c[2].mv_x = ref->fwd_mv_c[1][0];
                    mb_fwd_mv_c[2].mv_y = ref->fwd_mv_c[1][1] >> 1;
                    // forward field reference
                    fwd_ref_idx.ref_idx_blk0 = ref->fwd_frame_id;
                    fwd_ref_idx.bot_field_blk0 = mp4_state->hdr.forward_top_field_reference;
                    fwd_ref_idx.ref_idx_blk2 = ref->fwd_frame_id;
                    fwd_ref_idx.bot_field_blk2 = mp4_state->hdr.forward_bottom_field_reference;

                    // second backward MV
                    mb_bwd_mv[2].mv_x = mp4_state->mv_field[(mb_ypos + 1) * mp4_state->MV_stride + mb_xpos + 1][5].x;
                    mb_bwd_mv[2].mv_y = mp4_state->mv_field[(mb_ypos + 1) * mp4_state->MV_stride + mb_xpos + 1][5].y >> 1;
                    mb_bwd_mv_c[2].mv_x = ref->bwd_mv_c[1][0];
                    mb_bwd_mv_c[2].mv_y = ref->bwd_mv_c[1][1] >> 1;
                    // backward field reference
                    bwd_ref_idx.ref_idx_blk0 = ref->bwd_frame_id;
                    bwd_ref_idx.bot_field_blk0 = mp4_state->hdr.backward_top_field_reference;
                    bwd_ref_idx.ref_idx_blk2 = ref->bwd_frame_id;
                    bwd_ref_idx.bot_field_blk2 = mp4_state->hdr.backward_bottom_field_reference;
                } else {
                    // 16x16 block size
                    fwd_ref_idx.ref_idx_blk0 = ref->fwd_frame_id;
                    bwd_ref_idx.ref_idx_blk0 = ref->bwd_frame_id;
                }

                break;
            case MB_TYPE_001: // backward mc+q
                mp4_state->hdr.mb_intra = 0;
                mp4_state->hdr.mb_fwd_mc = 0;
                mp4_state->hdr.mb_bwd_mc = 1;

                pred_mode.pred_mode_blk0 = 2;
                pred_mode.pred_mode_blk1 = 2;
                pred_mode.pred_mode_blk2 = 2;
                pred_mode.pred_mode_blk3 = 2;
                // set first backward MB
                mb_bwd_mv[0].mv_x = mp4_state->mv[(mb_ypos + 1) * mp4_state->MV_stride + mb_xpos + 1][5].x;
                mb_bwd_mv[0].mv_y = mp4_state->mv[(mb_ypos + 1) * mp4_state->MV_stride + mb_xpos + 1][5].y;
                mb_bwd_mv_c[0].mv_x = ref->bwd_mv_c[0][0];
                mb_bwd_mv_c[0].mv_y = ref->bwd_mv_c[0][1];

                if ((mp4_state->hdr.interlaced == 1) && (mp4_state->hdr.field_prediction == 1)) {
                    mb_type.mb_field_mc_flag = 1;
                    mb_bwd_mv[0].mv_y = mp4_state->mv[(mb_ypos + 1) * mp4_state->MV_stride + mb_xpos + 1][5].y >> 1;
                    mb_bwd_mv_c[0].mv_y = ref->bwd_mv_c[0][1] >> 1;
                    // pred_size
                    pred_size.pred_size_blk0 = 1;
                    pred_size.pred_size_blk2 = 1;
                    // second backward MV
                    mb_bwd_mv[2].mv_x = mp4_state->mv_field[(mb_ypos + 1) * mp4_state->MV_stride + mb_xpos + 1][5].x;
                    mb_bwd_mv[2].mv_y = mp4_state->mv_field[(mb_ypos + 1) * mp4_state->MV_stride + mb_xpos + 1][5].y >> 1;
                    mb_bwd_mv_c[2].mv_x = ref->bwd_mv_c[1][0];
                    mb_bwd_mv_c[2].mv_y = ref->bwd_mv_c[1][1] >> 1;
                    // backward field reference
                    bwd_ref_idx.ref_idx_blk0 = ref->bwd_frame_id;
                    bwd_ref_idx.bot_field_blk0 = mp4_state->hdr.backward_top_field_reference;
                    bwd_ref_idx.ref_idx_blk2 = ref->bwd_frame_id;
                    bwd_ref_idx.bot_field_blk2 = mp4_state->hdr.backward_bottom_field_reference;
                } else {
                    bwd_ref_idx.ref_idx_blk0 = ref->bwd_frame_id;
                }
                break;
            case MB_TYPE_0001: // forward mc+q
                mp4_state->hdr.mb_intra = 0;
                mp4_state->hdr.mb_fwd_mc = 1;
                mp4_state->hdr.mb_bwd_mc = 0;

                pred_mode.pred_mode_blk0 = 1;
                pred_mode.pred_mode_blk1 = 1;
                pred_mode.pred_mode_blk2 = 1;
                pred_mode.pred_mode_blk3 = 1;

                // set first forward MV
                mb_fwd_mv[0].mv_x = mp4_state->mv[(mb_ypos + 1) * mp4_state->MV_stride + mb_xpos + 1][4].x;
                mb_fwd_mv[0].mv_y = mp4_state->mv[(mb_ypos + 1) * mp4_state->MV_stride + mb_xpos + 1][4].y;
                mb_fwd_mv_c[0].mv_x = ref->fwd_mv_c[0][0];
                mb_fwd_mv_c[0].mv_y = ref->fwd_mv_c[0][1];

                if ((mp4_state->hdr.interlaced == 1) && (mp4_state->hdr.field_prediction == 1)) {
                    mb_type.mb_field_mc_flag = 1;
                    pred_size.pred_size_blk0 = 1;
                    pred_size.pred_size_blk2 = 1;
                    // field mv should div2 here
                    mb_fwd_mv[0].mv_y = mp4_state->mv[(mb_ypos + 1) * mp4_state->MV_stride + mb_xpos + 1][4].y >> 1;
                    mb_fwd_mv_c[0].mv_y = ref->fwd_mv_c[0][1] >> 1;
                    // second forward MV
                    mb_fwd_mv[2].mv_x = mp4_state->mv_field[(mb_ypos + 1) * mp4_state->MV_stride + mb_xpos + 1][4].x;
                    mb_fwd_mv[2].mv_y = mp4_state->mv_field[(mb_ypos + 1) * mp4_state->MV_stride + mb_xpos + 1][4].y >> 1;
                    mb_fwd_mv_c[2].mv_x = ref->fwd_mv_c[1][0];
                    mb_fwd_mv_c[2].mv_y = ref->fwd_mv_c[1][1] >> 1;

                    // forward field reference
                    fwd_ref_idx.ref_idx_blk0 = ref->fwd_frame_id;
                    fwd_ref_idx.bot_field_blk0 = mp4_state->hdr.forward_top_field_reference;
                    fwd_ref_idx.ref_idx_blk2 = ref->fwd_frame_id;
                    fwd_ref_idx.bot_field_blk2 = mp4_state->hdr.forward_bottom_field_reference;
                } else {
                    fwd_ref_idx.ref_idx_blk0 = ref->fwd_frame_id;
                }

                break;
            case MB_TYPE_00001: // intra
                mp4_state->hdr.mb_intra = 1;
                mp4_state->hdr.mb_fwd_mc = 0;
                mp4_state->hdr.mb_bwd_mc = 0;

                mb_type.mpeg_intra = 1;
                mb_type.dct_type = mp4_state->hdr.dct_type;
                mb_type.mb_cbp = mp4_state->hdr.cbp;
                break;
            default:
                printf("error mb_type in B-VOP!\n");
            }
        } else { // direct mode when modb is 1
            mp4_state->hdr.mb_intra = 0;
            mp4_state->hdr.mb_fwd_mc = 1;
            mp4_state->hdr.mb_bwd_mc = 1;

            mb_type.mb_cbp = 0;
            mb_type.dct_type = 0;

            if ((mp4_state->hdr.interlaced) &&
                (mp4_state->fieldpredictedmap[(mb_ypos + 1) * mp4_state->fieldpredictedmap_stride + mb_xpos + 1] == 1)) {
                // 16x8
                mb_type.mb_field_mc_flag = 1;
                pred_size.pred_size_blk0 = 1;
                pred_size.pred_size_blk2 = 1;
                // forward top field reference
                fwd_ref_idx.ref_idx_blk0 = ref->fwd_frame_id;
                fwd_ref_idx.bot_field_blk0 = mp4_state->fieldrefmap[2 * mb_xpos + mb_ypos * mp4_state->fieldrefmap_stride];
                // forward bottom field reference
                fwd_ref_idx.ref_idx_blk2 = ref->fwd_frame_id;
                fwd_ref_idx.bot_field_blk2 = mp4_state->fieldrefmap[2 * mb_xpos + mb_ypos * mp4_state->fieldrefmap_stride + 1];
                // backward top field reference
                bwd_ref_idx.ref_idx_blk0 = ref->bwd_frame_id;
                bwd_ref_idx.bot_field_blk0 = 0;
                // backward bottom field reference
                bwd_ref_idx.ref_idx_blk2 = ref->bwd_frame_id;
                bwd_ref_idx.bot_field_blk2 = 1;

                // forward top field
                mb_fwd_mv[0].mv_x = mp4_state->mp4_direct_mv[0][0];
                mb_fwd_mv[0].mv_y = mp4_state->mp4_direct_mv[0][1] >> 1;
                mb_fwd_mv_c[0].mv_x = ref->fwd_mv_c[0][0];
                mb_fwd_mv_c[0].mv_y = ref->fwd_mv_c[0][1] >> 1;
                // forward bottom field
                mb_fwd_mv[2].mv_x = mp4_state->mp4_direct_mv[2][0];
                mb_fwd_mv[2].mv_y = mp4_state->mp4_direct_mv[2][1] >> 1;
                mb_fwd_mv_c[2].mv_x = ref->fwd_mv_c[1][0];
                mb_fwd_mv_c[2].mv_y = ref->fwd_mv_c[1][1] >> 1;
                // backward top field
                mb_bwd_mv[0].mv_x = mp4_state->mp4_direct_mv[1][0];
                mb_bwd_mv[0].mv_y = mp4_state->mp4_direct_mv[1][1] >> 1;
                mb_bwd_mv_c[0].mv_x = ref->bwd_mv_c[0][0];
                mb_bwd_mv_c[0].mv_y = ref->bwd_mv_c[0][1] >> 1;
                // backward bottom field
                mb_bwd_mv[2].mv_x = mp4_state->mp4_direct_mv[3][0];
                mb_bwd_mv[2].mv_y = mp4_state->mp4_direct_mv[3][1] >> 1;
                mb_bwd_mv_c[2].mv_x = ref->bwd_mv_c[1][0];
                mb_bwd_mv_c[2].mv_y = ref->bwd_mv_c[1][1] >> 1;
            } else {
                pred_size.pred_size_blk0 = 3;
                pred_size.pred_size_blk1 = 3;
                pred_size.pred_size_blk2 = 3;
                pred_size.pred_size_blk3 = 3;

                // forward ref idx
                fwd_ref_idx.ref_idx_blk0 = ref->fwd_frame_id;
                fwd_ref_idx.ref_idx_blk1 = ref->fwd_frame_id;
                fwd_ref_idx.ref_idx_blk2 = ref->fwd_frame_id;
                fwd_ref_idx.ref_idx_blk3 = ref->fwd_frame_id;

                // backward ref idx
                bwd_ref_idx.ref_idx_blk0 = ref->bwd_frame_id;
                bwd_ref_idx.ref_idx_blk1 = ref->bwd_frame_id;
                bwd_ref_idx.ref_idx_blk2 = ref->bwd_frame_id;
                bwd_ref_idx.ref_idx_blk3 = ref->bwd_frame_id;

                // Y0, Y1, Y2, Y3 blocks' forward motion vectors
                mb_fwd_mv[0].mv_x = mp4_state->mp4_direct_mv[0][0];
                mb_fwd_mv[0].mv_y = mp4_state->mp4_direct_mv[0][1];
                mb_fwd_mv_c[0].mv_x = ref->fwd_mv_c[0][0];
                mb_fwd_mv_c[0].mv_y = ref->fwd_mv_c[0][1];

                mb_fwd_mv[1].mv_x = mp4_state->mp4_direct_mv[1][0];
                mb_fwd_mv[1].mv_y = mp4_state->mp4_direct_mv[1][1];
                mb_fwd_mv_c[1].mv_x = ref->fwd_mv_c[0][0];
                mb_fwd_mv_c[1].mv_y = ref->fwd_mv_c[0][1];

                mb_fwd_mv[2].mv_x = mp4_state->mp4_direct_mv[2][0];
                mb_fwd_mv[2].mv_y = mp4_state->mp4_direct_mv[2][1];
                mb_fwd_mv_c[2].mv_x = ref->fwd_mv_c[0][0];
                mb_fwd_mv_c[2].mv_y = ref->fwd_mv_c[0][1];

                mb_fwd_mv[3].mv_x = mp4_state->mp4_direct_mv[3][0];
                mb_fwd_mv[3].mv_y = mp4_state->mp4_direct_mv[3][1];
                mb_fwd_mv_c[3].mv_x = ref->fwd_mv_c[0][0];
                mb_fwd_mv_c[3].mv_y = ref->fwd_mv_c[0][1];

                // Y0, Y1, Y2, Y3 blocks' backward motion vectors
                mb_bwd_mv[0].mv_x = mp4_state->mp4_direct_mv[4][0];
                mb_bwd_mv[0].mv_y = mp4_state->mp4_direct_mv[4][1];
                mb_bwd_mv_c[0].mv_x = ref->bwd_mv_c[0][0];
                mb_bwd_mv_c[0].mv_y = ref->bwd_mv_c[0][1];

                mb_bwd_mv[1].mv_x = mp4_state->mp4_direct_mv[5][0];
                mb_bwd_mv[1].mv_y = mp4_state->mp4_direct_mv[5][1];
                mb_bwd_mv_c[1].mv_x = ref->bwd_mv_c[0][0];
                mb_bwd_mv_c[1].mv_y = ref->bwd_mv_c[0][1];

                mb_bwd_mv[2].mv_x = mp4_state->mp4_direct_mv[6][0];
                mb_bwd_mv[2].mv_y = mp4_state->mp4_direct_mv[6][1];
                mb_bwd_mv_c[2].mv_x = ref->bwd_mv_c[0][0];
                mb_bwd_mv_c[2].mv_y = ref->bwd_mv_c[0][1];

                mb_bwd_mv[3].mv_x = mp4_state->mp4_direct_mv[7][0];
                mb_bwd_mv[3].mv_y = mp4_state->mp4_direct_mv[7][1];
                mb_bwd_mv_c[3].mv_x = ref->bwd_mv_c[0][0];
                mb_bwd_mv_c[3].mv_y = ref->bwd_mv_c[0][1];
            }
        }
    } else {    // not_coded MB in B-VOP
        mp4_state->hdr.mb_intra = 0;
        mp4_state->hdr.mb_fwd_mc = 1;
        mp4_state->hdr.mb_bwd_mc = 0;

        // set one flag about not_coded MB
        mp4_b_mb_not_coded = 1;

        mb_type.mb_field_mc_flag = 0;
        mb_type.mb_cbp = 0;
        mb_type.dct_type = 0;

        pred_mode.pred_mode_blk0 = 1;
        pred_mode.pred_mode_blk1 = 1;
        pred_mode.pred_mode_blk2 = 1;
        pred_mode.pred_mode_blk3 = 1;

        fwd_ref_idx.ref_idx_blk0 = ref->fwd_frame_id;

        // MVs should be zero for not_coded MB
        mb_fwd_mv[0].mv_x = 0;
        mb_fwd_mv[0].mv_y = 0;
#ifdef MPEG4_DUMP_ENABLE
        fprintf(g_mpeg4_ctx->fp_mb_info, "  not_coded MB, fwd-pred, mv(0, 0)\n");
#endif
    }

    // MPEG_MB_TYPE_REG
    pVal = (uint32_t *)&mb_type;
    g_mpeg4_ctx->ref.cur_mb_cfg_ptr[len++] = *pVal;

    // MB_QSCALE
    q_scale.quant_scale_code = mp4_state->hdr.quantizer;
    q_scale.q_scale_type = mp4_state->hdr.quant_type;
    pVal = (uint32_t *)&q_scale;
    g_mpeg4_ctx->ref.cur_mb_cfg_ptr[len++] = *pVal;

    if (mp4_state->hdr.mb_intra) {

    } else if (mp4_state->hdr.mb_fwd_mc && !mp4_state->hdr.mb_bwd_mc) {
        // MB_FWD_IDX
        pVal = (uint32_t *)&fwd_ref_idx;
        g_mpeg4_ctx->ref.cur_mb_cfg_ptr[len++] = *pVal;
        // PRED_SIZE
        pVal = (uint32_t *)&pred_size;
        g_mpeg4_ctx->ref.cur_mb_cfg_ptr[len++] = *pVal;
        // PRED_MODE
        pVal = (uint32_t *)&pred_mode;
        g_mpeg4_ctx->ref.cur_mb_cfg_ptr[len++] = *pVal;
#ifdef MPEG4_DUMP_ENABLE
        fprintf(g_mpeg4_ctx->fp_mb_info, "  MB_FWD_IDX: %08x\n", *((uint32_t *)&fwd_ref_idx));
        fprintf(g_mpeg4_ctx->fp_mb_info, "  PRED_SIZE: %08x\n", *((uint32_t *)&pred_size));
        fprintf(g_mpeg4_ctx->fp_mb_info, "  PRED_MODE: %08x\n", *((uint32_t *)&pred_mode));
#endif
        // MB_REF_IDX
        for (int i = 0; i < 5; i++) {
            g_mpeg4_ctx->ref.cur_mb_cfg_ptr[len++] = i;
#ifdef MPEG4_DUMP_ENABLE
            fprintf(g_mpeg4_ctx->fp_mb_info, "  FRAME_IDX: %08x\n", i);
#endif
        }
        // Y_MV
        for (int i = 0; i < 4; i++) {
            pVal = (uint32_t *)&mb_fwd_mv[i];
            g_mpeg4_ctx->ref.cur_mb_cfg_ptr[len++] = *pVal;
#ifdef MPEG4_DUMP_ENABLE
            fprintf(g_mpeg4_ctx->fp_mb_info, "  FWD MV%d: %08x\n", i, *((uint32_t *)&mb_fwd_mv[i]));
#endif
        }
        // C_MV
        for (int i = 0; i < 4; i++) {
            pVal = (uint32_t *)&mb_fwd_mv_c[i];
            g_mpeg4_ctx->ref.cur_mb_cfg_ptr[len++] = *pVal;
#ifdef MPEG4_DUMP_ENABLE
            fprintf(g_mpeg4_ctx->fp_mb_info, "  FWD C MV%d: %08x\n", i, *((uint32_t *)&mb_fwd_mv[i]));
#endif
        }
    } else {
        // MB_FWD_IDX
        pVal = (uint32_t *)&fwd_ref_idx;
        g_mpeg4_ctx->ref.cur_mb_cfg_ptr[len++] = *pVal;
        // MB_BWD_IDX
        pVal = (uint32_t *)&bwd_ref_idx;
        g_mpeg4_ctx->ref.cur_mb_cfg_ptr[len++] = *pVal;
        // PRED_SIZE
        pVal = (uint32_t *)&pred_size;
        g_mpeg4_ctx->ref.cur_mb_cfg_ptr[len++] = *pVal;
        // PRED_MODE
        pVal = (uint32_t *)&pred_mode;
        g_mpeg4_ctx->ref.cur_mb_cfg_ptr[len++] = *pVal;
#ifdef MPEG4_DUMP_ENABLE
        fprintf(g_mpeg4_ctx->fp_mb_info, "  MB_FWD_IDX: %08x\n", *((uint32_t *)&fwd_ref_idx));
        fprintf(g_mpeg4_ctx->fp_mb_info, "  MB_BWD_IDX: %08x\n", *((uint32_t *)&bwd_ref_idx));
        fprintf(g_mpeg4_ctx->fp_mb_info, "  PRED_SIZE: %08x\n", *((uint32_t *)&pred_size));
        fprintf(g_mpeg4_ctx->fp_mb_info, "  PRED_MODE: %08x\n", *((uint32_t *)&pred_mode));
#endif
        // MB_REF_IDX
        for (int i = 0; i < 5; i++) {
            g_mpeg4_ctx->ref.cur_mb_cfg_ptr[len++] = i;
        }
        // Y_MV
        for (int i = 0; i < 4; i++) {
            pVal = (uint32_t *)&mb_fwd_mv[i];
            g_mpeg4_ctx->ref.cur_mb_cfg_ptr[len++] = *pVal;
#ifdef MPEG4_DUMP_ENABLE
            fprintf(g_mpeg4_ctx->fp_mb_info, "  FWD MV%d: %08x\n", i, *((uint32_t *)&mb_fwd_mv[i]));
#endif
        }
        // C_MV
        for (int i = 0; i < 4; i++) {
            pVal = (uint32_t *)&mb_fwd_mv_c[i];
            g_mpeg4_ctx->ref.cur_mb_cfg_ptr[len++] = *pVal;
#ifdef MPEG4_DUMP_ENABLE
            fprintf(g_mpeg4_ctx->fp_mb_info, "  FWD C MV%d: %08x\n", i, *((uint32_t *)&mb_fwd_mv_c[i]));
#endif
        }

        // BWD
        for (int i = 0; i < 4; i++) {
            pVal = (uint32_t *)&mb_bwd_mv[i];
            g_mpeg4_ctx->ref.cur_mb_cfg_ptr[len++] = *pVal;
#ifdef MPEG4_DUMP_ENABLE
            fprintf(g_mpeg4_ctx->fp_mb_info, "  BWD MV%d: %08x\n", i, *((uint32_t *)&mb_bwd_mv[i]));
#endif
        }
        // C_MV
        for (int i = 0; i < 4; i++) {
            pVal = (uint32_t *)&mb_bwd_mv_c[i];
            g_mpeg4_ctx->ref.cur_mb_cfg_ptr[len++] = *pVal;
#ifdef MPEG4_DUMP_ENABLE
            fprintf(g_mpeg4_ctx->fp_mb_info, "  BWD C MV%d: %08x\n", i, *((uint32_t *)&mb_bwd_mv_c[i]));
#endif
        }
    }

    g_mpeg4_ctx->ref.mb_cfg_info_list[mp4_state->hdr.mba].len = len;
}

void mp4_set_svop_mbinfo(struct mp4_state *_mp4_state)
{
    reference_t *ref = &g_mpeg4_ctx->ref;
    struct mp4_state *mp4_state = _mp4_state;
    int mb_xpos = mp4_state->hdr.mba % mp4_state->mb_width;
    int mb_ypos = mp4_state->hdr.mba / mp4_state->mb_width;
    reg_mpeg_mb_type mb_type = {0};
    reg_quant_param q_scale = {0};
    reg_ref_idx fwd_ref_idx = {0};
    reg_mb_mv mb_fwd_mv[4] = {{0}, {0}, {0}, {0}};
    reg_mb_mv mb_fwd_mv_c[4] = {{0}, {0}, {0}, {0}};
    reg_pred_size pred_size = {0};
    reg_pred_mode pred_mode = {0};
    int len = 1;
    uint32_t *pVal;

    pred_mode.pred_mode_blk0 = 1;
    pred_mode.pred_mode_blk1 = 1;
    pred_mode.pred_mode_blk2 = 1;
    pred_mode.pred_mode_blk3 = 1;

    mb_type.mb_cbp = mp4_state->hdr.cbp;
    if ((mp4_state->hdr.interlaced == 1) && (mp4_state->hdr.cbp != 0))
        mb_type.dct_type = mp4_state->hdr.dct_type;
    else
        mb_type.dct_type = 0;

    if (!mp4_state->hdr.not_coded) {
        if ((mp4_state->hdr.derived_mb_type == INTRA) || (mp4_state->hdr.derived_mb_type == INTRA_Q)) { // 3, 4
            mp4_state->hdr.mb_intra = 1;
            mp4_state->hdr.mb_fwd_mc = 0;
            mp4_state->hdr.mb_bwd_mc = 0;

            change_cbp_intra(mp4_state);

            mb_type.mpeg_intra = 1;
            mb_type.dct_type = mp4_state->hdr.dct_type;
            mb_type.mb_cbp = mp4_state->hdr.cbp;
            mb_type.mb_field_mc_flag = 0;

#ifdef MPEG4_DUMP_ENABLE
            fprintf(g_mpeg4_ctx->fp_mb_info, "  %s, intra\n", mb_type_name[mp4_state->hdr.derived_mb_type]);
#endif
        } else if ((mp4_state->hdr.derived_mb_type == INTER) || (mp4_state->hdr.derived_mb_type == INTER_Q)) { // 0, 1
            if (mp4_state->hdr.mcsel == 1) {
                mp4_state->hdr.mb_intra = 0;
                mp4_state->hdr.mb_fwd_mc = 1;
                mp4_state->hdr.mb_bwd_mc = 0;
                loge("GMC, fwd_ifdx: %d", ref->fwd_frame_id);

                mb_type.mpeg_gmc = 1;
                // GMC MVs
                mb_fwd_mv[0].mv_x = mp4_state->gmc_lum_mv_x;
                mb_fwd_mv[0].mv_y = mp4_state->gmc_lum_mv_y;
                mb_fwd_mv_c[0].mv_x = mp4_state->gmc_chrom_mv_x;
                mb_fwd_mv_c[0].mv_y = mp4_state->gmc_chrom_mv_y;
                fwd_ref_idx.ref_idx_blk0 = ref->fwd_frame_id;
            } else {
                mp4_state->hdr.mb_intra = 0;
                mp4_state->hdr.mb_fwd_mc = 1;
                mp4_state->hdr.mb_bwd_mc = 0;

                // set MVs and prediction type
                // set first forward MV
                mb_fwd_mv[0].mv_x = mp4_state->mv[(mb_ypos + 1) * mp4_state->MV_stride + mb_xpos + 1][0].x;
                mb_fwd_mv[0].mv_y = mp4_state->mv[(mb_ypos + 1) * mp4_state->MV_stride + mb_xpos + 1][0].y;
                mb_fwd_mv_c[0].mv_x = ref->fwd_mv_c[0][0];
                mb_fwd_mv_c[0].mv_y = ref->fwd_mv_c[0][1];
                fwd_ref_idx.ref_idx_blk0 = ref->fwd_frame_id;

                if ((mp4_state->hdr.interlaced == 1) && (mp4_state->hdr.field_prediction == 1)) {
                    // field prediction
                    mb_type.mb_field_mc_flag = 1;
                    // pred_size
                    pred_size.pred_size_blk0 = 1;
                    pred_size.pred_size_blk2 = 1;

                    mb_fwd_mv[2].mv_x =
                        (unsigned long)((mp4_state->mv_field[(mb_ypos + 1) * mp4_state->MV_stride + mb_xpos + 1][0].x) & 0x1fff);
                    mb_fwd_mv[2].mv_y =
                        (unsigned long)((mp4_state->mv_field[(mb_ypos + 1) * mp4_state->MV_stride + mb_xpos + 1][0].y) & 0x1fff);
                    mb_fwd_mv_c[2].mv_x = ref->fwd_mv_c[0][0];
                    mb_fwd_mv_c[2].mv_y = ref->fwd_mv_c[0][1];
                    // forward field reference
                    fwd_ref_idx.bot_field_blk0 = mp4_state->hdr.forward_top_field_reference;
                    fwd_ref_idx.bot_field_blk2 = mp4_state->hdr.forward_bottom_field_reference;
                    fwd_ref_idx.ref_idx_blk2 = ref->fwd_frame_id;
                }
            }
        } else if (mp4_state->hdr.derived_mb_type == INTER4V) { // 2
            mp4_state->hdr.mb_intra = 0;
            mp4_state->hdr.mb_fwd_mc = 1;
            mp4_state->hdr.mb_bwd_mc = 0;

            // pred_size
            pred_size.pred_size_blk0 = 3;
            pred_size.pred_size_blk1 = 3;
            pred_size.pred_size_blk2 = 3;
            pred_size.pred_size_blk3 = 3;

            // ref_idx
            fwd_ref_idx.ref_idx_blk0 = ref->fwd_frame_id;
            fwd_ref_idx.ref_idx_blk1 = ref->fwd_frame_id;
            fwd_ref_idx.ref_idx_blk2 = ref->fwd_frame_id;
            fwd_ref_idx.ref_idx_blk3 = ref->fwd_frame_id;

            // set 4 MVs
            for (int i = 0; i < 4; i++) {
                mb_fwd_mv[i].mv_x = mp4_state->mv[(mb_ypos + 1) * mp4_state->MV_stride + mb_xpos + 1][i].x;
                mb_fwd_mv[i].mv_y = mp4_state->mv[(mb_ypos + 1) * mp4_state->MV_stride + mb_xpos + 1][i].y;
                mb_fwd_mv_c[i].mv_x = ref->fwd_mv_c[0][0];
                mb_fwd_mv_c[i].mv_y = ref->fwd_mv_c[0][1];
            }
        } else if (mp4_state->hdr.derived_mb_type == 5) { // H.263 inter4v+q
            // TBD
        }
    } else {
        // not_coded MB in S-VOP
        logi("not_coded MB in S-VOP, use GMC");
        mp4_state->hdr.mb_intra = 0;
        mp4_state->hdr.mb_fwd_mc = 1;
        mp4_state->hdr.mb_bwd_mc = 0;

        mb_type.mpeg_gmc = 1;
        mb_type.mpeg_skip = 0;
        mb_type.mb_cbp = 0;
        mb_type.dct_type = 0;
        // GMC MVs
        mb_fwd_mv[0].mv_x = mp4_state->gmc_lum_mv_x;
        mb_fwd_mv[0].mv_y = mp4_state->gmc_lum_mv_y;
        mb_fwd_mv_c[0].mv_x = mp4_state->gmc_chrom_mv_x;
        mb_fwd_mv_c[0].mv_y = mp4_state->gmc_chrom_mv_y;
        fwd_ref_idx.ref_idx_blk0 = ref->fwd_frame_id;
    }

    // MPEG_MB_TYPE_REG
    pVal = (uint32_t *)&mb_type;
    g_mpeg4_ctx->ref.cur_mb_cfg_ptr[len++] = *pVal;

    // MB_QSCALE
    q_scale.quant_scale_code = mp4_state->hdr.quantizer;
    q_scale.q_scale_type = mp4_state->hdr.quant_type;

    pVal = (uint32_t *)&q_scale;
    g_mpeg4_ctx->ref.cur_mb_cfg_ptr[len++] = *pVal;

    if (mp4_state->hdr.mb_fwd_mc) {
        // MB_FWD_IDX
        pVal = (uint32_t *)&fwd_ref_idx;
        g_mpeg4_ctx->ref.cur_mb_cfg_ptr[len++] = *pVal;
        // PRED_SIZE
        pVal = (uint32_t *)&pred_size;
        g_mpeg4_ctx->ref.cur_mb_cfg_ptr[len++] = *pVal;
        // PRED_MODE
        pVal = (uint32_t *)&pred_mode;
        g_mpeg4_ctx->ref.cur_mb_cfg_ptr[len++] = *pVal;
#ifdef MPEG4_DUMP_ENABLE
            fprintf(g_mpeg4_ctx->fp_mb_info, "  MB_FWD_IDX: %08x\n", *((uint32_t *)&fwd_ref_idx));
            fprintf(g_mpeg4_ctx->fp_mb_info, "  PRED_SIZE: %08x\n", *((uint32_t *)&pred_size));
            fprintf(g_mpeg4_ctx->fp_mb_info, "  PRED_MODE: %08x\n", *((uint32_t *)&pred_mode));
#endif
        // MB_REF_IDX
        for (int i = 0; i < 5; i++) {
            g_mpeg4_ctx->ref.cur_mb_cfg_ptr[len++] = i;
#ifdef MPEG4_DUMP_ENABLE
            fprintf(g_mpeg4_ctx->fp_mb_info, "  FRAME_IDX: %08x\n", i);
#endif
        }
        // Y_MV
        for (int i = 0; i < 4; i++) {
            pVal = (uint32_t *)&mb_fwd_mv[i];
            g_mpeg4_ctx->ref.cur_mb_cfg_ptr[len++] = *pVal;
        }
        // C_MV
        for (int i = 0; i < 4; i++) {
            pVal = (uint32_t *)&mb_fwd_mv_c[i];
            g_mpeg4_ctx->ref.cur_mb_cfg_ptr[len++] = *pVal;
        }
#ifdef MPEG4_DUMP_ENABLE
            fprintf(g_mpeg4_ctx->fp_mb_info, "  FWD MV0: %08x\n", *((uint32_t *)&mb_fwd_mv[0]));
            fprintf(g_mpeg4_ctx->fp_mb_info, "  FWD MV0: %08x\n", *((uint32_t *)&mb_fwd_mv[0]));
#endif
    }

    g_mpeg4_ctx->ref.mb_cfg_info_list[mp4_state->hdr.mba].len = len;
}

// set packet information when resync_mark is found
void mp4_set_packet_info(struct mp4_state *_mp4_state)
{

}

// set GOB information when resync_mark is found
void mp4_set_gob_info(struct mp4_state *_mp4_state)
{

}


void mp4_backup_mbh_info(struct mp4_state *_mp4_state)
{

}

void mp4_update_info(struct mp4_state *_mp4_state)
{

}

#define SKIP_TYPE_NONE 0
#define SKIP_TYPE_MPEG 1
#define SKIP_TYPE_ROW  2
#define SKIP_TYPE_COL  3

void parse_mb_skip(mp4_stream_t *ld, struct mp4_state *s)
{
    int mb_x, mb_y;
    int *mb_type = s->hdr.mb_skip;

    s->hdr.skip_type = getbits(ld, 2);
    memset(mb_type, 0, s->mb_width * s->mb_height * sizeof(int));
    switch (s->hdr.skip_type) {
    case SKIP_TYPE_NONE:
        break;
    case SKIP_TYPE_MPEG:
        for (mb_y = 0; mb_y < s->mb_height; mb_y++) {
            for (mb_x = 0; mb_x < s->mb_width; mb_x++) {
                mb_type[mb_y * s->mb_width + mb_x] = (getbits1(ld) ? 1 : 0);
            }
        }
        break;
    case SKIP_TYPE_ROW:
        for (mb_y = 0; mb_y < s->mb_height; mb_y++) {
            if (getbits1(ld)) {
                for (mb_x = 0; mb_x < s->mb_width; mb_x++) {
                    mb_type[mb_y * s->mb_width + mb_x] = 1;
                }
            } else {
                for (mb_x = 0; mb_x < s->mb_width; mb_x++) {
                    mb_type[mb_y * s->mb_width + mb_x] = (getbits1(ld) ? 1 : 0);
                }
            }
        }
        break;
    case SKIP_TYPE_COL:
        for (mb_x = 0; mb_x < s->mb_width; mb_x++) {
            if (getbits1(ld)) {
                for (mb_y = 0; mb_y < s->mb_height; mb_y++) {
                    mb_type[mb_y * s->mb_width + mb_x] = 1;
                }
            } else {
                for (mb_y = 0; mb_y < s->mb_height; mb_y++) {
                    mb_type[mb_y * s->mb_width + mb_x] = (getbits1(ld) ? 1 : 0);
                }
            }
        }
        break;
    }
}

int wmv2_decode_picture_header(mp4_stream_t *ld, struct mp4_state *s)
{
    s->hdr.prediction_type = s->hdr.picture_coding_type = getbits1(ld);
    if (s->hdr.prediction_type == I_VOP) {
        getbits(ld, 7);
        s->hdr.rounding_type = 1;
    }
    s->hdr.vop_quant = getbits(ld, 5);
    s->hdr.quantizer = s->hdr.vop_quant;

    if (s->hdr.prediction_type == I_VOP) {
        if (s->j_type_bit)
            s->hdr.j_type = getbits1(ld);
        else
            s->hdr.j_type = 0; // FIXME check

        if (!s->hdr.j_type) {
            if (s->per_mb_rl_bit)
                s->hdr.per_mb_rl_table = getbits1(ld);
            else
                s->hdr.per_mb_rl_table = 0;

            if (!s->hdr.per_mb_rl_table) {
                s->hdr.rl_chroma_table_index = getbits1(ld);
                if (s->hdr.rl_chroma_table_index)
                    s->hdr.rl_chroma_table_index += getbits1(ld);
                s->hdr.rl_table_index = getbits1(ld);
                if (s->hdr.rl_table_index)
                    s->hdr.rl_table_index += getbits1(ld);
            }

            s->hdr.dc_table_index = getbits1(ld);
        }
        s->hdr.inter_intra_pred = 0;
        s->hdr.rounding_type = 1;
    } else {
        int cbp_index;
        s->hdr.j_type = 0;

        parse_mb_skip(ld, s);
        cbp_index = getbits1(ld);
        if (cbp_index)
            cbp_index += getbits1(ld);
        if (s->hdr.vop_quant <= 10) {
            int map[3] = {0, 2, 1};
            s->hdr.cbp_table_index = map[cbp_index];
        } else if (s->hdr.vop_quant <= 20) {
            int map[3] = {1, 0, 2};
            s->hdr.cbp_table_index = map[cbp_index];
        } else {
            int map[3] = {2, 1, 0};
            s->hdr.cbp_table_index = map[cbp_index];
        }

        if (s->mspel_bit)
            s->hdr.mspel = getbits1(ld);
        else
            s->hdr.mspel = 0; // FIXME check

        if (s->abt_flag) {
            s->hdr.per_mb_abt = getbits1(ld) ^ 1;
            if (!s->hdr.per_mb_abt) {
                s->hdr.abt_type = getbits1(ld);
                if (s->hdr.abt_type)
                    s->hdr.abt_type += getbits1(ld);
            }
        }

        if (s->per_mb_rl_bit)
            s->hdr.per_mb_rl_table = getbits1(ld);
        else
            s->hdr.per_mb_rl_table = 0;

        if (!s->hdr.per_mb_rl_table) {
            s->hdr.rl_table_index = getbits1(ld);
            if (s->hdr.rl_table_index)
                s->hdr.rl_table_index += getbits1(ld);
            s->hdr.rl_chroma_table_index = s->hdr.rl_table_index;
        }

        s->hdr.dc_table_index = getbits1(ld);
        s->hdr.mv_table_index = getbits1(ld);

        s->hdr.inter_intra_pred = 0; //(s->width*s->height < 320*240 && s->bit_rate<=II_BITRATE);
        s->hdr.rounding_type ^= 1;
    }
    if (s->hdr.dc_table_index) {
        s->hdr.dc_lum_table = &get_dc_311_1_lum;
        s->hdr.dc_chrom_table = &get_dc_311_1_chrom;
    } else {
        s->hdr.dc_lum_table = &get_dc_311_0_lum;
        s->hdr.dc_chrom_table = &get_dc_311_0_chrom;
    }
    if (s->hdr.mv_table_index)
        s->hdr.mv_table = &get_mv_data_311_1;
    else
        s->hdr.mv_table = &get_mv_data_311_0;

    s->hdr.esc3_level_length = 0;
    s->hdr.esc3_run_length = 0;

    return 0;
}

int msmpeg4_decode_picture_header(mp4_stream_t *ld, struct mp4_state *s)
{
    int code;

    if (s->msmpeg_version == 1) {
        int start_code;
        start_code = (getbits(ld, 16) << 16) | getbits(ld, 16);
        if (start_code != 0x00000100) {
            return -1;
        }
        getbits(ld, 5); // frame number
    }
    s->hdr.prediction_type = s->hdr.picture_coding_type = getbits(ld, 2);
    s->hdr.vop_quant = getbits(ld, 5);
    s->hdr.quantizer = s->hdr.vop_quant;
    if (s->hdr.prediction_type == I_VOP) {
        code = getbits(ld, 5);
        if (s->msmpeg_version == 1) {
            s->hdr.slice_height = code;
        } else {
            s->hdr.slice_height = s->mb_height / (code - 0x16);
        }
        switch (s->msmpeg_version) {
        case 1:
        case 2:
            s->hdr.rl_chroma_table_index = 2;
            s->hdr.rl_table_index = 2;
            break;
        case 3:
            s->hdr.rl_chroma_table_index = getbits1(ld);
            if (s->hdr.rl_chroma_table_index)
                s->hdr.rl_chroma_table_index += getbits1(ld);

            s->hdr.rl_table_index = getbits1(ld);
            if (s->hdr.rl_table_index)
                s->hdr.rl_table_index += getbits1(ld);

            s->hdr.dc_table_index = getbits1(ld);
            break;
        case 4:
            getbits(ld, 5);     // fps
            s->hdr.bit_rate = getbits(ld, 11) * 1024;
            s->hdr.switch_rounding = getbits1(ld);

            if (s->hdr.bit_rate > MBAC_BITRATE)
                s->hdr.per_mb_rl_table = getbits1(ld);
            else
                s->hdr.per_mb_rl_table = 0;

            if (!s->hdr.per_mb_rl_table) {
                s->hdr.rl_chroma_table_index = getbits1(ld);
                if (s->hdr.rl_chroma_table_index)
                    s->hdr.rl_chroma_table_index += getbits1(ld);

                s->hdr.rl_table_index = getbits1(ld);
                if (s->hdr.rl_table_index)
                    s->hdr.rl_table_index += getbits1(ld);
            }

            s->hdr.dc_table_index = getbits1(ld);
            s->hdr.inter_intra_pred = 0;
            break;
        default:
            break;
        }
        s->hdr.rounding_type = 1;
    } else {
        switch (s->msmpeg_version) {
        case 1:
        case 2:
            if (s->msmpeg_version == 1)
                s->hdr.has_skips = 1;
            else
                s->hdr.has_skips = getbits1(ld);
            s->hdr.rl_table_index = 2;
            s->hdr.rl_chroma_table_index = 2;
            s->hdr.dc_table_index = 0;
            s->hdr.mv_table_index = 0;
            break;
        case 3:
            s->hdr.has_skips = getbits1(ld);
            s->hdr.rl_table_index = getbits1(ld);
            if (s->hdr.rl_table_index)
                s->hdr.rl_table_index += getbits1(ld);
            s->hdr.rl_chroma_table_index = s->hdr.rl_table_index;
            s->hdr.dc_table_index = getbits1(ld);
            s->hdr.mv_table_index = getbits1(ld);
            break;
        case 4:
            s->hdr.has_skips = getbits1(ld);
            if (s->hdr.bit_rate > MBAC_BITRATE)
                s->hdr.per_mb_rl_table = getbits1(ld);
            else
                s->hdr.per_mb_rl_table = 0;

            if (!s->hdr.per_mb_rl_table) {
                s->hdr.rl_table_index = getbits1(ld);
                if (s->hdr.rl_table_index)
                    s->hdr.rl_table_index += getbits1(ld);
                s->hdr.rl_chroma_table_index = s->hdr.rl_table_index;
            }

            s->hdr.dc_table_index = getbits1(ld);
            s->hdr.mv_table_index = getbits1(ld);
            s->hdr.inter_intra_pred = (s->hdr.width * s->hdr.height < 320 * 240 && s->hdr.bit_rate <= II_BITRATE);
            break;
        default:
            break;
        }

        if (s->hdr.switch_rounding) {
            s->hdr.rounding_type ^= 1;
        } else {
            s->hdr.rounding_type = 0;
        }
    }

    if (s->hdr.dc_table_index) {
        s->hdr.dc_lum_table = &get_dc_311_1_lum;
        s->hdr.dc_chrom_table = &get_dc_311_1_chrom;
    } else {
        s->hdr.dc_lum_table = &get_dc_311_0_lum;
        s->hdr.dc_chrom_table = &get_dc_311_0_chrom;
    }
    if (s->hdr.mv_table_index) {
        s->hdr.mv_table = &get_mv_data_311_1;
    } else {
        s->hdr.mv_table = &get_mv_data_311_0;
    }

    s->hdr.esc3_level_length = 0;
    s->hdr.esc3_run_length = 0;

    return 0;
}
// save software recon result ( for_ref, back_ref, recontruct ) to files
void mp4_save_sw_recon_result(reference_t *ref)
{

}
