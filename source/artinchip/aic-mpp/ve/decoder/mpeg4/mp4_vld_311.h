/*
 * Copyright (C) 2020-2025 ArtInChip Technology Co. Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 *  author: <xiaodong.zhao@artinchip.com>
 *  Desc: mpeg4 vld 311 interface
 *
 */

#include "mp4_decore.h"
#include "mp4_header.h"
#include "mp4_types_311.h"
#include "mp4_vld.h"

extern event_t vld_intra_dct_311_0_lum(struct mp4_state *, mp4_stream_t *ld);
extern event_t vld_intra_dct_311_10_lum(struct mp4_state *, mp4_stream_t *ld);
extern event_t vld_intra_dct_311_11_lum(struct mp4_state *, mp4_stream_t *ld);
extern event_t vld_intra_dct_311_0_chrom(struct mp4_state *, mp4_stream_t *ld);
extern event_t vld_intra_dct_311_10_chrom(struct mp4_state *, mp4_stream_t *ld);
extern event_t vld_intra_dct_311_11_chrom(struct mp4_state *, mp4_stream_t *ld);
extern event_t vld_inter_dct_311_0(struct mp4_state *, mp4_stream_t *ld);
extern event_t vld_inter_dct_311_10(struct mp4_state *, mp4_stream_t *ld);
extern event_t vld_inter_dct_311_11(struct mp4_state *, mp4_stream_t *ld);

extern void get_mv_data_311_0(mp4_stream_t *ld, int *mv_x, int *mv_y);
extern void get_mv_data_311_1(mp4_stream_t *ld, int *mv_x, int *mv_y);

extern short get_dc_311_0_lum(struct mp4_state *, mp4_stream_t *ld);
extern short get_dc_311_1_lum(struct mp4_state *, mp4_stream_t *ld);
extern short get_dc_311_0_chrom(struct mp4_state *, mp4_stream_t *ld);
extern short get_dc_311_1_chrom(struct mp4_state *, mp4_stream_t *ld);

extern const int cbp_intra_indices[];
extern const int cbp_inter_indices[];
extern const int cbp_inter_indices2[];
extern const int cbp_inter_indices3[];
extern const int cbp_inter_indices4[];

static inline short get_cbp_311_i(mp4_stream_t *ld)
{
    short cbp;
    if (getbits1(ld))
        cbp = 0;
    else
        cbp = get_short_311(ld, cbp_intra_indices);
    return cbp;
}

static inline short get_cbp_311_p(mp4_stream_t *ld)
{
    short cbp;
    cbp = get_short_311(ld, cbp_inter_indices);
    return cbp;
}

static inline short get_cbp_311_p2(struct mp4_state *mp4_state, mp4_stream_t *ld)
{
    short cbp = 0;

    if (mp4_state->hdr.cbp_table_index == 0)
        cbp = get_short_311(ld, cbp_inter_indices2);
    else if (mp4_state->hdr.cbp_table_index == 1)
        cbp = get_short_311(ld, cbp_inter_indices3);
    else if (mp4_state->hdr.cbp_table_index == 2)
        cbp = get_short_311(ld, cbp_inter_indices4);

    return cbp;
}
