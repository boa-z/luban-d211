/*
 * Copyright (C) 2020-2025 ArtInChip Technology Co. Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 *  author: <xiaodong.zhao@artinchip.com>
 *  Desc: mpeg4 header interface
 *
 */

#include <stdlib.h>
#include <string.h>

#include "mp4_getbits.h"
#include "mp4_global.h"
#include "mp4_header.h"
#include "mp4_mblock.h"
#include "mp4_recon.h"
#include "mp4_tables.h"
#include "mp4_vars.h"
#include "mpp_log.h"

#define verify(x) \
    if (!(x))     \
        return 0

static int check_stuffingcode(mp4_stream_t *ld, int nbit);
static void getcomplexityestimationhdr(mp4_stream_t *ld, struct mp4_state *mp4_state);
static void read_vop_complexity_estimation_header(mp4_stream_t *ld, struct mp4_state *mp4_state);
void getusrhdr(mp4_stream_t *ld, struct mp4_state *mp4_state, int just_vol_init);

unsigned int m_pctszSize;

static inline int divround(int v1, int v2)
{
    return (v1 + v2 / 2) / v2;
}

int log2ceil(int arg)
{
    int j = 0;
    int i = 1;
    if (arg == 0)
        return 0;
#ifdef WIN32
    __asm
    {
        mov ecx, arg
            bsr edx, ecx
            btr ecx, edx
            xor eax, eax
            test ecx, ecx
            setnz al
            add edx, eax
            mov j, edx
    }
    return j;
#else
    while (arg > i) {
        i *= 2;
        j++;
    }
    return j;
#endif
}

/** Video Object Sequence */
int getvoshdr(mp4_stream_t *_ld, struct mp4_state *_mp4_state, int just_vol_init)
{
    mp4_stream_t *ld = _ld;
    struct mp4_state *mp4_state = _mp4_state;

    int code = showbits(ld, 32);
    if (code == VOS_START_CODE) {
        getbits(ld, 24);
        getbits(ld, 8);

        getbits(ld, 8); // profile and level indication
        while (showbits(ld, 32) == USR_START_CODE)
            getusrhdr(ld, mp4_state, just_vol_init);
    }

    return 1;
}

static void video_signal_type(mp4_stream_t *ld, struct mp4_state *mp4_state)
{
    int video_signal_type = getbits(ld, 1);
    if (video_signal_type) {
        getbits(ld, 3);  // video_format
        getbits(ld, 1);  // video_range
        int coulour_description = getbits(ld, 1);
        if (coulour_description) {
            getbits(ld, 8);  // colour_primaries
            getbits(ld, 8);  // transfer_characteristics
            getbits(ld, 8);  // matrix_coefficients
        }
    }
}

int getvsohdr(mp4_stream_t *_ld, struct mp4_state *_mp4_state)
{
    mp4_stream_t *ld = _ld;
    struct mp4_state *mp4_state = _mp4_state;

    const int video_id = 1, still_texture_id = 2;

    int code = showbits(ld, 32);
    if (code == VSO_START_CODE) {
        int is_visual_object_id;
        int visual_object_type;

        getbits(ld, 24);
        getbits(ld, 8);

        is_visual_object_id = getbits(ld, 1);
        if (is_visual_object_id) {
            getbits(ld, 4);  // visual_object_verid
            getbits(ld, 3);  // visual_object_priority
        }

        visual_object_type = getbits(ld, 4);

        if ((visual_object_type == video_id) || (visual_object_type == still_texture_id)) {
            video_signal_type(ld, mp4_state);
        }

        return 1;
    }

    return 0;
}

#define EXTENDED_PAR 0x000f

extern unsigned int intra_quant_matrix[64];
extern unsigned int nonintra_quant_matrix[64];

int avi_dump = 1;

int getvolhdr(mp4_stream_t *_ld, struct mp4_state *_mp4_state, int just_vol_init)
{
    mp4_stream_t *ld = _ld;
    struct mp4_state *mp4_state = _mp4_state;

    int code = showbits(ld, 32);
    if ((code >= VO_START_CODE_MIN) && (code <= VO_START_CODE_MAX)) {
        getbits(ld, 24);
        getbits(ld, 8);
    }

    code = showbits(ld, 28);
    if (code == VOL_START_CODE) {
        getbits(ld, 24);
        getbits(ld, 4);

        mp4_state->flag_keyframe = 1;

        mp4_state->hdr.ident = getbits(ld, 4); // vol_id
        mp4_state->hdr.random_accessible_vol = getbits(ld, 1);
        mp4_state->hdr.type_indication = getbits(ld, 8);
        mp4_state->hdr.is_object_layer_identifier = getbits(ld, 1);

        if (mp4_state->hdr.is_object_layer_identifier) {
            mp4_state->hdr.visual_object_layer_verid = getbits(ld, 4);
            mp4_state->hdr.visual_object_layer_priority = getbits(ld, 3);
        } else {
            mp4_state->hdr.visual_object_layer_verid = 1;
            mp4_state->hdr.visual_object_layer_priority = 1;
        }
        mp4_state->hdr.aspect_ratio_info = getbits(ld, 4);
        if (mp4_state->hdr.aspect_ratio_info == EXTENDED_PAR) {
            mp4_state->hdr.par_width = getbits(ld, 8);
            mp4_state->hdr.par_height = getbits(ld, 8);

            if (mp4_state->hdr.par_width == 0 || mp4_state->hdr.par_height == 0) {
                loge("parameter error [%d / %d]", mp4_state->hdr.par_width, mp4_state->hdr.par_height);
                return -1;
            }
        }
        mp4_state->hdr.vol_control_parameters = getbits(ld, 1);
        if (mp4_state->hdr.vol_control_parameters) {
            mp4_state->hdr.chroma_format = getbits(ld, 2);
            mp4_state->hdr.low_delay = getbits(ld, 1);
            mp4_state->hdr.vbv_parameters = getbits(ld, 1);
            if (mp4_state->hdr.vbv_parameters) {
                mp4_state->hdr.first_half_bit_rate = getbits(ld, 15);
                getbits1(ld); // marker
                mp4_state->hdr.latter_half_bit_rate = getbits(ld, 15);
                getbits1(ld); // marker
                mp4_state->hdr.first_half_vbv_buffer_size = getbits(ld, 15);
                getbits1(ld); // marker
                mp4_state->hdr.latter_half_vbv_buffer_size = getbits(ld, 3);
                mp4_state->hdr.first_half_vbv_occupancy = getbits(ld, 11);
                getbits1(ld); // marker
                mp4_state->hdr.latter_half_vbv_occupancy = getbits(ld, 15);
                getbits1(ld); // marker
            }
        }
        mp4_state->hdr.shape = getbits(ld, 2);
        if (mp4_state->hdr.shape != RECTANGULAR)
            return DEC_NOT_IMPLEMENTED;

        if ((mp4_state->hdr.shape == GRAY_SCALE) && (mp4_state->hdr.visual_object_layer_verid != 0x01)) {
            mp4_state->hdr.video_object_layer_shape_extension = getbits(ld, 4);
        }

        getbits1(ld); // marker
        mp4_state->hdr.time_increment_resolution = getbits(ld, 16);
        mp4_state->test_timeinc = -2;
        getbits1(ld); // marker
        mp4_state->hdr.fixed_vop_rate = getbits(ld, 1);

        if (mp4_state->hdr.fixed_vop_rate) {
            int bits = log2ceil(mp4_state->hdr.time_increment_resolution);
            if (bits < 1)
                bits = 1;
            mp4_state->hdr.fixed_vop_time_increment = getbits(ld, bits);
        }

        if (mp4_state->hdr.shape != BINARY_SHAPE_ONLY) {
            if (mp4_state->hdr.shape == RECTANGULAR) {
                getbits1(ld); // marker
                mp4_state->hdr.width = getbits(ld, 13);
                getbits1(ld); // marker
                mp4_state->hdr.height = getbits(ld, 13);
                getbits1(ld); // marker
            }

            mp4_state->hdr.interlaced = getbits(ld, 1);
            mp4_state->hdr.obmc_disable = getbits(ld, 1);
            if (mp4_state->hdr.interlaced) {
                loge("====> interlace abort");
            }

            if ((mp4_state->hdr.time_increment_resolution == 0) || (mp4_state->hdr.time_increment_resolution == 1)) {
                /**
                 * value 0 is forbidden and value 1 is meaningless because
                 * spec says about valid fixed_time_increment in range
                 * [0; time_increment_resolution).
                 * Thus, we have a not quite MPEG-4 compliant stream
                 * ( e.g. the one produced by DivX 4 builds 167 to 208 ).
                 * The following is not determined by standard, but hopefully
                 * no one will notice.
                 */
                mp4_state->hdr.obmc_disable = 1;
            }
            if (mp4_state->hdr.visual_object_layer_verid == 1) {
                mp4_state->hdr.sprite_usage = getbits(ld, 1);
            } else {
                mp4_state->hdr.sprite_usage = getbits(ld, 2);
            }

            if ((mp4_state->hdr.sprite_usage == STATIC_SPRITE) || (mp4_state->hdr.sprite_usage == GMC_SPRITE)) {
                if (mp4_state->hdr.sprite_usage != GMC_SPRITE) {
                    getbits(ld, 13); // sprite_width
                    getbits(ld, 1);  // marker
                    getbits(ld, 13); // sprite_height
                    getbits(ld, 1);  // marker
                    getbits(ld, 13); // sprite_left_coordinate
                    getbits(ld, 1);  // marker
                    getbits(ld, 13); // sprite_top_coordinate
                    getbits(ld, 1);  // marker
                }
                mp4_state->hdr.no_of_sprite_warping_points = getbits(ld, 6);
                mp4_state->hdr.sprite_warping_accuracy = getbits(ld, 2);
                // 00: halfpel
                // 01: 1/4 pel
                // 10: 1/8 pel
                // 11: 1/16 pel

                if (mp4_state->hdr.no_of_sprite_warping_points > 1) {
                    loge("not support no_of_sprite_warping_points(%d)", mp4_state->hdr.no_of_sprite_warping_points);
                }

                mp4_state->hdr.sprite_brightness_change = getbits(ld, 1);
                if (mp4_state->hdr.sprite_usage != GMC_SPRITE)
                    mp4_state->hdr.low_latency_sprite_enable = getbits(ld, 1);
            }

            if (mp4_state->hdr.sprite_usage == STATIC_SPRITE)
                return DEC_NOT_IMPLEMENTED;

            if ((mp4_state->hdr.visual_object_layer_verid != 0x01) && (mp4_state->hdr.shape != RECTANGULAR)) {
                mp4_state->hdr.sadct_disable = getbits(ld, 1);
            }

            mp4_state->hdr.not_8_bit = getbits(ld, 1);
            if (mp4_state->hdr.not_8_bit) {
                loge("only support 8bit");
                return DEC_NOT_IMPLEMENTED;
                // mp4_state->hdr.quant_precision = getbits(ld, 4);
                // mp4_state->hdr.bits_per_pixel = getbits(ld, 4);
            } else {
                mp4_state->hdr.quant_precision = 5;
                mp4_state->hdr.bits_per_pixel = 8;
            }

            if (mp4_state->hdr.shape == GRAY_SCALE) {
                getbits(ld, 1); // no_gray_quant_update
                getbits(ld, 1); // composition_method
                getbits(ld, 1); // linear_composition
            }

            mp4_state->hdr.quant_type = getbits(ld, 1); // quant type

            if (mp4_state->hdr.quant_type) {
                if (just_vol_init)
                    return 0;

                mp4_state->hdr.load_intra_quant_matrix = getbits(ld, 1);
                if (mp4_state->hdr.load_intra_quant_matrix) {
                    // load intra quant matrix
                    unsigned int val;
                    int i, k = 0;
                    do {
                        val = getbits(ld, 8);
                        mp4_state->hdr.intra_quant_matrix[zig_zag_scan[k]] = val;
                        k++;
                    } while ((k < 64) && (val != 0));
                    if (k < 64)
                        k--;
                    for (i = k; i < 64; i++) {
                        mp4_state->hdr.intra_quant_matrix[zig_zag_scan[i]] = mp4_state->hdr.intra_quant_matrix[zig_zag_scan[k - 1]];
                    }
                } else {
                    memcpy(mp4_state->hdr.intra_quant_matrix, intra_quant_matrix, sizeof(intra_quant_matrix));
                }
                mp4_state->hdr.load_nonintra_quant_matrix = getbits(ld, 1);
                if (mp4_state->hdr.load_nonintra_quant_matrix) {
                    // load nonintra quant matrix
                    unsigned int val;
                    int i, k = 0;
                    do {
                        val = getbits(ld, 8);
                        mp4_state->hdr.nonintra_quant_matrix[zig_zag_scan[k]] = val;
                        k++;
                    } while ((k < 64) && (val != 0));
                    if (k < 64)
                        k--;
                    for (i = k; i < 64; i++) {
                        mp4_state->hdr.nonintra_quant_matrix[zig_zag_scan[i]] = mp4_state->hdr.nonintra_quant_matrix[zig_zag_scan[k - 1]];
                    }
                } else {
                    memcpy(mp4_state->hdr.nonintra_quant_matrix, nonintra_quant_matrix, sizeof(nonintra_quant_matrix));
                }
                if (mp4_state->hdr.shape == GRAY_SCALE) {
                }
            } else {
                memcpy(mp4_state->hdr.intra_quant_matrix, intra_quant_matrix, sizeof(intra_quant_matrix));
                memcpy(mp4_state->hdr.nonintra_quant_matrix, nonintra_quant_matrix, sizeof(nonintra_quant_matrix));
            }

            if (mp4_state->hdr.visual_object_layer_verid /*ident*/ != 1) {
                mp4_state->hdr.quarter_pixel = getbits(ld, 1);
            } else {
                mp4_state->hdr.quarter_pixel = 0;
            }

            mp4_state->hdr.complexity_estimation_disable = getbits(ld, 1);
            if (!mp4_state->hdr.complexity_estimation_disable)
                getcomplexityestimationhdr(ld, mp4_state);

            mp4_state->hdr.resync_marker_disable = getbits(ld, 1);
            mp4_state->hdr.data_partitioning = getbits(ld, 1);
            if (mp4_state->hdr.data_partitioning) {
                mp4_state->hdr.reversible_vlc = getbits(ld, 1);
            }

            if (mp4_state->hdr.visual_object_layer_verid != 1) {
                mp4_state->hdr.newpred_enable = getbits(ld, 1); // newpred
                if (mp4_state->hdr.newpred_enable) {
                    return DEC_NOT_IMPLEMENTED;
                    mp4_state->hdr.request_upstream_message_type = getbits(ld, 2);
                    mp4_state->hdr.newpred_segment_type = getbits(ld, 1);
                }
                mp4_state->hdr.reduced_resolution_vop_enable = getbits(ld, 1);
            }

            mp4_state->hdr.intra_acdc_pred_disable = 0;

            mp4_state->hdr.scalability = getbits(ld, 1);
            if ((mp4_state->hdr.scalability) && (mp4_state->userdata_codec_version != 412))
                return DEC_NOT_IMPLEMENTED;
        }

        bytealign(ld);
        // [v503] patch for the divx stream prior of the v503
        // before v503 there was just a bytealign() instead of
        // the next_start_code() specified by the standard
        if ((showbits(ld, 24) != 0x01) && ((showbits(ld, 32) & 0x00ffffff) == 0x01))
            next_start_code(ld, mp4_state);
        if (showbits(ld, 32) == USR_START_CODE) {
            getusrhdr(ld, mp4_state, just_vol_init);
        }
        mp4_state->hdr.tframe = -1;
        return 0;
    }
    return -1; // no VO start code
}

void getusrhdr(mp4_stream_t *ld, struct mp4_state *mp4_state, int just_vol_init)
{
    // unsigned char aucBuffer[64];
    // unsigned char * pBuffer = aucBuffer;
    // unsigned char *pucTmp = NULL;
    char aucBuffer[64];
    // char *pBuffer = aucBuffer;
    char *pucTmp = NULL;
    int iBufferPos = 0;

    getbits(ld, 32); // user data start code
    memset(aucBuffer, 0, 64);

    mp4_state->history_prefixed = 0;
    mp4_state->prefixed = 0;

    while ((showbits(ld, 24) != 0x01) && (just_vol_init ? (ld->startptr + ld->length >= ld->rdptr) : 1)) {
        aucBuffer[iBufferPos] = getbits(ld, 8); // right now I just parse through the data without using it
        iBufferPos++;

        if (iBufferPos == 64) {
            iBufferPos = 0;
        }
    }

    pucTmp = strstr(aucBuffer, "DivX");
    if (!pucTmp) {
        return;
    }
    mp4_state->userdata_codec_version = atoi(&pucTmp[4]);

    pucTmp = strstr(aucBuffer, "Build");
    if (pucTmp) {
        mp4_state->userdata_build_number = atoi(&pucTmp[5]);
        return;
    }
    if (mp4_state->userdata_codec_version >= 500) {
        mp4_state->edge_hor_start = mp4_state->mb_width * 16;
        mp4_state->edge_ver_start = mp4_state->mb_height * 16;
    }

    pucTmp = strstr(aucBuffer, "b");
    if (pucTmp) {
        mp4_state->userdata_build_number = atoi(&pucTmp[1]);
    }

    pucTmp = strstr(aucBuffer, "p");
    if (pucTmp) {
        mp4_state->prefixed = 1;         // this frame is prefixed
        mp4_state->history_prefixed = 1; // this gop has been prefixed
    }
}

static void getcomplexityestimationhdr(mp4_stream_t *ld, struct mp4_state *mp4_state)
{
    mp4_state->hdr.estimation_method = getbits(ld, 2);
    if ((mp4_state->hdr.estimation_method == 0) || (mp4_state->hdr.estimation_method == 1)) {
        mp4_state->hdr.shape_complexity_estimation_disable = getbits(ld, 1);
        if (!mp4_state->hdr.shape_complexity_estimation_disable) {
            mp4_state->hdr.opaque = getbits(ld, 1);
            mp4_state->hdr.transparent = getbits(ld, 1);
            mp4_state->hdr.intra_cae = getbits(ld, 1);
            mp4_state->hdr.inter_cae = getbits(ld, 1);
            mp4_state->hdr.no_update = getbits(ld, 1);
            mp4_state->hdr.upsampling = getbits(ld, 1);
        }
        mp4_state->hdr.texture_complexity_estimation_set_1_disable = getbits(ld, 1);
        if (!mp4_state->hdr.texture_complexity_estimation_set_1_disable) {
            mp4_state->hdr.intra_blocks = getbits(ld, 1);
            mp4_state->hdr.inter_blocks = getbits(ld, 1);
            mp4_state->hdr.inter4v_blocks = getbits(ld, 1);
            mp4_state->hdr.not_coded_blocks = getbits(ld, 1);
        }
        getbits(ld, 1); // marker bit
        mp4_state->hdr.texture_complexity_estimation_set_2_disable = getbits(ld, 1);
        if (!mp4_state->hdr.texture_complexity_estimation_set_2_disable) {
            mp4_state->hdr.dct_coefs = getbits(ld, 1);
            mp4_state->hdr.dct_lines = getbits(ld, 1);
            mp4_state->hdr.vlc_symbols = getbits(ld, 1);
            mp4_state->hdr.vlc_bits = getbits(ld, 1);
        }
        mp4_state->hdr.motion_compensation_complexity_disable = getbits(ld, 1);
        if (!mp4_state->hdr.motion_compensation_complexity_disable) {
            mp4_state->hdr.apm = getbits(ld, 1);
            mp4_state->hdr.npm = getbits(ld, 1);
            mp4_state->hdr.interpolate_mc_q = getbits(ld, 1);
            mp4_state->hdr.forw_back_mc_q = getbits(ld, 1);
            mp4_state->hdr.halfpel2 = getbits(ld, 1);
            mp4_state->hdr.halfpel4 = getbits(ld, 1);
        }
        getbits(ld, 1); // marker bit

        if (mp4_state->hdr.estimation_method == 1) {
            mp4_state->hdr.version2_complexity_estimation_disable = getbits(ld, 1);
            if (!mp4_state->hdr.version2_complexity_estimation_disable) {
                mp4_state->hdr.sadct = getbits(ld, 1);
                mp4_state->hdr.quarterpel = getbits(ld, 1);
            }
        }
    }
}

static void read_vop_complexity_estimation_header(mp4_stream_t *ld, struct mp4_state *mp4_state)
{
    if ((mp4_state->hdr.estimation_method == 0) || (mp4_state->hdr.estimation_method == 1)) {
        if (mp4_state->hdr.prediction_type == I_VOP) {
            if (mp4_state->hdr.opaque)
                mp4_state->hdr.dcecs_opaque = getbits(ld, 8);
            if (mp4_state->hdr.transparent)
                mp4_state->hdr.dcecs_transparent = getbits(ld, 8);
            if (mp4_state->hdr.intra_cae)
                mp4_state->hdr.dcecs_intra_cae = getbits(ld, 8);
            if (mp4_state->hdr.inter_cae)
                mp4_state->hdr.dcecs_inter_cae = getbits(ld, 8);
            if (mp4_state->hdr.no_update)
                mp4_state->hdr.dcecs_no_update = getbits(ld, 8);
            if (mp4_state->hdr.upsampling)
                mp4_state->hdr.dcecs_upsampling = getbits(ld, 8);
            if (mp4_state->hdr.intra_blocks)
                mp4_state->hdr.dcecs_intra_blocks = getbits(ld, 8);
            if (mp4_state->hdr.not_coded_blocks)
                mp4_state->hdr.dcecs_not_coded_blocks = getbits(ld, 8);
            if (mp4_state->hdr.dct_coefs)
                mp4_state->hdr.dcecs_dct_coefs = getbits(ld, 8);
            if (mp4_state->hdr.dct_lines)
                mp4_state->hdr.dcecs_dct_lines = getbits(ld, 8);
            if (mp4_state->hdr.vlc_symbols)
                mp4_state->hdr.dcecs_vlc_symbols = getbits(ld, 8);
            if (mp4_state->hdr.vlc_bits)
                mp4_state->hdr.dcecs_vlc_bits = getbits(ld, 4);
            if (mp4_state->hdr.sadct)
                mp4_state->hdr.dcecs_sadct = getbits(ld, 8);
        }
        if (mp4_state->hdr.prediction_type == P_VOP) {
            if (mp4_state->hdr.opaque)
                mp4_state->hdr.dcecs_opaque = getbits(ld, 8);
            if (mp4_state->hdr.transparent)
                mp4_state->hdr.dcecs_transparent = getbits(ld, 8);
            if (mp4_state->hdr.intra_cae)
                mp4_state->hdr.dcecs_intra_cae = getbits(ld, 8);
            if (mp4_state->hdr.inter_cae)
                mp4_state->hdr.dcecs_inter_cae = getbits(ld, 8);
            if (mp4_state->hdr.no_update)
                mp4_state->hdr.dcecs_no_update = getbits(ld, 8);
            if (mp4_state->hdr.upsampling)
                mp4_state->hdr.dcecs_upsampling = getbits(ld, 8);
            if (mp4_state->hdr.intra_blocks)
                mp4_state->hdr.dcecs_intra_blocks = getbits(ld, 8);
            if (mp4_state->hdr.not_coded_blocks)
                mp4_state->hdr.dcecs_not_coded_blocks = getbits(ld, 8);
            if (mp4_state->hdr.dct_coefs)
                mp4_state->hdr.dcecs_dct_coefs = getbits(ld, 8);
            if (mp4_state->hdr.dct_lines)
                mp4_state->hdr.dcecs_dct_lines = getbits(ld, 8);
            if (mp4_state->hdr.vlc_symbols)
                mp4_state->hdr.dcecs_vlc_symbols = getbits(ld, 8);
            if (mp4_state->hdr.vlc_bits)
                mp4_state->hdr.dcecs_vlc_bits = getbits(ld, 4);
            if (mp4_state->hdr.inter_blocks)
                mp4_state->hdr.dcecs_inter_blocks = getbits(ld, 8);
            if (mp4_state->hdr.inter4v_blocks)
                mp4_state->hdr.dcecs_inter4v_blocks = getbits(ld, 8);
            if (mp4_state->hdr.apm)
                mp4_state->hdr.dcecs_apm = getbits(ld, 8);
            if (mp4_state->hdr.npm)
                mp4_state->hdr.dcecs_npm = getbits(ld, 8);
            if (mp4_state->hdr.forw_back_mc_q)
                mp4_state->hdr.dcecs_forw_back_mc_q = getbits(ld, 8);
            if (mp4_state->hdr.halfpel2)
                mp4_state->hdr.dcecs_halfpel2 = getbits(ld, 8);
            if (mp4_state->hdr.halfpel4)
                mp4_state->hdr.dcecs_halfpel4 = getbits(ld, 8);
            if (mp4_state->hdr.sadct)
                mp4_state->hdr.dcecs_sadct = getbits(ld, 8);
            if (mp4_state->hdr.quarterpel)
                mp4_state->hdr.dcecs_quarterpel = getbits(ld, 8);
        }
        if (mp4_state->hdr.prediction_type == B_VOP) {
            if (mp4_state->hdr.opaque)
                mp4_state->hdr.dcecs_opaque = getbits(ld, 8);
            if (mp4_state->hdr.transparent)
                mp4_state->hdr.dcecs_transparent = getbits(ld, 8);
            if (mp4_state->hdr.intra_cae)
                mp4_state->hdr.dcecs_intra_cae = getbits(ld, 8);
            if (mp4_state->hdr.inter_cae)
                mp4_state->hdr.dcecs_inter_cae = getbits(ld, 8);
            if (mp4_state->hdr.no_update)
                mp4_state->hdr.dcecs_no_update = getbits(ld, 8);
            if (mp4_state->hdr.upsampling)
                mp4_state->hdr.dcecs_upsampling = getbits(ld, 8);
            if (mp4_state->hdr.intra_blocks)
                mp4_state->hdr.dcecs_intra_blocks = getbits(ld, 8);
            if (mp4_state->hdr.not_coded_blocks)
                mp4_state->hdr.dcecs_not_coded_blocks = getbits(ld, 8);
            if (mp4_state->hdr.dct_coefs)
                mp4_state->hdr.dcecs_dct_coefs = getbits(ld, 8);
            if (mp4_state->hdr.dct_lines)
                mp4_state->hdr.dcecs_dct_lines = getbits(ld, 8);
            if (mp4_state->hdr.vlc_symbols)
                mp4_state->hdr.dcecs_vlc_symbols = getbits(ld, 8);
            if (mp4_state->hdr.vlc_bits)
                mp4_state->hdr.dcecs_vlc_bits = getbits(ld, 4);
            if (mp4_state->hdr.inter_blocks)
                mp4_state->hdr.dcecs_inter_blocks = getbits(ld, 8);
            if (mp4_state->hdr.inter4v_blocks)
                mp4_state->hdr.dcecs_inter4v_blocks = getbits(ld, 8);
            if (mp4_state->hdr.apm)
                mp4_state->hdr.dcecs_apm = getbits(ld, 8);
            if (mp4_state->hdr.npm)
                mp4_state->hdr.dcecs_npm = getbits(ld, 8);
            if (mp4_state->hdr.forw_back_mc_q)
                mp4_state->hdr.dcecs_forw_back_mc_q = getbits(ld, 8);
            if (mp4_state->hdr.halfpel2)
                mp4_state->hdr.dcecs_halfpel2 = getbits(ld, 8);
            if (mp4_state->hdr.halfpel4)
                mp4_state->hdr.dcecs_halfpel4 = getbits(ld, 8);
            if (mp4_state->hdr.interpolate_mc_q)
                mp4_state->hdr.dcecs_interpolate_mc_q = getbits(ld, 8);
            if (mp4_state->hdr.sadct)
                mp4_state->hdr.dcecs_sadct = getbits(ld, 8);
            if (mp4_state->hdr.quarterpel)
                mp4_state->hdr.dcecs_quarterpel = getbits(ld, 8);
        }
        if ((mp4_state->hdr.prediction_type == S_VOP) && (mp4_state->hdr.sprite_usage == STATIC_SPRITE)) {
            if (mp4_state->hdr.intra_blocks)
                mp4_state->hdr.dcecs_intra_blocks = getbits(ld, 8);
            if (mp4_state->hdr.not_coded_blocks)
                mp4_state->hdr.dcecs_not_coded_blocks = getbits(ld, 8);
            if (mp4_state->hdr.dct_coefs)
                mp4_state->hdr.dcecs_dct_coefs = getbits(ld, 8);
            if (mp4_state->hdr.dct_lines)
                mp4_state->hdr.dcecs_dct_lines = getbits(ld, 8);
            if (mp4_state->hdr.vlc_symbols)
                mp4_state->hdr.dcecs_vlc_symbols = getbits(ld, 8);
            if (mp4_state->hdr.vlc_bits)
                mp4_state->hdr.dcecs_vlc_bits = getbits(ld, 4);
            if (mp4_state->hdr.inter_blocks)
                mp4_state->hdr.dcecs_inter_blocks = getbits(ld, 8);
            if (mp4_state->hdr.inter4v_blocks)
                mp4_state->hdr.dcecs_inter4v_blocks = getbits(ld, 8);
            if (mp4_state->hdr.apm)
                mp4_state->hdr.dcecs_apm = getbits(ld, 8);
            if (mp4_state->hdr.npm)
                mp4_state->hdr.dcecs_npm = getbits(ld, 8);
            if (mp4_state->hdr.forw_back_mc_q)
                mp4_state->hdr.dcecs_forw_back_mc_q = getbits(ld, 8);
            if (mp4_state->hdr.halfpel2)
                mp4_state->hdr.dcecs_halfpel2 = getbits(ld, 8);
            if (mp4_state->hdr.halfpel4)
                mp4_state->hdr.dcecs_halfpel4 = getbits(ld, 8);
            if (mp4_state->hdr.interpolate_mc_q)
                mp4_state->hdr.dcecs_interpolate_mc_q = getbits(ld, 8);
        }
    }
}

int getgophdr(mp4_stream_t *_ld, struct mp4_state *_mp4_state)
{
    mp4_stream_t *ld = _ld;
    struct mp4_state *mp4_state = _mp4_state;
    int h, m, s;

    if (nextbits(ld, 32) == GOP_START_CODE) // [Ag][Review] possible bug, it's not possible to read 32 bits
    {
        getbits(ld, 24);
        getbits(ld, 8);

        mp4_state->hdr.time_code = getbits(ld, 18);
        mp4_state->hdr.closed_gov = getbits(ld, 1);
        mp4_state->hdr.broken_link = getbits(ld, 1);

        // Add time code processing in gop header by Robert Yuan, Oct 10,2003
        s = mp4_state->hdr.time_code & 0x3f;
        m = (mp4_state->hdr.time_code >> 7) & 0x3f;
        h = (mp4_state->hdr.time_code >> 13) & 0x1f;
        mp4_state->hdr.time_base = h * 3600 + m * 60 + s;
    }

    return 0;
}



int getshvhdr(mp4_stream_t *_ld, struct mp4_state *_mp4_state)
{
    mp4_stream_t *ld = _ld;
    struct mp4_state *mp4_state = _mp4_state;

    // end of sequence
    if (nextbits(ld, 22) == SHV_END_MARKER) {
        getbits(ld, 22);
        mp4_state->hdr.short_video_header = 0;

        return 0;
    }

    // start of sequence
    if (nextbits(ld, 22) != (int)SHV_START_CODE) {
        mp4_state->hdr.short_video_header = 0;
        return -1;
    }

    getbits(ld, 22);
    mp4_state->hdr.short_video_header = 1;
    mp4_state->userdata_codec_version = 263;

    mp4_state->hdr.temporal_reference = getbits(ld, 8);
    getbits1(ld); // marker bit
    getbits1(ld); // zero_bit
    mp4_state->hdr.split_screen_indicator = getbits(ld, 1);
    mp4_state->hdr.document_camera_indicator = getbits(ld, 1);
    mp4_state->hdr.full_picture_freeze_release = getbits(ld, 1);
    mp4_state->hdr.source_format = getbits(ld, 3);

    switch (mp4_state->hdr.source_format) {
    case 1:
        mp4_state->hdr.width = 128;
        mp4_state->hdr.height = 96;
        mp4_state->hdr.num_mb_in_gob = 8;
        mp4_state->hdr.num_gobs_in_vop = 6;
        break;
    case 2:
        mp4_state->hdr.width = 176;
        mp4_state->hdr.height = 144;
        mp4_state->hdr.num_mb_in_gob = 11;
        mp4_state->hdr.num_gobs_in_vop = 9;
        break;
    case 3:
        mp4_state->hdr.width = 352;
        mp4_state->hdr.height = 288;
        mp4_state->hdr.num_mb_in_gob = 22;
        mp4_state->hdr.num_gobs_in_vop = 18;
        break;
    case 4:
        mp4_state->hdr.width = 704;
        mp4_state->hdr.height = 576;
        mp4_state->hdr.num_mb_in_gob = 88;
        mp4_state->hdr.num_gobs_in_vop = 18;
        break;
    case 5:
        mp4_state->hdr.width = 1408;
        mp4_state->hdr.height = 1152;
        mp4_state->hdr.num_mb_in_gob = 352;
        mp4_state->hdr.num_gobs_in_vop = 18;
        break;
    default:
        return DEC_BAD_FORMAT; // no valid picture format found
    }

    mp4_state->hdr.picture_coding_type = getbits(ld, 1);
    if (mp4_state->hdr.picture_coding_type == 0) {
        mp4_state->hdr.prediction_type = I_VOP;
        mp4_state->flag_keyframe = 1;
    } else {
        mp4_state->hdr.prediction_type = P_VOP;
        mp4_state->flag_keyframe = 0;
    }

    if (mp4_state->hdr.picture_coding_type == B_VOP) { // B-VOPs not supported in short header mode
        loge("not support B_VOP");
        return -1;
    }

    mp4_state->hdr.four_reserved_zero_bits = getbits(ld, 4);

    mp4_state->hdr.vop_quant = getbits(ld, 5);
    mp4_state->hdr.quantizer = mp4_state->hdr.vop_quant;

    getbits1(ld); // zero_bit

    while (getbits(ld, 1) == 1) {
        getbits(ld, 8); // pei + psupp mechanism
    }

    // fixed setting for short header
    mp4_state->hdr.shape = RECTANGULAR;
    mp4_state->hdr.obmc_disable = 1;
    mp4_state->hdr.quant_type = 0;
    mp4_state->hdr.resync_marker_disable = 1;
    mp4_state->hdr.data_partitioning = 0;
    mp4_state->hdr.reversible_vlc = 0;
    mp4_state->hdr.rounding_type = 0;
    mp4_state->hdr.fcode_for = 1;
    mp4_state->hdr.vop_coded = 1;
    mp4_state->hdr.interlaced = 0;
    mp4_state->hdr.complexity_estimation_disable = 1;
    mp4_state->hdr.use_intra_dc_vlc = 0;
    mp4_state->hdr.scalability = 0;
    mp4_state->hdr.not_8_bit = 0;
    mp4_state->hdr.bits_per_pixel = 8;

    return 0;
}

int get_flv_pic_hdr(mp4_stream_t *_ld, struct mp4_state *_mp4_state)
{
    mp4_stream_t *ld = _ld;
    struct mp4_state *mp4_state = _mp4_state;

    mp4_state->flag_disposable = 0;

    mp4_state->hdr.picture_coding_type = getbits(ld, 4);
    if (mp4_state->hdr.picture_coding_type == 1) {
        mp4_state->hdr.picture_coding_type = I_VOP;
        mp4_state->hdr.prediction_type = I_VOP;
        mp4_state->flag_keyframe = 1;
        mp4_state->flag_disposable = 0;
    } else if (mp4_state->hdr.picture_coding_type == 2) {
        mp4_state->hdr.picture_coding_type = P_VOP;
        mp4_state->hdr.prediction_type = P_VOP;
        mp4_state->flag_keyframe = 0;
        mp4_state->flag_disposable = 0;
    } else if (mp4_state->hdr.picture_coding_type == 3) {
        mp4_state->hdr.picture_coding_type = P_VOP;
        mp4_state->hdr.prediction_type = P_VOP;
        mp4_state->flag_keyframe = 0;
        mp4_state->flag_disposable = 1;
    }

    if (mp4_state->hdr.picture_coding_type == B_VOP) { // B-VOPs not supported in short header mode
        loge("not support B_VOP");
        return -1;
    }

    getbits(ld, 4); // CODECID

    getbits(ld, 17);

    mp4_state->hdr.short_video_header = 1;
    mp4_state->userdata_codec_version = 263;
    mp4_state->flvh263version = getbits(ld, 5);

    mp4_state->hdr.temporal_reference = getbits(ld, 8);
    mp4_state->hdr.split_screen_indicator = 0;
    mp4_state->hdr.document_camera_indicator = 0;
    mp4_state->hdr.full_picture_freeze_release = 0;
    mp4_state->hdr.source_format = getbits(ld, 3);

    switch (mp4_state->hdr.source_format) {
    case 0:
        mp4_state->hdr.width = getbits(ld, 8);
        mp4_state->hdr.height = getbits(ld, 8);
        break;
    case 1:
        mp4_state->hdr.width = getbits(ld, 16);
        mp4_state->hdr.height = getbits(ld, 16);
        break;
    case 2:
        mp4_state->hdr.width = 352;
        mp4_state->hdr.height = 288;
        break;
    case 3:
        mp4_state->hdr.width = 176;
        mp4_state->hdr.height = 144;
        break;
    case 4:
        mp4_state->hdr.width = 128;
        mp4_state->hdr.height = 96;
        break;
    case 5:
        mp4_state->hdr.width = 320;
        mp4_state->hdr.height = 240;
        break;
    case 6:
        mp4_state->hdr.width = 160;
        mp4_state->hdr.height = 120;
        break;
    default:
        mp4_state->hdr.width = mp4_state->hdr.height = 0;
        return DEC_BAD_FORMAT; // no valid picture format found
    }
    mp4_state->hdr.num_mb_in_gob = ((mp4_state->hdr.width + 15) / 16) * ((mp4_state->hdr.height + 15) / 16);
    mp4_state->hdr.num_gobs_in_vop = 1;

    mp4_state->hdr.picture_coding_type = getbits(ld, 2);
    if (mp4_state->hdr.picture_coding_type == 0) {
        mp4_state->hdr.picture_coding_type = I_VOP;
        mp4_state->hdr.prediction_type = I_VOP;
        mp4_state->flag_keyframe = 1;
        mp4_state->flag_disposable = 0;
    } else if (mp4_state->hdr.picture_coding_type == 1) {
        mp4_state->hdr.picture_coding_type = P_VOP;
        mp4_state->hdr.prediction_type = P_VOP;
        mp4_state->flag_keyframe = 0;
        mp4_state->flag_disposable = 0;
    } else if (mp4_state->hdr.picture_coding_type == 2) {
        mp4_state->hdr.picture_coding_type = P_VOP;
        mp4_state->hdr.prediction_type = P_VOP;
        mp4_state->flag_keyframe = 0;
        mp4_state->flag_disposable = 1;
    }
    mp4_state->deblockingflag = getbits(ld, 1);

    mp4_state->hdr.vop_quant = getbits(ld, 5);
    mp4_state->hdr.quantizer = mp4_state->hdr.vop_quant;

    // getbits1(ld); // zero_bit, for flv there is no this bit

    while (getbits(ld, 1) == 1) {
        getbits(ld, 8); // pei + psupp mechanism
    }

    // fixed setting for short header
    mp4_state->hdr.shape = RECTANGULAR;
    mp4_state->hdr.obmc_disable = 1;
    mp4_state->hdr.quant_type = 0;
    mp4_state->hdr.resync_marker_disable = 1;
    mp4_state->hdr.data_partitioning = 0;
    mp4_state->hdr.reversible_vlc = 0;
    mp4_state->hdr.rounding_type = 0;
    mp4_state->hdr.fcode_for = 1;
    mp4_state->hdr.vop_coded = 1;
    mp4_state->hdr.interlaced = 0;
    mp4_state->hdr.complexity_estimation_disable = 1;
    mp4_state->hdr.use_intra_dc_vlc = 0;
    mp4_state->hdr.scalability = 0;
    mp4_state->hdr.not_8_bit = 0;
    mp4_state->hdr.bits_per_pixel = 8;

    return 0;
}

int get_use_intra_dc_vlc(int quantizer, int intra_dc_vlc_thr)
{
    int use_intra_dc_vlc = 1;

    if (intra_dc_vlc_thr == 0) {
        return 1;
    }

    switch (intra_dc_vlc_thr & 7) {
    case 1:
        if (quantizer >= 13)
            use_intra_dc_vlc = 0;
        break;
    case 2:
        if (quantizer >= 15)
            use_intra_dc_vlc = 0;
        break;
    case 3:
        if (quantizer >= 17)
            use_intra_dc_vlc = 0;
        break;
    case 4:
        if (quantizer >= 19)
            use_intra_dc_vlc = 0;
        break;
    case 5:
        if (quantizer >= 21)
            use_intra_dc_vlc = 0;
        break;
    case 6:
        if (quantizer >= 23)
            use_intra_dc_vlc = 0;
        break;
    case 7:
        use_intra_dc_vlc = 0;
        break;
    default:
        use_intra_dc_vlc = 1;
        break;
    }

    return use_intra_dc_vlc;
}


/**
 * Table B-33 -- Code table for the first trajectory point
 * dmv value	SSS	VLC	dmv_code
 * -16383 ?-8192, 8192 ?16383	14	111111111110	00000000000000...01111111111111, 10000000000000...11111111111111
 * -8191 ?-4096, 4096 ?8191	13	11111111110	0000000000000...0111111111111, 1000000000000...1111111111111
 * -4095 ?-2048, 2048 ?4095	12	1111111110	000000000000...011111111111, 100000000000...111111111111
 * -2047...-1024, 1024...2047	11	111111110	00000000000...01111111111, 10000000000...11111111111
 * -1023...-512, 512...1023	10	11111110	0000000000...0111111111, 1000000000...1111111111
 * -511...-256, 256...511	9	1111110	000000000...011111111, 100000000...111111111
 * -255...-128, 128...255	8	111110	00000000...01111111, 10000000...11111111
 * -127...-64, 64...127	7	11110	0000000...0111111, 1000000...1111111
 * -63...-32, 32...63	6	1110	000000...011111, 100000...111111
 * -31...-16, 16...31	5	110	00000...01111, 10000...1111
 * -15...-8, 8...15	4	101	0000...0111, 1000...1111
 * -7...-4, 4...7	3	100	000...011, 100...111
 * -3...-2, 2...3	2	011	00...01, 10...11
 * -1, 1	1	010	0, 1
 * 0	0	00	-
 */
static inline int read_dmv_length(mp4_stream_t *ld)
{
    int retval;
    switch (getbits(ld, 2)) {
    default:
    case 0:
        return 0;
    case 1:
        return 1 + getbits1(ld);
    case 2:
        return 3 + getbits1(ld);
    case 3:
        retval = 5;
        while (getbits1(ld) && (retval <= 14))
            retval++;
        if (retval == 15)
            return -1;
        return retval;
    }
}

static inline int read_dmv_code(mp4_stream_t *ld, int len)
{
    int base;
    if (!len)
        return 0;
    if (getbits1(ld) == 1)
        base = 1 << (len - 1);
    else
        base = -(1 << len) + 1;
    if (len > 1)
        base += getbits(ld, len - 1);
    return base;
}

static int decode_sprite_trajectory(struct mp4_state *mp4_state, mp4_stream_t *ld)
{
    int i;
    int bad_divx = (mp4_state->userdata_codec_version == 500) && (mp4_state->userdata_build_number >= 370) &&
                   (mp4_state->userdata_build_number <= 413);
    if ((mp4_state->hdr.no_of_sprite_warping_points < 0) || (mp4_state->hdr.no_of_sprite_warping_points > 3))
        return DEC_BAD_FORMAT;
    for (i = 0; i < mp4_state->hdr.no_of_sprite_warping_points; i++) {
        int dmv_length = read_dmv_length(ld);
        if (dmv_length < 0)
            return DEC_BAD_FORMAT;
        mp4_state->hdr.warping_points[i][0] = read_dmv_code(ld, dmv_length);
        if (!bad_divx)
            getbits1(ld);
        dmv_length = read_dmv_length(ld);
        if (dmv_length < 0)
            return DEC_BAD_FORMAT;
        mp4_state->hdr.warping_points[i][1] = read_dmv_code(ld, dmv_length);
        getbits1(ld);
    }
    mp4_state->hdr.iEffectiveWarpingPoints = mp4_state->hdr.no_of_sprite_warping_points;

    // This is a divx bug, the second wrapping points is zero, so it can same as one wrapping points.
    // if we support more than one wrapping points really, this adjustment can be removed.
    while (mp4_state->hdr.iEffectiveWarpingPoints && !(mp4_state->hdr.warping_points[mp4_state->hdr.iEffectiveWarpingPoints - 1][0] ||
                                                       mp4_state->hdr.warping_points[mp4_state->hdr.iEffectiveWarpingPoints - 1][1]))
        mp4_state->hdr.iEffectiveWarpingPoints--;

    mp4_state->set_gmc_mv = set_gmc_mv_pointers[mp4_state->hdr.iEffectiveWarpingPoints];
    // mp4_state->reconstruct_skip = reconstruct_gmc_pointers[mp4_state->hdr.iEffectiveWarpingPoints];
    return 0;
}

/**
 * brightness_change_factor value	brightness_change_factor_length value	brightness_change_factor_length VLC	brightness_change_factor
 * -16...-1, 1...16	1	0	00000...01111, 10000...11111
 * -48...-17, 17...48	2	10	000000...011111, 100000...111111
 * 112...-49, 49...112	3	110	0000000...0111111, 1000000...1111111
 * 113?24	4	1110	000000000...111 111 111
 * 625...1648	4	1111	0000000000?111111111
 */
static int decode_brightness_change_factor(struct mp4_state *mp4_state, mp4_stream_t *ld)
{
    int length = 0;
    while (getbits1(ld) && (length < 4))
        length++;
    switch (length) {
    case 0:
        if (getbits1(ld))
            return 1 + getbits(ld, 4);
        else
            return -16 + getbits(ld, 4);
    case 1:
        if (getbits1(ld))
            return 17 + getbits(ld, 5);
        else
            return -48 + getbits(ld, 5);
    case 2:
        if (getbits1(ld))
            return 49 + getbits(ld, 6);
        else
            return -112 + getbits(ld, 6);
    case 3:
        return 113 + getbits(ld, 9);
    case 4:
        return 625 + getbits(ld, 10);
    }

    return 0;
}

static inline int div_twoslash(int v1, int v2)
{
    if (v2 <= 0) {
        loge("v2 <= 0");
        return 0;
    }
    if (v2 & 1) {
        loge("v2 & 1");
        return 0;
    }

    if (v1 > 0)
        return (v1 + v2 / 2) / v2;
    else
        return (v1 - v2 / 2) / v2;
}

static inline int div_threeslash(int v1, int v2)
{
    if (v2 <= 0) {
        loge("v2 <= 0");
        return 0;
    }
    if (v2 & 1) {
        loge("v2 & 1");
        return 0;
    }
    return (v1 + v2 / 2) / v2;
}
/**
 * Spec tells us the following
 *
 * '//' is an integer division with rounding to the nearest integer. Half-integers are rounded away from zero.
 * '///' is an integer division with rounding to the nearest integer. Half-integers are rounded up.
 * '////' is an integer division with truncation towards minus infinity.
 *
 * i1'' = 16 (i0 + W') + ((W - W') (r i0' - 16 i0) + W' (r i1' - 16 i1)) // W
 * j1'' = 16 j0 + ((W - W') (r j0' - 16 j0) + W' (r j1' - 16 j1)) // W
 *
 * in 1/16 pel accuracy, and r = 16/s ( scaler from stream accuracy to 1/16 pel accuracy ).
 *
 * I = i - i0 ( horizontal coordinate relative to p.0 )
 * J = j - j0 ( vertical coordinate relative to p.0 )
 * Ic = 4 ic  - 2 i0 + 1
 * Jc = 4 jc  - 2 j0 + 1
 *
 * in these equations W' and H' are lowest powers of 2 larger or equal to width and height of picture, respectively
***/
static void calc_affine_transforms_1point(struct mp4_state *mp4_state)
{
    int s = 2 << mp4_state->hdr.sprite_warping_accuracy;

    // with 1 pel accuracy:
    int i0 = 0, j0 = 0;

    int i0_, j0_;
    int X0, Y0, XX, YX, XY, YY, rounder, shifter;
    // following equations are unchecked

    // with 1/s pel accuracy
    if ((mp4_state->userdata_codec_version == 500) && (mp4_state->userdata_build_number >= 370) &&
        (mp4_state->userdata_build_number <= 413)) {
        i0_ = s * i0 + mp4_state->hdr.warping_points[0][0];
        j0_ = s * j0 + mp4_state->hdr.warping_points[0][1];
    } else {
        i0_ = (s / 2) * (2 * i0 + mp4_state->hdr.warping_points[0][0]);                                       // 18bits
        j0_ = (s / 2) * (2 * j0 + mp4_state->hdr.warping_points[0][1]);                                       // 18bits
    }

    // luminance:
    XX = s;
    YX = 0;
    XY = 0;
    YY = s;
    shifter = 0;
    rounder = 0;

    mp4_state->at_lum.X0 = i0_;
    mp4_state->at_lum.XX = XX;
    mp4_state->at_lum.YX = YX;
    mp4_state->at_lum.Y0 = j0_;
    mp4_state->at_lum.XY = XY;
    mp4_state->at_lum.YY = YY;
    mp4_state->at_lum.shifter = shifter;
    mp4_state->at_lum.rounder1 = mp4_state->at_lum.rounder2 = rounder;

    // chrominance:
    XX = s;
    YX = 0;
    XY = 0;
    YY = s;
    shifter = 0;
    rounder = 0;
    X0 = ((i0_ >> 1) | (i0_ & 1)) - s * i0 / 2;
    Y0 = ((j0_ >> 1) | (j0_ & 1)) - s * j0 / 2;

    mp4_state->at_chrom.XX = XX;
    mp4_state->at_chrom.YX = YX;
    mp4_state->at_chrom.XY = XY;
    mp4_state->at_chrom.YY = YY;
    mp4_state->at_chrom.shifter = shifter;
    mp4_state->at_chrom.rounder1 = mp4_state->at_chrom.rounder2 = rounder;
    mp4_state->at_chrom.X0 = X0;
    mp4_state->at_chrom.Y0 = Y0;
}

/**
 * Spec tells us the following
 *
 * '//' is an integer division with rounding to the nearest integer. Half-integers are rounded away from zero.
 * '///' is an integer division with rounding to the nearest integer. Half-integers are rounded up.
 * '////' is an integer division with truncation towards minus infinity.
 *
 * i1'' = 16 (i0 + W') + ((W - W') (r i0' - 16 i0) + W' (r i1' - 16 i1)) // W
 * j1'' = 16 j0 + ((W - W') (r j0' - 16 j0) + W' (r j1' - 16 j1)) // W
 *
 * in 1/16 pel accuracy, and r = 16/s ( scaler from stream accuracy to 1/16 pel accuracy ).
 *
 * I = i - i0 ( horizontal coordinate relative to p.0 )
 * J = j - j0 ( vertical coordinate relative to p.0 )
 * Ic = 4 ic  - 2 i0 + 1
 * Jc = 4 jc  - 2 j0 + 1
 *
 * in these equations W' and H' are lowest powers of 2 larger or equal to width and height of picture, respectively
 */
static void calc_affine_transforms_2point(struct mp4_state *mp4_state)
{
    int s = 2 << mp4_state->hdr.sprite_warping_accuracy;
    int r = 16 / s;

    // with 1 pel accuracy:
    int i0 = 0, j0 = 0;
    int i1 = mp4_state->horizontal_size, j1 = 0;

    int W = mp4_state->horizontal_size;
    int W_ = 1 << log2ceil(W);

    int i0_, j0_, i1_, j1_;
    int i1__, j1__;
    int X0, Y0, XX, YX, XY, YY, rounder, shifter;
    // following equations are unchecked

    // with 1/s pel accuracy
    if ((mp4_state->userdata_codec_version == 500) && (mp4_state->userdata_build_number >= 370) &&
        (mp4_state->userdata_build_number <= 413)) {
        i0_ = s * i0 + mp4_state->hdr.warping_points[0][0];
        j0_ = s * j0 + mp4_state->hdr.warping_points[0][1];
        i1_ = s * i1 + mp4_state->hdr.warping_points[0][0] + mp4_state->hdr.warping_points[1][0];
        j1_ = s * j1 + mp4_state->hdr.warping_points[0][1] + mp4_state->hdr.warping_points[1][1];
    } else {
        i0_ = (s / 2) * (2 * i0 + mp4_state->hdr.warping_points[0][0]);
        j0_ = (s / 2) * (2 * j0 + mp4_state->hdr.warping_points[0][1]);
        i1_ = (s / 2) * (2 * i1 + mp4_state->hdr.warping_points[0][0] + mp4_state->hdr.warping_points[1][0]);
        j1_ = (s / 2) * (2 * j1 + mp4_state->hdr.warping_points[0][1] + mp4_state->hdr.warping_points[1][1]);
    }

    // with 1/16 pel accuracy
    i1__ = 16 * (i0 + W_) + div_twoslash((W - W_) * (r * i0_ - 16 * i0) + W_ * (r * i1_ - 16 * i1), W);
    // (i0 + W_) + ((W-W_)*(i0_-i0) + W_*(i1_-i1)) / W
    j1__ = 16 * j0 + div_twoslash((W - W_) * (r * j0_ - 16 * j0) + W_ * (r * j1_ - 16 * j1), W);

    /**
     * luminance:
     * F(i, j) = i0' + ((-r i0' + i1'') I + (r j0' - j1'') J) /// (W' r)
     * G(i, j) = j0' + ((-r j0' + j1'') I + (-r i0' + i1'') J) /// (W' r)
     */
    XX = -r * i0_ + i1__;
    YX = r * j0_ - j1__;
    XY = -r * j0_ + j1__;
    YY = -r * i0_ + i1__;
    shifter = log2ceil(W_ * r);
    rounder = 1 << (shifter - 1);

    while (!((XX | YX | XY | YY | rounder) & 1)) {
        if (shifter == 0)
            break;
        XX >>= 1;
        YX >>= 1;
        XY >>= 1;
        YY >>= 1;
        rounder >>= 1;
        shifter--;
    }

    mp4_state->at_lum.X0 = i0_;
    mp4_state->at_lum.XX = XX;
    mp4_state->at_lum.YX = YX;
    mp4_state->at_lum.Y0 = j0_;
    mp4_state->at_lum.XY = XY;
    mp4_state->at_lum.YY = YY;
    mp4_state->at_lum.shifter = shifter;
    mp4_state->at_lum.rounder1 = mp4_state->at_lum.rounder2 = rounder;

    /**
     * chrominance:
     * Fc(ic, jc) = ((-r i0' + i1 '') Ic  + (r j0' - j1'') Jc  + 2 W' r i0' - 16W') /// (4 W' r)
     * Gc(ic, jc) = ((-r j0' + j1'') Ic  + (-r i0' + i1'') Jc  + 2 W' r j0' - 16W') /// (4 W' r)
     */
    XX = -r * i0_ + i1__;
    YX = r * j0_ - j1__;
    XY = -r * j0_ + j1__;
    YY = -r * i0_ + i1__;
    shifter = log2ceil(4 * W_ * r);
    rounder = 1 << (shifter - 1);
    X0 = 2 * W_ * r * i0_ - 16 * W_ + rounder;
    Y0 = 2 * W_ * r * j0_ - 16 * W_ + rounder;

    while (!((X0 | Y0 | XX | YX | XY | YY | rounder) & 1)) {
        if (shifter == 0)
            break;
        X0 >>= 1;
        Y0 >>= 1;
        XX >>= 1;
        YX >>= 1;
        XY >>= 1;
        YY >>= 1;
        rounder >>= 1;
        shifter--;
    }

    mp4_state->at_chrom.XX = XX;
    mp4_state->at_chrom.YX = YX;
    mp4_state->at_chrom.XY = XY;
    mp4_state->at_chrom.YY = YY;
    mp4_state->at_chrom.shifter = shifter;
    mp4_state->at_chrom.rounder1 = mp4_state->at_chrom.rounder2 = rounder;
    mp4_state->at_chrom.X0 = X0;
    mp4_state->at_chrom.Y0 = Y0;
}

/**
 * i1'' = 16 (i0 + W') + ((W - W') (r i0' - 16 i0) + W' (r i1' - 16 i1)) // W
 * j1'' = 16 j0 + ((W - W') (r j0' - 16 j0) + W' (r j1' - 16 j1)) // W
 *
 * i2'' = 16 i0 + ((H - H') (r i0' - 16 i0) + H' (r i2' - 16 i2)) // H,
 * j2'' = 16 (j0 + H') + ((H - H') (r j0' - 16 j0) + H' (r j2' - 16 j2)) // H
 *
 * luminance:
 * F(i, j) = (i0' + ((-r i0' + i1'') H' I + (-r i0'+ i2'')W' J) /// (W'H'r)
 * G(i, j) = j0' + ((-r j0' + j1'') H' I + (-r j0'+ j2'')W' J) /// (W'H'r)
 * chrominance:
 * Fc(ic, jc) = ((-r i0' + i1'') H' Ic  + (-r i0'+ i2'')W' Jc  + 2 W'H'r i0' - 16W'H') /// (4W'H'r)
 * Gc(ic, jc) = ((-r j0' + j1'') H' Ic  + (-r j0'+ j2'')W' Jc  + 2 W'H'r j0' - 16W'H') /// (4W'H'r)
 */
static void calc_affine_transforms_3point(struct mp4_state *mp4_state)
{
    int s = 2 << mp4_state->hdr.sprite_warping_accuracy;
    int r = 16 / s;

    // with 1 pel accuracy:
    int i0 = 0, j0 = 0;
    int i1 = mp4_state->horizontal_size, j1 = 0;
    int i2 = 0, j2 = mp4_state->vertical_size;

    int W = mp4_state->horizontal_size, H = mp4_state->vertical_size;
    int W_ = 1 << log2ceil(W), H_ = 1 << log2ceil(H);

    // following equations are unchecked
    int i0_, j0_, i1_, j1_, i2_, j2_;
    int i1__, j1__, i2__, j2__;

    int XX, YX, XY, YY;
    int X0, Y0;
    int64_t X1, Y1;

    int shifter = log2ceil(W_ * H_ * r);
    int rounder = 1 << (shifter - 1);

    // with 1/s pel accuracy
    if ((mp4_state->userdata_codec_version == 500) && (mp4_state->userdata_build_number >= 370) &&
        (mp4_state->userdata_build_number <= 413)) {
        i0_ = s * i0 + mp4_state->hdr.warping_points[0][0];
        j0_ = s * j0 + mp4_state->hdr.warping_points[0][1];
        i1_ = s * i1 + mp4_state->hdr.warping_points[0][0] + mp4_state->hdr.warping_points[1][0];
        j1_ = s * j1 + mp4_state->hdr.warping_points[0][1] + mp4_state->hdr.warping_points[1][1];
        i2_ = s * i2 + mp4_state->hdr.warping_points[0][0] + mp4_state->hdr.warping_points[2][0];
        j2_ = s * j2 + mp4_state->hdr.warping_points[0][1] + mp4_state->hdr.warping_points[2][1];
    } else { // mpeg-4 compliant
        i0_ = (s / 2) * (2 * i0 + mp4_state->hdr.warping_points[0][0]); // 3bits * 15bits==>18bits
        j0_ = (s / 2) * (2 * j0 + mp4_state->hdr.warping_points[0][1]); // 3bits * 15bits==>18bits
        i1_ = (s / 2) * (2 * i1 + mp4_state->hdr.warping_points[0][0] +
                         mp4_state->hdr.warping_points[1][0]); // 3bits * (1bit * 11bit + 15bits + 15bits)==>19bits
        j1_ = (s / 2) *
              (2 * j1 + mp4_state->hdr.warping_points[0][1] + mp4_state->hdr.warping_points[1][1]); // 3bits * (15bits + 15bits)==>19bits
        i2_ = (s / 2) *
              (2 * i2 + mp4_state->hdr.warping_points[0][0] + mp4_state->hdr.warping_points[2][0]); // 3bits * (15bits + 15bits)==>19bits
        j2_ = (s / 2) * (2 * j2 + mp4_state->hdr.warping_points[0][1] +
                         mp4_state->hdr.warping_points[2][1]); // 3bits * (1bit * 11bit + 15bits + 15bits)==>19bits
    }

    // with 1/16 pel accuracy
    i1__ = 16 * (i0 + W_) + div_twoslash((W - W_) * (r * i0_ - 16 * i0) + W_ * (r * i1_ - 16 * i1), W);
    // 4bit*12bits + (10bit * 18bits + 11bit*19bit-4bit*11bit)//11bit===>19bits
    //  (i0 + W_) + ((W-W_)*(i0_-i0) + W_*(i1_-i1)) / W
    j1__ = 16 * j0 + div_twoslash((W - W_) * (r * j0_ - 16 * j0) + W_ * (r * j1_ - 16 * j1), W); //==>19bits

    i2__ = 16 * i0 + div_twoslash((H - H_) * (r * i0_ - 16 * i0) + H_ * (r * i2_ - 16 * i2), H);        //==>19bits
    j2__ = 16 * (j0 + H_) + div_twoslash((H - H_) * (r * j0_ - 16 * j0) + H_ * (r * j2_ - 16 * j2), H); //==>19bits

    XX = (-r * i0_ + i1__) * H_; //(18bit+19bits)*11bit==>30bits
    YX = (-r * i0_ + i2__) * W_;
    XY = (-r * j0_ + j1__) * H_;
    YY = (-r * j0_ + j2__) * W_;

    /**
     * luminance:
     * F(i, j) = i0' + ((-r i0' + i1'') H' I + (-r i0'+ i2'')W' J) /// (W'H'r)
     * G(i, j) = j0' + ((-r j0' + j1'') H' I + (-r j0'+ j2'')W' J) /// (W'H'r)
     */
    // fixme ( minor )
    // sometimes 16-bit precision is still not enough
    while (!((XX | YX | XY | YY | rounder) & 1)) {
        if (shifter == 0)
            break;
        XX >>= 1;
        YX >>= 1;
        XY >>= 1;
        YY >>= 1;
        rounder >>= 1;
        shifter--;
    }
    // Assume that W_ and H_ are same or half, that after shifting, the result is maximum 20 bits

    mp4_state->at_lum.X0 = i0_; // 18bits
    mp4_state->at_lum.XX = XX;  // 20bits
    mp4_state->at_lum.YX = YX;  // 20bits
    mp4_state->at_lum.Y0 = j0_; // 18bits
    mp4_state->at_lum.XY = XY;  // 20bits
    mp4_state->at_lum.YY = YY;  // 20bits
    mp4_state->at_lum.shifter = shifter;
    mp4_state->at_lum.rounder1 = mp4_state->at_lum.rounder2 = rounder;
    /**
     * chrominance:
     * Fc(ic, jc) = ((-r i0' + i1'') H' Ic  + (-r i0'+ i2'')W' Jc  + 2 W'H'r i0' - 16W'H') /// (4W'H'r)
     * Gc(ic, jc) = ((-r j0' + j1'') H' Ic  + (-r j0'+ j2'')W' Jc  + 2 W'H'r j0' - 16W'H') /// (4W'H'r)
     */
    XX = (-r * i0_ + i1__) * H_;
    YX = (-r * i0_ + i2__) * W_;
    XY = (-r * j0_ + j1__) * H_;
    YY = (-r * j0_ + j2__) * W_;

    shifter = log2ceil(4 * W_ * H_ * r);
    rounder = 1 << (shifter - 1);
    X0 = 2 * W_ * H_ * r * i0_ - 16 * W_ * H_ + rounder;
    Y0 = 2 * W_ * H_ * r * j0_ - 16 * W_ * H_ + rounder;
    X1 = i0_;
    X1 *= 2 * W_ * H_ * r;
    X1 -= 16 * W_ * H_;
    X1 += rounder;
    Y1 = j0_;
    Y1 *= 2 * W_ * H_ * r;
    Y1 -= 16 * W_ * H_;
    Y1 += rounder;

    while (!((XX | YX | XY | YY | X0 | Y0 | rounder) & 1)) {
        if (shifter == 0)
            break;
        XX >>= 1;
        YX >>= 1;
        XY >>= 1;
        YY >>= 1;
        X1 >>= 1;
        Y1 >>= 1;
        rounder >>= 1;
        shifter--;
    }

    mp4_state->at_chrom.XX = XX; // 20bits
    mp4_state->at_chrom.YX = YX; // 20bits
    mp4_state->at_chrom.XY = XY; // 20bits
    mp4_state->at_chrom.YY = YY; // 20bits
    mp4_state->at_chrom.shifter = shifter;
    mp4_state->at_chrom.rounder1 = mp4_state->at_chrom.rounder2 = rounder;
    mp4_state->at_chrom.X0 = X1; // 18+1+12+12-12 = 31bits
    mp4_state->at_chrom.Y0 = Y1; // 30bits
}

int getvophdr(mp4_stream_t *_ld, struct mp4_state *_mp4_state)
{
    mp4_stream_t *ld = _ld;
    struct mp4_state *mp4_state = _mp4_state;

    int display_time, i = 0, ptype;

    bytealign(ld);

    while (showbits(ld, 32) != (int)VOP_START_CODE) {
        i++;
        next_start_code(ld, mp4_state);
        if (i > 100)
            return -1; // vop start code not found
    }
    flushbits(ld, 32);

    ptype = getbits(ld, 2);
    mp4_state->hdr.picture_coding_type = ptype;
    if (ptype == I_VOP)
        mp4_state->flag_keyframe = 1;
    else
        mp4_state->flag_keyframe = 0;
    if (ptype != B_VOP) {
        mp4_state->hdr.old_time_base = mp4_state->hdr.time_base;
        display_time = mp4_state->hdr.time_base;
    } else {
        display_time = mp4_state->hdr.old_time_base;
    }
    while (getbits(ld, 1) == 1) // temporal time base
    {
        if (ptype != B_VOP)
            mp4_state->hdr.time_base++;
        display_time++;
    }
    getbits1(ld); // marker bit

    int bits = log2ceil(mp4_state->hdr.time_increment_resolution);
    if (bits < 1)
        bits = 1;
    mp4_state->hdr.time_inc = getbits(ld, bits); // vop_time_increment (1-16 bits)

    getbits1(ld); // marker bit

    // trb/trd - display_time calculation

    display_time = display_time * mp4_state->hdr.time_increment_resolution + mp4_state->hdr.time_inc;
    if (ptype != B_VOP) {

        mp4_state->hdr.display_time_prev = mp4_state->hdr.display_time_next;
        mp4_state->hdr.display_time_next = display_time;

        if (display_time != mp4_state->hdr.display_time_prev)
            mp4_state->hdr.trd = mp4_state->hdr.display_time_next - mp4_state->hdr.display_time_prev;
    } else {
        mp4_state->hdr.trb = display_time - mp4_state->hdr.display_time_prev;
        // remove from interlaced case to all case
        if (mp4_state->hdr.tframe == -1)
            // Changed interlaced B direct time calculation by Robert Yuan,Sept 17,2003
            // mp4_state->hdr.tframe = mp4_state->hdr.display_time_next - display_time;
            mp4_state->hdr.tframe = display_time - mp4_state->hdr.display_time_prev;
        if (mp4_state->hdr.tframe == 0)
            mp4_state->hdr.tframe = 1;
        if (mp4_state->hdr.interlaced) {
            mp4_state->hdr.trbi =
                2 * (divround(display_time, mp4_state->hdr.tframe) - divround(mp4_state->hdr.display_time_prev, mp4_state->hdr.tframe));
            mp4_state->hdr.trdi = 2 * (divround(mp4_state->hdr.display_time_next, mp4_state->hdr.tframe) -
                                       divround(mp4_state->hdr.display_time_prev, mp4_state->hdr.tframe));
        }
    }

    mp4_state->hdr.vop_coded = getbits(ld, 1);
    if (mp4_state->hdr.vop_coded == 0) {
        loge("Not Coded VOP\n");
        next_start_code(ld, mp4_state);
        mp4_state->hdr.prediction_type = ptype;
        return 0;
    }

    if (mp4_state->hdr.last_coded_prediction_type != B_VOP)
        mp4_state->hdr.old_prediction_type = mp4_state->hdr.last_coded_prediction_type;
    mp4_state->hdr.last_coded_prediction_type = mp4_state->hdr.prediction_type = ptype;

    if (mp4_state->hdr.newpred_enable) {
        int bit_length = ((mp4_state->hdr.time_inc + 3) < 15) ? (mp4_state->hdr.time_inc + 3) : 15;
        getbits(ld, bit_length);
        int vop_id_for_prediction_indication = getbits(ld, 1);
        if (vop_id_for_prediction_indication) {
            getbits(ld, bit_length);
            getbits1(ld); // marker bit
        }
    }

    if ((mp4_state->hdr.shape != BINARY_SHAPE_ONLY) &&
        ((mp4_state->hdr.prediction_type == P_VOP) ||
         ((mp4_state->hdr.prediction_type == S_VOP) && mp4_state->hdr.sprite_usage == GMC_SPRITE))) {
        mp4_state->hdr.rounding_type = getbits(ld, 1);
    } else {
        mp4_state->hdr.rounding_type = 0;
    }

    verify(mp4_state->hdr.shape == RECTANGULAR);

    if (mp4_state->hdr.shape != RECTANGULAR) {
        if (!(mp4_state->hdr.sprite_usage == STATIC_SPRITE && mp4_state->hdr.prediction_type == I_VOP)) {
            mp4_state->hdr.width = getbits(ld, 13);
            getbits1(ld);
            mp4_state->hdr.height = getbits(ld, 13);
            getbits1(ld);
            mp4_state->hdr.hor_spat_ref = getbits(ld, 13);
            getbits1(ld);
            mp4_state->hdr.ver_spat_ref = getbits(ld, 13);
            getbits1(ld); // corr
        }

        mp4_state->hdr.change_CR_disable = getbits(ld, 1);

        mp4_state->hdr.constant_alpha = getbits(ld, 1);
        if (mp4_state->hdr.constant_alpha) {
            mp4_state->hdr.constant_alpha_value = getbits(ld, 8);
        }
    }

    if (mp4_state->hdr.complexity_estimation_disable != 1) {
        loge("Complexity estimation is not supported");
        return -1;
    }
    if (mp4_state->hdr.shape != BINARY_SHAPE_ONLY) {
        if (!mp4_state->hdr.complexity_estimation_disable)
            read_vop_complexity_estimation_header(ld, mp4_state);
    }

    if (mp4_state->hdr.shape != BINARY_SHAPE_ONLY) {
        mp4_state->hdr.intra_dc_vlc_thr = getbits(ld, 3);

        if (mp4_state->hdr.interlaced == 1) {
            mp4_state->hdr.top_field_first = getbits(ld, 1);
            mp4_state->hdr.alternate_vertical_scan_flag = getbits(ld, 1);
        }
    }

    // mp4_state->reconstruct_skip = reconstruct_skip;
    if ((mp4_state->hdr.prediction_type == S_VOP) &&
        ((mp4_state->hdr.sprite_usage == STATIC_SPRITE) || (mp4_state->hdr.sprite_usage == GMC_SPRITE))) {
        if (mp4_state->hdr.no_of_sprite_warping_points > 0) {
            int err = decode_sprite_trajectory(mp4_state, ld);
            if (err != 0)
                return err;
        }
        if (mp4_state->hdr.sprite_brightness_change)
            mp4_state->hdr.sprite_brightness_change_factor = decode_brightness_change_factor(mp4_state, ld);
        if (mp4_state->hdr.sprite_usage == STATIC_SPRITE) {
            return DEC_NOT_IMPLEMENTED;
        }
    }

    if (mp4_state->hdr.shape != BINARY_SHAPE_ONLY) {
        mp4_state->hdr.quantizer = getbits(ld, mp4_state->hdr.quant_precision); // vop quant
        mp4_state->hdr.use_intra_dc_vlc = get_use_intra_dc_vlc(mp4_state->hdr.quantizer, mp4_state->hdr.intra_dc_vlc_thr);

        if (mp4_state->hdr.prediction_type != I_VOP) {
            mp4_state->hdr.fcode_for = getbits(ld, 3);
            if (mp4_state->hdr.fcode_for == 0)
                return DEC_BAD_FORMAT;
        }
        if (mp4_state->hdr.prediction_type == B_VOP) {
            mp4_state->hdr.fcode_back = getbits(ld, 3);
        }

        if (!mp4_state->hdr.scalability) {
            if (mp4_state->hdr.shape && mp4_state->hdr.prediction_type != I_VOP)
                mp4_state->hdr.shape_coding_type = getbits(ld, 1); // vop shape coding type

            /* motion_shape_texture() */
        }
    }

    if (mp4_state->hdr.prediction_type == I_VOP)
        mp4_state->hdr.resync_length = 17;
    else if (mp4_state->hdr.prediction_type == P_VOP || mp4_state->hdr.prediction_type == S_VOP)
        mp4_state->hdr.resync_length = 16 + mp4_state->hdr.fcode_for;
    else if (mp4_state->hdr.prediction_type == B_VOP)
        mp4_state->hdr.resync_length = MAX(16 + mp4_state->hdr.fcode_for, 17);

    if (mp4_state->hdr.prediction_type == S_VOP) {
        switch (mp4_state->hdr.no_of_sprite_warping_points) {
        case 0:
            // /* do nothing */
            // break;
        case 1:
            // Add no_of_sprite_warping_points = 1 by Robert yuan, Sept 26,2003
            calc_affine_transforms_1point(mp4_state);
            break;
        case 2:
            calc_affine_transforms_2point(mp4_state);
            break;
        case 3:
            calc_affine_transforms_3point(mp4_state);
            break;
        default:
            // spec forbids this value to be more than 3 for GMC frames
            return DEC_BAD_FORMAT;
        }
        // luma part motion vectors for hw GMC
        mp4_state->gmc_lum_mv_x = mp4_state->at_lum.X0;
        mp4_state->gmc_lum_mv_y = mp4_state->at_lum.Y0;
        mp4_state->gmc_lum_mv_x <<= (3 - mp4_state->hdr.sprite_warping_accuracy);
        mp4_state->gmc_lum_mv_y <<= (3 - mp4_state->hdr.sprite_warping_accuracy);
        // chroma part motion vectors for hw GMC
        if (mp4_state->hdr.no_of_sprite_warping_points >= 2) {
            mp4_state->gmc_chrom_mv_x = mp4_state->at_chrom.X0 + mp4_state->at_chrom.XX + mp4_state->at_chrom.YX;
            mp4_state->gmc_chrom_mv_y = mp4_state->at_chrom.Y0 + mp4_state->at_chrom.XY + mp4_state->at_chrom.YY;
            mp4_state->gmc_chrom_mv_x >>= mp4_state->at_chrom.shifter;
            mp4_state->gmc_chrom_mv_y >>= mp4_state->at_chrom.shifter;
        } else {
            mp4_state->gmc_chrom_mv_x = mp4_state->at_chrom.X0;
            mp4_state->gmc_chrom_mv_y = mp4_state->at_chrom.Y0;
        }
        mp4_state->gmc_chrom_mv_x <<= (3 - mp4_state->hdr.sprite_warping_accuracy);
        mp4_state->gmc_chrom_mv_y <<= (3 - mp4_state->hdr.sprite_warping_accuracy);
    }
    return 0;
}



void sprite_trajectory(mp4_stream_t *ld, struct mp4_state *mp4_state)
{

}

// errors in the header must be considered unrecoverable (return value is 0)
int getpackethdr(mp4_stream_t *_ld, struct mp4_state *_mp4_state)
{
    mp4_stream_t *ld = _ld;
    struct mp4_state *mp4_state = _mp4_state;

    next_resync_marker(ld, mp4_state); // [Review]
    get_resync_marker(ld, mp4_state);

    mp4_state->hdr.macroblock_number = getbits(ld, mp4_state->hdr.mb_in_vop_length);

    if (mp4_state->hdr.shape != BINARY_SHAPE_ONLY) {
        mp4_state->hdr.quant_scale = getbits(ld, 5);
        // It seems it should not be update for B-VOP. Need to be verify.Robert Yuan,Oct 3,2003
        if (mp4_state->hdr.prediction_type != B_VOP) {
            mp4_state->hdr.quantizer = mp4_state->hdr.quant_scale; // Added by Robert Yuan in Sept 8,2003
            mp4_state->hdr.use_intra_dc_vlc =
                get_use_intra_dc_vlc(mp4_state->hdr.quantizer, mp4_state->hdr.intra_dc_vlc_thr); // Added by Robert Yuan in Sept 10,2003
        }
    }

    mp4_state->hdr.header_extension_code = getbits(ld, 1);
    if (mp4_state->hdr.header_extension_code) {
        while (getbits(ld, 1) == 1) { // temporal time base
            // It should be masked since it is same meaning as in picture header by Robert Yuan, Oct 3,2003
            // mp4_state->hdr.time_base++;
        }
        getbits1(ld); // marker bit
        {
            int bits = log2ceil(mp4_state->hdr.time_increment_resolution);
            if (bits < 1)
                bits = 1;

            mp4_state->hdr.time_inc = getbits(ld, bits); // vop_time_increment (1-16 bits)
        }
        getbits1(ld); // marker bit

        if (mp4_state->hdr.shape_coding_type != RECTANGULAR) {
        }

        if (getbits(ld, 2) != mp4_state->hdr.prediction_type) {
            // unrecovereable error
            return DEC_BAD_FORMAT;
        }

        if (mp4_state->hdr.shape != BINARY_SHAPE_ONLY) {
            int intra_dc_vlc_thr = getbits(ld, 3);
            if ((mp4_state->hdr.prediction_type == S_VOP) && (mp4_state->hdr.no_of_sprite_warping_points > 0)) {
                sprite_trajectory(ld, mp4_state);
            }
            if (intra_dc_vlc_thr != mp4_state->hdr.intra_dc_vlc_thr) {
                mp4_state->hdr.intra_dc_vlc_thr = intra_dc_vlc_thr; // [Ag][Review]
                mp4_state->hdr.use_intra_dc_vlc = get_use_intra_dc_vlc(mp4_state->hdr.quantizer, mp4_state->hdr.intra_dc_vlc_thr);
            }
            if (mp4_state->hdr.prediction_type != I_VOP) {
                if (getbits(ld, 3) != mp4_state->hdr.fcode_for) {
                    // unrecovereable error
                    return DEC_BAD_FORMAT;
                }
            }
            if (mp4_state->hdr.prediction_type == B_VOP) {
                if (getbits(ld, 3) != mp4_state->hdr.fcode_back) {
                    // unrecovereable error
                    return DEC_BAD_FORMAT;
                }
            }
        }
    }

    if (mp4_state->hdr.newpred_enable) {
        int bit_length = ((mp4_state->hdr.time_inc + 3) < 15) ? (mp4_state->hdr.time_inc + 3) : 15;
        getbits(ld, bit_length);
        int vop_id_for_prediction_indication = getbits(ld, 1);
        if (vop_id_for_prediction_indication) {
            getbits(ld, bit_length);
            getbits1(ld); // marker bit
        }
    }

    return 0;
}

int nextbits_resync_marker(mp4_stream_t *ld, struct mp4_state *mp4_state)
{
    if (mp4_state->hdr.resync_marker_disable == 0) {
        int code = nextbits_bytealigned(ld, mp4_state->hdr.resync_length, mp4_state->hdr.short_video_header);
        if (code == 0) {
            return 2;
        } else if (code == 1) {
            return 1;
        }
    }

    return 0;
}

int get_resync_marker(mp4_stream_t *_ld, struct mp4_state *_mp4_state)
{
    mp4_stream_t *ld = _ld;
    struct mp4_state *mp4_state = _mp4_state;

    if (mp4_state->hdr.resync_marker_disable == 0) {
        int code = showbits(ld, mp4_state->hdr.resync_length);
        if (code != 1) {
            return 0;
        } else {
            bytealign(ld); // it should already be bytealigned (next_resync_marker)
            getbits(ld, mp4_state->hdr.resync_length);

            return 1;
        }
    }

    return 0;
}

int nextbits(mp4_stream_t *_ld, int nbit)
{
    mp4_stream_t *ld = _ld;

    return showbits(ld, nbit);
}

// Purpose: look nbit forward for an alignement
int bytealigned(mp4_stream_t *_ld, int nbit)
{
    mp4_stream_t *ld = _ld;

    return (((ld->bitcnt + nbit) % 8) == 0);
}

int bytealign(mp4_stream_t *_ld)
{
    mp4_stream_t *ld = _ld;
    int skipcnt = 0;

    while (!bytealigned(ld, skipcnt))
        skipcnt += 1;

    flushbits(ld, skipcnt);
    return skipcnt;
}

void next_resync_marker(mp4_stream_t *ld, struct mp4_state *mp4_state)
{
    next_start_code(ld, mp4_state);
}

void next_start_code(mp4_stream_t *_ld, struct mp4_state *_mp4_state)
{
    mp4_stream_t *ld = _ld;
    // struct mp4_state * mp4_state = _mp4_state;

    getbits(ld, 1);
    bytealign(ld);
}

// returns the next nbit bits startign from the next bytealigned position
// requires that the stuffing bits are correct (0111...) in mpeg-4 syntax
int nextbits_bytealigned(mp4_stream_t *_ld, int nbit, int short_video_header)
{
    mp4_stream_t *ld = _ld;

    int code;
    int skipcnt = 0;

    if (bytealigned(ld, skipcnt)) {
        // stuffing bits
        if (showbits(ld, 8) == 127) {
            skipcnt += 8;
        }
    } else {
        // count skipbits until bytealign
        while (!bytealigned(ld, skipcnt)) {
            skipcnt += 1;
        }
    }

    // verify stuffing bits here
    if ((!short_video_header) && (!check_stuffingcode(ld, skipcnt)))
        return -1;

    code = showbits(ld, nbit + skipcnt);
    return (code & msk[nbit]);
}

int check_sync_marker(mp4_stream_t *ld)
{
    const unsigned char *ptr = ld->rdptr + (ld->bitcnt + 7) / 8 - 8;
    int skipcnt = 0;
    // check that will return us to caller in 99.95% of cases
    if (ptr[0] || ptr[1] || (ptr[2] & ~1))
        return 0;
    // but it is not sufficient if we are less than 8 bits from the next frame
    if (bytealigned(ld, 0)) {
        // stuffing bits
        if (showbits(ld, 8) == 127) {
            skipcnt = 8;
        }
    } else {
        // count skipbits until bytealign
        while (!bytealigned(ld, skipcnt)) {
            skipcnt += 1;
        }
    }
    if (!check_stuffingcode(ld, skipcnt)) // there was a check for short_video_header here but i removed it
        return 0;

    return 1;
}

static int check_stuffingcode(mp4_stream_t *ld, int skipcnt)
{
    int code, i;

    // verify stuffing bits
    code = showbits(ld, skipcnt);
    for (i = 0; i < skipcnt - 1; i++, code >>= 1) {
        if ((code & 1) != 1)
            return 0;
    }
    if ((code & 1) != 0)
        return 0;

    return 1; // valid stuffing code
}
