/*
 * Copyright (C) 2020-2025 ArtInChip Technology Co. Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 *  author: <xiaodong.zhao@artinchip.com>
 *  Desc: mpeg4 vld 311 interface
 *
 */

#include "mp4_vld_311.h"
#include "mp4_getbits.h"
#include "mp4_tables_311.h"
#include "mp4_types_311.h"

const short intra_mb_runs_last_10[] = {
    -999, 0x24, 0xf, 4, 3, 1, 0
};

const short intra_mb_runs_10[] = {
    -999, 0x1d, 0x10, 0xf, 9, 5, 4, 3,
    3, 3, 3, 3, 2, 1, 1, 1, 1, 1, 1, 1
};

const short intra_mb_levels_10[] = {
    0x13, 0xf, 0xc, 0xb, 6, 5, 4, 4,
    4,    4,   3,   3,   3, 3, 3, 3,
    2,    2,   1,   1,   1, 1, 1, 1,
    1,    1,   1,   1,   1, 1, 1
};

const short intra_mb_levels_last_10[] = {
    6, 5, 4, 4, 3, 2, 2, 2,
    2, 2, 2, 2, 2, 2, 2, 2,
    1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1
};

const short intra_mb_levels_11[] = {
    27, 10, 5, 4, 3, 3, 3, 3,
    2,   2, 1, 1, 1, 1, 1
};

const short intra_mb_levels_last_11[] = {
    8, 3, 2, 2, 2, 2, 2, 1,
    1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1
};

const short intra_mb_runs_11[] = {
    -999, 13, 8, 7, 3, 2, 1, 1,
    1,    1,  1, 0, 0, 0, 0, 0,
    0,    0,  0, 0, 0, 0, 0, 0,
    0,    0,  0, 0
};

const short intra_mb_runs_last_11[] = {
    -999, 19, 6, 1, 0, 0, 0, 0, 0
};

const short intra_mb_runs_last_0[] = {
    -999, 0x19, 0xd, 3, 1
};

const short intra_mb_runs_0[] = {
    -999, 0x13, 0xe, 0xc, 6, 4, 3, 3,
    2,    1,    1,   1,   0, 0, 0, 0,
    0
};

const short intra_mb_levels_last_0[] = {
    4, 4, 3, 3, 2, 2, 2, 2,
    2, 2, 2, 2, 2, 2, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1
};

const short intra_mb_levels_0[] = {
    0x10, 0xb, 8, 7, 5, 4, 4, 3,
    3,    3,   3, 3, 3, 3, 2, 2,
    1,    1,   1, 1, 1
};

const short inter_mb_runs_last_0[] = {
    -999, 43, 15, 3, 1, 0
};

const short inter_mb_runs_0[] = {
    -999, 29, 15, 12, 5, 2, 1, 1,
    1,    1,  0,  0,  0, 0, 0
};

const short inter_mb_levels_last_0[] = {
    5, 4, 3, 3, 2, 2, 2, 2,
    2, 2, 2, 2, 2, 2, 2, 2,
    1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1
};

const short inter_mb_levels_0[] = {
    14, 9, 5, 4, 4, 4, 3, 3,
    3,  3, 3, 3, 3, 2, 2, 2,
    1,  1, 1, 1, 1, 1, 1, 1,
    1,  1, 1, 1, 1, 1,
};

const short inter_mb_runs_last_10[] = {
    -999, 36, 14, 6, 3, 1, 0, 0, 0, 0
};

const short inter_mb_runs_10[] = {
    -999, 26, 16, 11, 7, 5, 3, 3,
    2,    1,  1,  1,  0, 0, 0, 0,
    0,    0,  0,  0,  0, 0, 0, 0
};

const short inter_mb_levels_last_10[] = {
    9, 5, 4, 4, 3, 3, 3, 2,
    2, 2, 2, 2, 2, 2, 2, 1,
    1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1
};

const short inter_mb_levels_10[] = {
    23, 11, 8, 7, 5, 5, 4, 4,
    3,  3,  3, 3, 2, 2, 2, 2,
    2,  1,  1, 1, 1, 1, 1, 1,
    1, 1, 1
};

const short inter_mb_levels_11[] = {
    12, 6, 4, 3, 3, 3, 3, 2,
    2,  2, 2, 1, 1, 1, 1, 1,
    1,  1, 1, 1, 1, 1, 1, 1,
    1, 1, 1
};

const short inter_mb_levels_last_11[] = {
    3, 2, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1,
    1
};

const short inter_mb_runs_11[] = {
    -13, 26, 10, 6, 2, 1, 1, 0,
    0, 0, 0, 0, 0
};

const short inter_mb_runs_last_11[] = {
    -13, 40, 1, 0
};

static inline event_t vld_dct_311(struct mp4_state *mp4_state, mp4_stream_t *ld, const short *runs,
                                  const short *runs_last, const short *levels, const short *levels_last,
                                  const item_t *items, const int *indices, int intra_chrom, int run_diff)
{
    event_t e;
    e = get_event_311(ld, items, indices);
    if ((e.run >= 0) && (e.level > 0)) {
        if (getbits1(ld))
            e.level = -e.level;
        return e;
    }
    if ((e.run < 0) && (e.level < 0)) {
        if (mp4_state->msmpeg_version < 4) {
            e.last = getbits1(ld);
            e.run = getbits(ld, 6);
            e.level = getbits(ld, 8);
            if (e.level & 128)
                e.level = -1 - (e.level ^ 255);
        } else {
            int ls;
            e.last = getbits1(ld);
            if (!mp4_state->hdr.esc3_level_length) {
                int ll;
                if (mp4_state->hdr.quantizer < 8) {
                    ll = getbits(ld, 3);
                    if (ll == 0) {
                        getbits1(ld);
                        ll = 8;
                    }
                } else {
                    ll = 2;
                    while (ll < 8 && (showbits(ld, 1) == 0)) {
                        ll++;
                        getbits1(ld);
                    }
                    if (ll < 8)
                        getbits1(ld);
                }
                mp4_state->hdr.esc3_level_length = ll;
                mp4_state->hdr.esc3_run_length = 3 + getbits(ld, 2);
            }
            e.run = getbits(ld, mp4_state->hdr.esc3_run_length);
            ls = getbits1(ld);
            e.level = getbits(ld, mp4_state->hdr.esc3_level_length);
            if (ls)
                e.level = -e.level;
        }
        return e;
    }
    if (e.run < 0) {
        e = get_event_311(ld, items, indices);
        if (e.last)
            e.run += runs_last[e.level] + 1 - intra_chrom;
        else
            e.run += runs[e.level] + 1 - intra_chrom;
        e.run += run_diff;
        if (getbits1(ld))
            e.level = -e.level;
        return e;
    }
    e = get_event_311(ld, items, indices);
    if (e.last)
        e.level += levels_last[e.run];
    else
        e.level += levels[e.run];
    if (getbits1(ld))
        e.level = -e.level;
    return e;
}

event_t vld_intra_dct_311_0_lum(struct mp4_state *mp4_state, mp4_stream_t *ld)
{
    int run_diff;
    if (mp4_state->msmpeg_version >= 4)
        run_diff = 1;
    else
        run_diff = 0;
    return vld_dct_311(mp4_state, ld, intra_mb_runs_0, intra_mb_runs_last_0, intra_mb_levels_0, intra_mb_levels_last_0,
                       intra_0_items, intra_0_indices, 0, run_diff);
}

event_t vld_intra_dct_311_10_lum(struct mp4_state *mp4_state, mp4_stream_t *ld)
{
    int run_diff;
    if (mp4_state->msmpeg_version >= 4)
        run_diff = 1;
    else
        run_diff = 0;
    return vld_dct_311(mp4_state, ld, intra_mb_runs_10, intra_mb_runs_last_10, intra_mb_levels_10,
                       intra_mb_levels_last_10, intra_10_items, intra_10_indices, 0, run_diff);
}

event_t vld_intra_dct_311_11_lum(struct mp4_state *mp4_state, mp4_stream_t *ld)
{
    int run_diff;
    if (mp4_state->msmpeg_version >= 4)
        run_diff = 1;
    else
        run_diff = 0;
    return vld_dct_311(mp4_state, ld, intra_mb_runs_11, intra_mb_runs_last_11, intra_mb_levels_11,
                       intra_mb_levels_last_11, intra_11_items, intra_11_indices, 0, run_diff);
}

event_t vld_intra_dct_311_0_chrom(struct mp4_state *mp4_state, mp4_stream_t *ld)
{
    int run_diff;
    if (mp4_state->msmpeg_version >= 4)
        run_diff = 1;
    else
        run_diff = 0;
    return vld_dct_311(mp4_state, ld, inter_mb_runs_0, inter_mb_runs_last_0, inter_mb_levels_0, inter_mb_levels_last_0,
                       inter_0_items, inter_0_indices, 1, run_diff);
}

event_t vld_intra_dct_311_10_chrom(struct mp4_state *mp4_state, mp4_stream_t *ld)
{
    int run_diff;
    if (mp4_state->msmpeg_version >= 4)
        run_diff = 1;
    else
        run_diff = 0;
    return vld_dct_311(mp4_state, ld, inter_mb_runs_10, inter_mb_runs_last_10, inter_mb_levels_10,
                       inter_mb_levels_last_10, inter_10_items, inter_10_indices, 1, run_diff);
}

event_t vld_intra_dct_311_11_chrom(struct mp4_state *mp4_state, mp4_stream_t *ld)
{
    int run_diff;
    if (mp4_state->msmpeg_version >= 4)
        run_diff = 1;
    else
        run_diff = 0;
    return vld_dct_311(mp4_state, ld, inter_mb_runs_11, inter_mb_runs_last_11, inter_mb_levels_11,
                       inter_mb_levels_last_11, inter_11_items, inter_11_indices, 1, run_diff);
}

event_t vld_inter_dct_311_0(struct mp4_state *mp4_state, mp4_stream_t *ld)
{
    return vld_dct_311(mp4_state, ld, inter_mb_runs_0, inter_mb_runs_last_0, inter_mb_levels_0,
                       inter_mb_levels_last_0, inter_0_items, inter_0_indices, 0, 0);
}

event_t vld_inter_dct_311_10(struct mp4_state *mp4_state, mp4_stream_t *ld)
{
    return vld_dct_311(mp4_state, ld, inter_mb_runs_10, inter_mb_runs_last_10, inter_mb_levels_10,
                       inter_mb_levels_last_10, inter_10_items, inter_10_indices, 0, 0);
}

event_t vld_inter_dct_311_11(struct mp4_state *mp4_state, mp4_stream_t *ld)
{
    return vld_dct_311(mp4_state, ld, inter_mb_runs_11, inter_mb_runs_last_11, inter_mb_levels_11,
                       inter_mb_levels_last_11, inter_11_items, inter_11_indices, 0, 0);
}

#define DC_MAX 119
static inline short get_dc_311(struct mp4_state *s, mp4_stream_t *ld, const int *indices)
{
    short value;
    value = get_short_311(ld, indices);
    if (value == DC_MAX && s->msmpeg_version >= 4)
        value = getbits(ld, 8);
    if (value)
        if (getbits1(ld) == 1)
            value = -value;
    return value;
}

/* for wmv1/2, the maximum DC level is 0x77, but divx3.11 expands it, so the table is different.
 */
short get_dc_311_0_lum(struct mp4_state *s, mp4_stream_t *ld)
{
    if (s->msmpeg_version < 4)
        return get_dc_311(s, ld, dc_lum0_indices);
    else
        return get_dc_311(s, ld, wmv2_table0_dc_luma);
}

short get_dc_311_1_lum(struct mp4_state *s, mp4_stream_t *ld)
{
    if (s->msmpeg_version < 4)
        return get_dc_311(s, ld, dc_lum1_indices);
    else
        return get_dc_311(s, ld, wmv2_table1_dc_luma);
}

short get_dc_311_0_chrom(struct mp4_state *s, mp4_stream_t *ld)
{
    if (s->msmpeg_version < 4)
        return get_dc_311(s, ld, dc_chrom0_indices);
    else
        return get_dc_311(s, ld, wmv2_table0_dc_chroma);
}

short get_dc_311_1_chrom(struct mp4_state *s, mp4_stream_t *ld)
{
    if (s->msmpeg_version < 4)
        return get_dc_311(s, ld, dc_chrom1_indices);
    else
        return get_dc_311(s, ld, wmv2_table1_dc_chroma);
}

static inline void get_mv_data_311(mp4_stream_t *ld, int *mv_x, int *mv_y, const mv_item_t *items, const int *indices)
{
    const struct ShortVector *mv;
    mv = get_motionvector_311(ld, items, indices);
    if (mv->x != -128) {
        *mv_x = mv->x;
        *mv_y = mv->y;
        return;
    }
    *mv_x = getbits(ld, 6) - 32;
    *mv_y = getbits(ld, 6) - 32;
}

void get_mv_data_311_0(mp4_stream_t *ld, int *mv_x, int *mv_y)
{
    get_mv_data_311(ld, mv_x, mv_y, mv_tree0_items, mv_tree0_indices);
}

void get_mv_data_311_1(mp4_stream_t *ld, int *mv_x, int *mv_y)
{
    get_mv_data_311(ld, mv_x, mv_y, mv_tree1_items, mv_tree1_indices);
}
