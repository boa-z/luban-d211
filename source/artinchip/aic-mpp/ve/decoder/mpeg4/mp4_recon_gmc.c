/*
 * Copyright (C) 2020-2025 ArtInChip Technology Co. Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 *  author: <xiaodong.zhao@artinchip.com>
 *  Desc: mpeg4 recon gmc interface
 *
 */

#include "mp4_recon.h"
#include "mpeg4_decoder.h"


static inline int div_twoslash(int v1, int v2)
{
    if (v2 <= 0 || (v2 & 1)) {
        loge("v2 <= 0 || (v2 & 1)");
        return 0;
    }
    if (v1 > 0)
        return (v1 + v2 / 2) / v2;
    else
        return (v1 - v2 / 2) / v2;
}

static inline int div_threeslash(int v1, int v2)
{
    if (v2 <= 0 || (v2 & 1)) {
        loge("v2 <= 0 || (v2 & 1)");
        return 0;
    }
    return (v1 + v2 / 2) / v2;
}

// ptrans:
// 0: X0 Y0
// 8: XX YX XY YY ( shorts )
// 16: 1 << (shifter-1) twice
// 24: shifter
static inline void affine_transform(motion_vector_t *pv, affine_transform_t *ptrans)
{
    int x_;
    int y_;
    y_ = (int)(ptrans->Y0 + ((ptrans->XY * pv->x + ptrans->YY * pv->y + ptrans->rounder2) >> (int)ptrans->shifter));
    x_ = (int)(ptrans->X0 + ((ptrans->XX * pv->x + ptrans->YX * pv->y + ptrans->rounder1) >> (int)ptrans->shifter));
    pv->x = x_;
    pv->y = y_;
}

static inline void affine_transform_chr(motion_vector_t *pv, affine_transform_t *ptrans)
{
    int x_;
    int y_;
    x_ = (int)((ptrans->X0 + ptrans->XX * pv->x + ptrans->YX * pv->y /* + ptrans->rounder1*/) >> (int)ptrans->shifter);
    y_ = (int)((ptrans->Y0 + ptrans->XY * pv->x + ptrans->YY * pv->y /* + ptrans->rounder2*/) >> (int)ptrans->shifter);
    pv->x = x_;
    pv->y = y_;
}

static inline int clamp(int dv, int minval, int maxval)
{
#if (defined(LINUX) && defined(X86))
    int orig = dv;
    __asm__ __volatile__("cmpl %0, %2\n\t"
                         "cmovg %2, %0\n\t"
                         "cmpl %0, %3\n\t"
                         "cmovl %3, %0\n\t"
                         : "=r"(dv)
                         : "0"(dv), "r"(minval), "g"(maxval));
    //    if(orig!=dv)
    //    {
    //	printf("%d %d %d\n", orig, minval, maxval);
    //	printf("%d->%d\n", orig, dv);
    //    }
    return dv;
#else
    if (dv < minval)
        dv = minval;
    if (dv > maxval)
        dv = maxval;
    return dv;
#endif
}

// we need set_gmc_mv separate from reconstruct_gmc because of data partitioning
static void set_gmc_mv_stationary(struct mp4_state *mp4_state)
{
    int i;
    int bx = mp4_state->hdr.mb_xpos;
    int by = mp4_state->hdr.mb_ypos;

#ifdef MPEG4_DUMP_ENABLE
    fprintf(g_mpeg4_ctx->fp_mb_info, " set_gmc_mv_stationary, all mvs set 0\n");
#endif

    for (i = 0; i < 4; i++) {
        mp4_state->mv[(by + 1) * mp4_state->MV_stride + bx + 1][i].x = 0;
        mp4_state->mv[(by + 1) * mp4_state->MV_stride + bx + 1][i].y = 0;
        mp4_state->mv_field[(by + 1) * mp4_state->MV_stride + bx + 1][i].x = 0;
        mp4_state->mv_field[(by + 1) * mp4_state->MV_stride + bx + 1][i].y = 0;
    }
}

static void set_gmc_mv_translation(struct mp4_state *mp4_state)
{
    int i;
    // reference_t *ref = g_mpeg4_ctx->ref;
    int s = 2 << mp4_state->hdr.sprite_warping_accuracy;
    int qs = (mp4_state->hdr.quarter_pixel ? 4 : 2);
    motion_vector_t v;
    int bx = mp4_state->hdr.mb_xpos;
    int by = mp4_state->hdr.mb_ypos;
    int px = bx << 4;
    int py = by << 4;
    int vop_fcode = mp4_state->hdr.fcode_for;
    int scale_fac = 1 << (vop_fcode - 1);
    int high = (32 * scale_fac) - 1;
    int low = ((-32) * scale_fac);

    v.x = px + 8;
    v.y = py + 8;

#ifdef MPEG4_DUMP_ENABLE
    fprintf(g_mpeg4_ctx->fp_mb_info, " before affine mv(%d, %d)\n", v.x, v.y);
#endif
    affine_transform(&v, &mp4_state->at_lum);
#ifdef MPEG4_DUMP_ENABLE
    fprintf(g_mpeg4_ctx->fp_mb_info, " after affine mv(%d, %d)\n", v.x, v.y);
#endif
    v.x = (v.x - s * (px + 8));
    v.y = (v.y - s * (py + 8));

    v.x = v.x * qs / s;
    v.y = v.y * qs / s;
    // clip, Berg Xing Nov 26, 2003
    if (v.x < low)
        v.x = low;
    if (v.x > high)
        v.x = high;

    if (v.y < low)
        v.y = low;
    if (v.y > high)
        v.y = high;

#ifdef MPEG4_DUMP_ENABLE
    fprintf(g_mpeg4_ctx->fp_mb_info, " set_gmc_mv_translation, mv(%d, %d)\n", v.x, v.y);
#endif
    for (i = 0; i < 4; i++) {
        mp4_state->mv[(by + 1) * mp4_state->MV_stride + bx + 1][i] = v;
        mp4_state->mv_field[(by + 1) * mp4_state->MV_stride + bx + 1][i] = v;
    }
}

// Using average method for GMC MB MVs by Robert Yuan, Sept 26,2003
#define RSHIFT(a, b) ((a) > 0 ? ((a) + (1 << ((b) - 1))) >> (b) : ((a) + (1 << ((b) - 1)) - 1) >> (b))

static void set_gmc_mv_affine(struct mp4_state *mp4_state)
{
    int s = 2 << mp4_state->hdr.sprite_warping_accuracy;
    int qs = (mp4_state->hdr.quarter_pixel ? 4 : 2);
    // int lx = mp4_state->coded_picture_width;
    int px, py;
    int i;
    motion_vector_t v;
    int bx = mp4_state->hdr.mb_xpos;
    int by = mp4_state->hdr.mb_ypos;
    // Using average method for GMC MB MVs by Robert Yuan, Sept 26,2003
    int dxx, dyx, dxy, dyy;
    int sum;
    int a = mp4_state->hdr.sprite_warping_accuracy;
    int shift = mp4_state->at_lum.shifter;
    int vop_fcode = mp4_state->hdr.fcode_for;
    int scale_fac = 1 << (vop_fcode - 1);
    int high = (32 * scale_fac) - 1;
    int low = ((-32) * scale_fac);

    px = bx << 4;
    py = by << 4;

    // Using average method for GMC MB MVs by Robert Yuan, Sept 26,2003
    if (0) {
        // fixme: prediction motion vectors are not 100% correct

        v.x = px + 8;
        v.y = py + 8;

        affine_transform(&v, &mp4_state->at_lum);
        v.x = (v.x - s * (px + 8)) * qs / s;
        v.y = (v.y - s * (py + 8)) * qs / s;
    } else {
        int i, j, temp0, temp;
        int mdx, mdy;

        dxx = mp4_state->at_lum.XX - (1 << (a + 1 + shift));
        dyy = mp4_state->at_lum.YY - (1 << (a + 1 + shift));
        dxy = mp4_state->at_lum.XY;
        dyx = mp4_state->at_lum.YX;

        sum = 0;
        temp0 = dxx * px + dyx * py;
        mdy = 0;
        for (i = 0; i < 16; i++) {
            mdx = 0;
            for (j = 0; j < 16; j++) {
                temp = mp4_state->at_lum.X0 + ((temp0 + mdy + mdx + mp4_state->at_lum.rounder1) >> shift);
                sum += temp;
                mdx += dxx;
            }
            mdy += dyx;
        }
        v.x = sum / 256;

        sum = 0;
        temp0 = dxy * px + dyy * py;
        mdy = 0;
        for (i = 0; i < 16; i++) {
            mdx = 0;
            for (j = 0; j < 16; j++) {
                temp = mp4_state->at_lum.Y0 + ((temp0 + mdx + mdy + mp4_state->at_lum.rounder1) >> shift);
                sum += temp;
                mdx += dxy;
            }
            mdy += dyy;
        }
        v.y = sum / 256;

        v.x = RSHIFT(v.x * qs, a + 1);
        if (v.x < low)
            v.x = low;
        if (v.x > high)
            v.x = high;

        v.y = RSHIFT(v.y * qs, a + 1);
        if (v.y < low)
            v.y = low;
        if (v.y > high)
            v.y = high;
    }

    for (i = 0; i < 4; i++) {
        mp4_state->mv[(by + 1) * mp4_state->MV_stride + bx + 1][i] = v;
        mp4_state->mv_field[(by + 1) * mp4_state->MV_stride + bx + 1][i] = v;
    }
}

void (*const set_gmc_mv_pointers[4])(struct mp4_state *) = {
    set_gmc_mv_stationary,
    set_gmc_mv_translation,
    set_gmc_mv_affine,
    set_gmc_mv_affine,
};
