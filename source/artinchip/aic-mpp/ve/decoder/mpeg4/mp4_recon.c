/*
 * Copyright (C) 2020-2025 ArtInChip Technology Co. Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 *  author: <xiaodong.zhao@artinchip.com>
 *  Desc: mpeg4 recon interface
 *
 */

#include <stdlib.h>
#include "mp4_recon.h"
#include "mp4_tables.h"
#include "mp4_vars.h"
#include "mpeg4_decoder.h"

static const int eight_table[8] = {0, 0, 1, 1, 1, 1, 1, 2};

// Various rounding modes
#define EightRound(x)   (2 * ((x) >> 3) + eight_table[x & 7]);
#define Div2Round(x)    (((x) >> 1) | (x & 1))

void reconstruct(reference_t *ref)
{
    struct mp4_state *mp4_state = ref->mp4_state;
    int comp, sum_x, sum_y;
    motion_vector_t dv;
    int x, y;
    const int bx = mp4_state->hdr.mb_xpos;
    const int by = mp4_state->hdr.mb_ypos;
    const int mode = mp4_state->hdr.derived_mb_type;
    const motion_vector_t *mv_base, *mv_base_field;

    x = bx + 1;
    y = by + 1;

    mv_base = mp4_state->mv[y * mp4_state->MV_stride + x];
    mv_base_field = mp4_state->mv_field[y * mp4_state->MV_stride + x];

#ifdef MPEG4_DUMP_ENABLE
    fprintf(g_mpeg4_ctx->fp_mb_info, "  fwd_idx: %d, cur_idx: %d\n", ref->fwd_frame_id, ref->cur_frame_id);
#endif

    if (mode == INTER4V) {
#ifdef MPEG4_DUMP_ENABLE
        fprintf(g_mpeg4_ctx->fp_mb_info, "  inter4V, 8x8 block MC\n");
#endif
        for (comp = 0; comp < 4; comp++) {
            dv = mv_base[comp];
#ifdef MPEG4_DUMP_ENABLE
            fprintf(g_mpeg4_ctx->fp_mb_info, "  block(%d), mv(%d %d)\n", comp, dv.x, dv.y);
#endif
        }
    } else {
        if ((!mp4_state->hdr.interlaced) || (!mp4_state->hdr.field_prediction)) {
            dv = mv_base[0];
#ifdef MPEG4_DUMP_ENABLE
            fprintf(g_mpeg4_ctx->fp_mb_info, "  16x16 MC, mv(%d %d)\n", dv.x, dv.y);
#endif
        } else {
#ifdef MPEG4_DUMP_ENABLE
            motion_vector_t dv_top = mv_base[0];
            motion_vector_t dv_bottom = mv_base_field[0];
            fprintf(g_mpeg4_ctx->fp_mb_info, "  mb(%d %d), field MC, top_mv(%d %d), bot_mv(%d %d)\n", bx, by, dv_top.x, dv_top.y >> 1,
                    dv_bottom.x, dv_bottom.y >> 1);
            fprintf(g_mpeg4_ctx->fp_mb_info, "  ref frame idx: %d, top field ref: %d, bot field ref: %d\n", ref->fwd_frame_id,
                    mp4_state->hdr.forward_top_field_reference, mp4_state->hdr.forward_bottom_field_reference);
#endif
        }
    }

    if ((!mp4_state->hdr.interlaced) || (!mp4_state->hdr.field_prediction)) {
        // ~interlaced
        if (mode == INTER4V) {
            // $7.6.5, in quarter sample mode vectors are splitted by two before summation
            if (mp4_state->hdr.quarter_pixel) {
                sum_x = (mv_base[0].x >> 1) + (mv_base[1].x >> 1) + (mv_base[2].x >> 1) + (mv_base[3].x >> 1);
                sum_y = (mv_base[0].y >> 1) + (mv_base[1].y >> 1) + (mv_base[2].y >> 1) + (mv_base[3].y >> 1);
            } else {
                sum_x = mv_base[0].x + mv_base[1].x + mv_base[2].x + mv_base[3].x;
                sum_y = mv_base[0].y + mv_base[1].y + mv_base[2].y + mv_base[3].y;
            }

            dv.x = sign(sum_x) * (roundtab[abs(sum_x) % 16] + (abs(sum_x) / 16) * 2);
            dv.y = sign(sum_y) * (roundtab[abs(sum_y) % 16] + (abs(sum_y) / 16) * 2);
        } else {
            dv = mv_base[0];

            // chroma rounding
            if (mp4_state->hdr.quarter_pixel) {
                if ((mp4_state->userdata_codec_version == 0) || (mp4_state->userdata_codec_version >= 503)) {
                    dv.x = EightRound(dv.x); // $7.6.5, tab 7-8 of the standard
                    dv.y = EightRound(dv.y);
                } else {
                    dv.x = Div2Round(dv.x);
                    dv.y = Div2Round(dv.y);
                    dv.x = Div2Round(dv.x); // round to the next half pel position
                    dv.y = Div2Round(dv.y);
                }
            } else {
                dv.x = Div2Round(dv.x);
                dv.y = Div2Round(dv.y);
            }
        }

        ref->fwd_mv_c[0][0] = dv.x;
        ref->fwd_mv_c[0][1] = dv.y;
#ifdef MPEG4_DUMP_ENABLE
        fprintf(g_mpeg4_ctx->fp_mb_info, "  chroma mv(%d %d)\n", dv.x, dv.y);
#endif
    } else {
        // interlaced
        int dx_top, dx_bottom, dy_top, dy_bottom;

        dx_top = mv_base[0].x;
        dy_top = mv_base[0].y;

        dx_bottom = mv_base_field[0].x;
        dy_bottom = mv_base_field[0].y;

        // $7.7.2.1, field_motion_compensate for chroma
        dx_top = Div2Round(dx_top); // note: same as Div2Round
        dx_bottom = Div2Round(dx_bottom);
        dy_top = Div2Round(dy_top);
        dy_bottom = Div2Round(dy_bottom);

        if (mp4_state->hdr.quarter_pixel) {
            dx_top = Div2Round(dx_top);
            dy_top = Div2Round(dy_top);
            dx_bottom = Div2Round(dx_bottom);
            dy_bottom = Div2Round(dy_bottom);
        }

        ref->fwd_mv_c[0][0] = dx_top;
        ref->fwd_mv_c[0][1] = dy_top;
        ref->fwd_mv_c[1][0] = dx_bottom;
        ref->fwd_mv_c[1][1] = dy_bottom;
#ifdef MPEG4_DUMP_ENABLE
        fprintf(g_mpeg4_ctx->fp_mb_info, "  chroma top_mv(%d %d), bot_mv(%d %d)\n", dx_top, dy_top >> 1, dx_bottom, dy_bottom >> 1);
#endif
    }
}
