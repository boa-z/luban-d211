/*
 * Copyright (C) 2020-2025 ArtInChip Technology Co. Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 *  author: <xiaodong.zhao@artinchip.com>
 *  Desc: mpeg4 mblock interface
 *
 */

#include <string.h>
#include "mp4_header.h"

// #include "mp4_addblock.h"
#include "mp4_block.h"
#include "mp4_getbits.h"
#include "mp4_global.h"
#include "mp4_mblock.h"
#include "mp4_mblock_util.h"
#include "mp4_predict.h"
#include "mp4_recon.h"
#include "mp4_tables.h"
#include "mp4_vars.h"
#include "mpp_log.h"
#include "mpeg4_decoder.h"

char mb_type_name[12][16] = {"inter", "inter_Q", "inter4v", "intra", "intraQ", "stuffing"};
static const int MBA_NumMBs[] = {47, 98, 395, 1583, 6335, 9215};
static const int MBA_FieldWidth[] = {6, 7, 9, 11, 13, 14};
// Annex T tables used in encoder too
int gNewTAB_DQUANT_MQ[32][2] = {
    {0, 0},  // 0
    {2, 1},  // 1
    {-1, 1}, // 2
    {-1, 1}, // 3
    {-1, 1}, // 4
    {-1, 1}, // 5
    {-1, 1}, // 6
    {-1, 1}, // 7
    {-1, 1}, // 8
    {-1, 1}, // 9
    {-1, 1}, // 10
    {-2, 2}, // 11
    {-2, 2}, // 12
    {-2, 2}, // 13
    {-2, 2}, // 14
    {-2, 2}, // 15
    {-2, 2}, // 16
    {-2, 2}, // 17
    {-2, 2}, // 18
    {-2, 2}, // 19
    {-2, 2}, // 20
    {-3, 3}, // 21
    {-3, 3}, // 22
    {-3, 3}, // 23
    {-3, 3}, // 24
    {-3, 3}, // 25
    {-3, 3}, // 26
    {-3, 3}, // 27
    {-3, 3}, // 28
    {-3, 2}, // 29
    {-3, 1}, // 30
    {-3, -5} // 31
};
extern event_t vld_intra_dct(mp4_stream_t *_ld);
extern event_t vld_intra_aic_dct(mp4_stream_t *_ld);
extern event_t vld_rmg2_intra_dct(mp4_stream_t *_ld);
extern unsigned int m_pctszSize;
extern int macroblock_h263_bvop(reference_t *ref);
extern void set_frame_to_display(reference_t *ref);
extern unsigned int GetBitsMask[33];
extern void mp4_set_packet_info(struct mp4_state *_mp4_state);
extern void mp4_set_mb_info(struct mp4_state *_mp4_state);


#include <stdarg.h>
#include <stdio.h>

#ifdef COUNT_RECON
static inline int64_t read_counter()
{
    int64_t a;
    __asm__ __volatile__("rdtsc\n\t"
                         "movl %%eax, (%%ecx)\n\t"
                         "movl %%edx, 4(%%ecx)\n\t"
                         :
                         : "c"(&a)
                         : "eax", "edx");
    return a;
}
#endif
extern int dct_count, recon_count;
extern int64_t dct_time, recon_time;

void interlaced_information(mp4_stream_t *ld, struct mp4_state *mp4_state);


void dump_mb_xy_for_trace(struct mp4_state *mp4_state)
{
#ifdef MPEG4_DUMP_ENABLE
    fprintf(g_mpeg4_ctx->fp_mb_coeff, "mb_pos: (x:%02x, y:%02x)\n", mp4_state->hdr.mb_xpos, mp4_state->hdr.mb_ypos);
    fprintf(g_mpeg4_ctx->fp_mb_info, "mb_pos: (x:%02x, y:%02x)\n", mp4_state->hdr.mb_xpos, mp4_state->hdr.mb_ypos);
    fprintf(g_mpeg4_ctx->fp_mb_cfg_data, "mb_pos: (x:%02x, y:%02x), dec_frame_num: %d\n", mp4_state->hdr.mb_xpos,
            mp4_state->hdr.mb_ypos, g_mpeg4_ctx->dec_frame_num);
#endif
}

// return 1 if succesfull
// return 0 on error or if derived_mb_type is stuffing
int macroblock_i_vop(reference_t *ref)
{
    mp4_stream_t *ld = ref->ld;
    struct mp4_state *mp4_state = ref->mp4_state;

    int j;

    int intraFlag;

    dump_mb_xy_for_trace(mp4_state);

    // 1. decode macroblock header info
    mp4_state->hdr.not_coded = 0;
    mp4_state->hdr.mcbpc = get_mcbpc_i_vop(ld); // mcbpc
    mp4_state->hdr.derived_mb_type = mp4_state->hdr.mcbpc & 7;

    mp4_state->hdr.cbpc = (mp4_state->hdr.mcbpc >> 4) & 3;

    mp4_state->modemap[(mp4_state->hdr.mb_ypos + 1) * mp4_state->modemap_stride + mp4_state->hdr.mb_xpos + 1] =
        mp4_state->hdr.derived_mb_type; // [Review] used in P-VOPs and to identify the video packet boundaries
    intraFlag = 1;
    // interFlag = 0;

    mp4_state->codedmap[mp4_state->hdr.mb_ypos * mp4_state->codedmap_stride + mp4_state->hdr.mb_xpos] = 0;
    mp4_state->fieldpredictedmap[(mp4_state->hdr.mb_ypos + 1) * mp4_state->fieldpredictedmap_stride + mp4_state->hdr.mb_xpos + 1] = 0;

    // Clear MVs buffer when I VOP by Robert Yuan,Sept 24,2003
    if (!mp4_state->hdr.interlaced) {
        memset(&mp4_state->mv[(mp4_state->hdr.mb_ypos + 1) * mp4_state->MV_stride + mp4_state->hdr.mb_xpos + 1][0], 0,
               4 * sizeof(motion_vector_t));
    } else {
        memset(&mp4_state->mv[(mp4_state->hdr.mb_ypos + 1) * mp4_state->MV_stride + mp4_state->hdr.mb_xpos + 1][0], 0,
               4 * sizeof(motion_vector_t));
        memset(&mp4_state->mv_field[(mp4_state->hdr.mb_ypos + 1) * mp4_state->MV_stride + mp4_state->hdr.mb_xpos + 1][0], 0,
               4 * sizeof(motion_vector_t));
    }

    // If derived_mb_type is STUFFING in I-VOP, don't get one bit (Berg Xing, Sept 28.2003)
    if ((!mp4_state->hdr.short_video_header) && (mp4_state->hdr.derived_mb_type == INTRA || mp4_state->hdr.derived_mb_type == INTRA_Q))
        mp4_state->hdr.ac_pred_flag = getbits(ld, 1);

    if (mp4_state->hdr.derived_mb_type == STUFFING) {
        mp4_state->hdr.cbp = mp4_state->hdr.cbpc;
        return 0;
    }

    mp4_state->hdr.cbpy = get_cbpy(ld, intraFlag); // cbpy
    mp4_state->hdr.cbp = (mp4_state->hdr.cbpy << 2) | mp4_state->hdr.cbpc;

    // Added by Robert Yuan in Sept 11,2003
    mp4_state->hdr.use_intra_dc_vlc = get_use_intra_dc_vlc(mp4_state->hdr.quantizer, mp4_state->hdr.intra_dc_vlc_thr);

    if (mp4_state->hdr.derived_mb_type == INTRA_Q) {
        mp4_state->hdr.dquant = getbits(ld, 2);
        mp4_state->hdr.quantizer += DQtab[mp4_state->hdr.dquant];
        if (mp4_state->hdr.quantizer > 31)
            mp4_state->hdr.quantizer = 31;
        else if (mp4_state->hdr.quantizer < 1)
            mp4_state->hdr.quantizer = 1;
    }

    if (mp4_state->hdr.interlaced)
        interlaced_information(ld, mp4_state);

    if (mp4_state->hdr.packetnum != 0)
        rescue_predict(mp4_state);

    // 2. decode 6 8x8 blocks, texture decoding add
    for (j = 0; j < 6; j++) {
        int coded = mp4_state->hdr.cbp & (1 << (5 - j));
#ifdef MPEG4_DUMP_ENABLE
        fprintf(g_mpeg4_ctx->fp_mb_coeff, "block(%d), coded: %d\n", j, coded);
#endif
        if (!block_intra(mp4_state->frame_to_decode, ld, mp4_state, &ld->block[0], j, (coded != 0))) {
            if (mp4_state->hdr.mb_xpos < (mp4_state->mb_width - 1)) {
                mp4_state->hdr.mb_xpos++;
            } else {
                mp4_state->hdr.mb_ypos++;
                mp4_state->hdr.mb_xpos = 0;
            }
            return 0;
        }
    }

    mp4_state->quant_store[(mp4_state->hdr.mb_ypos + 1) * mp4_state->quant_store_stride + mp4_state->hdr.mb_xpos + 1] =
        mp4_state->hdr.quantizer;

    if (mp4_state->hdr.mb_xpos < (mp4_state->mb_width - 1)) {
        mp4_state->hdr.mb_xpos++;
    } else {
        mp4_state->hdr.mb_ypos++;
        mp4_state->hdr.mb_xpos = 0;
    }

    return 1;
}

void interlaced_information(mp4_stream_t *ld, struct mp4_state *mp4_state)
{
    if ((mp4_state->hdr.derived_mb_type == INTRA) || (mp4_state->hdr.derived_mb_type == INTRA_Q) || (mp4_state->hdr.cbp != 0))
        mp4_state->hdr.dct_type = getbits(ld, 1);

    // review: check this IF expression
    if (((mp4_state->hdr.prediction_type == P_VOP) &&
         ((mp4_state->hdr.derived_mb_type == INTER) || (mp4_state->hdr.derived_mb_type == INTER_Q))) ||
        ((mp4_state->hdr.sprite_usage == GMC_SPRITE) && (mp4_state->hdr.prediction_type == S_VOP) && (mp4_state->hdr.derived_mb_type < 2) &&
         (!mp4_state->hdr.mcsel)) ||
        ((mp4_state->hdr.prediction_type == B_VOP) && (mp4_state->hdr.mb_type != MB_TYPE_1))) {
        mp4_state->hdr.field_prediction = getbits(ld, 1);
    } else {
        mp4_state->hdr.field_prediction = 0;
    }

    if (mp4_state->hdr.field_prediction) {
        if ((mp4_state->hdr.prediction_type == P_VOP) || (mp4_state->hdr.prediction_type == S_VOP) ||
            ((mp4_state->hdr.prediction_type == B_VOP) && (mp4_state->hdr.mb_type != MB_TYPE_001))) {
            mp4_state->hdr.forward_top_field_reference = getbits(ld, 1);
            mp4_state->hdr.forward_bottom_field_reference = getbits(ld, 1);
        }
        if ((mp4_state->hdr.prediction_type == B_VOP) && (mp4_state->hdr.mb_type != MB_TYPE_0001)) {
            mp4_state->hdr.backward_top_field_reference = getbits(ld, 1);
            mp4_state->hdr.backward_bottom_field_reference = getbits(ld, 1);
        }
        if (mp4_state->hdr.prediction_type != B_VOP) {
            mp4_state->fieldrefmap[2 * mp4_state->hdr.mb_xpos + mp4_state->hdr.mb_ypos * mp4_state->fieldrefmap_stride] =
                mp4_state->hdr.forward_top_field_reference;
            mp4_state->fieldrefmap[2 * mp4_state->hdr.mb_xpos + mp4_state->hdr.mb_ypos * mp4_state->fieldrefmap_stride + 1] =
                mp4_state->hdr.forward_bottom_field_reference;
        }
    }
}

void interlaced_information_bvop(mp4_stream_t *ld, struct mp4_state *mp4_state)
{
    if (mp4_state->hdr.cbp != 0)
        mp4_state->hdr.dct_type = getbits(ld, 1);

    // review: check this IF expression
    if (mp4_state->hdr.mb_type != MB_TYPE_1)
        mp4_state->hdr.field_prediction = getbits(ld, 1);
    else
        mp4_state->hdr.field_prediction = 0;

    if (mp4_state->hdr.field_prediction) {
        if (mp4_state->hdr.mb_type != MB_TYPE_001) {
            mp4_state->hdr.forward_top_field_reference = getbits(ld, 1);
            mp4_state->hdr.forward_bottom_field_reference = getbits(ld, 1);
        }
        if (mp4_state->hdr.mb_type != MB_TYPE_0001) {
            mp4_state->hdr.backward_top_field_reference = getbits(ld, 1);
            mp4_state->hdr.backward_bottom_field_reference = getbits(ld, 1);
        }
    }
}

// return 1 if succesfull
// return 0 on error or if derived_mb_type is stuffing
int macroblock_p_vop(reference_t *ref)
{
    mp4_stream_t *ld = ref->ld;
    struct mp4_state *mp4_state = ref->mp4_state;
    int j;
    int intraFlag, interFlag;

    int mb_xpos = mp4_state->hdr.mb_xpos;
    int mb_ypos = mp4_state->hdr.mb_ypos;
    mp4_state->hdr.mcsel = 0;

    dump_mb_xy_for_trace(mp4_state);

    mp4_state->hdr.not_coded = getbits(ld, 1);

    mp4_state->codedmap[mb_ypos * mp4_state->codedmap_stride + mb_xpos] = mp4_state->hdr.not_coded;

    // coded macroblock or I-VOP
    if (!mp4_state->hdr.not_coded) {
        mp4_state->hdr.mcbpc = get_mcbpc_p_vop(ld); // mcbpc
        mp4_state->hdr.derived_mb_type = mp4_state->hdr.mcbpc & 7;
        mp4_state->hdr.cbpc = (mp4_state->hdr.mcbpc >> 4) & 3;

        mp4_state->modemap[(mb_ypos + 1) * mp4_state->modemap_stride + mb_xpos + 1] =
            mp4_state->hdr.derived_mb_type; // [Review] used in P-VOPs and to identify the video packet boundaries
        intraFlag = ((mp4_state->hdr.derived_mb_type == INTRA) || (mp4_state->hdr.derived_mb_type == INTRA_Q)) ? 1 : 0;
        interFlag = (!intraFlag);

        if ((mp4_state->hdr.prediction_type == S_VOP) && (mp4_state->hdr.sprite_usage == GMC_SPRITE)) {
            if ((mp4_state->hdr.derived_mb_type == INTER) || (mp4_state->hdr.derived_mb_type == INTER_Q))
                mp4_state->hdr.mcsel = getbits1(ld);
#ifdef MPEG4_DUMP_ENABLE
            fprintf(g_mpeg4_ctx->fp_mb_info, "  S vop, mcsel: %d\n", mp4_state->hdr.mcsel);
#endif
        }

        if ((intraFlag) && (!mp4_state->hdr.short_video_header))
            mp4_state->hdr.ac_pred_flag = getbits(ld, 1);

        if (mp4_state->hdr.derived_mb_type == STUFFING)
            return 0;

        mp4_state->hdr.cbpy = get_cbpy(ld, intraFlag); // cbpy
        mp4_state->hdr.cbp = (mp4_state->hdr.cbpy << 2) | mp4_state->hdr.cbpc;

        // Added by Robert Yuan in Sept 11,2003
        mp4_state->hdr.use_intra_dc_vlc = get_use_intra_dc_vlc(mp4_state->hdr.quantizer, mp4_state->hdr.intra_dc_vlc_thr);

        if ((mp4_state->hdr.derived_mb_type == INTER_Q) || (mp4_state->hdr.derived_mb_type == INTRA_Q)) {
#ifdef MPEG4_DUMP_ENABLE
            fprintf(g_mpeg4_ctx->fp_mb_info, "  %s\n", mb_type_name[mp4_state->hdr.derived_mb_type]);
#endif
            mp4_state->hdr.dquant = getbits(ld, 2);
            mp4_state->hdr.quantizer += DQtab[mp4_state->hdr.dquant];
            if (mp4_state->hdr.quantizer > 31)
                mp4_state->hdr.quantizer = 31;
            else if (mp4_state->hdr.quantizer < 1)
                mp4_state->hdr.quantizer = 1;
        }

        if (mp4_state->hdr.interlaced) {
            interlaced_information(ld, mp4_state);
            mp4_state->fieldpredictedmap[(mb_ypos + 1) * mp4_state->fieldpredictedmap_stride + (mb_xpos + 1)] =
                (mp4_state->hdr.field_prediction == 1) ? 1 : 0;
        }

        if ((mp4_state->hdr.derived_mb_type == INTER || mp4_state->hdr.derived_mb_type == INTER_Q)) {
#ifdef MPEG4_DUMP_ENABLE
            fprintf(g_mpeg4_ctx->fp_mb_info, "  %s\n", mb_type_name[mp4_state->hdr.derived_mb_type]);
#endif
            if (!mp4_state->hdr.mcsel) {
                if (!mp4_state->hdr.interlaced) {
                    if (!set_mv(ld, mp4_state, mb_xpos, mb_ypos, -1))
                        return 0; // mv
                } else {
                    if (!set_mv_interlaced(ld, mp4_state, mb_xpos, mb_ypos, 0 /* frame mb or top field mv */))
                        return 0;

                    if (mp4_state->hdr.field_prediction) {
                        // fixme: here I read the field motion vectors, I need to put them in a separate structure
                        if (!set_mv_interlaced(ld, mp4_state, mb_xpos, mb_ypos, 1 /* bottom field mv */))
                            return 0;
                    }
                }
            } else {
#ifdef MPEG4_DUMP_ENABLE
                fprintf(g_mpeg4_ctx->fp_mb_info, "  %d\n", mp4_state->hdr.mcsel);
#endif
                memset(&mp4_state->mv[(mb_ypos + 1) * mp4_state->MV_stride + mb_xpos + 1][0], 0, 4 * sizeof(motion_vector_t));
                memset(&mp4_state->mv_field[(mb_ypos + 1) * mp4_state->MV_stride + mb_xpos + 1][0], 0, 4 * sizeof(motion_vector_t));
            }
        } else if (mp4_state->hdr.derived_mb_type == INTER4V) {
#ifdef MPEG4_DUMP_ENABLE
            fprintf(g_mpeg4_ctx->fp_mb_info, "%s\n", mb_type_name[mp4_state->hdr.derived_mb_type]);
#endif

            for (j = 0; j < 4; j++) {
                if (!set_mv(ld, mp4_state, mb_xpos, mb_ypos, j))
                    return 0; // mv
            }
        } else { // INTRA
#ifdef MPEG4_DUMP_ENABLE
            fprintf(g_mpeg4_ctx->fp_mb_info, "  intra\n");
#endif
            if (!mp4_state->hdr.interlaced) {
                int i;
                for (i = 0; i < 4; i++) {
                    mp4_state->mv[(mb_ypos + 1) * mp4_state->MV_stride + mb_xpos + 1][i].x = 0;
                    mp4_state->mv[(mb_ypos + 1) * mp4_state->MV_stride + mb_xpos + 1][i].y = 0;
                }
            } else {
                int i;
                for (i = 0; i < 4; i++) {
                    mp4_state->mv[(mb_ypos + 1) * mp4_state->MV_stride + mb_xpos + 1][i].x = 0;
                    mp4_state->mv[(mb_ypos + 1) * mp4_state->MV_stride + mb_xpos + 1][i].y = 0;
                    mp4_state->mv_field[(mb_ypos + 1) * mp4_state->MV_stride + mb_xpos + 1][i].x = 0;
                    mp4_state->mv_field[(mb_ypos + 1) * mp4_state->MV_stride + mb_xpos + 1][i].y = 0;
                }
            }
        }

        // motion compensation
        if (interFlag) {
            // 1) MC

            if (mp4_state->do_mc) {
                if (mp4_state->hdr.mcsel) {
                    // set_gmc_mv is used for mv pred in next mb
                    mp4_state->set_gmc_mv(mp4_state);
                    // mp4_state->reconstruct_skip(ref);
                } else {
                    reconstruct(ref);
                }
            }

            // 2) IQ/IDCT and reconstruct, texture decoding add
            for (j = 0; j < 6; j++) {
                int coded = mp4_state->hdr.cbp & (1 << (5 - j));
                mp4_state->mpeg_coef_matrix_no[j] = 0;

                if (coded) {
                    if (!block_inter(ref, ld, mp4_state, &ld->block[0], j, (coded != 0)))
                        return 0;
                }
            }

        } else {
            rescue_predict(mp4_state);
            for (j = 0; j < 6; j++) {
                int coded = mp4_state->hdr.cbp & (1 << (5 - j));

                if (!block_intra(mp4_state->frame_to_decode, ld, mp4_state, &ld->block[0], j, (coded != 0)))
                    return 0;
            }
        }
    } else { // not coded macroblock
#ifdef MPEG4_DUMP_ENABLE
        fprintf(g_mpeg4_ctx->fp_mb_info, "  not_coded mb\n");
#endif
        mp4_state->hdr.derived_mb_type = INTER;
        if (!mp4_state->hdr.interlaced) {
            memset(&mp4_state->mv[(mb_ypos + 1) * mp4_state->MV_stride + mb_xpos + 1][0], 0, 4 * sizeof(motion_vector_t));
        } else {
            memset(&mp4_state->mv[(mb_ypos + 1) * mp4_state->MV_stride + mb_xpos + 1][0], 0, 4 * sizeof(motion_vector_t));
            memset(&mp4_state->mv_field[(mb_ypos + 1) * mp4_state->MV_stride + mb_xpos + 1][0], 0, 4 * sizeof(motion_vector_t));
        }

        if ((mp4_state->hdr.prediction_type == S_VOP) && (mp4_state->hdr.sprite_usage == GMC_SPRITE)) {
#ifdef MPEG4_DUMP_ENABLE
            fprintf(g_mpeg4_ctx->fp_mb_info, "  SVOP, GMC_SPRITE\n");
            fprintf(g_mpeg4_ctx->fp_mb_info, "  gmc luma mv(%ld %ld)\n", mp4_state->gmc_lum_mv_x, mp4_state->gmc_lum_mv_y);
            fprintf(g_mpeg4_ctx->fp_mb_info, "  gmc luma mv(%ld %ld)\n", mp4_state->gmc_chrom_mv_x, mp4_state->gmc_chrom_mv_y);
#endif
            mp4_state->set_gmc_mv(mp4_state);
            // if (mp4_state->do_mc) {
            //     mp4_state->reconstruct_skip(ref);
            // }
        } else {
            // otherwise the data in this macroblock is exactly the same as 2 frames ago, no need to do reconstruction
            // reconstruct_skip(ref);
        }
        mp4_state->modemap[(mb_ypos + 1) * mp4_state->modemap_stride + mb_xpos + 1] =
            NOT_CODED; // [Review] used in P-VOPs and to identify the video packet boundaries
        mp4_state->fieldpredictedmap[(mb_ypos + 1) * mp4_state->fieldpredictedmap_stride + (mb_xpos + 1)] = 0;
    }

    mp4_state->quant_store[(mb_ypos + 1) * mp4_state->quant_store_stride + mb_xpos + 1] = mp4_state->hdr.quantizer;

    if (mp4_state->hdr.mb_xpos < (mp4_state->mb_width - 1)) {
        mp4_state->hdr.mb_xpos++;
    } else {
        mp4_state->hdr.mb_ypos++;
        mp4_state->hdr.mb_xpos = 0;
    }

    return 1;
}

int get_next_next_slice_mba(reference_t *ref, int nbits)
{
    struct mp4_state *mp4_state = ref->mp4_state;
    mp4_stream_t *ld = ref->ld;
    int byte_offset1;
    char t1, t2, t3, t4;
    unsigned int value;
    int m_mbaSize, i, j;

    if (!mp4_state->packet_format) {
        const unsigned char *mrdptr;
        int find_resync = 0;

        mrdptr = ld->rdptr - 4 - (32 - ld->bitcnt) / 8;
        while (mrdptr < (ld->startptr + ld->length - 4)) {
            if (*mrdptr == 0x00 && *(mrdptr + 1) == 0x00 && (*(mrdptr + 2) & 0x80) == 0x80) {
                find_resync = 1;
                break;
            }
            mrdptr++;
        }

        if (!find_resync) {
            value = mp4_state->mb_height * mp4_state->mb_width;
            return value;
        }

        j = mp4_state->hdr.mba_size - 1;
        for (i = 0; i < 6 && MBA_NumMBs[i] < j; i++) {

        }
        if (i < 6)
            m_mbaSize = MBA_FieldWidth[i];
        else
            logw("connot find this idx: %d", i);

        value = ((*(mrdptr + 2)) << 8) | (*(mrdptr + 3));
        value &= 0x3fff;
        value >>= 14 - m_mbaSize;
    } else {
        if (mp4_state->hdr.gob_number != (mp4_state->packet_num - 1)) {
            byte_offset1 = mp4_state->data_offset[mp4_state->hdr.gob_number + 1];
            if (!byte_offset1) {
                value = mp4_state->mb_height * mp4_state->mb_width;
                return value;
            }
        } else {
            value = mp4_state->mb_height * mp4_state->mb_width;
            return value;
        }

        t1 = *(ref->ld->startptr + byte_offset1 + 2);
        t2 = *(ref->ld->startptr + byte_offset1 + 3);
        t3 = *(ref->ld->startptr + byte_offset1 + 4);
        t4 = *(ref->ld->startptr + byte_offset1 + 5);
        value = (t1 << 24) | (t2 << 16) | (t3 << 8) | t4;
        if (m_pctszSize > 0)
            value <<= m_pctszSize;
        value >>= (32 - nbits);
    }
    return value;
}


int get_ssc(reference_t *ref)
{
    mp4_stream_t *ld = ref->ld;
    unsigned int w, w0;
    int n;
    unsigned int msb = (1 << 17) - 1;

    w0 = showbits(ld, 24);
    w = w0 >> 7;

    for (n = 0; w != 1 && n < 7; n++) {
        if (w)
            break;
        w = w0 >> (6 - n);
        w &= msb;
    }

    if (w == 1) {
        flushbits(ld, 17 + n);
        return (0);
    } else {
        return (-1);
    }
    return -1;
}

// Purpose: generic macroblock function - used for short header decoding
int macroblock(reference_t *ref)
{
    mp4_stream_t *ld = ref->ld;
    struct mp4_state *mp4_state = ref->mp4_state;
    int j;
    int intraFlag, interFlag;

    int mb_xpos = mp4_state->hdr.mb_xpos;
    int mb_ypos = mp4_state->hdr.mb_ypos;
    memset(mp4_state->mpeg_coef_matrix, 0, 6 * 64 * sizeof(unsigned long));

    if (mb_xpos == 0 && mb_ypos >= 1 && mp4_frame_ctr == 1)
        j = 0;

    if (mb_xpos < (mp4_state->mb_width - 1)) {
        if (!mb_xpos)
            mp4_state->hdr.gob_edge.ileft = 1;
    }

after_stuffing:
    if (mp4_state->hdr.prediction_type != I_VOP)
        mp4_state->hdr.not_coded = getbits(ld, 1);
    else
        mp4_state->hdr.not_coded = 0;

    mp4_state->codedmap[mb_ypos * mp4_state->codedmap_stride + mb_xpos] = mp4_state->hdr.not_coded;

    // coded macroblock or I-VOP
    if ((!mp4_state->hdr.not_coded) || mp4_state->hdr.prediction_type == I_VOP) {
        if (mp4_state->hdr.prediction_type == I_VOP) {
            mp4_state->hdr.mcbpc = get_mcbpc_i_vop(ld); // mcbpc
        } else {
            mp4_state->hdr.mcbpc = get_mcbpc_p_vop(ld); // mcbpc
        }
        mp4_state->hdr.derived_mb_type = mp4_state->hdr.mcbpc & 7;

        // Added the stuffing MB process by Robert Yuan in Sept 15,2003
        if (mp4_state->hdr.derived_mb_type == 7)
            goto after_stuffing;

        mp4_state->hdr.cbpc = (mp4_state->hdr.mcbpc >> 4) & 3;

        mp4_state->modemap[(mb_ypos + 1) * mp4_state->modemap_stride + mb_xpos + 1] =
            mp4_state->hdr.derived_mb_type; // [Review] used in P-VOPs and to identify the video packet boundaries

        if (mp4_state->hdr.derived_mb_type == NOT_VALID) {
            loge("Invalid mb_type:%d", mp4_state->hdr.derived_mb_type);
            return -1;
        }

        intraFlag = ((mp4_state->hdr.derived_mb_type == INTRA) || (mp4_state->hdr.derived_mb_type == INTRA_Q)) ? 1 : 0;
        interFlag = (!intraFlag);

        if ((intraFlag) && (!mp4_state->hdr.short_video_header || mp4_state->hdr.h263_aic)) {
            mp4_state->hdr.ac_pred_flag = getbits(ld, 1);
            if (mp4_state->hdr.h263_aic && mp4_state->hdr.ac_pred_flag)
                mp4_state->hdr.h263_aic_dir = getbits(ld, 1); // 1: AINTRA_Mode_horiz, 0:AINTRA_MODE_VERT
        } else {
            mp4_state->hdr.ac_pred_flag = 0;
        }
        if (mp4_state->hdr.derived_mb_type != STUFFING) {

            mp4_state->hdr.cbpy = get_cbpy(ld, intraFlag); // cbpy
            mp4_state->hdr.cbp = (mp4_state->hdr.cbpy << 2) | mp4_state->hdr.cbpc;
        } else {
            return 0; // stuffing bits - return
        }
        // Added by Robert Yuan in Sept 11,2003
        mp4_state->hdr.use_intra_dc_vlc = get_use_intra_dc_vlc(mp4_state->hdr.quantizer, mp4_state->hdr.intra_dc_vlc_thr);

        if (mp4_state->hdr.derived_mb_type == INTER_Q || mp4_state->hdr.derived_mb_type == INTRA_Q) {

            if (mp4_state->hdr.modified_qantization) {

                if (getbits(ld, 1)) {
                    mp4_state->hdr.dquant = gNewTAB_DQUANT_MQ[mp4_state->hdr.quantizer][getbits(ld, 1)];
                    mp4_state->hdr.quantizer += mp4_state->hdr.dquant;

                } else {
                    int m_tmp;
                    m_tmp = mp4_state->hdr.quantizer;
                    mp4_state->hdr.quantizer = getbits(ld, 5);
                    mp4_state->hdr.dquant = mp4_state->hdr.quantizer - m_tmp;
                }
            } else {
                mp4_state->hdr.dquant = getbits(ld, 2);
                mp4_state->hdr.quantizer += DQtab[mp4_state->hdr.dquant];
                if (mp4_state->hdr.quantizer > 31)
                    mp4_state->hdr.quantizer = 31;
                else if (mp4_state->hdr.quantizer < 1)
                    mp4_state->hdr.quantizer = 1;
            }
        }
        if (mp4_state->hdr.derived_mb_type == INTER || mp4_state->hdr.derived_mb_type == INTER_Q) {

            if (!set_mv(ld, mp4_state, mp4_state->hdr.mb_xpos, mp4_state->hdr.mb_ypos, -1))
                return 0; // mv
        } else if (mp4_state->hdr.derived_mb_type == INTER4V) {
            for (j = 0; j < 4; j++) {

                if (!set_mv(ld, mp4_state, mp4_state->hdr.mb_xpos, mp4_state->hdr.mb_ypos, j))
                    return 0; // mv
            }
        } else {// intra
            if (mp4_state->hdr.prediction_type == P_VOP)
                memset(&mp4_state->mv[(mp4_state->hdr.mb_ypos + 1) * mp4_state->MV_stride + mp4_state->hdr.mb_xpos + 1][0], 0,
                       4 * sizeof(motion_vector_t));
        }

        // motion compensation
        if (interFlag) {
            reconstruct(ref);
            // texture decoding add
            for (j = 0; j < 6; j++) {
                int coded = mp4_state->hdr.cbp & (1 << (5 - j));

                mp4_state->mpeg_coef_matrix_no[j] = 0;
                if (coded) {
                    block_inter(ref, ld, mp4_state, &ld->block[0], j, (coded != 0));
                }
            }
        } else {
            if ((mp4_state->hdr.prediction_type == P_VOP) || (mp4_state->hdr.prediction_type == S_VOP) || (mp4_state->hdr.packetnum != 0))
                rescue_predict(mp4_state);

            // texture decoding add
            for (j = 0; j < 6; j++) {
                int coded = mp4_state->hdr.cbp & (1 << (5 - j));

                block_intra(mp4_state->frame_to_decode, ld, mp4_state, &ld->block[0], j, (coded != 0));
            }
        }
    } else { // not coded macroblock

        memset(&mp4_state->mv[(mb_ypos + 1) * mp4_state->MV_stride + mb_xpos + 1][0], 0, 4 * sizeof(motion_vector_t));
        // Add the valid condition  by Robert Yuan in Sept 13,2003
        mp4_state->modemap[(mb_ypos + 1) * mp4_state->modemap_stride + mb_xpos + 1] = 0;
        mp4_state->hdr.derived_mb_type = INTER;
        // reconstruct_skip(ref);
    }

    if (mp4_state->hdr.gob_edge.ileft)
        mp4_state->hdr.gob_edge.ileft--;
    if (mp4_state->hdr.gob_edge.iabove)
        mp4_state->hdr.gob_edge.iabove--;
    mp4_state->quant_store[(mp4_state->hdr.mb_ypos + 1) * mp4_state->quant_store_stride + mp4_state->hdr.mb_xpos + 1] =
        mp4_state->hdr.quantizer;

    if (mp4_state->hdr.mb_xpos < (mp4_state->mb_width - 1)) {
        mp4_state->hdr.mb_xpos++;
    } else {
        mp4_state->hdr.mb_ypos++;
        mp4_state->hdr.mb_xpos = 0;
    }

    return 1;
}

int getgobhdr(reference_t *ref, int gob_index)
{
    mp4_stream_t *ld = ref->ld;
    struct mp4_state *mp4_state = ref->mp4_state;
    int sliceMba = 0, i, j, m_mbaSize, next_sliceMba;
    int ret;
    int display_time = 0;

    if (mp4_state->hdr.slice_structured) {
        mp4_state->hdr.gob_edge.iabove = mp4_state->mb_width;
        mp4_state->hdr.gob_edge.ileft = 1;
    }

    mp4_state->hdr.gob_header_emtpy = 1;

    if (mp4_state->hdr.gob_number != 0 || mp4_state->if_rm_h263) {
        if (mp4_state->if_rm_h263) {
            getbits(ld, 1);
            j = mp4_state->hdr.mba_size - 1;
            for (i = 0; i < 6 && MBA_NumMBs[i] < j; i++) {

            }

            if (i < 6)
                m_mbaSize = MBA_FieldWidth[i];
            else
                logw("connot find this idx: %d", i);

            sliceMba = getbits(ld, m_mbaSize);
            if (m_mbaSize > 11) {
                /* Must be 1 to prevent start code emulation (SEPB2) */
                getbits(ld, 1);
            }
            if (mp4_state->hdr.gob_number) {
                mp4_state->hdr.vop_quant = getbits(ld, 5);
                mp4_state->hdr.quantizer = mp4_state->hdr.vop_quant;
                mp4_state->hdr.quant_scale = mp4_state->hdr.vop_quant;
            }

            /* Must be 1 to prevent start code emulation (SEPB3) */
            getbits(ld, 1);

            /* Get GOB frame ID. */
            if (mp4_state->hdr.gob_number)
                getbits(ld, 2);

            if (!gob_index)
                set_frame_to_display(ref);

            if (mp4_state->hdr.h263_aic)
                mp4_state->vld_intra_fun = vld_intra_aic_dct;
            else
                mp4_state->vld_intra_fun = vld_rmg2_intra_dct;

            if (mp4_state->hdr.picture_coding_type != B_VOP && !gob_index) {

                mp4_state->hdr.display_time_prev = mp4_state->hdr.display_time_next;
                mp4_state->hdr.display_time_next = display_time;

                if (display_time != mp4_state->hdr.display_time_prev) {
                    mp4_state->hdr.trd = mp4_state->hdr.display_time_next - mp4_state->hdr.display_time_prev;
                    if (mp4_state->hdr.trd < 0)
                        mp4_state->hdr.trd += 8192; // for rmvb89, it should be 256
                }
            } else {
                mp4_state->hdr.trb = display_time - mp4_state->hdr.display_time_prev;
                if (mp4_state->hdr.trb < 0)
                    mp4_state->hdr.trb += 8192;
            }
            // try to get next slize Mba to calcument mb numbers in current gob
            next_sliceMba = get_next_next_slice_mba(ref, m_mbaSize);
            mp4_state->hdr.num_mb_in_gob = next_sliceMba - sliceMba;

            mp4_state->hdr.macroblock_number = sliceMba;

            mp4_set_packet_info(mp4_state);
        } else {
            if (nextbits(ld, 17) == RESYNC_MARKER) {
                if (getbits(ld, 17) != RESYNC_MARKER) {
                    return 0; // resync marker not found!
                }
                mp4_state->hdr.gob_number = getbits(ld, 5);
                mp4_state->hdr.gob_frame_id = getbits(ld, 2);
                mp4_state->hdr.quant_scale = getbits(ld, 5);
                mp4_state->hdr.quantizer = mp4_state->hdr.quant_scale; // Added by Robert Yuan in Sept 8,2003
                mp4_state->hdr.use_intra_dc_vlc =
                    get_use_intra_dc_vlc(mp4_state->hdr.quantizer, mp4_state->hdr.intra_dc_vlc_thr); // Added by Robert Yuan in Sept 10,2003
                mp4_state->hdr.gob_header_emtpy = 0;
            }
        }
    }

    // Mask the GOB boundary by Robert Yuan in Sept 13,2003
    if (!mp4_state->hdr.gob_header_emtpy) {
        mark_packet_boundary(mp4_state, mp4_state->hdr.mba);
        mp4_state->hdr.gob_header_emtpy = 0;
    }

    if (mp4_state->if_rm_h263) {
        i = mp4_state->hdr.mba = sliceMba;
        for (; i < mp4_state->hdr.mba_size; i++) {
            // decide slice data is over, if over, break for next gob
            if (mp4_state->hdr.slice_structured && get_ssc(ref))
                goto decode_slice_end;
            for (j = 0; j < 6; j++) {
                mp4_state->mpeg_coef_matrix_no[j] = 0;
            }

            ret = 0;
            while (!ret && mp4_state->hdr.mba < mp4_state->hdr.mba_size) {
                if (ref->mp4_state->hdr.prediction_type != B_VOP)
                    ret = macroblock(ref);
                else
                    ret = macroblock_h263_bvop(ref);
            };

            mp4_set_mb_info(mp4_state);

            mp4_state->hdr.mba++;
            mp4_state->hdr.macroblock_number = mp4_state->hdr.mba;
        }
    } else {
#ifdef MPEG4_DUMP_ENABLE
        mp4_set_packet_info(mp4_state);
#endif
        for (i = 0; i < mp4_state->hdr.num_mb_in_gob; i++) {
            for (j = 0; j < 6; j++) {
                mp4_state->mpeg_coef_matrix_no[j] = 0;
            }

            while (!macroblock(ref)) {
                mp4_set_mb_info(mp4_state); // storing stuffing MB information
            };

            mp4_set_mb_info(mp4_state);

            mp4_state->hdr.mba++;
            mp4_state->hdr.macroblock_number = mp4_state->hdr.mba;
        }
    }


    if ((nextbits(ld, 17) != RESYNC_MARKER) && (nextbits_bytealigned(ld, 17, 1) == RESYNC_MARKER)) {
        bytealign(ld);
    }
decode_slice_end:

    mp4_state->hdr.gob_number++;

    return 1;
}
