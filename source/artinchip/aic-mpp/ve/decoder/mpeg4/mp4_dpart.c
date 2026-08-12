/*
 * Copyright (C) 2020-2025 ArtInChip Technology Co. Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 *  author: <xiaodong.zhao@artinchip.com>
 *  Desc: mpeg4 dpart interface
 *
 */

#include "mp4_dpart.h"
#include "mp4_block.h"
#include "mp4_getbits.h"
#include "mp4_global.h"
#include "mp4_header.h"
#include "mp4_mblock.h"
#include "mp4_mblock_util.h"
#include "mp4_predict.h"
#include "mp4_recon.h"
#include "mp4_tables.h"
#include "mpeg4_decoder.h"
#include <string.h>

extern void mp4_set_mb_info(struct mp4_state *_mp4_state);

static inline void inc_mbpos(int *mb_xpos, int *mb_ypos, int mb_width)
{
    if ((*mb_xpos) < (mb_width - 1)) {
        (*mb_xpos)++;
    } else {
        (*mb_ypos)++;
        (*mb_xpos) = 0;
    }
}

void restore_dpart_dc_value(short dc_dpart_stored, short *block_dc)
{
    (*block_dc) = dc_dpart_stored;
}

int data_partitioned_i_vop(reference_t *ref)
{
    mp4_stream_t *ld = ref->ld;
    struct mp4_state *mp4_state = ref->mp4_state;
    int packet_mbnum = 0;
    int i;
    int m_quantizer = mp4_state->hdr.quantizer;

    do {
        memset(mp4_state->mpeg_coef_matrix, 0, 6 * 64 * sizeof(unsigned long));
        if (mp4_state->hdr.shape != RECTANGULAR) {
            loge("mp4_state->hdr.shape != RECTANGULAR");
            return -1;
        }
        if (1) { // !transparent
            // Add the loop to process Stuffing by Robert Yuan,Sept 19,2003
            do {
                // Correct processing when having stuffing MB before DC_MARKER by Robert Yuan,Sept 19,2003
                if (nextbits(ld, 19) == DC_MARKER)
                    goto check_dc_marker;
                mp4_state->hdr.a_mcbpc[packet_mbnum] = get_mcbpc_i_vop(ld); // mcbpc
            } while (mp4_state->hdr.a_mcbpc[packet_mbnum] == -1);

            mp4_state->hdr.a_derived_mb_type[packet_mbnum] = mp4_state->hdr.a_mcbpc[packet_mbnum] & 7;
            mp4_state->hdr.a_cbpc[packet_mbnum] = (mp4_state->hdr.a_mcbpc[packet_mbnum] >> 4) & 3;

            // Add use_intra_dc_vlc decision when data partition by Robert Yuan,Oct 3,2003
            mp4_state->hdr.use_intra_dc_vlc = get_use_intra_dc_vlc(m_quantizer, mp4_state->hdr.intra_dc_vlc_thr);
            if (mp4_state->hdr.a_derived_mb_type[packet_mbnum] == INTRA_Q) {
                mp4_state->hdr.a_dquant[packet_mbnum] = getbits(ld, 2);
                // Add use_intra_dc_vlc decision when data partition by Robert Yuan,Oct 3,2003
                m_quantizer += DQtab[mp4_state->hdr.a_dquant[packet_mbnum]];
            }

            if (mp4_state->hdr.use_intra_dc_vlc) {
                int j;
                int dct_dc_size, dct_dc_diff;

                for (j = 0; j < 4; j++) {
                    dct_dc_size = get_dc_size_lum(ld);
                    if (dct_dc_size != 0)
                        dct_dc_diff = get_dc_diff(ld, dct_dc_size);
                    else
                        dct_dc_diff = 0;
                    if (dct_dc_size > 8)
                        getbits1(ld); // marker bit

                    mp4_state->hdr.a_dc_store[packet_mbnum][j] = (short)dct_dc_diff;
                }
                for (j = 4; j < 6; j++) {
                    dct_dc_size = get_dc_size_chr(ld);
                    if (dct_dc_size != 0)
                        dct_dc_diff = get_dc_diff(ld, dct_dc_size);
                    else
                        dct_dc_diff = 0;
                    if (dct_dc_size > 8)
                        getbits1(ld); // marker bit

                    mp4_state->hdr.a_dc_store[packet_mbnum][j] = (short)dct_dc_diff;
                }
            } else {    // Add clear dc buffer when use_intra_dc_vlc=0 by Robert Yuan, Oct 4,2003
                for (int j = 0; j < 6; j++)
                    mp4_state->hdr.a_dc_store[packet_mbnum][j] = 0;
            }
        }

        packet_mbnum++;

check_dc_marker:
        if (packet_mbnum > MAX_MB_PACKET) {
            return 0; // you will never find the DC_MARKER, something went wrong
        }
    } while (nextbits(ld, 19) != DC_MARKER);

    getbits(ld, 19); // dc_marker

    for (i = 0; i < packet_mbnum; i++) {
        if (1) { // !transparent
            int intraFlag = (mp4_state->hdr.a_derived_mb_type[i] >= INTRA);
            mp4_state->hdr.a_ac_pred_flag[i] = getbits(ld, 1);
            mp4_state->hdr.a_cbpy[i] = get_cbpy(ld, intraFlag); // cbpy

            mp4_state->hdr.a_cbp[i] = (mp4_state->hdr.a_cbpy[i] << 2) | mp4_state->hdr.a_cbpc[i];
        }
    }

    for (i = 0; i < packet_mbnum; i++) {
        int j;
        mp4_state->hdr.derived_mb_type = mp4_state->hdr.a_derived_mb_type[i];
        mp4_state->hdr.cbp = mp4_state->hdr.a_cbp[i];
        mp4_state->hdr.not_coded = 0;

        mp4_state->modemap[(mp4_state->hdr.mb_ypos + 1) * mp4_state->modemap_stride + mp4_state->hdr.mb_xpos + 1] =
            mp4_state->hdr.a_derived_mb_type[i];

        // Clear co_located_not_coded flag when I VOP by Robert Yuan,Sept 24,2003
        mp4_state->codedmap[mp4_state->hdr.mb_ypos * mp4_state->codedmap_stride + mp4_state->hdr.mb_xpos] = 0;
        //		mp4_state->modemap[mp4_state->hdr.mb_ypos * mp4_state->codedmap_stride + mp4_state->hdr.mb_xpos] = 0;
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

        mp4_state->hdr.ac_pred_flag = mp4_state->hdr.a_ac_pred_flag[i]; // [intra update]

        // Added by Robert Yuan in Sept 11,2003
        mp4_state->hdr.use_intra_dc_vlc = get_use_intra_dc_vlc(mp4_state->hdr.quantizer, mp4_state->hdr.intra_dc_vlc_thr);
        if (mp4_state->hdr.a_derived_mb_type[i] == INTRA_Q) {
            mp4_state->hdr.quantizer += DQtab[mp4_state->hdr.a_dquant[i]];
            if (mp4_state->hdr.quantizer > 31)
                mp4_state->hdr.quantizer = 31;
            else if (mp4_state->hdr.quantizer < 1)
                mp4_state->hdr.quantizer = 1;
        }

        // texture decoding add
        for (j = 0; j < 6; j++) {
            int coded = mp4_state->hdr.a_cbp[i] & (1 << (5 - j));

            // [Ag] here, restore dc values for this block from the packetmb_num storage
            clearblock(&ld->block[0]);
            if (mp4_state->hdr.packetnum != 0)
                rescue_predict(mp4_state);

            restore_dpart_dc_value(mp4_state->hdr.a_dc_store[i][j], &ld->block[0]);

            if (!block_intra(mp4_state->frame_to_decode, ld, mp4_state, &ld->block[0], j, (coded != 0)))
                return 0;
        }

        mp4_state->quant_store[(mp4_state->hdr.mb_ypos + 1) * mp4_state->quant_store_stride + mp4_state->hdr.mb_xpos + 1] =
            mp4_state->hdr.quantizer;

        // mpeg_qp_buf[mp4_state->hdr.mb_ypos][mp4_state->hdr.mb_xpos] = (unsigned char)mp4_state->hdr.quantizer;
        mp4_set_mb_info(mp4_state);
        inc_mbpos(&mp4_state->hdr.mb_xpos, &mp4_state->hdr.mb_ypos, mp4_state->mb_width);
        mp4_state->hdr.mba++;
    }

    return 1;
}

int data_partitioned_p_vop(reference_t *ref)
{
    mp4_stream_t *ld = ref->ld;
    struct mp4_state *mp4_state = ref->mp4_state;
    int packet_mbnum = 0;
    int mb_xpos, mb_ypos;
    int i;
    int m_quantizer = mp4_state->hdr.quantizer;

    mb_xpos = mp4_state->hdr.mb_xpos;
    mb_ypos = mp4_state->hdr.mb_ypos;

    if (mp4_state->hdr.prediction_type == S_VOP) {
        // loge("gmc sprite not support, maybe, (xuqi !!!");
        if (mp4_state->hdr.sprite_usage != GMC_SPRITE) {
            loge("mp4_state->hdr.sprite_usage != GMC_SPRITE");
            return -1;
        }
    }

    do {
        memset(mp4_state->mpeg_coef_matrix, 0, 6 * 64 * sizeof(unsigned long));
        if (mp4_state->hdr.shape != RECTANGULAR) {
            loge("mp4_state->hdr.shape != RECTANGULAR");
            return -1;
        }
        mp4_state->hdr.a_mcsel[packet_mbnum] = 0;
        if (1) { // !transparent
            if (mp4_state->hdr.mb_ypos >= mp4_state->hdr.mb_ysize)
                break;

            mp4_state->codedmap[mp4_state->hdr.mb_ypos * mp4_state->codedmap_stride + mp4_state->hdr.mb_xpos] =
                mp4_state->hdr.a_not_coded[packet_mbnum] = getbits(ld, 1);

            if (!mp4_state->hdr.a_not_coded[packet_mbnum]) {
                int mcsel = 0;
                mp4_state->hdr.a_mcbpc[packet_mbnum] = get_mcbpc_p_vop(ld); // mcbpc

                mp4_state->hdr.a_derived_mb_type[packet_mbnum] = mp4_state->hdr.a_mcbpc[packet_mbnum] & 7;
                mp4_state->hdr.a_cbpc[packet_mbnum] = (mp4_state->hdr.a_mcbpc[packet_mbnum] >> 4) & 3;

                // Added the condition by Robert Yuan in Sept 13,2003
                if (mp4_state->hdr.a_derived_mb_type[packet_mbnum] == 7) // not stuffing MB
                    continue;

                if (mp4_state->hdr.prediction_type == S_VOP) {
                    if (mp4_state->hdr.a_derived_mb_type[packet_mbnum] == INTER ||
                        mp4_state->hdr.a_derived_mb_type[packet_mbnum] == INTER_Q) {
                        mcsel = mp4_state->hdr.a_mcsel[packet_mbnum] = getbits1(ld);
                    }
                }
                mp4_state->modemap[(mp4_state->hdr.mb_ypos + 1) * mp4_state->modemap_stride + mp4_state->hdr.mb_xpos + 1] =
                    mp4_state->hdr.a_derived_mb_type[packet_mbnum];

                if (mp4_state->hdr.a_derived_mb_type[packet_mbnum] == INTER || mp4_state->hdr.a_derived_mb_type[packet_mbnum] == INTER_Q) {
                    if (!mcsel) {
                        if (!set_mv(ld, mp4_state, mp4_state->hdr.mb_xpos, mp4_state->hdr.mb_ypos, -1))
                            return 0; // mv
                    } else {
                        mp4_state->set_gmc_mv(mp4_state);
                    }
                } else if (mp4_state->hdr.a_derived_mb_type[packet_mbnum] == INTER4V) {
                    for (int j = 0; j < 4; j++)
                        set_mv(ld, mp4_state, mp4_state->hdr.mb_xpos, mp4_state->hdr.mb_ypos, j); // mv
                } else {                                                                         // intra
                    memset(&mp4_state->mv[(mp4_state->hdr.mb_ypos + 1) * mp4_state->MV_stride + mp4_state->hdr.mb_xpos + 1][0], 0,
                           4 * sizeof(motion_vector_t));
                }
            } else {
                if (mp4_state->hdr.prediction_type != S_VOP) {
                    memset(&mp4_state->mv[(mp4_state->hdr.mb_ypos + 1) * mp4_state->MV_stride + mp4_state->hdr.mb_xpos + 1][0], 0,
                           4 * sizeof(motion_vector_t));
                } else {
                    mp4_state->set_gmc_mv(mp4_state);
                }
                mp4_state->hdr.a_derived_mb_type[packet_mbnum] = NOT_CODED;
                mp4_state->modemap[(mp4_state->hdr.mb_ypos + 1) * mp4_state->modemap_stride + mp4_state->hdr.mb_xpos + 1] =
                    NOT_CODED; // [Review] used in P-VOPs and to identify the video packet boundaries
                if (mp4_state->hdr.prediction_type == S_VOP)
                    mp4_state->hdr.a_mcsel[packet_mbnum] = 1;
                else
                    mp4_state->hdr.a_mcsel[packet_mbnum] = 0;
            }
        }

        inc_mbpos(&mp4_state->hdr.mb_xpos, &mp4_state->hdr.mb_ypos, mp4_state->mb_width);
        packet_mbnum++;

        if (packet_mbnum > MAX_MB_PACKET)
            return 0; // you will never find the MOTION_MARKER, something went wrong
    } while (nextbits(ld, 17) != MOTION_MARKER);

    mp4_state->hdr.mb_xpos = mb_xpos;
    mp4_state->hdr.mb_ypos = mb_ypos;

    // Added the stuffing processing by Robert Yuan in Sept 13,2003
    if (showbits(ld, 17) != MOTION_MARKER) {
        while (showbits(ld, 10) == 1)
            getbits(ld, 10); // stuffing
    }
    if (getbits(ld, 17) != MOTION_MARKER) { // motion_marker
        printf("MOTION_MARKER ERROR!\n");
        return 0;
    }

    for (i = 0; i < packet_mbnum; i++) {
        if (1) { // !transparent
            if (!mp4_state->hdr.a_not_coded[i]) {
                int intraFlag = (mp4_state->hdr.a_derived_mb_type[i] >= INTRA);
                if (intraFlag) {
                    mp4_state->hdr.a_ac_pred_flag[i] = getbits(ld, 1);
                }
                mp4_state->hdr.a_cbpy[i] = get_cbpy(ld, intraFlag); // cbpy

                mp4_state->hdr.a_cbp[i] = (mp4_state->hdr.a_cbpy[i] << 2) | mp4_state->hdr.a_cbpc[i];

                // Add use_intra_dc_vlc decision when data partition by Robert Yuan,Oct 3,2003
                mp4_state->hdr.use_intra_dc_vlc = get_use_intra_dc_vlc(m_quantizer, mp4_state->hdr.intra_dc_vlc_thr);
                if ((mp4_state->hdr.a_derived_mb_type[i] == INTER_Q) || (mp4_state->hdr.a_derived_mb_type[i] == INTRA_Q)) {
                    mp4_state->hdr.a_dquant[i] = getbits(ld, 2);
                    // Add use_intra_dc_vlc decision when data partition by Robert Yuan,Oct 3,2003
                    m_quantizer += DQtab[mp4_state->hdr.a_dquant[i]];
                }
                if ((mp4_state->hdr.a_derived_mb_type[i] >= INTRA) && mp4_state->hdr.use_intra_dc_vlc) {
                    int dct_dc_size, dct_dc_diff;

                    for (int j = 0; j < 4; j++) {
                        dct_dc_size = get_dc_size_lum(ld);
                        if (dct_dc_size != 0)
                            dct_dc_diff = get_dc_diff(ld, dct_dc_size);
                        else
                            dct_dc_diff = 0;
                        if (dct_dc_size > 8)
                            getbits1(ld); // marker bit

                        mp4_state->hdr.a_dc_store[i][j] = (short)dct_dc_diff;
                    }
                    for (int j = 4; j < 6; j++) {
                        dct_dc_size = get_dc_size_chr(ld);
                        if (dct_dc_size != 0)
                            dct_dc_diff = get_dc_diff(ld, dct_dc_size);
                        else
                            dct_dc_diff = 0;
                        if (dct_dc_size > 8)
                            getbits1(ld); // marker bit

                        mp4_state->hdr.a_dc_store[i][j] = (short)dct_dc_diff;
                    }
                // Add clear dc buffer when use_intra_dc_vlc=0 by Robert Yuan, Oct 4,2003
                } else {
                    for (int j = 0; j < 6; j++) {
                        mp4_state->hdr.a_dc_store[i][j] = 0;
                    }
                }
            }
        }
    }

    for (i = 0; i < packet_mbnum; i++) {
        int j;

        // Update of the information (ex. quantizer) that
        // weren't updated during the previous parsing loop

        int intraFlag, interFlag;
        mp4_state->hdr.derived_mb_type = mp4_state->hdr.a_derived_mb_type[i];
        intraFlag = ((mp4_state->hdr.derived_mb_type == INTRA) || (mp4_state->hdr.derived_mb_type == INTRA_Q)) ? 1 : 0;
        interFlag = (!intraFlag);

        mp4_state->hdr.derived_mb_type = mp4_state->hdr.a_derived_mb_type[i];
        mp4_state->hdr.cbp = mp4_state->hdr.a_cbp[i];
        mp4_state->hdr.not_coded = mp4_state->hdr.a_not_coded[i];
        mp4_state->hdr.mcsel = mp4_state->hdr.a_mcsel[i];

        // Added by Robert Yuan in Sept 11,2003
        mp4_state->hdr.use_intra_dc_vlc =
            get_use_intra_dc_vlc(mp4_state->hdr.quantizer, mp4_state->hdr.intra_dc_vlc_thr); // [Review] should not be adjusted here
        if ((mp4_state->hdr.derived_mb_type == INTER_Q) || (mp4_state->hdr.derived_mb_type == INTRA_Q)) {
            mp4_state->hdr.quantizer += DQtab[mp4_state->hdr.a_dquant[i]];
            if (mp4_state->hdr.quantizer > 31)
                mp4_state->hdr.quantizer = 31;
            else if (mp4_state->hdr.quantizer < 1)
                mp4_state->hdr.quantizer = 1;
        }

        if (!mp4_state->hdr.a_not_coded[i]) {
            mp4_state->modemap[(mp4_state->hdr.mb_ypos + 1) * mp4_state->modemap_stride + mp4_state->hdr.mb_xpos + 1] =
                mp4_state->hdr.derived_mb_type;

            // motion compensation
            if (interFlag) {
                if (mp4_state->hdr.a_mcsel[i]) {
                    // mp4_state->reconstruct_skip(ref);
                } else {
                    reconstruct(ref);
                }
                // texture decoding add
                for (j = 0; j < 6; j++) {
                    int coded = mp4_state->hdr.a_cbp[i] & (1 << (5 - j));
                    mp4_state->mpeg_coef_matrix_no[j] = 0;

                    if (coded) {
                        clearblock(&ld->block[0]);
                        restore_dpart_dc_value(mp4_state->hdr.a_dc_store[i][j], &ld->block[0]);

                        if (!block_inter(ref, ld, mp4_state, &ld->block[0], j, (coded != 0)))
                            return 0;
                        // addblockInter(ref, &ld->block[0], j, mp4_state->hdr.mb_xpos, mp4_state->hdr.mb_ypos);
                    }
                }
            } else {
                mp4_state->hdr.ac_pred_flag = mp4_state->hdr.a_ac_pred_flag[i]; // [intra update]
                // texture decoding add
                for (j = 0; j < 6; j++) {
                    int coded = mp4_state->hdr.a_cbp[i] & (1 << (5 - j));

                    rescue_predict(mp4_state);
                    clearblock(&ld->block[0]);
                    restore_dpart_dc_value(mp4_state->hdr.a_dc_store[i][j], &ld->block[0]);

                    block_intra(mp4_state->frame_to_decode, ld, mp4_state, &ld->block[0], j, (coded != 0));
                }
            }
        // not coded macroblock
        } else {
            // mp4_state->reconstruct_skip(ref);
        }
        mp4_state->quant_store[(mp4_state->hdr.mb_ypos + 1) * mp4_state->quant_store_stride + mp4_state->hdr.mb_xpos + 1] =
            mp4_state->hdr.quantizer;

        // mpeg_qp_buf[mp4_state->hdr.mb_ypos][mp4_state->hdr.mb_xpos] = (unsigned char)mp4_state->hdr.quantizer;
        mp4_set_mb_info(mp4_state);
        inc_mbpos(&mp4_state->hdr.mb_xpos, &mp4_state->hdr.mb_ypos,
                  mp4_state->mb_width); // [Review] probably it's the same of mp4_state->hdr.mb_size
        mp4_state->hdr.mba++;
    }

    return 1;
}
