/*
 * Copyright (C) 2020-2025 ArtInChip Technology Co. Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 *  author: <xiaodong.zhao@artinchip.com>
 *  Desc: mpeg4 predict interface
 *
 */

#include <stdlib.h>
#include "mp4_predict.h"
#include "mp4_global.h"
#include "mp4_vars.h"
#include "mpeg4_decoder.h"


void rescue_predict(struct mp4_state *_mp4_state);

/**
 * B - C
 * |   |
 * A - x
 */
void set_prediction_direction(struct mp4_state *mp4_state, int block_num)
{
    ac_dc *coeff_pred = &mp4_state->coeff_pred;
    int *pred_row_above, *pred_row_current;
    int pred_x;
    int pred_y;
    int pred_diag;

    if (block_num < 4) {
        int b_xpos = (mp4_state->hdr.mb_xpos << 1) + (block_num & 1);
        int b_ypos = (mp4_state->hdr.mb_ypos << 1) + ((block_num & 2) >> 1);

        // set prediction direction
        pred_row_above = &coeff_pred->dc_store_lum[(b_ypos + 1 - 1) * coeff_pred->dc_store_lum_stride + b_xpos + 1 - 1];
        pred_row_current = pred_row_above + coeff_pred->dc_store_lum_stride;
    } else {
        int b_xpos = mp4_state->hdr.mb_xpos;
        int b_ypos = mp4_state->hdr.mb_ypos;
        int chr_num = block_num - 4;

        pred_row_above = &coeff_pred->dc_store_chr[chr_num][(b_ypos + 1 - 1) * coeff_pred->dc_store_chr_stride + b_xpos + 1 - 1];
        pred_row_current = pred_row_above + coeff_pred->dc_store_chr_stride;
    }
    // set prediction direction
    pred_x = pred_row_current[0];
    pred_y = pred_row_above[1];
    pred_diag = pred_row_above[0];

    if (abs(pred_diag - pred_x) < abs(pred_diag - pred_y))
        mp4_state->coeff_pred.predict_dir = TOP;
    else
        mp4_state->coeff_pred.predict_dir = LEFT;
}

void set_prediction_direction_intradc(struct mp4_state *mp4_state, int block_num, short *dc_value)
{
    ac_dc *coeff_pred = &mp4_state->coeff_pred;
    int *pred_row_above, *pred_row_current;
    int dc_pred;
    int pred_x;
    int pred_y;
    int pred_diag;

    if (block_num < 4) {
        int b_xpos = (mp4_state->hdr.mb_xpos << 1) + (block_num & 1);
        int b_ypos = (mp4_state->hdr.mb_ypos << 1) + ((block_num & 2) >> 1);

        // set prediction direction
        pred_row_above = &coeff_pred->dc_store_lum[(b_ypos + 1 - 1) * coeff_pred->dc_store_lum_stride + b_xpos + 1 - 1];
        pred_row_current = pred_row_above + coeff_pred->dc_store_lum_stride;
    } else {
        int b_xpos = mp4_state->hdr.mb_xpos;
        int b_ypos = mp4_state->hdr.mb_ypos;
        int chr_num = block_num - 4;

        pred_row_above = &coeff_pred->dc_store_chr[chr_num][(b_ypos + 1 - 1) * coeff_pred->dc_store_chr_stride + b_xpos + 1 - 1];
        pred_row_current = pred_row_above + coeff_pred->dc_store_chr_stride;
    }
    // set prediction direction
    pred_x = pred_row_current[0];
    pred_y = pred_row_above[1];
    pred_diag = pred_row_above[0];

    if (abs(pred_diag - pred_x) < abs(pred_diag - pred_y)) {
        mp4_state->coeff_pred.predict_dir = TOP;
        dc_pred = pred_y;
    } else {
        mp4_state->coeff_pred.predict_dir = LEFT;
        dc_pred = pred_x;
    }

    *dc_value += _div_div(dc_pred, mp4_state->hdr.dc_scaler);
    // DC Prediction are saturated to lie in the range [-2048,2047], see 14496-2 7.4.3.4, N4350
    *dc_value = CLIP(*dc_value, -2048, 2047);

    g_mpeg4_ctx->intra_dc_coeff = *dc_value;

    *dc_value *= mp4_state->hdr.dc_scaler;
    // Inverse Quant value should be saturated in the range [-2048,2047], see 14496-2 7.4.4.4, N4350
    *dc_value = CLIP(*dc_value, -2048, 2047);

    pred_row_current[1] = *dc_value;
}

void dc_recon(struct mp4_state *_mp4_state, int block_num, short *dc_value)
{
    struct mp4_state *mp4_state = _mp4_state;
    ac_dc *coeff_pred = &mp4_state->coeff_pred;
    int *predictor;
    int stride;
    int dc_pred;

    if (block_num < 4) {
        int b_xpos = (mp4_state->hdr.mb_xpos << 1) + (block_num & 1);
        int b_ypos = (mp4_state->hdr.mb_ypos << 1) + ((block_num & 2) >> 1);

        stride = coeff_pred->dc_store_lum_stride;
        predictor = &coeff_pred->dc_store_lum[(b_ypos + 1) * stride + b_xpos + 1];
    } else { // chrominance blocks
        int b_xpos = mp4_state->hdr.mb_xpos;
        int b_ypos = mp4_state->hdr.mb_ypos;
        int chr_num = block_num - 4;

        stride = coeff_pred->dc_store_chr_stride;
        predictor = &coeff_pred->dc_store_chr[chr_num][(b_ypos + 1) * stride + b_xpos + 1];
    }
    if (mp4_state->coeff_pred.predict_dir == TOP)
        dc_pred = predictor[-stride];
    else
        dc_pred = predictor[-1];

    *dc_value += _div_div(dc_pred, mp4_state->hdr.dc_scaler);
    // DC Prediction are saturated to lie in the range [-2048,2047], see 14496-2 7.4.3.4, N4350
    *dc_value = CLIP(*dc_value, -2048, 2047);

    g_mpeg4_ctx->intra_dc_coeff = *dc_value;

    *dc_value *= mp4_state->hdr.dc_scaler;
    // Inverse Quant value should be saturated in the range [-2048,2047], see 14496-2 7.4.4.4, N4350
    *dc_value = CLIP(*dc_value, -2048, 2047);

    // store dc value
    *predictor = *dc_value;
}

void ac_recon(struct mp4_state *_mp4_state, int block_num, short *psBlock)
{
    struct mp4_state *mp4_state = _mp4_state;
    ac_dc *coeff_pred = &mp4_state->coeff_pred;

    int b_xpos, b_ypos;
    int i;

    if (block_num < 4) {
        b_xpos = (mp4_state->hdr.mb_xpos << 1) + (block_num & 1);
        b_ypos = (mp4_state->hdr.mb_ypos << 1) + ((block_num & 2) >> 1);
    } else {
        b_xpos = mp4_state->hdr.mb_xpos;
        b_ypos = mp4_state->hdr.mb_ypos;
    }

    // predict coefficients
    if (mp4_state->hdr.ac_pred_flag) {
        if (block_num < 4) {
            if (mp4_state->coeff_pred.predict_dir == TOP) {
                for (i = 1; i < 8; i++) {
                    psBlock[i] += coeff_pred->ac_top_lum[(b_ypos + 1 - 1) * coeff_pred->ac_top_lum_stride + (b_xpos + 1) * 7 + i - 1];
                    // AC Prediction is saturated to lie in the range [-2048, 2047], see 14496-2 7.4.3.4
                    psBlock[i] = CLIP(psBlock[i], -2047, 2048);
                }
            } else { // left prediction
                for (i = 1; i < 8; i++) {
                    psBlock[i << 3] +=
                        coeff_pred->ac_left_lum[(b_ypos + 1) * coeff_pred->ac_left_lum_stride + (b_xpos + 1 - 1) * 7 + i - 1];
                    // AC Prediction is saturated to lie in the range [-2048, 2047], see 14496-2 7.4.3.4
                    psBlock[i << 3] = CLIP(psBlock[i << 3], -2047, 2048);
                }
            }
        } else {
            int chr_num = block_num - 4;

            if (mp4_state->coeff_pred.predict_dir == TOP) {
                for (i = 1; i < 8; i++) {
                    psBlock[i] +=
                        coeff_pred->ac_top_chr[chr_num][(b_ypos + 1 - 1) * coeff_pred->ac_top_chr_stride + (b_xpos + 1) * 7 + i - 1];
                    // AC Prediction is saturated to lie in the range [-2048, 2047], see 14496-2 7.4.3.4
                    psBlock[i] = CLIP(psBlock[i], -2047, 2048);
                }
            } else { // left prediction
                for (i = 1; i < 8; i++) {
                    psBlock[i << 3] +=
                        coeff_pred->ac_left_chr[chr_num][(b_ypos + 1) * coeff_pred->ac_left_chr_stride + (b_xpos + 1 - 1) * 7 + i - 1];
                    // AC Prediction is saturated to lie in the range [-2048, 2047], see 14496-2 7.4.3.4
                    psBlock[i << 3] = CLIP(psBlock[i << 3], -2047, 2048);
                }
            }
        }
    }
}

void ac_store(struct mp4_state *_mp4_state, int block_num, short *psBlock)
{
    struct mp4_state *mp4_state = _mp4_state;
    ac_dc *coeff_pred = &mp4_state->coeff_pred;

    int b_xpos, b_ypos;
    int i;

    if (block_num < 4) {
        b_xpos = (mp4_state->hdr.mb_xpos << 1) + (block_num & 1);
        b_ypos = (mp4_state->hdr.mb_ypos << 1) + ((block_num & 2) >> 1);
    } else {
        b_xpos = mp4_state->hdr.mb_xpos;
        b_ypos = mp4_state->hdr.mb_ypos;
    }

    // store coefficients
    if (block_num < 4) {
        for (i = 1; i < 8; i++) {
            coeff_pred->ac_top_lum[(b_ypos + 1) * coeff_pred->ac_top_lum_stride + (b_xpos + 1) * 7 + i - 1] = psBlock[i];
            coeff_pred->ac_left_lum[(b_ypos + 1) * coeff_pred->ac_left_lum_stride + (b_xpos + 1) * 7 + i - 1] = psBlock[i << 3];
        }
    } else {
        int chr_num = block_num - 4;

        for (i = 1; i < 8; i++) {
            coeff_pred->ac_top_chr[chr_num][(b_ypos + 1) * coeff_pred->ac_top_chr_stride + (b_xpos + 1) * 7 + i - 1] = psBlock[i];
            coeff_pred->ac_left_chr[chr_num][(b_ypos + 1) * coeff_pred->ac_left_chr_stride + (b_xpos + 1) * 7 + i - 1] = psBlock[i << 3];
        }
    }
}

#define _IsIntra(mb_y, mb_x)                                                                 \
    ((mp4_state->modemap[((mb_y) + 1) * mp4_state->modemap_stride + (mb_x) + 1] == INTRA) || \
     (mp4_state->modemap[((mb_y) + 1) * mp4_state->modemap_stride + (mb_x) + 1] == INTRA_Q))
#define _IsInter(mb_y, mb_x)                                                                 \
    ((mp4_state->modemap[((mb_y) + 1) * mp4_state->modemap_stride + (mb_x) + 1] == INTER) || \
     (mp4_state->modemap[((mb_y) + 1) * mp4_state->modemap_stride + (mb_x) + 1] == INTER_Q))

#define _rescale(predict_quant, current_quant, coeff) (coeff != 0) ? _div_div((coeff) * (predict_quant), (current_quant)) : 0


static int ac_rescaling_top_direction(struct mp4_state *mp4_state, int block_num, short *psBlock, int b_xpos, int b_ypos)
{
    ac_dc *coeff_pred = &mp4_state->coeff_pred;
    int current_quant = mp4_state->hdr.quantizer;
    int predict_quant = mp4_state->quant_store[mp4_state->hdr.mb_ypos * mp4_state->quant_store_stride + mp4_state->hdr.mb_xpos + 1];
    int i;

    switch (block_num) {
    case 0:
    case 1:
        for (i = 1; i < 8; i++) {
            psBlock[i] += _rescale(predict_quant, current_quant,
                                   coeff_pred->ac_top_lum[b_ypos * coeff_pred->ac_top_lum_stride + (b_xpos + 1) * 7 + i - 1]);
        }
        return 1;
    case 4:
        for (i = 1; i < 8; i++) {
            psBlock[i] += _rescale(predict_quant, current_quant,
                                   coeff_pred->ac_top_chr[0][b_ypos * coeff_pred->ac_top_chr_stride + (b_xpos + 1) * 7 + i - 1]);
        }
        return 1;
    case 5:
        for (i = 1; i < 8; i++) {
            psBlock[i] += _rescale(predict_quant, current_quant,
                                   coeff_pred->ac_top_chr[1][b_ypos * coeff_pred->ac_top_chr_stride + (b_xpos + 1) * 7 + i - 1]);
        }
        return 1;
    }
    return 0;
}

static int ac_rescaling_left_direction(struct mp4_state *mp4_state, int block_num, short *psBlock, int b_xpos, int b_ypos)
{
    ac_dc *coeff_pred = &mp4_state->coeff_pred;
    int current_quant = mp4_state->hdr.quantizer;
    int predict_quant = mp4_state->quant_store[(mp4_state->hdr.mb_ypos + 1) * mp4_state->quant_store_stride + mp4_state->hdr.mb_xpos];
    int i;

    switch (block_num) {
    case 0:
    case 2:
        for (i = 1; i < 8; i++) {
            psBlock[i << 3] += _rescale(predict_quant, current_quant,
                                        coeff_pred->ac_left_lum[(b_ypos + 1) * coeff_pred->ac_left_lum_stride + b_xpos * 7 + i - 1]);
        }
        return 1;
    case 4:
        for (i = 1; i < 8; i++) {
            psBlock[i << 3] += _rescale(predict_quant, current_quant,
                                        coeff_pred->ac_left_chr[0][(b_ypos + 1) * coeff_pred->ac_left_chr_stride + b_xpos * 7 + i - 1]);
        }
        return 1;
    case 5:
        for (i = 1; i < 8; i++) {
            psBlock[i << 3] += _rescale(predict_quant, current_quant,
                                        coeff_pred->ac_left_chr[1][(b_ypos + 1) * coeff_pred->ac_left_chr_stride + b_xpos * 7 + i - 1]);
        }
        return 1;
    }
    return 0;
}

int ac_rescaling(struct mp4_state *_mp4_state, int block_num, short *psBlock)
{
    struct mp4_state *mp4_state = _mp4_state;

    int mb_xpos = mp4_state->hdr.mb_xpos;
    int mb_ypos = mp4_state->hdr.mb_ypos;
    int current_quant = mp4_state->hdr.quantizer;
    int predict_quant = (mp4_state->coeff_pred.predict_dir == TOP)
                            ? mp4_state->quant_store[mb_ypos * mp4_state->quant_store_stride + mb_xpos + 1]
                            : mp4_state->quant_store[(mb_ypos + 1) * mp4_state->quant_store_stride + mb_xpos];
    int b_xpos, b_ypos; // index for stored coeff matrix

    if ((!mp4_state->hdr.ac_pred_flag) || (current_quant == predict_quant) || (block_num == 3))
        return 0;

    if ((mb_ypos == 0) && (mp4_state->coeff_pred.predict_dir == TOP))
        return 0;
    if ((mb_xpos == 0) && (mp4_state->coeff_pred.predict_dir == LEFT))
        return 0;
    if ((mb_xpos == 0) && (mb_ypos == 0))
        return 0;

    if (mp4_state->hdr.data_partitioning) {
        if ((mp4_state->coeff_pred.predict_dir == TOP) && (!_IsIntra(mb_ypos - 1, mb_xpos)))
            return 0;

        if ((mp4_state->coeff_pred.predict_dir == LEFT) && (!_IsIntra(mb_ypos, mb_xpos - 1)))
            return 0;
    }

    if (block_num < 4) {
        b_xpos = (mp4_state->hdr.mb_xpos << 1) + (block_num & 1);
        b_ypos = (mp4_state->hdr.mb_ypos << 1) + ((block_num & 2) >> 1);
    } else {
        b_xpos = mp4_state->hdr.mb_xpos;
        b_ypos = mp4_state->hdr.mb_ypos;
    }

    if (mp4_state->coeff_pred.predict_dir == TOP) {
        return ac_rescaling_top_direction(mp4_state, block_num, psBlock, b_xpos, b_ypos);
    } else {
        return ac_rescaling_left_direction(mp4_state, block_num, psBlock, b_xpos, b_ypos);
    }
}

void rescue_predict(struct mp4_state *_mp4_state)
{
    struct mp4_state *mp4_state = _mp4_state;
    ac_dc *coeff_pred = &mp4_state->coeff_pred;

    int mb_xpos = mp4_state->hdr.mb_xpos;
    int mb_ypos = mp4_state->hdr.mb_ypos;
    int i;

    // [Review] Should be called once for each macroblock
    if (!_IsIntra(mb_ypos - 1, mb_xpos - 1)) {
        // rescue -A- DC value
        coeff_pred->dc_store_lum[(2 * mb_ypos + 1 - 1) * coeff_pred->dc_store_lum_stride + 2 * mb_xpos + 1 - 1] = 1024;
        coeff_pred->dc_store_chr[0][(mb_ypos + 1 - 1) * coeff_pred->dc_store_chr_stride + mb_xpos + 1 - 1] = 1024;
        coeff_pred->dc_store_chr[1][(mb_ypos + 1 - 1) * coeff_pred->dc_store_chr_stride + mb_xpos + 1 - 1] = 1024;
    }
    // left
    if (!_IsIntra(mb_ypos, mb_xpos - 1)) {
        // rescue -B- DC values
        coeff_pred->dc_store_lum[(2 * mb_ypos + 1) * coeff_pred->dc_store_lum_stride + 2 * mb_xpos + 1 - 1] = 1024;
        coeff_pred->dc_store_lum[(2 * mb_ypos + 1 + 1) * coeff_pred->dc_store_lum_stride + 2 * mb_xpos + 1 - 1] = 1024;
        coeff_pred->dc_store_chr[0][(mb_ypos + 1) * coeff_pred->dc_store_chr_stride + mb_xpos + 1 - 1] = 1024;
        coeff_pred->dc_store_chr[1][(mb_ypos + 1) * coeff_pred->dc_store_chr_stride + mb_xpos + 1 - 1] = 1024;
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
        coeff_pred->dc_store_lum[(2 * mb_ypos + 1 - 1) * coeff_pred->dc_store_lum_stride + 2 * mb_xpos + 1] = 1024;
        coeff_pred->dc_store_lum[(2 * mb_ypos + 1 - 1) * coeff_pred->dc_store_lum_stride + 2 * mb_xpos + 1 + 1] = 1024;
        coeff_pred->dc_store_chr[0][(mb_ypos + 1 - 1) * coeff_pred->dc_store_chr_stride + mb_xpos + 1] = 1024;
        coeff_pred->dc_store_chr[1][(mb_ypos + 1 - 1) * coeff_pred->dc_store_chr_stride + mb_xpos + 1] = 1024;
        // rescue -C- AC values
        for (i = 0; i < 7; i++) {
            coeff_pred->ac_top_lum[(2 * mb_ypos + 1 - 1) * coeff_pred->ac_top_lum_stride + (2 * mb_xpos + 1) * 7 + i] = 0;
            coeff_pred->ac_top_lum[(2 * mb_ypos + 1 - 1) * coeff_pred->ac_top_lum_stride + (2 * mb_xpos + 1 + 1) * 7 + i] = 0;
            coeff_pred->ac_top_chr[0][(mb_ypos + 1 - 1) * coeff_pred->ac_top_chr_stride + (mb_xpos + 1) * 7 + i] = 0;
            coeff_pred->ac_top_chr[1][(mb_ypos + 1 - 1) * coeff_pred->ac_top_chr_stride + (mb_xpos + 1) * 7 + i] = 0;
        }
    }
}

static void calculate_reference_block(struct mp4_state *mp4_state, int block_num, int *ref_block, int *href_offset, int *vref_offset)
{
    *ref_block = 0;
    *href_offset = 0;
    *vref_offset = 0;

    switch (block_num) {
    case 0:
        if (!mp4_state->hdr.gob_edge.iabove)
            *ref_block |= 1;
        if (!mp4_state->hdr.gob_edge.ileft)
            *ref_block |= 2;
        break;
    case 1:
        if (!mp4_state->hdr.gob_edge.iabove)
            *ref_block |= 3;
        else
            *ref_block |= 2;
        *href_offset = 8;
        break;
    case 2:
        if (!mp4_state->hdr.gob_edge.ileft)
            *ref_block |= 3;
        else
            *ref_block |= 1;
        *vref_offset = 1;
        break;
    case 3:
        *ref_block = 3;
        *href_offset = 8;
        *vref_offset = 1;
        break;
    case 4:
        if (!mp4_state->hdr.gob_edge.iabove)
            *ref_block |= 1;
        if (!mp4_state->hdr.gob_edge.ileft)
            *ref_block |= 2;
        *vref_offset = 2;
        break;
    case 5:
        if (!mp4_state->hdr.gob_edge.iabove)
            *ref_block |= 1;
        if (!mp4_state->hdr.gob_edge.ileft)
            *ref_block |= 2;
        *vref_offset = 3;
        break;
    default:
        break;
    }
}

static int* get_top_prediction_pointer(struct mp4_state *mp4_state, ac_dc *coeff_pred, int block_num, int href_offset)
{
    if (block_num < 4)
        return coeff_pred->pAic_luma_top + mp4_state->hdr.mb_xpos * 16 + href_offset;
    else if (block_num == 4)
        return coeff_pred->pAic_chroma0_top + mp4_state->hdr.mb_xpos * 8;
    else if (block_num == 5)
        return coeff_pred->pAic_chroma1_top + mp4_state->hdr.mb_xpos * 8;

    return NULL;
}

static void handle_dc_prediction(ac_dc *coeff_pred, int ref_block, int vref_offset, int *p_top_pred_ptr, short *psBlock)
{
    int iPred = 0;

    switch (ref_block) {
    case 0:
        iPred = 1024;
        break;
    case 1:
        iPred = p_top_pred_ptr[0];
        break;
    case 2:
        iPred = coeff_pred->Aic_left[vref_offset][0];
        break;
    case 3:
        iPred = (p_top_pred_ptr[0] + coeff_pred->Aic_left[vref_offset][0]) >> 1;
        break;
    default:
        break;
    }
    psBlock[0] += iPred;
    if (psBlock[0] > 2047)
        psBlock[0] = 2047;
}

static void handle_left_prediction(ac_dc *coeff_pred, int ref_block, int vref_offset, int *iPred, short *psBlock)
{
    int i;

    switch (ref_block) {
    case 0:
        iPred[0] = 1024;
        break;
    case 1:
        iPred[0] = 1024;
        break;
    case 2:
    case 3:
        for (i = 0; i < 8; i++)
            iPred[i] = coeff_pred->Aic_left[vref_offset][i];
        break;
    default:
        break;
    }
    for (i = 0; i < 8; i++) {
        psBlock[i * 8] += iPred[i];
        if (psBlock[i * 8] > 2047)
            psBlock[i * 8] = 2047;
        if (psBlock[i * 8] < -2048)
            psBlock[i * 8] = -2048;
    }
}

static void handle_top_prediction(ac_dc *coeff_pred, int ref_block, int *p_top_pred_ptr, int *iPred, short *psBlock)
{
    int i;

    switch (ref_block) {
    case 0:
        iPred[0] = 1024;
        break;
    case 2:
        iPred[0] = 1024;
        break;
    case 1:
    case 3:
        for (i = 0; i < 8; i++)
            iPred[i] = p_top_pred_ptr[i];
        break;
    default:
        break;
    }
    for (i = 0; i < 8; i++) {
        psBlock[i] += iPred[i];
        if (psBlock[i] > 2047)
            psBlock[i] = 2047;
        if (psBlock[i] < -2048)
            psBlock[i] = -2048;
    }
}

static void post_process_prediction(short *psBlock, ac_dc *coeff_pred, int *p_top_pred_ptr, int vref_offset)
{
    int i;

    psBlock[0] |= 1;
    if (psBlock[0] < 0)
        psBlock[0] = 0;

    for (i = 0; i < 8; i++) {
        p_top_pred_ptr[i] = psBlock[i];
        coeff_pred->Aic_left[vref_offset][i] = psBlock[8 * i];
    }
}

void advanced_intra_prediction(struct mp4_state *_mp4_state, int block_num, short *psBlock)
{
    struct mp4_state *mp4_state = _mp4_state;
    ac_dc *coeff_pred = &mp4_state->coeff_pred;
    int ref_block, href_offset, vref_offset;
    int iPred[8] = {0, 0, 0, 0, 0, 0, 0, 0};
    int *p_top_pred_ptr = NULL;

    calculate_reference_block(mp4_state, block_num, &ref_block, &href_offset, &vref_offset);

    p_top_pred_ptr = get_top_prediction_pointer(mp4_state, coeff_pred, block_num, href_offset);

    switch (mp4_state->coeff_pred.predict_dir) {
    case NONE: // DC
        handle_dc_prediction(coeff_pred, ref_block, vref_offset, p_top_pred_ptr, psBlock);
        break;
    case LEFT:
        handle_left_prediction(coeff_pred, ref_block, vref_offset, iPred, psBlock);
        break;
    case TOP:
        handle_top_prediction(coeff_pred, ref_block, p_top_pred_ptr, iPred, psBlock);
        break;
    default:
        break;
    }

    post_process_prediction(psBlock, coeff_pred, p_top_pred_ptr, vref_offset);
}
