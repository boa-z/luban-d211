/*
 * Copyright (C) 2020-2025 ArtInChip Technology Co. Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 *  author: <xiaodong.zhao@artinchip.com>
 *  Desc: mpeg4 header 311 interface
 *
 */

#include "mp4_vars.h"
#include <math.h>
#include <stdlib.h>

#include "mp4_getbits.h"
#include "mp4_header.h"
#include "mp4_mblock.h"
#include "mp4_vld_311.h"

int getvophdr_311(mp4_stream_t *ld, struct mp4_state *mp4_state)
{
    mp4_state->hdr.prediction_type = getbits(ld, 2);
    mp4_state->hdr.quantizer = getbits(ld, 5);
    if (mp4_state->hdr.prediction_type == I_VOP) {
        mp4_state->hdr.rounding_type = 1;
        mp4_state->hdr.vol_mode = getbits(ld, 5);
        if (mp4_state->hdr.vol_mode == 24)
            mp4_state->hdr.slice_height = mp4_state->mb_height >> 1;
        else
            mp4_state->hdr.slice_height = mp4_state->mb_height;

        /**
         * Meaning of this entry is not perfectly known.
         * It is stored in the very beginning of I-VOP ( bits 8..12 ) and
         * affects decoding of stream until next I-VOP.
         * Observed values include 23 ( 10111 ) and 24 ( 11000 ).
         * Value 23: more 'normal' MPEG-4.
         * Value 24: so far observed for streams with height = 15 and 23
         * macroblocks.
         * When vol_mode = 24, AC/DC and MV predictions are restarted on
         * middle and bottom macroblock rows, i.e. for these rows
         * prediction is carried on as if they were top-most rows in the
         * picture. CBP prediction is not affected.
         */
        switch (showbits(ld, 2)) {
        case 0:
        case 1:
            mp4_state->hdr.ac_intra_chrom_table = &vld_intra_dct_311_0_chrom;
            flushbits(ld, 1);
            mp4_state->hdr.rl_chroma_table_index = 0;

            break;
        case 2:
            mp4_state->hdr.ac_intra_chrom_table = &vld_intra_dct_311_10_chrom;
            flushbits(ld, 2);
            mp4_state->hdr.rl_chroma_table_index = 1;
            break;
        case 3:
            mp4_state->hdr.ac_intra_chrom_table = &vld_intra_dct_311_11_chrom;
            flushbits(ld, 2);
            mp4_state->hdr.rl_chroma_table_index = 2;
            break;
        }
        switch (showbits(ld, 2)) {
        case 0:
        case 1:
            mp4_state->hdr.ac_intra_lum_table = &vld_intra_dct_311_0_lum;
            flushbits(ld, 1);
            mp4_state->hdr.rl_table_index = 0;
            break;
        case 2:
            mp4_state->hdr.ac_intra_lum_table = &vld_intra_dct_311_10_lum;
            flushbits(ld, 2);
            mp4_state->hdr.rl_table_index = 1;
            break;
        case 3:
            mp4_state->hdr.ac_intra_lum_table = &vld_intra_dct_311_11_lum;
            flushbits(ld, 2);
            mp4_state->hdr.rl_table_index = 2;
            break;
        }

        if (getbits1(ld)) {
            mp4_state->hdr.dc_lum_table = &get_dc_311_1_lum;
            mp4_state->hdr.dc_chrom_table = &get_dc_311_1_chrom;
            mp4_state->hdr.dc_table_index = 1;
        } else {
            mp4_state->hdr.dc_lum_table = &get_dc_311_0_lum;
            mp4_state->hdr.dc_chrom_table = &get_dc_311_0_chrom;
            mp4_state->hdr.dc_table_index = 0;
        }
        mp4_state->hdr.get_cbp = &get_cbp_311_i;
    } else {
        if (mp4_state->hdr.switch_rounding)
            mp4_state->hdr.rounding_type = 1 - mp4_state->hdr.rounding_type;
        else
            mp4_state->hdr.rounding_type = 0;
        mp4_state->hdr.has_skips = getbits1(ld);
        switch (showbits(ld, 2)) {
        case 0:
        case 1:
            flushbits(ld, 1);
            mp4_state->hdr.ac_inter_table = &vld_inter_dct_311_0;
            mp4_state->hdr.ac_intra_chrom_table = &vld_intra_dct_311_0_chrom;
            mp4_state->hdr.ac_intra_lum_table = &vld_intra_dct_311_0_lum;
            mp4_state->hdr.rl_table_index = 0;
            mp4_state->hdr.rl_chroma_table_index = 0;
            break;
        case 2:
            mp4_state->hdr.ac_inter_table = &vld_inter_dct_311_10;
            mp4_state->hdr.ac_intra_chrom_table = &vld_intra_dct_311_10_chrom;
            mp4_state->hdr.ac_intra_lum_table = &vld_intra_dct_311_10_lum;
            flushbits(ld, 2);
            mp4_state->hdr.rl_table_index = 1;
            mp4_state->hdr.rl_chroma_table_index = 1;
            break;
        case 3:
            mp4_state->hdr.ac_inter_table = &vld_inter_dct_311_11;
            mp4_state->hdr.ac_intra_chrom_table = &vld_intra_dct_311_11_chrom;
            mp4_state->hdr.ac_intra_lum_table = &vld_intra_dct_311_11_lum;
            flushbits(ld, 2);
            mp4_state->hdr.rl_table_index = 2;
            mp4_state->hdr.rl_chroma_table_index = 2;
            break;
        }
        if (getbits1(ld)) {
            mp4_state->hdr.dc_lum_table = &get_dc_311_1_lum;
            mp4_state->hdr.dc_chrom_table = &get_dc_311_1_chrom;
            mp4_state->hdr.dc_table_index = 1;
        } else {
            mp4_state->hdr.dc_lum_table = &get_dc_311_0_lum;
            mp4_state->hdr.dc_chrom_table = &get_dc_311_0_chrom;
            mp4_state->hdr.dc_table_index = 0;
        }
        if (getbits1(ld)) {
            mp4_state->hdr.mv_table = &get_mv_data_311_1;
            mp4_state->hdr.mv_table_index = 1;
        } else {
            mp4_state->hdr.mv_table = &get_mv_data_311_0;
            mp4_state->hdr.mv_table_index = 0;
        }
        mp4_state->hdr.get_cbp = &get_cbp_311_p;
    }

    return 0;
}
