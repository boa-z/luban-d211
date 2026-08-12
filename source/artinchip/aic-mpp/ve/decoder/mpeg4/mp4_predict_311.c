/*
 * Copyright (C) 2020-2025 ArtInChip Technology Co. Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 *  author: <xiaodong.zhao@artinchip.com>
 *  Desc: mpeg4 predict 311 interface
 *
 */

#include <math.h>

#include "mp4_vars.h"
#include "mp4_global.h"
#include "mp4_predict.h"
#include "mpeg4_decoder.h"

static void rescue_predict_311(struct mp4_state *);
#define _IsIntra(mb_y, mb_x) (mp4_state->modemap[((mb_y) + 1) * mp4_state->modemap_stride + (mb_x) + 1] == INTRA)

void dc_recon_311(struct mp4_state *mp4_state, int block_num, short *dc_value)
{
    if (mp4_state->hdr.prediction_type == P_VOP)
        rescue_predict_311(mp4_state);

    if (block_num < 4) {
        int b_xpos = (mp4_state->hdr.mb_xpos << 1) + (block_num & 1);
        int b_ypos = (mp4_state->hdr.mb_ypos << 1) + ((block_num & 2) >> 1);
        short dc_pred;

        // set prediction direction
        short left_val = mp4_state->coeff_pred.dc_store_lum[(b_ypos + 1) * mp4_state->coeff_pred.dc_store_lum_stride + b_xpos + 1 - 1];
        short top_val = mp4_state->coeff_pred.dc_store_lum[(b_ypos + 1 - 1) * mp4_state->coeff_pred.dc_store_lum_stride + b_xpos + 1];
        short diag_val = mp4_state->coeff_pred.dc_store_lum[(b_ypos + 1 - 1) * mp4_state->coeff_pred.dc_store_lum_stride + b_xpos + 1 - 1];

        if (mp4_state->msmpeg_version < 4) {
            if (abs(diag_val - left_val) <= abs(diag_val - top_val)) {
                mp4_state->coeff_pred.predict_dir = TOP;
                dc_pred = top_val;
            } else {
                mp4_state->coeff_pred.predict_dir = LEFT;
                dc_pred = left_val;
            }
        } else {
            if (abs(diag_val - left_val) < abs(diag_val - top_val)) {
                mp4_state->coeff_pred.predict_dir = TOP;
                dc_pred = top_val;
            } else {
                mp4_state->coeff_pred.predict_dir = LEFT;
                dc_pred = left_val;
            }
        }

        (*dc_value) += dc_pred;
        mp4_state->coeff_pred.dc_store_lum[(b_ypos + 1) * mp4_state->coeff_pred.dc_store_lum_stride + b_xpos + 1] = (*dc_value);

        g_mpeg4_ctx->intra_dc_coeff = *dc_value;
        (*dc_value) *= mp4_state->hdr.dc_scaler;

        // store dc value
    } else {    // chrominance blocks

        int b_xpos = mp4_state->hdr.mb_xpos;
        int b_ypos = mp4_state->hdr.mb_ypos;
        int chr_num = block_num - 4;
        short dc_pred;

        // set prediction direction
        short left_val =
            mp4_state->coeff_pred.dc_store_chr[chr_num][(b_ypos + 1) * mp4_state->coeff_pred.dc_store_chr_stride + b_xpos + 1 - 1];
        short top_val =
            mp4_state->coeff_pred.dc_store_chr[chr_num][(b_ypos + 1 - 1) * mp4_state->coeff_pred.dc_store_chr_stride + b_xpos + 1];
        short diag_val =
            mp4_state->coeff_pred.dc_store_chr[chr_num][(b_ypos + 1 - 1) * mp4_state->coeff_pred.dc_store_chr_stride + b_xpos + 1 - 1];

        if (mp4_state->msmpeg_version < 4) {
            if (abs(diag_val - left_val) <= abs(diag_val - top_val)) // Fb - Fc
            {
                mp4_state->coeff_pred.predict_dir = TOP;
                dc_pred = top_val;
            } else {
                mp4_state->coeff_pred.predict_dir = LEFT;
                dc_pred = left_val;
            }
        } else {
            if (abs(diag_val - left_val) < abs(diag_val - top_val)) // Fb - Fc
            {
                mp4_state->coeff_pred.predict_dir = TOP;
                dc_pred = top_val;
            } else {
                mp4_state->coeff_pred.predict_dir = LEFT;
                dc_pred = left_val;
            }
        }

        (*dc_value) += dc_pred;
        mp4_state->coeff_pred.dc_store_chr[chr_num][(b_ypos + 1) * mp4_state->coeff_pred.dc_store_chr_stride + b_xpos + 1] = (*dc_value);

        g_mpeg4_ctx->intra_dc_coeff = *dc_value;
        (*dc_value) *= mp4_state->hdr.dc_scaler;
    }
}
extern int dcscaler_311(int quant, int block_num);

static void rescue_predict_311(struct mp4_state *mp4_state)
{
    ac_dc *coeff_pred = &mp4_state->coeff_pred;
    int mb_xpos = mp4_state->hdr.mb_xpos;
    int mb_ypos = mp4_state->hdr.mb_ypos;
    int i;
    int dc_defval_lum;
    int dc_defval_chrom;
    int dc_scaler;

    if (mp4_state->msmpeg_version < 4) {
        dc_scaler = dcscaler_311(mp4_state->hdr.quantizer, 0);
        dc_defval_lum = (1024 + dc_scaler / 2) / dc_scaler;
        dc_scaler = dcscaler_311(mp4_state->hdr.quantizer, 4);
        dc_defval_chrom = (1024 + dc_scaler / 2) / dc_scaler;
    } else {
        dc_defval_lum = (1024 + mp4_state->hdr.dc_scaler / 2) / mp4_state->hdr.dc_scaler;
        dc_defval_chrom = (1024 + mp4_state->hdr.dc_scaler / 2) / mp4_state->hdr.dc_scaler;
    }
    if (!_IsIntra(mb_ypos - 1, mb_xpos - 1)) {
        // rescue -A- DC value
        coeff_pred->dc_store_lum[(2 * mb_ypos + 1 - 1) * coeff_pred->dc_store_lum_stride + 2 * mb_xpos + 1 - 1] = dc_defval_lum;
        coeff_pred->dc_store_chr[0][(mb_ypos + 1 - 1) * coeff_pred->dc_store_chr_stride + mb_xpos + 1 - 1] = dc_defval_chrom;
        coeff_pred->dc_store_chr[1][(mb_ypos + 1 - 1) * coeff_pred->dc_store_chr_stride + mb_xpos + 1 - 1] = dc_defval_chrom;
    }
    // left
    if (!_IsIntra(mb_ypos, mb_xpos - 1)) {
        // rescue -B- DC values
        coeff_pred->dc_store_lum[(2 * mb_ypos + 1) * coeff_pred->dc_store_lum_stride + 2 * mb_xpos + 1 - 1] = dc_defval_lum;
        coeff_pred->dc_store_lum[(2 * mb_ypos + 1 + 1) * coeff_pred->dc_store_lum_stride + 2 * mb_xpos + 1 - 1] = dc_defval_lum;
        coeff_pred->dc_store_chr[0][(mb_ypos + 1) * coeff_pred->dc_store_chr_stride + mb_xpos + 1 - 1] = dc_defval_chrom;
        coeff_pred->dc_store_chr[1][(mb_ypos + 1) * coeff_pred->dc_store_chr_stride + mb_xpos + 1 - 1] = dc_defval_chrom;
        //  rescue -B- AC values
        for (i = 0; i < 7; i++) {
            coeff_pred->ac_left_lum[(2 * mb_ypos + 1) * coeff_pred->ac_left_lum_stride + (2 * mb_xpos + 1 - 1) * 7 + i] = 0;
            coeff_pred->ac_left_lum[(2 * mb_ypos + 1 + 1) * coeff_pred->ac_left_lum_stride + (2 * mb_xpos + 1 - 1) * 7 + i] = 0;
            coeff_pred->ac_left_chr[0][(mb_ypos + 1) * coeff_pred->ac_left_chr_stride + (mb_xpos + 1 - 1) * 7 + i] = 0;
            coeff_pred->ac_left_chr[1][(mb_ypos + 1) * coeff_pred->ac_left_chr_stride + (mb_xpos + 1 - 1) * 7 + i] = 0;
        }
    }
    // top
    if (!_IsIntra(mb_ypos - 1, mb_xpos)) {
        // rescue -C- DC values
        coeff_pred->dc_store_lum[(2 * mb_ypos + 1 - 1) * coeff_pred->dc_store_lum_stride + (2 * mb_xpos + 1)] = dc_defval_lum;
        coeff_pred->dc_store_lum[(2 * mb_ypos + 1 - 1) * coeff_pred->dc_store_lum_stride + (2 * mb_xpos + 1 + 1)] = dc_defval_lum;
        coeff_pred->dc_store_chr[0][(mb_ypos + 1 - 1) * coeff_pred->dc_store_chr_stride + (mb_xpos + 1)] = dc_defval_chrom;
        coeff_pred->dc_store_chr[1][(mb_ypos + 1 - 1) * coeff_pred->dc_store_chr_stride + (mb_xpos + 1)] = dc_defval_chrom;
        // rescue -C- AC values
        for (i = 0; i < 7; i++) {
            coeff_pred->ac_top_lum[(2 * mb_ypos + 1 - 1) * coeff_pred->ac_top_lum_stride + (2 * mb_xpos + 1) * 7 + i] = 0;
            coeff_pred->ac_top_lum[(2 * mb_ypos + 1 - 1) * coeff_pred->ac_top_lum_stride + (2 * mb_xpos + 1 + 1) * 7 + i] = 0;
            coeff_pred->ac_top_chr[0][(mb_ypos + 1 - 1) * coeff_pred->ac_top_chr_stride + (mb_xpos + 1) * 7 + i] = 0;
            coeff_pred->ac_top_chr[1][(mb_ypos + 1 - 1) * coeff_pred->ac_top_chr_stride + (mb_xpos + 1) * 7 + i] = 0;
        }
    }
}
