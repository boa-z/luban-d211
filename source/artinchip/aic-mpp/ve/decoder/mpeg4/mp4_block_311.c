/*
 * Copyright (C) 2020-2025 ArtInChip Technology Co. Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 *  author: <xiaodong.zhao@artinchip.com>
 *  Desc: mpeg4 block 311 interface
 *
 */

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#include "mp4_block.h"
#include "mp4_getbits.h"
#include "mp4_predict.h"
#include "mp4_tables.h"
#include "mp4_vars.h"
#include "mp4_vld.h"
#include "mp4_global.h"
#include "mpp_log.h"
#include "mpeg4_decoder.h"

extern int dcscaler_311(int, int);
extern void dc_recon_311(struct mp4_state *mp4_state, int block_num, short *dc_value);
extern uint8_t wmv1_y_dc_scale_table[32];
extern uint8_t wmv1_c_dc_scale_table[32];
extern event_t vld_intra_dct_311_0_lum(struct mp4_state *, mp4_stream_t *ld);
extern event_t vld_intra_dct_311_10_lum(struct mp4_state *, mp4_stream_t *ld);
extern event_t vld_intra_dct_311_11_lum(struct mp4_state *, mp4_stream_t *ld);
extern event_t vld_intra_dct_311_0_chrom(struct mp4_state *, mp4_stream_t *ld);
extern event_t vld_intra_dct_311_10_chrom(struct mp4_state *, mp4_stream_t *ld);
extern event_t vld_intra_dct_311_11_chrom(struct mp4_state *, mp4_stream_t *ld);
extern event_t vld_inter_dct_311_0(struct mp4_state *, mp4_stream_t *ld);
extern event_t vld_inter_dct_311_10(struct mp4_state *, mp4_stream_t *ld);
extern event_t vld_inter_dct_311_11(struct mp4_state *, mp4_stream_t *ld);
extern int my_debug;

int block_intra_311(unsigned char *frame_ref[], mp4_stream_t *ld, struct mp4_state *mp4_state, short *block, int block_num, int coded)
{
    mp4_state->mpeg_coef_matrix_no[block_num] = 0;
    unsigned long tmp;
    event_t event;
    int i;

    clearblock(block); // clearblock
    // dc coeff
    if (mp4_state->msmpeg_version < 4) {
        mp4_state->hdr.dc_scaler = dcscaler_311(mp4_state->hdr.quantizer, block_num); // calculate DC scaler
    } else {
        if (block_num < 4)
            mp4_state->hdr.dc_scaler = wmv1_y_dc_scale_table[mp4_state->hdr.quantizer];
        else
            mp4_state->hdr.dc_scaler = wmv1_c_dc_scale_table[mp4_state->hdr.quantizer];
    }

    if (block_num < 4)
        block[0] = mp4_state->hdr.dc_lum_table(mp4_state, ld);
    else
        block[0] = mp4_state->hdr.dc_chrom_table(mp4_state, ld);

    if (1) {
        if (block[0] >= 0) {
            tmp = block[0];
        } else {
            tmp = -block[0];
            tmp |= 0x800;
        }
        mp4_state->mpeg_coef_matrix[block_num][0] = tmp & 0xfff; // no dc pred
        mp4_state->mpeg_coef_matrix_no[block_num]++;
    }

    // dc reconstruction, prediction direction
    dc_recon_311(mp4_state, block_num, block);

    if (mp4_state->msmpeg_version >= 4) {
        switch (mp4_state->hdr.rl_chroma_table_index) {
        case 0:
            mp4_state->hdr.ac_intra_chrom_table = &vld_intra_dct_311_0_chrom;
            break;
        case 1:
            mp4_state->hdr.ac_intra_chrom_table = &vld_intra_dct_311_10_chrom;
            break;
        case 2:
            mp4_state->hdr.ac_intra_chrom_table = &vld_intra_dct_311_11_chrom;
            break;
        }

        switch (mp4_state->hdr.rl_table_index) {
        case 0:
            mp4_state->hdr.ac_intra_lum_table = &vld_intra_dct_311_0_lum;
            break;
        case 1:
            mp4_state->hdr.ac_intra_lum_table = &vld_intra_dct_311_10_lum;
            break;
        case 2:
            mp4_state->hdr.ac_intra_lum_table = &vld_intra_dct_311_11_lum;
            break;
        }
    }

    if (coded) {
        unsigned int *zigzag; // zigzag scan dir

        if (mp4_state->msmpeg_version < 4) {
            if (mp4_state->hdr.ac_pred_flag) {
                if (mp4_state->coeff_pred.predict_dir == TOP)
                    zigzag = alternate_horizontal_scan;
                else
                    zigzag = alternate_vertical_scan;
            } else {
                zigzag = zig_zag_scan;
            }
        } else {
            if (mp4_state->hdr.ac_pred_flag) {
                if (mp4_state->coeff_pred.predict_dir == TOP)
                    zigzag = wmv1_scantable[2];
                else
                    zigzag = wmv1_scantable[3];
            } else {
                zigzag = wmv1_scantable[1];
            }
        }

        i = 1;
        if (block_num < 4) {
            do {    // event vld
                event = mp4_state->hdr.ac_intra_lum_table(mp4_state, ld);
                i += event.run;
                if (my_debug) {
                    loge("block=%d level=%d run=%d", block_num, event.level, event.run + 1);
                }
                if (i >= 64) {
                    printf("OOPS: block_intra_311: event index = %d\n", i);
                    if (!event.last)
                        return 0;
                    i = 63;
                }
                block[zigzag[i]] = (short)event.level;
                i++;
            } while (!event.last);
        } else {
            do {    // event vld
                event = mp4_state->hdr.ac_intra_chrom_table(mp4_state, ld);
                i += event.run;
                if (i >= 64) {
                    printf("OOPS: block_intra_311: event index = %d\n", i);
                    if (!event.last) {
                        return 0;
                    }
                    i = 63;
                }

                block[zigzag[i]] = (short)event.level;
                i++;
            } while (!event.last);
        }
    }
    // ac reconstruction
    ac_recon(mp4_state, block_num, block);
    ac_store(mp4_state, block_num, block);

    // store mb coeffs to mpeg_coef_matrix
    int dc_tmp = block[0];
    if (!mp4_state->hdr.short_video_header) {
        // DC coeff should
        block[0] = g_mpeg4_ctx->intra_dc_coeff;
    }
    mpeg_save_mb_coeff(mp4_state, block, block_num);
    if (!mp4_state->hdr.short_video_header) {
        block[0] = dc_tmp;
    }

    return 1;
}

int block_inter_311(mp4_stream_t *ld, struct mp4_state *mp4_state, short *block, int block_num)
{
    unsigned int *zigzag = zig_zag_scan; // zigzag scan dir
    int sub_cbp = 0, sub_cbp_idx = 0;
    int sub_cbp_table[3] = {2, 3, 1};
    short coeff_block[64] = {0};
    short block2[64];
    event_t event;
    int i;

    mp4_state->mpeg_coef_matrix_no[block_num] = 0;

    if (mp4_state->msmpeg_version >= 4) {
        zigzag = wmv1_scantable[0];
        switch (mp4_state->hdr.rl_table_index) {
        case 0:
            mp4_state->hdr.ac_inter_table = &vld_inter_dct_311_0;
            mp4_state->hdr.ac_intra_chrom_table = &vld_intra_dct_311_0_chrom;
            mp4_state->hdr.ac_intra_lum_table = &vld_intra_dct_311_0_lum;
            break;
        case 1:
            mp4_state->hdr.ac_inter_table = &vld_inter_dct_311_10;
            mp4_state->hdr.ac_intra_chrom_table = &vld_intra_dct_311_10_chrom;
            mp4_state->hdr.ac_intra_lum_table = &vld_intra_dct_311_10_lum;
            break;
        case 2:
            mp4_state->hdr.ac_inter_table = &vld_inter_dct_311_11;
            mp4_state->hdr.ac_intra_chrom_table = &vld_intra_dct_311_11_chrom;
            mp4_state->hdr.ac_intra_lum_table = &vld_intra_dct_311_11_lum;
            break;
        }
    }

    clearblock(block); // clearblock

#ifdef MPEG4_DUMP_ENABLE
    fprintf(g_mpeg4_ctx->fp_mb_coeff, "block_inter, block num(%d), cbp: 0x%x\n", block_num, mp4_state->hdr.cbp);
#endif

    if (mp4_state->msmpeg_version > 4) {
        if (mp4_state->hdr.per_block_abt) {
            mp4_state->hdr.abt_type = getbits1(ld);
            if (mp4_state->hdr.abt_type)
                mp4_state->hdr.abt_type += getbits1(ld);
        }
        mp4_state->hdr.abt_type_table[block_num] = mp4_state->hdr.abt_type;

        if (mp4_state->hdr.abt_type) {
            sub_cbp_idx = getbits1(ld);
            if (sub_cbp_idx)
                sub_cbp_idx += getbits1(ld);

            sub_cbp = sub_cbp_table[sub_cbp_idx];
            if (mp4_state->hdr.abt_type == 1)
                zigzag = wmv2_scantableA;
            else
                zigzag = wmv2_scantableB;
            clearblock(block2); // clearblock
        }
    }

    if (!mp4_state->hdr.abt_type || (mp4_state->hdr.abt_type && (sub_cbp & 1))) { // 8x8 or first 8x4/4x8
        int q_scale = mp4_state->hdr.quantizer;
        int q_2scale = q_scale << 1;
        int q_add = (q_scale & 1) ? q_scale : (q_scale - 1);

        i = 0;
        do {    // event vld
            int index = 0;
            event = mp4_state->hdr.ac_inter_table(mp4_state, ld);
            i += event.run;
            if (i >= 64) {
                printf("OOPS: block_inter_311: event index = %d\n", i);
                /**
                 * It is logical to return 0 here, signaling that there was an error in the stream. However,
                 * we have a clip ( as of today it's z:\engineering\content\bugs (5.0.2)\works_only_with_311.avi )
                 * that is decoded correctly if we ignore index overflow, but shows artifacts if we abort decoding here. Weird.
                 * Debugging divxc32.dll to see what should actually happen is just too painful.
                 */
                if (!event.last)
                    return 0;
                i = 63;
            }

            if (do_idct_mode == T2_IDCT_MODE)
                index = zigzag[i];
            else if (do_idct_mode == DIVX_IDCT_MODE)
                index = (zigzag[i] & 7) * 8 + (zigzag[i] >> 3);

            coeff_block[index] = event.level;
            if (event.level > 0)
                block[index] = (q_2scale * event.level) + q_add;
            else
                block[index] = (q_2scale * event.level) - q_add;

            i++;
        } while (!event.last);
    }

    if (mp4_state->hdr.abt_type && (sub_cbp & 2)) {
        int q_scale = mp4_state->hdr.quantizer;
        int q_2scale = q_scale << 1;
        int q_add = (q_scale & 1) ? q_scale : (q_scale - 1);

        i = 0;
        do {    // event vld

            int index;
            event = mp4_state->hdr.ac_inter_table(mp4_state, ld);
            i += event.run;
            if (i >= 64) {
                printf("OOPS: block_inter_311: event index = %d\n", i);
                /**
                 * It is logical to return 0 here, signaling that there was an error in the stream. However,
                 * we have a clip ( as of today it's z:\engineering\content\bugs (5.0.2)\works_only_with_311.avi )
                 * that is decoded correctly if we ignore index overflow, but shows artifacts if we abort decoding here. Weird.
                 * Debugging divxc32.dll to see what should actually happen is just too painful.
                 */
                if (!event.last)
                    return 0;
                i = 63;
            }
            mp4_state->mpeg_coef_matrix[block_num][mp4_state->mpeg_coef_matrix_no[block_num]] = event.level;
            mp4_state->mpeg_coef_matrix_no[block_num]++;

            index = zigzag[i];

            coeff_block[index] = event.level;
            if (event.level > 0)
                block2[index] = (q_2scale * event.level) + q_add;
            else
                block2[index] = (q_2scale * event.level) - q_add;

            i++;
        } while (!event.last);
    }

    mpeg_save_mb_coeff(mp4_state, coeff_block, block_num);

    return 1;
}
