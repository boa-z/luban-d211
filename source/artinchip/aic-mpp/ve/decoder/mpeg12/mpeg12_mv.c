/*
 * Copyright (C) 2020-2025 ArtInChip Technology Co. Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 *  author: <xiaodong.zhao@artinchip.com>
 *  Desc: mpeg1/2 mv interface
 *
 */

#include "mpeg12_decoder.h"
#include "vlc.h"
#include <stdlib.h>

static int mpeg2_motion_vector_calc(struct mpeg12_dec_ctx *s, int r, int n, int t)
{
    int r_size = s->pic_code_extension.f_code[n][t] - 1;
    int f = 1 << r_size;
    int high = (16 * f) - 1;
    int low = (-16) * f;
    int range = 32 * f;
    int delta, prediction, vector;

    int motion_code = s->mb_info.motion_code[r][n][t];
    if (f == 1 || motion_code == 0) {
        delta = motion_code;
    } else {
        delta = (((motion_code >= 0 ? motion_code : -motion_code) - 1) << r_size) +
                s->mb_info.motion_residual[r][n][t] + 1;
        if (motion_code < 0)
            delta = -delta;
    }

    prediction = s->mb_info.pmv[r][n][t];
    if ((s->mb_info.mv_format == MV_FORMAT_FIELD) && (t == 1) &&
        (s->pic_code_extension.picture_structure == FRAME)) {
        prediction >>= 1;
        vector = prediction + delta;
        if (vector < low)
            vector += range;
        else if (vector > high)
            vector -= range;
        s->mb_info.pmv[r][n][t] = vector << 1;
    } else {
        vector = prediction + delta;
        if (vector < low)
            vector += range;
        else if (vector > high)
            vector -= range;
        s->mb_info.pmv[r][n][t] = vector;
    }
    s->mb_info.mv[r][n][t] = vector;
    return 0;
}

/* ISO/IEC 13818-2 section 7.6.3.6: Dual prime additional arithmetic */
void dual_prime_arithmetic(struct mpeg12_dec_ctx *s, long *dmvector, int mvx, int mvy)
{
    if (s->pic_code_extension.picture_structure == FRAME) {
        if (s->pic_code_extension.top_field_first) {
            /* vector for prediction of top field from bottom field */
            s->mb_info.mv[2][0][0] = ((mvx + (mvx > 0)) >> 1) + dmvector[0];
            s->mb_info.mv[2][0][1] = ((mvy + (mvy > 0)) >> 1) + dmvector[1] - 1;

            /* vector for prediction of bottom field from top field */
            s->mb_info.mv[3][0][0] = ((3 * mvx + (mvx > 0)) >> 1) + dmvector[0];
            s->mb_info.mv[3][0][1] = ((3 * mvy + (mvy > 0)) >> 1) + dmvector[1] + 1;
        } else {
            /* vector for prediction of top field from bottom field */
            s->mb_info.mv[2][0][0] = ((3 * mvx + (mvx > 0)) >> 1) + dmvector[0];
            s->mb_info.mv[2][0][1] = ((3 * mvy + (mvy > 0)) >> 1) + dmvector[1] - 1;

            /* vector for prediction of bottom field from top field */
            s->mb_info.mv[3][0][0] = ((mvx + (mvx > 0)) >> 1) + dmvector[0];
            s->mb_info.mv[3][0][1] = ((mvy + (mvy > 0)) >> 1) + dmvector[1] + 1;
        }
    } else {
        /* vector for prediction from field of opposite 'parity' */
        s->mb_info.mv[2][0][0] = ((mvx + (mvx > 0)) >> 1) + dmvector[0];
        s->mb_info.mv[2][0][1] = ((mvy + (mvy > 0)) >> 1) + dmvector[1];

        /* correct for vertical field shift */
        if (s->pic_code_extension.picture_structure == TOP_FIELD)
            s->mb_info.mv[2][0][1]--;
        else
            s->mb_info.mv[2][0][1]++;
    }
}

static int mpeg2_motion_vector(struct mpeg12_dec_ctx *s, int r, int n)
{
    struct vlc_tab1 *tab;
    int tmp;

    // motion_code[r][n][0]   1-11 bits
    tab = &motion_code_table[show_bits(&s->gb, 11)];
    if (tab->len) {
        skip_bits(&s->gb, tab->len);
        s->mb_info.motion_code[r][n][0] = tab->value;
    } else {
        loge("motion_code error!\n");
        abort();
    }

    // motion_residual[r][n][0]  1-8 bits
    if (s->pic_code_extension.f_code[n][0] != 1 && s->mb_info.motion_code[r][n][0] != 0) {
        tmp = s->pic_code_extension.f_code[n][0] - 1;
        s->mb_info.motion_residual[r][n][0] = read_bits(&s->gb, tmp);
    }

    // dmvector[0]   1-2 bits
    if (s->mb_info.dmv == 1) {
        s->mb_info.dmvector[0] = (read_bits(&s->gb, 1) == 0) ? 0 : (read_bits(&s->gb, 1) ? -1 : 1);
    }
    mpeg2_motion_vector_calc(s, r, n, 0);

    tab = &motion_code_table[show_bits(&s->gb, 11)];
    if (tab->len) {
        skip_bits(&s->gb, tab->len);
        s->mb_info.motion_code[r][n][1] = tab->value;
    } else {
        loge("motion_code error!\n");
        abort();
    }

    // motion_residual[r][n][1]  1-8 bits
    if (s->pic_code_extension.f_code[n][1] != 1 && s->mb_info.motion_code[r][n][1] != 0) {
        tmp = s->pic_code_extension.f_code[n][1] - 1;
        s->mb_info.motion_residual[r][n][1] = read_bits(&s->gb, tmp);
    }

    // dmvector[1]   1-2 bits
    if (s->mb_info.dmv == 1) {
        s->mb_info.dmvector[1] = (read_bits(&s->gb, 1) == 0) ? 0 : (read_bits(&s->gb, 1) ? -1 : 1);
    }
    mpeg2_motion_vector_calc(s, r, n, 1);

    if (s->mb_info.dmv == 1) {
        dual_prime_arithmetic(s, s->mb_info.dmvector, s->mb_info.mv[r][n][0],
                              s->mb_info.mv[r][n][1]);
    }

    return 0;
}

int mpeg2_motion_vectors(struct mpeg12_dec_ctx *s, int n)
{
    if (s->mb_info.motion_vector_count == 1) {
        if (s->mb_info.mv_format == MV_FORMAT_FIELD && s->mb_info.dmv != 1)
            s->mb_info.motion_vertical_field_select[0][n] = read_bits(&s->gb, 1);
        if (mpeg2_motion_vector(s, 0, n))
            return -1;

        // update other motion vector predictors
        s->mb_info.pmv[1][n][0] = s->mb_info.pmv[0][n][0];
        s->mb_info.pmv[1][n][1] = s->mb_info.pmv[0][n][1];
    } else {
        s->mb_info.motion_vertical_field_select[0][n] = read_bits(&s->gb, 1);
        if (mpeg2_motion_vector(s, 0, n))
            return -1;
        s->mb_info.motion_vertical_field_select[1][n] = read_bits(&s->gb, 1);
        if (mpeg2_motion_vector(s, 1, n))
            return -1;
    }
    return 0;
}
