/*
 * Copyright (C) 2020-2025 ArtInChip Technology Co. Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 *  author: <xiaodong.zhao@artinchip.com>
 *  Desc: mpeg4 recon bvop interface
 *
 */

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include "mp4_global.h"
#include "mp4_recon.h"
#include "mp4_tables.h"
#include "mp4_vars.h"
#include "mpeg4_decoder.h"

typedef void(recon_bvop_fun)(reference_t *ref);
typedef recon_bvop_fun *recon_bvop_fun_ptr;

recon_bvop_fun recon_bvop_forward;
recon_bvop_fun recon_bvop_backward;
recon_bvop_fun recon_bvop_bidir;
recon_bvop_fun recon_bvop_direct;

// quarter pixel (mp4_recon_qpel_bvop.c)
extern recon_bvop_fun recon_bvop_forward_qpel;
extern recon_bvop_fun recon_bvop_backward_qpel;
extern recon_bvop_fun recon_bvop_bidir_qpel;
extern recon_bvop_fun recon_bvop_direct_qpel;

#define Div2Round(x) (((x) >> 1) | (x & 1))

static const recon_bvop_fun_ptr recon_bvop_functions[4] = {recon_bvop_direct, recon_bvop_bidir, recon_bvop_backward, recon_bvop_forward};

void reconstruct_bvop(reference_t *ref, int mode)
{
    if ((mode < 0) || (mode > 3))
        return;
    recon_bvop_functions[mode](ref);
}

void recon_bvop_forward(reference_t *ref)
{
    struct mp4_state *mp4_state = ref->mp4_state;
    // int px, py;
    int dx = 0, dy = 0;
    int interlaced = mp4_state->hdr.interlaced && mp4_state->hdr.field_prediction;
    int dx_top = 0, dy_top = 0, dx_bottom = 0, dy_bottom = 0;
    int bx = mp4_state->hdr.mb_xpos;
    int by = mp4_state->hdr.mb_ypos;

#ifdef MPEG4_DUMP_ENABLE
    fprintf(g_mpeg4_ctx->fp_mb_info, "  bvop_forward mode\n");
    fprintf(g_mpeg4_ctx->fp_mb_info, "  fwd_ref_idx: %d, cur_frame_idx: %d\n", ref->fwd_frame_id, ref->cur_frame_id);
#endif

    // Lum
    if (!interlaced) {
        dx = mp4_state->mv[(by + 1) * mp4_state->MV_stride + bx + 1][4].x;
        dy = mp4_state->mv[(by + 1) * mp4_state->MV_stride + bx + 1][4].y;
    } else {
        motion_vector_t dv_top = mp4_state->mv[(by + 1) * mp4_state->MV_stride + bx + 1][4];
        motion_vector_t dv_bottom = mp4_state->mv_field[(by + 1) * mp4_state->MV_stride + bx + 1][4];
        dx_top = dv_top.x;
        dy_top = dv_top.y;
        dx_bottom = dv_bottom.x;
        dy_bottom = dv_bottom.y;

#ifdef MPEG4_DUMP_ENABLE
        fprintf(g_mpeg4_ctx->fp_mb_info, "  field pred, top ref: %d bot ref: %d\n", mp4_state->hdr.forward_top_field_reference,
                mp4_state->hdr.forward_bottom_field_reference);
        fprintf(g_mpeg4_ctx->fp_mb_info, "  Y component fwd top mv(%d %d)\n", dv_top.x, dv_top.y);
        fprintf(g_mpeg4_ctx->fp_mb_info, "  Y component fwd bot mv(%d %d)\n", dv_bottom.x, dv_bottom.y);
#endif
    }

    if (!interlaced) {
        dx = Div2Round(dx);
        dy = Div2Round(dy);

        if (mp4_state->hdr.quarter_pixel == 1) {
            // I have to round to the next half pel position
            dx = Div2Round(dx);
            dy = Div2Round(dy);
        }

        ref->fwd_mv_c[0][0] = dx;
        ref->fwd_mv_c[0][1] = dy;
        ref->fwd_mv_c[1][0] = 0;
        ref->fwd_mv_c[1][1] = 0;
        ref->bwd_mv_c[0][0] = 0;
        ref->bwd_mv_c[0][1] = 0;
        ref->bwd_mv_c[1][0] = 0;
        ref->bwd_mv_c[1][1] = 0;

#ifdef MPEG4_DUMP_ENABLE
        fprintf(g_mpeg4_ctx->fp_mb_info, "  chroma fwd_mv(%d, %d)\n", dx, dy);
#endif
    } else {
        if (mp4_state->hdr.quarter_pixel) {
            dx_top /= 2;
            dx_bottom /= 2;
            dy_top /= 2;
            dy_bottom /= 2;
        }

        dx_top = Div2Round(dx_top);
        dy_top = Div2Round(dy_top);
        dx_bottom = Div2Round(dx_bottom);
        dy_bottom = Div2Round(dy_bottom);

        ref->fwd_mv_c[0][0] = dx_top;
        ref->fwd_mv_c[0][1] = dy_top;
        ref->fwd_mv_c[1][0] = dx_bottom;
        ref->fwd_mv_c[1][1] = dy_bottom;
        ref->bwd_mv_c[0][0] = 0;
        ref->bwd_mv_c[0][1] = 0;
        ref->bwd_mv_c[1][0] = 0;
        ref->bwd_mv_c[1][1] = 0;

#ifdef MPEG4_DUMP_ENABLE
        fprintf(g_mpeg4_ctx->fp_mb_info, "chroma fwd top mv(%d, %d)\n", dx_top, dy_top);
        fprintf(g_mpeg4_ctx->fp_mb_info, "chroma fwd bot mv(%d, %d)\n", dx_bottom, dy_bottom);
#endif
    }
}

void recon_bvop_backward(reference_t *ref)
{
    struct mp4_state *mp4_state = ref->mp4_state;
    int dx = 0, dy = 0;
    int interlaced = mp4_state->hdr.interlaced && mp4_state->hdr.field_prediction;
    int dx_top = 0, dy_top = 0, dx_bottom = 0, dy_bottom = 0;
    int bx = mp4_state->hdr.mb_xpos;
    int by = mp4_state->hdr.mb_ypos;

#ifdef MPEG4_DUMP_ENABLE
    fprintf(g_mpeg4_ctx->fp_mb_info, "  bvop_backward\n");
    fprintf(g_mpeg4_ctx->fp_mb_info, "  bwd_ref_idx: %d, cur_frame_idx: %d\n", ref->bwd_frame_id, ref->cur_frame_id);
#endif

    // Lum
    if (!interlaced) {
        dx = mp4_state->mv[(by + 1) * mp4_state->MV_stride + bx + 1][5].x;
        dy = mp4_state->mv[(by + 1) * mp4_state->MV_stride + bx + 1][5].y;
#ifdef MPEG4_DUMP_ENABLE
        fprintf(g_mpeg4_ctx->fp_mb_info, "  bwd_mv(%d %d)\n", dx, dy);
#endif
    } else {
        motion_vector_t dv_top = mp4_state->mv[(by + 1) * mp4_state->MV_stride + bx + 1][5];
        motion_vector_t dv_bottom = mp4_state->mv_field[(by + 1) * mp4_state->MV_stride + bx + 1][5];
        dx_top = dv_top.x;
        dy_top = dv_top.y;
        dx_bottom = dv_bottom.x;
        dy_bottom = dv_bottom.y;

#ifdef MPEG4_DUMP_ENABLE
        fprintf(g_mpeg4_ctx->fp_mb_info, "  field pred, top ref: %d, bot ref: %d\n", mp4_state->hdr.backward_top_field_reference,
                mp4_state->hdr.backward_bottom_field_reference);
        fprintf(g_mpeg4_ctx->fp_mb_info, "  bwd top mv(%d %d), bot mv(%d, %d)\n", dx_top, dy_top >> 1, dx_bottom, dy_bottom >> 1);
#endif
    }

    if (!interlaced) {
        dx = (dx % 4 == 0 ? dx >> 1 : (dx >> 1) | 1);
        dy = (dy % 4 == 0 ? dy >> 1 : (dy >> 1) | 1);

        if (mp4_state->hdr.quarter_pixel == 1) {
            // I have to round to the next half pel position
            dx = (dx % 4 == 0 ? dx >> 1 : (dx >> 1) | 1);
            dy = (dy % 4 == 0 ? dy >> 1 : (dy >> 1) | 1);
        }

        ref->fwd_mv_c[0][0] = 0;
        ref->fwd_mv_c[0][1] = 0;
        ref->fwd_mv_c[1][0] = 0;
        ref->fwd_mv_c[1][1] = 0;
        ref->bwd_mv_c[0][0] = dx;
        ref->bwd_mv_c[0][1] = dy;
        ref->bwd_mv_c[1][0] = 0;
        ref->bwd_mv_c[1][1] = 0;

#ifdef MPEG4_DUMP_ENABLE
        fprintf(g_mpeg4_ctx->fp_mb_info, "  chroma bwd_mv(%d, %d)\n", dx, dy);
#endif
    } else {
        if (mp4_state->hdr.quarter_pixel) {
            dx_top /= 2;
            dx_bottom /= 2;
            dy_top /= 2;
            dy_bottom /= 2;
        }

        dx_top = Div2Round(dx_top);
        dy_top = Div2Round(dy_top);
        dx_bottom = Div2Round(dx_bottom);
        dy_bottom = Div2Round(dy_bottom);

        ref->fwd_mv_c[0][0] = 0;
        ref->fwd_mv_c[0][1] = 0;
        ref->fwd_mv_c[1][0] = 0;
        ref->fwd_mv_c[1][1] = 0;
        ref->bwd_mv_c[0][0] = dx_top;
        ref->bwd_mv_c[0][1] = dy_top;
        ref->bwd_mv_c[1][0] = dx_bottom;
        ref->bwd_mv_c[1][1] = dy_bottom;

#ifdef MPEG4_DUMP_ENABLE
        fprintf(g_mpeg4_ctx->fp_mb_info, "chroma bwd_mv(%d, %d)\n", dx_top, dy_top);
        fprintf(g_mpeg4_ctx->fp_mb_info, "chroma bwd_mv(%d, %d)\n", dx_bottom, dy_bottom);
#endif
    }
}

static int direct_delta(int field, int ref_field, int top_field_first)
{
    // Changed interlaced B direct time calculation by Robert Yuan,Sept 17,2003
    if (top_field_first)
        return field - ref_field;
    else
        return ref_field - field;
}

void recon_bvop_direct(reference_t *ref)
{
    struct mp4_state *mp4_state = ref->mp4_state;
    int comp;
    // int xp, yp, px, py;
    int dxf[4] = {0}, dxb[4] = {0}, dyf[4] = {0}, dyb[4] = {0};
    int sum, dcxf, dcyf, dcxb, dcyb; // mv
    motion_vector_t dv_top_for = {0}, dv_top_back = {0}, dv_bot_for = {0}, dv_bot_back = {0};
    int interlaced = mp4_state->hdr.interlaced && mp4_state->hdr.field_prediction;
    int trb = mp4_state->hdr.trb;
    int trd = mp4_state->hdr.trd;
    int bx = mp4_state->hdr.mb_xpos;
    int by = mp4_state->hdr.mb_ypos;

#ifdef MPEG4_DUMP_ENABLE
    fprintf(g_mpeg4_ctx->fp_mb_info, "  bvop direct mode, 8x8 block bi-pred\n");
    fprintf(g_mpeg4_ctx->fp_mb_info, "  fwd_idx: %d, bwd_idx: %d, cur_idx: %d\n", ref->fwd_frame_id, ref->bwd_frame_id,
            ref->cur_frame_id);
#endif

    // Lum
    if (!interlaced) {
        for (comp = 0; comp < 4; comp++) {
            // forward
            dxf[comp] = (trb * mp4_state->mv[(by + 1) * mp4_state->MV_stride + bx + 1][comp].x) / trd +
                        mp4_state->mv[(by + 1) * mp4_state->MV_stride + bx + 1][4].x;
            dyf[comp] = (trb * mp4_state->mv[(by + 1) * mp4_state->MV_stride + bx + 1][comp].y) / trd +
                        mp4_state->mv[(by + 1) * mp4_state->MV_stride + bx + 1][4].y;

            mp4_state->mp4_direct_mv[comp][0] = dxf[comp];
            mp4_state->mp4_direct_mv[comp][1] = dyf[comp];

            // backward
            dxb[comp] = (mp4_state->mv[(by + 1) * mp4_state->MV_stride + bx + 1][4].x == 0)
                            ? ((trb - trd) * mp4_state->mv[(by + 1) * mp4_state->MV_stride + bx + 1][comp].x) / trd
                            : dxf[comp] - mp4_state->mv[(by + 1) * mp4_state->MV_stride + bx + 1][comp].x;
            dyb[comp] = (mp4_state->mv[(by + 1) * mp4_state->MV_stride + bx + 1][4].y == 0)
                            ? ((trb - trd) * mp4_state->mv[(by + 1) * mp4_state->MV_stride + bx + 1][comp].y) / trd
                            : dyf[comp] - mp4_state->mv[(by + 1) * mp4_state->MV_stride + bx + 1][comp].y;

            mp4_state->mp4_direct_mv[4 + comp][0] = dxb[comp];
            mp4_state->mp4_direct_mv[4 + comp][1] = dyb[comp];
#ifdef MPEG4_DUMP_ENABLE
            fprintf(g_mpeg4_ctx->fp_mb_info, " block(%d), fwd_mv(%d, %d), bwd_mv(%d, %d)\n", comp, dxf[comp], dyf[comp], dxb[comp],
                    dyb[comp]);
#endif

            if (mp4_state->if_rm_h263) {
                mp4_state->mv[(by + 1) * mp4_state->MV_stride + bx + 1][comp].x = dxf[comp];
                mp4_state->mv[(by + 1) * mp4_state->MV_stride + bx + 1][comp].y = dyf[comp];
                mp4_state->mv_back[(by + 1) * mp4_state->MV_stride + bx + 1][comp].x = dxb[comp];
                mp4_state->mv_back[(by + 1) * mp4_state->MV_stride + bx + 1][comp].y = dyb[comp];
            }
        }
    } else {
        // we know that co-located macroblock is field-predicted
        motion_vector_t MVD = mp4_state->mv[(by + 1) * mp4_state->MV_stride + bx + 1][4];
        motion_vector_t MV0 =
            mp4_state->mv[(by + 1) * mp4_state->MV_stride + bx + 1][0]; // motion vector of top field of co-located macroblock
        motion_vector_t MV1 =
            mp4_state->mv_field[(by + 1) * mp4_state->MV_stride + bx + 1][0]; // motion vector of bottom field of co-located macroblock
        int TRB[2], TRD[2];
        /// fixme: where do we get mp4_state->hdr.forward_top_field_reference and such???
        TRB[0] = mp4_state->hdr.trbi + direct_delta(0, mp4_state->hdr.forward_top_field_reference, mp4_state->hdr.top_field_first);
        TRB[1] = mp4_state->hdr.trbi + direct_delta(1, mp4_state->hdr.forward_bottom_field_reference, mp4_state->hdr.top_field_first);
        TRD[0] = mp4_state->hdr.trdi + direct_delta(0, mp4_state->hdr.forward_top_field_reference, mp4_state->hdr.top_field_first);
        TRD[1] = mp4_state->hdr.trdi + direct_delta(1, mp4_state->hdr.forward_bottom_field_reference, mp4_state->hdr.top_field_first);

        dv_top_for.x = (TRB[0] * MV0.x) / TRD[0] + MVD.x;
        dv_top_for.y = (TRB[0] * MV0.y) / TRD[0] + MVD.y;
        dv_bot_for.x = (TRB[1] * MV1.x) / TRD[1] + MVD.x;
        dv_bot_for.y = (TRB[1] * MV1.y) / TRD[1] + MVD.y;

        dv_top_back.x = (MVD.x == 0) ? (((TRB[0] - TRD[0]) * MV0.x) / TRD[0]) : (dv_top_for.x - MV0.x);
        dv_top_back.y = (MVD.y == 0) ? (((TRB[0] - TRD[0]) * MV0.y) / TRD[0]) : (dv_top_for.y - MV0.y);
        dv_bot_back.x = (MVD.x == 0) ? (((TRB[1] - TRD[1]) * MV1.x) / TRD[1]) : (dv_bot_for.x - MV1.x);
        dv_bot_back.y = (MVD.y == 0) ? (((TRB[1] - TRD[1]) * MV1.y) / TRD[1]) : (dv_bot_for.y - MV1.y);

        mp4_state->mp4_direct_mv[0][0] = dv_top_for.x;
        mp4_state->mp4_direct_mv[0][1] = dv_top_for.y;
        mp4_state->mp4_direct_mv[2][0] = dv_bot_for.x;
        mp4_state->mp4_direct_mv[2][1] = dv_bot_for.y;

        mp4_state->mp4_direct_mv[1][0] = dv_top_back.x;
        mp4_state->mp4_direct_mv[1][1] = dv_top_back.y;
        mp4_state->mp4_direct_mv[3][0] = dv_bot_back.x;
        mp4_state->mp4_direct_mv[3][1] = dv_bot_back.y;

#ifdef MPEG4_DUMP_ENABLE
        fprintf(g_mpeg4_ctx->fp_mb_info, "field pred, fwd, top_field_ref: %d, bot_field_ref: %d\n",
                mp4_state->hdr.forward_top_field_reference, mp4_state->hdr.forward_bottom_field_reference);
        fprintf(g_mpeg4_ctx->fp_mb_info, "field, fwd_top_mv(%d, %d), fwd_bot_mv(%d, %d)\n", dv_top_for.x, dv_top_for.y >> 1,
                dv_bot_for.x, dv_bot_for.y >> 1);
        fprintf(g_mpeg4_ctx->fp_mb_info, "field pred, bwd: top_field_ref: %d, bot_field_ref: %d\n",
                mp4_state->hdr.backward_top_field_reference, mp4_state->hdr.backward_bottom_field_reference);
        fprintf(g_mpeg4_ctx->fp_mb_info, "field, bwd_top_mv(%d, %d), bwd_bot_mv(%d, %d)\n", dv_top_back.x, dv_top_back.y >> 1,
                dv_bot_back.x, dv_bot_back.y >> 1);
#endif
    }

    sum = dxf[0] + dxf[1] + dxf[2] + dxf[3];
    if (sum == 0)
        dcxf = 0;
    else
        dcxf = sign(sum) * (roundtab[abs(sum) % 16] + (abs(sum) / 16) * 2);
    sum = dyf[0] + dyf[1] + dyf[2] + dyf[3];
    if (sum == 0)
        dcyf = 0;
    else
        dcyf = sign(sum) * (roundtab[abs(sum) % 16] + (abs(sum) / 16) * 2);

    sum = dxb[0] + dxb[1] + dxb[2] + dxb[3];
    if (sum == 0)
        dcxb = 0;
    else
        dcxb = sign(sum) * (roundtab[abs(sum) % 16] + (abs(sum) / 16) * 2);
    sum = dyb[0] + dyb[1] + dyb[2] + dyb[3];
    if (sum == 0)
        dcyb = 0;
    else
        dcyb = sign(sum) * (roundtab[abs(sum) % 16] + (abs(sum) / 16) * 2);

    if (mp4_state->hdr.quarter_pixel == 1) {
        // I have to round to the next half pel position
        dcxb = Div2Round(dcxb);
        dcyb = Div2Round(dcyb);
        dcxf = Div2Round(dcxf);
        dcyf = Div2Round(dcyf);
    }

    if (!interlaced) {
        ref->fwd_mv_c[0][0] = dcxf;
        ref->fwd_mv_c[0][1] = dcyf;
        ref->bwd_mv_c[0][0] = dcxb;
        ref->bwd_mv_c[0][1] = dcyb;
#ifdef MPEG4_DUMP_ENABLE
        fprintf(g_mpeg4_ctx->fp_mb_info, "  chroma fwd_mv(%d, %d), bwd_mv(%d, %d)\n", dcxf, dcyf, dcxb, dcyb);
#endif
    } else {
        if (mp4_state->hdr.quarter_pixel == 1) {
            dv_top_for.x /= 2;
            dv_top_for.y /= 2;
            dv_bot_for.x /= 2;
            dv_bot_for.y /= 2;
            dv_top_back.x /= 2;
            dv_top_back.y /= 2;
            dv_bot_back.x /= 2;
            dv_bot_back.y /= 2;
        }
        dv_top_for.x = Div2Round(dv_top_for.x);
        dv_top_for.y = Div2Round(dv_top_for.y);
        dv_bot_for.x = Div2Round(dv_bot_for.x);
        dv_bot_for.y = Div2Round(dv_bot_for.y);
        dv_top_back.x = Div2Round(dv_top_back.x);
        dv_top_back.y = Div2Round(dv_top_back.y);
        dv_bot_back.x = Div2Round(dv_bot_back.x);
        dv_bot_back.y = Div2Round(dv_bot_back.y);

        ref->fwd_mv_c[0][0] = dv_top_for.x;
        ref->fwd_mv_c[0][1] = dv_top_for.y;
        ref->fwd_mv_c[1][0] = dv_bot_for.x;
        ref->fwd_mv_c[1][1] = dv_bot_for.y;
        ref->bwd_mv_c[0][0] = dv_top_back.x;
        ref->bwd_mv_c[0][1] = dv_top_back.y;
        ref->bwd_mv_c[1][0] = dv_bot_back.x;
        ref->bwd_mv_c[1][1] = dv_bot_back.y;

#ifdef MPEG4_DUMP_ENABLE
        fprintf(g_mpeg4_ctx->fp_mb_info, "  chroma fwd_top_mv(%d, %d), fwd_bot_mv(%d, %d)\n", dv_top_for.x, dv_top_for.y >> 1,
                dv_bot_for.x, dv_bot_for.y >> 1);
        fprintf(g_mpeg4_ctx->fp_mb_info, "  chroma bwd_top_mv(%d, %d), bwd_bot_mv(%d, %d)\n", dv_top_back.x, dv_top_back.y >> 1,
                dv_bot_back.x, dv_bot_back.y >> 1);
#endif
    }
}

void recon_bvop_bidir(reference_t *ref)
{
    struct mp4_state *mp4_state = ref->mp4_state;
    // int px, py;
    int interlaced = mp4_state->hdr.interlaced && mp4_state->hdr.field_prediction;
    motion_vector_t dv_top_for = {0}, dv_bot_for = {0}, dv_top_back = {0}, dv_bot_back = {0};
    int bx = mp4_state->hdr.mb_xpos;
    int by = mp4_state->hdr.mb_ypos;

#ifdef MPEG4_DUMP_ENABLE
    fprintf(g_mpeg4_ctx->fp_mb_info, "  bi-direct mode\n");
    fprintf(g_mpeg4_ctx->fp_mb_info, "  fwd_idx: %d, bwd_idx: %d, cur_idx: %d\n", ref->fwd_frame_id, ref->bwd_frame_id,
            ref->cur_frame_id);
#endif

    if (!interlaced) {
        dv_top_for = mp4_state->mv[(by + 1) * mp4_state->MV_stride + bx + 1][4];

#ifdef MPEG4_DUMP_ENABLE
        fprintf(g_mpeg4_ctx->fp_mb_info, "  Y component fwd mv(%d %d)\n", dv_top_for.x, dv_top_for.y);
#endif

    } else {
        dv_top_for = mp4_state->mv[(by + 1) * mp4_state->MV_stride + bx + 1][4];
        dv_bot_for = mp4_state->mv_field[(by + 1) * mp4_state->MV_stride + bx + 1][4];

#ifdef MPEG4_DUMP_ENABLE
        fprintf(g_mpeg4_ctx->fp_mb_info, "  Y component top field fwd mv(%d %d)\n", dv_top_for.x, dv_top_for.y);
        fprintf(g_mpeg4_ctx->fp_mb_info, "  Y component top field fwd mv(%d %d)\n", dv_bot_for.x, dv_bot_for.y);
#endif
    }

    // * backward *
    if (!interlaced) {
        dv_top_back = mp4_state->mv[(by + 1) * mp4_state->MV_stride + bx + 1][5];
#ifdef MPEG4_DUMP_ENABLE
        fprintf(g_mpeg4_ctx->fp_mb_info, "  Y component bwd mv(%d %d)\n", dv_top_back.x, dv_top_back.y);
#endif
    } else {
        dv_top_back = mp4_state->mv[(by + 1) * mp4_state->MV_stride + bx + 1][5];
        dv_bot_back = mp4_state->mv_field[(by + 1) * mp4_state->MV_stride + bx + 1][5];
#ifdef MPEG4_DUMP_ENABLE
        fprintf(g_mpeg4_ctx->fp_mb_info, "  Y component top field bwd mv(%d %d)\n", dv_top_back.x, dv_top_back.y);
        fprintf(g_mpeg4_ctx->fp_mb_info, "  Y component top field bwd mv(%d %d)\n", dv_bot_back.x, dv_bot_back.y);
#endif
    }

    if (!interlaced) {
        int dcxf, dcyf, dcxb, dcyb;

        dcxf = Div2Round(dv_top_for.x);
        dcyf = Div2Round(dv_top_for.y);
        dcxb = Div2Round(dv_top_back.x);
        dcyb = Div2Round(dv_top_back.y);

        if (mp4_state->hdr.quarter_pixel == 1) {
            // I have to round to the next half pel position
            dcxb = Div2Round(dcxb);
            dcyb = Div2Round(dcyb);
            dcxf = Div2Round(dcxf);
            dcyf = Div2Round(dcyf);
        }

        ref->fwd_mv_c[0][0] = dcxf;
        ref->fwd_mv_c[0][1] = dcyf;
        ref->bwd_mv_c[0][0] = dcxb;
        ref->bwd_mv_c[0][1] = dcyb;
#ifdef MPEG4_DUMP_ENABLE
        fprintf(g_mpeg4_ctx->fp_mb_info, "  chroma fwd_mv(%d, %d), bwd_mv(%d, %d)\n", dcxf, dcyf, dcxb, dcyb);
#endif
    } else {
        if (mp4_state->hdr.quarter_pixel == 1) {
            dv_top_for.x /= 2;
            dv_top_for.y /= 2;
            dv_bot_for.x /= 2;
            dv_bot_for.y /= 2;
            dv_top_back.x /= 2;
            dv_top_back.y /= 2;
            dv_bot_back.x /= 2;
            dv_bot_back.y /= 2;
        }
        dv_top_for.x = Div2Round(dv_top_for.x);
        dv_top_for.y = Div2Round(dv_top_for.y);
        dv_bot_for.x = Div2Round(dv_bot_for.x);
        dv_bot_for.y = Div2Round(dv_bot_for.y);
        dv_top_back.x = Div2Round(dv_top_back.x);
        dv_top_back.y = Div2Round(dv_top_back.y);
        dv_bot_back.x = Div2Round(dv_bot_back.x);
        dv_bot_back.y = Div2Round(dv_bot_back.y);

        ref->fwd_mv_c[0][0] = dv_top_for.x;
        ref->fwd_mv_c[0][1] = dv_top_for.y;
        ref->fwd_mv_c[1][0] = dv_bot_for.x;
        ref->fwd_mv_c[1][1] = dv_bot_for.y;
        ref->bwd_mv_c[0][0] = dv_top_back.x;
        ref->bwd_mv_c[0][1] = dv_top_back.y;
        ref->bwd_mv_c[1][0] = dv_bot_back.x;
        ref->bwd_mv_c[1][1] = dv_bot_back.y;

#ifdef MPEG4_DUMP_ENABLE
        fprintf(g_mpeg4_ctx->fp_mb_info, "chroma fwd_top_mv(%d, %d), fwd_bot_mv(%d, %d)\n", dv_top_for.x, dv_top_for.y, dv_bot_for.x,
                dv_bot_for.y);
        fprintf(g_mpeg4_ctx->fp_mb_info, "chroma bwd_top_mv(%d, %d), bwd_bot_mv(%d, %d)\n", dv_top_back.x, dv_top_back.y, dv_bot_back.x,
                dv_bot_back.y);
#endif
    }
}
