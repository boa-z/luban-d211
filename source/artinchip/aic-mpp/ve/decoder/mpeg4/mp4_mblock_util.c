/*
 * Copyright (C) 2020-2025 ArtInChip Technology Co. Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 *  author: <xiaodong.zhao@artinchip.com>
 *  Desc: mpeg4 mblock util interface
 *
 */

#include <stdlib.h>
#include "mp4_getbits.h"
#include "mp4_global.h"
#include "mp4_mblock_util.h"
#include "mp4_tables.h"
#include "mp4_vars.h"
#include "mpeg4_decoder.h"

#define verify(x) \
    if (!(x))     \
        return 0

motion_vector_t h263_find_pmv(struct mp4_state *mp4_state, int x, int y, int block, int mode);
motion_vector_t find_pmv(struct mp4_state *mp4_state, int x, int y, int block);
motion_vector_t find_pmv_interlaced(struct mp4_state *mp4_state, int x, int y, int block_num, int field, int interlaced_case);
motion_vector_t find_pmv_3(struct mp4_state *_mp4_state, int x, int y, int block, int field);

int get_mcbpc_i_vop(mp4_stream_t *_ld)
{
    mp4_stream_t *ld = _ld;

    int code = showbits(ld, 9);

    if (code < 8) {
        // Added the stuffing MB process by Robert Yuan in Sept 15,2003
        flushbits(ld, 9);
        return -1;
    }

    code >>= 3;
    if (code >= 32) {
        flushbits(ld, 1);
        return 3;
    }

    flushbits(ld, MCBPCtabIntra[code].len);
    return MCBPCtabIntra[code].val;
}

int get_mcbpc_p_vop(mp4_stream_t *_ld)
{
    mp4_stream_t *ld = _ld;

    int code = showbits(ld, 9);

    if (code == 0)
        return -1;

    if (code >= 256) {
        flushbits(ld, 1);
        return 0;
    }

    flushbits(ld, MCBPCtabInter[code].len);
    return MCBPCtabInter[code].val;
}

int get_cbpy(mp4_stream_t *_ld, int intraFlag)
{
    mp4_stream_t *ld = _ld;

    int cbpy;
    int code = showbits(ld, 6);

    if (code < 2) {
        return -1;
    }

    if (code >= 48) {
        flushbits(ld, 2);
        cbpy = 15;
    } else {
        flushbits(ld, CBPYtab[code].len);
        cbpy = CBPYtab[code].val;
    }

    if (!intraFlag)
        cbpy = 15 - cbpy;

    return cbpy;
}

// [Review][Ag] use this function to clean the code
static inline int decode_mvd(mp4_stream_t *ld, struct mp4_state *mp4_state, int *mvdata, int *mvres)
{
    int vop_fcode = mp4_state->hdr.fcode_for;
    int scale_fac = 1 << (vop_fcode - 1);
    int mvd_x, hor_mv_res;

    /**
     * [hor_mv_data]
     * if ((vop_fcode_forward != 1) && (hor_mv_data != 0))
     *     [hor_mv_residual]
     */
    int hor_mv_data = get_mv_data(ld); // mv data

    hor_mv_res = 0;

    if ((scale_fac == 1) || (hor_mv_data == 0)) {
        mvd_x = hor_mv_data;
    } else {
        hor_mv_res = getbits(ld, vop_fcode - 1); // mv residual
        mvd_x = ((abs(hor_mv_data) - 1) * scale_fac) + hor_mv_res + 1;
        if (hor_mv_data < 0)
            mvd_x = -mvd_x;
    }

    return mvd_x;
}

// INTER4V macroblocks in interlaced frames, all macroblocks in non-interlaced frames
int set_mv(mp4_stream_t *_ld, struct mp4_state *_mp4_state, int mb_xpos, int mb_ypos,
                        int block_num) // too many parameters -> inlined
{
    mp4_stream_t *ld = _ld;
    struct mp4_state *mp4_state = _mp4_state;

    int vop_fcode = mp4_state->hdr.fcode_for;

    int hor_mv_data, ver_mv_data, hor_mv_res, ver_mv_res;
    int scale_fac = 1 << (vop_fcode - 1);
    int high = (32 * scale_fac) - 1;
    int low = ((-32) * scale_fac);
    int range = (64 * scale_fac);

    int mvd_x, mvd_y, mv_x, mv_y;
    motion_vector_t pmv;
    /**
     * [hor_mv_data]
     * if ((vop_fcode_forward != 1) && (hor_mv_data != 0))
     *     [hor_mv_residual]
     */
    hor_mv_res = ver_mv_res = 0;
    hor_mv_data = get_mv_data(ld); // mv data
    if ((scale_fac == 1) || (hor_mv_data == 0)) {
        mvd_x = hor_mv_data;
    } else {
        hor_mv_res = getbits(ld, vop_fcode - 1); // mv residual
        mvd_x = ((abs(hor_mv_data) - 1) * scale_fac) + hor_mv_res + 1;
        if (hor_mv_data < 0)
            mvd_x = -mvd_x;
    }

    ver_mv_data = get_mv_data(ld);
    if ((scale_fac == 1) || (ver_mv_data == 0)) {
        mvd_y = ver_mv_data;
    } else {
        ver_mv_res = getbits(ld, vop_fcode - 1);
        mvd_y = ((abs(ver_mv_data) - 1) * scale_fac) + ver_mv_res + 1;
        if (ver_mv_data < 0)
            mvd_y = -mvd_y;
    }

    if (!mp4_state->hdr.interlaced) {
        if (mp4_state->if_rm_h263 || mp4_state->if_flv_h263) {
            if (block_num == -1)
                pmv = h263_find_pmv(mp4_state, mb_xpos, mb_ypos, 0, FORWARD_MODE);
            else
                pmv = h263_find_pmv(mp4_state, mb_xpos, mb_ypos, block_num, FORWARD_MODE);
        } else {
            if (block_num == -1)
                pmv = find_pmv(mp4_state, mb_xpos, mb_ypos, 0);
            else
                pmv = find_pmv(mp4_state, mb_xpos, mb_ypos, block_num);
        }
    } // ~interlaced
    else
        pmv = find_pmv_3(mp4_state, mb_xpos + 1, mb_ypos + 1, block_num, 0);

    mv_x = pmv.x + mvd_x;

    if (mv_x < low)
        mv_x += range;
    if (mv_x > high)
        mv_x -= range;

    mv_y = pmv.y + mvd_y;

    if (mv_y < low)
        mv_y += range;
    if (mv_y > high)
        mv_y -= range;

    // [Review][Ag] Table 7-5: Range for motion vectors, the upper limit is [-2048, 2047]
    // store_mvi:
    //  put [mv_x, mv_y] in MV struct
    if (block_num == -1) {
        int i;
        for (i = 0; i < 4; i++) {
            mp4_state->mv[(mb_ypos + 1) * mp4_state->MV_stride + mb_xpos + 1][i].x = mv_x;
            mp4_state->mv[(mb_ypos + 1) * mp4_state->MV_stride + mb_xpos + 1][i].y = mv_y;
        }
    } else {
        if (!mp4_state->hdr.interlaced) {
            mp4_state->mv[(mb_ypos + 1) * mp4_state->MV_stride + mb_xpos + 1][block_num].x = mv_x;
            mp4_state->mv[(mb_ypos + 1) * mp4_state->MV_stride + mb_xpos + 1][block_num].y = mv_y;
        } else {    // ~interlaced
            // interlaced
            mp4_state->mv[(mb_ypos + 1) * mp4_state->MV_stride + mb_xpos + 1][block_num].x = mv_x;
            mp4_state->mv[(mb_ypos + 1) * mp4_state->MV_stride + mb_xpos + 1][block_num].y = mv_y;
            mp4_state->mv_field[(mb_ypos + 1) * mp4_state->MV_stride + mb_xpos + 1][block_num].x = mv_x;
            mp4_state->mv_field[(mb_ypos + 1) * mp4_state->MV_stride + mb_xpos + 1][block_num].y = mv_y;
        } // interlaced
    }

#ifdef MPEG4_DUMP_ENABLE
    fprintf(g_mpeg4_ctx->fp_mb_info, "  mv(%d, %d)\n", mv_x, mv_y);
#endif

    return 1;
}

#define _IsFieldPredictedMb(x, y)    (mp4_state->fieldpredictedmap[(y) * mp4_state->fieldpredictedmap_stride + (x)] == 1)
#define _IsNotFieldPredictedMb(x, y) (mp4_state->fieldpredictedmap[(y) * mp4_state->fieldpredictedmap_stride + (x)] != 1)

// field: 0 top field, 1 bottom field
// INTER macroblocks in interlaced frames
int set_mv_interlaced(mp4_stream_t *ld, struct mp4_state *mp4_state, int mb_xpos, int mb_ypos, int field)
{
    int vop_fcode = mp4_state->hdr.fcode_for;
    int scale_fac = 1 << (vop_fcode - 1);

    int high = (32 * scale_fac) - 1; // [Review] Probably -1 is wrong!
    int low = ((-32) * scale_fac);
    int range = (64 * scale_fac);

    int mvd_x, mvd_y, mv_x, mv_y;
    motion_vector_t pmv;
    int interlaced_case; // $7.7.2.1

    int x = mb_xpos + 1; // index position for field predicted map
    int y = mb_ypos + 1; // index position for field predicted map

    // interlaced case
    if (_IsFieldPredictedMb(x, y)) {
        if (_IsNotFieldPredictedMb(x - 1, y) && _IsNotFieldPredictedMb(x, y - 1) && _IsNotFieldPredictedMb(x + 1, y - 1)) {
            // current mb is field and neighboors are not
            interlaced_case = 1;
        } else {
            // at least one neighboor is field too
            interlaced_case = 3;
        }
    } else {
        if (_IsFieldPredictedMb(x - 1, y) || _IsFieldPredictedMb(x, y - 1) || _IsFieldPredictedMb(x + 1, y - 1)) {
            // current mb is frame and at least one neighboor is field
            interlaced_case = 2;
        } else {
            // treat this as a normal case (this macroblock is frame and neighboors are frame coded too)
            interlaced_case = 0;
        }
    }

    mvd_x = decode_mvd(ld, mp4_state, mp4_state->mp4_mv_data, mp4_state->mp4_mv_res);
    mvd_y = decode_mvd(ld, mp4_state, (mp4_state->mp4_mv_data + 1), (mp4_state->mp4_mv_res + 1));

    // [Review] is there need to repeat the prediction for both the fields?
    pmv = find_pmv_3(mp4_state, x, y, 0, field);

    mv_x = pmv.x + mvd_x;

    // [Review] is *2 needed also for not interlaced case?
    if (mv_x < low)
        mv_x += range;
    if (mv_x > high)
        mv_x -= range;

    switch (interlaced_case) // $7.7.2.1
    {
    case 1:
    case 3: // field predicted macroblock
            //		if (field)
            //			pmv.y = 2 * (pmv.y/2);
        mv_y = ((mvd_y + (pmv.y / 2)) << 1);
        break;
    case 0:
    case 2: // frame predicted macroblock
        mv_y = mvd_y + pmv.y;
        break;
    }

    if (mv_y < low)
        mv_y += range;
    if (mv_y > high)
        mv_y -= range;

    verify(mv_x > -2048);
    verify(mv_x < 2047);
    verify(mv_y > -2040);
    verify(mv_y < 2047);

    // put [mv_x, mv_y] in MV struct
    if (!field) {
        // [Review] can I use only one field (macroblock prediction only)?
        int i;
        for (i = 0; i < 4; i++) {
            mp4_state->mv[(mb_ypos + 1) * mp4_state->MV_stride + mb_xpos + 1][i].x = mv_x;
            mp4_state->mv[(mb_ypos + 1) * mp4_state->MV_stride + mb_xpos + 1][i].y = mv_y;
        }
    } else {
        int i;
        for (i = 0; i < 4; i++) {
            mp4_state->mv_field[(mb_ypos + 1) * mp4_state->MV_stride + mb_xpos + 1][i].x = mv_x;
            mp4_state->mv_field[(mb_ypos + 1) * mp4_state->MV_stride + mb_xpos + 1][i].y = mv_y;
        }
    }

    if (!mp4_state->hdr.field_prediction) {
        int i;
        for (i = 0; i < 4; i++) {
            mp4_state->mv_field[(mb_ypos + 1) * mp4_state->MV_stride + mb_xpos + 1][i].x = mv_x;
            mp4_state->mv_field[(mb_ypos + 1) * mp4_state->MV_stride + mb_xpos + 1][i].y = mv_y;
        }
    }

#ifdef MPEG4_DUMP_ENABLE
    fprintf(g_mpeg4_ctx->fp_mb_info, "  %s field mv(%d, %d)\n", field ? "bottom" : "top", mv_x, mv_y);
#endif

    return 1;
}

// [Review] must be copied in mp4_mblock.c, not used in mp4_mblock_bvop.c
// #define _IsValid(x, y)	(mp4_state->modemap[(y)][(x)] != NOT_VALID)
#define _IsValid(x, y) (mp4_state->modemap[(y) * mp4_state->modemap_stride + (x)] != NOT_VALID)

// int x, y: block coord
// int block block num
// int mv comp (0: x, 1: y)
//
// Purpose: compute motion vector prediction
motion_vector_t find_pmv(struct mp4_state *_mp4_state, int x, int y, int block)
{
    struct mp4_state *mp4_state = _mp4_state;

    motion_vector_t p1, p2, p3, mv, mv0;
    int xin1, xin2, xin3;
    int yin1, yin2, yin3;
    int vec1, vec2, vec3;

    mv0.x = mv0.y = 0;
    x++;
    y++;

    // top row
    if ((y == 1) && ((block == 0) || (block == 1))) {
        if ((x == 1) && (block == 0)) {
            return mv0;
        } else if (block == 1) {
            return mp4_state->mv[y * mp4_state->MV_stride + x][0];
        } else {// block == 0
            if _IsValid (x - 1, 1) {// here I put the border in the formula
                return mp4_state->mv[y * mp4_state->MV_stride + x - 1][1];
            } else {
                return mv0;
            }
        }
    } else {
        switch (block) {
        case 0:
            if (_IsValid(x - 1, y) && _IsValid(x, y - 1) && _IsValid(x + 1, y - 1)) {
                vec1 = 1;
                yin1 = y;
                xin1 = x - 1;
                vec2 = 2;
                yin2 = y - 1;
                xin2 = x;
                vec3 = 2;
                yin3 = y - 1;
                xin3 = x + 1;
            // one candidate predictor not valid
            } else if ((!_IsValid(x - 1, y)) && _IsValid(x, y - 1) && _IsValid(x + 1, y - 1)) {
                vec1 = 0;
                yin1 = 0;
                xin1 = 0;
                vec2 = 2;
                yin2 = y - 1;
                xin2 = x;
                vec3 = 2;
                yin3 = y - 1;
                xin3 = x + 1;
            } else if ((!_IsValid(x, y - 1)) && _IsValid(x - 1, y) && _IsValid(x + 1, y - 1)) {
                vec1 = 1;
                yin1 = y;
                xin1 = x - 1;
                vec2 = 0;
                yin2 = 0;
                xin2 = 0;
                vec3 = 2;
                yin3 = y - 1;
                xin3 = x + 1;
            } else if ((!_IsValid(x + 1, y - 1)) && _IsValid(x - 1, y) && _IsValid(x, y - 1)) {
                vec1 = 1;
                yin1 = y;
                xin1 = x - 1;
                vec2 = 2;
                yin2 = y - 1;
                xin2 = x;
                vec3 = 0;
                yin3 = 0;
                xin3 = 0;
            }
            // all three candidate predictors not valid
            else if ((!_IsValid(x - 1, y)) && (!_IsValid(x + 1, y - 1)) && (!_IsValid(x, y - 1)))
                return mv0;
            // two candidate predictors not valid
            else if _IsValid (x - 1, y) {
                return mp4_state->mv[y * mp4_state->MV_stride + x - 1][1];
            } else if _IsValid (x, y - 1) {
                return mp4_state->mv[(y - 1) * mp4_state->MV_stride + x][2];
            } else if _IsValid (x + 1, y - 1) {
                return mp4_state->mv[(y - 1) * mp4_state->MV_stride + x + 1][2];
            }
            break;
        case 1:
            if (_IsValid(x, y - 1) && _IsValid(x + 1, y - 1)) {
                vec1 = 0;
                yin1 = y;
                xin1 = x;
                vec2 = 3;
                yin2 = y - 1;
                xin2 = x;
                vec3 = 2;
                yin3 = y - 1;
                xin3 = x + 1;
            } else if _IsValid (x, y - 1) {
                vec1 = 0;
                yin1 = y;
                xin1 = x;
                vec2 = 3;
                yin2 = y - 1;
                xin2 = x;
                vec3 = 0;
                yin3 = 0;
                xin3 = 0;
            } else if _IsValid (x + 1, y - 1) {
                vec1 = 0;
                yin1 = y;
                xin1 = x;
                vec2 = 0;
                yin2 = 0;
                xin2 = 0;
                vec3 = 2;
                yin3 = y - 1;
                xin3 = x + 1;
            } else {
                return mp4_state->mv[y * mp4_state->MV_stride + x][0];
            }
            break;
        case 2:
            if _IsValid (x - 1, y) {
                vec1 = 3;
                yin1 = y;
                xin1 = x - 1;
                vec2 = 0;
                yin2 = y;
                xin2 = x;
                vec3 = 1;
                yin3 = y;
                xin3 = x;
            } else {
                vec1 = 0;
                yin1 = 0;
                xin1 = 0;
                vec2 = 0;
                yin2 = y;
                xin2 = x;
                vec3 = 1;
                yin3 = y;
                xin3 = x;
            }
            break;
        default: // case 3
            vec1 = 2;
            yin1 = y;
            xin1 = x;
            vec2 = 0;
            yin2 = y;
            xin2 = x;
            vec3 = 1;
            yin3 = y;
            xin3 = x;
            break;
        }

        p1 = mp4_state->mv[yin1 * mp4_state->MV_stride + xin1][vec1];
        p2 = mp4_state->mv[yin2 * mp4_state->MV_stride + xin2][vec2];
        p3 = mp4_state->mv[yin3 * mp4_state->MV_stride + xin3][vec3];

        mv.x = mmin(mmax(p1.x, p2.x), mmin(mmax(p2.x, p3.x), mmax(p1.x, p3.x)));
        mv.y = mmin(mmax(p1.y, p2.y), mmin(mmax(p2.y, p3.y), mmax(p1.y, p3.y)));
        return mv;
    }
}

int iBlockIndex[4][3] = {{1, 2, 2}, {0, 3, 2}, {3, 0, 1}, {2, 0, 1}};
int iMBIndex_x[4][3] = {{-1, 0, 1}, {0, 0, 1}, {-1, 0, 0}, {0, 0, 0}};
int iMBIndex_y[4][3] = {{0, -1, -1}, {0, -1, -1}, {0, 0, 0}, {0, 0, 0}};
motion_vector_t h263_find_pmv(struct mp4_state *_mp4_state, int x, int y, int block, int mode)
{
    struct mp4_state *mp4_state = _mp4_state;

    motion_vector_t p1, p2, p3, mv, mv0;
    int xin1, xin2, xin3;
    int yin1, yin2, yin3;
    int vec1, vec2, vec3;

    mv0.x = mv0.y = 0;
    x++;
    y++;

    if ((block & 1) || !mp4_state->hdr.gob_edge.ileft) { // not left edge
        vec1 = iBlockIndex[block][0];
        yin1 = y + iMBIndex_y[block][0];
        xin1 = x + iMBIndex_x[block][0];
    } else {
        vec1 = 0;
        yin1 = 0;
        xin1 = 0;
    }

    if (mp4_state->hdr.gob_edge.iabove && block < 2) {
        vec2 = vec1;
        yin2 = yin1;
        xin2 = xin1;
        // if(x==mp4_state->mb_width)
        {
            vec3 = vec1;
            yin3 = yin1;
            xin3 = xin1;
        }
    } else {
        vec2 = iBlockIndex[block][1];
        yin2 = y + iMBIndex_y[block][1];
        xin2 = x + iMBIndex_x[block][1];

        if (x == mp4_state->mb_width && block < 2) {
            vec3 = 0;
            yin3 = 0;
            xin3 = 0;
        } else {
            vec3 = iBlockIndex[block][2];
            yin3 = y + iMBIndex_y[block][2];
            xin3 = x + iMBIndex_x[block][2];
        }
    }

    if (mode == FORWARD_MODE) {
        p1 = mp4_state->mv[yin1 * mp4_state->MV_stride + xin1][vec1];
        p2 = mp4_state->mv[yin2 * mp4_state->MV_stride + xin2][vec2];
        p3 = mp4_state->mv[yin3 * mp4_state->MV_stride + xin3][vec3];
    } else if (mode == BACKWARD_MODE) {
        p1 = mp4_state->mv_back[yin1 * mp4_state->MV_stride + xin1][vec1];
        p2 = mp4_state->mv_back[yin2 * mp4_state->MV_stride + xin2][vec2];
        p3 = mp4_state->mv_back[yin3 * mp4_state->MV_stride + xin3][vec3];
    }

    mv.x = mmin(mmax(p1.x, p2.x), mmin(mmax(p2.x, p3.x), mmax(p1.x, p3.x)));
    mv.y = mmin(mmax(p1.y, p2.y), mmin(mmax(p2.y, p3.y), mmax(p1.y, p3.y)));

    return mv;
}

#define Div2Round(x) (((x) >> 1) | ((x) & 1))

// int x = mp4_state->hdr.mb_xpos +1;
// int y = mp4_state->hdr.mb_ypos +1;
// field: 0 top field, 1 bottom field
motion_vector_t find_pmv_interlaced(struct mp4_state *_mp4_state, int x, int y, int block, int field, int interlaced_case)
{
    struct mp4_state *mp4_state = _mp4_state;

    motion_vector_t p1, p2, p3, mv, mv0;
    int xin1, xin2, xin3;
    int yin1, yin2, yin3;

    int p1_field = 0, p2_field = 0, p3_field = 0;

    // int *MV[3];
    motion_vector_t(*MV)[6];
    mv0.x = mv0.y = 0;

    if ((interlaced_case == 2) || (interlaced_case == 3)) {
        p1_field = _IsFieldPredictedMb(x - 1, y);
        p2_field = _IsFieldPredictedMb(x, y - 1);
        p3_field = _IsFieldPredictedMb(x + 1, y - 1);
    }

    if ((field == 0) || (interlaced_case == 0)) // top field
        MV = mp4_state->mv;
    else
        MV = mp4_state->mv_field;

    // top row
    if ((y == 1) && (x == 1))
        return mv0;
    if (y == 1) {
        if ((interlaced_case == 2) || (interlaced_case == 3)) {
            if (p1_field) {
                mv.x = Div2Round(mp4_state->mv[y * mp4_state->MV_stride + x - 1][1].x +
                                 mp4_state->mv_field[y * mp4_state->MV_stride + x - 1][1].x);
                mv.y = Div2Round(mp4_state->mv[y * mp4_state->MV_stride + x - 1][1].y +
                                 mp4_state->mv_field[y * mp4_state->MV_stride + x - 1][1].y);
                return mv;
            }
        }
        return MV[y * mp4_state->MV_stride + x - 1][1];
    } else {
        yin1 = y;
        xin1 = x - 1;
        yin2 = y - 1;
        xin2 = x;
        yin3 = y - 1;
        xin3 = x + 1;

        p1 = MV[yin1 * mp4_state->MV_stride + xin1][1];
        p2 = MV[yin2 * mp4_state->MV_stride + xin2][2];
        p3 = MV[yin3 * mp4_state->MV_stride + xin3][2];

        if ((interlaced_case == 2) || (interlaced_case == 3)) {
            if (p1_field) {
                p1.x = Div2Round(mp4_state->mv[yin1 * mp4_state->MV_stride + xin1][1].x +
                                 mp4_state->mv_field[yin1 * mp4_state->MV_stride + xin1][1].x);
                p1.y = Div2Round(mp4_state->mv[yin1 * mp4_state->MV_stride + xin1][1].y +
                                 mp4_state->mv_field[yin1 * mp4_state->MV_stride + xin1][1].y);
            }
            if (p2_field) {
                p2.x = Div2Round(mp4_state->mv[yin2 * mp4_state->MV_stride + xin2][2].x +
                                 mp4_state->mv_field[yin2 * mp4_state->MV_stride + xin2][2].x);
                p2.y = Div2Round(mp4_state->mv[yin2 * mp4_state->MV_stride + xin2][2].y +
                                 mp4_state->mv_field[yin2 * mp4_state->MV_stride + xin2][2].y);
            }
            if (p3_field) {
                p3.x = Div2Round(mp4_state->mv[yin3 * mp4_state->MV_stride + xin3][2].x +
                                 mp4_state->mv_field[yin3 * mp4_state->MV_stride + xin3][2].x);
                p3.y = Div2Round(mp4_state->mv[yin3 * mp4_state->MV_stride + xin3][2].y +
                                 mp4_state->mv_field[yin3 * mp4_state->MV_stride + xin3][2].y);
            }
        }

        mv.x = mmin(mmax(p1.x, p2.x), mmin(mmax(p2.x, p3.x), mmax(p1.x, p3.x)));
        mv.y = mmin(mmax(p1.y, p2.y), mmin(mmax(p2.y, p3.y), mmax(p1.y, p3.y)));
        return mv;
    }
}

// purpose: motion vector prediction for inter4v in case interlaced
motion_vector_t find_pmv_3(struct mp4_state *_mp4_state, int x, int y, int block, int field)
{
    struct mp4_state *mp4_state = _mp4_state;

    int interlaced_case;
    int p1_field = 0, p2_field = 0, p3_field = 0;

    motion_vector_t(*MV)[6];

    motion_vector_t p1, p2, p3, mv, mv0;
    int xin1, xin2, xin3;
    int yin1, yin2, yin3;
    int vec1, vec2, vec3;
    mv0.x = mv0.y = 0;

    // retrieve interlaced case
    if (_IsFieldPredictedMb(x, y)) {
        if (_IsNotFieldPredictedMb(x - 1, y) && _IsNotFieldPredictedMb(x, y - 1) && _IsNotFieldPredictedMb(x + 1, y - 1))
            interlaced_case = 1; // current mb is field and neighboors are not
        else
            interlaced_case = 3; // at least one neighboor is field too
    } else {
        if (_IsFieldPredictedMb(x - 1, y) || _IsFieldPredictedMb(x, y - 1) || _IsFieldPredictedMb(x + 1, y - 1))
            interlaced_case = 2; // current mb is frame and at least one neighboor is field
        else
            interlaced_case = 0; // treat this as a normal case (this macroblock is frame and neighboors are frame coded too)
    }

    // retrieve field predicted predictors
    if ((interlaced_case == 2) || (interlaced_case == 3)) {
        p1_field = _IsFieldPredictedMb(x - 1, y);
        p2_field = _IsFieldPredictedMb(x, y - 1);
        p3_field = _IsFieldPredictedMb(x + 1, y - 1);
    }

    if ((field == 0) || (interlaced_case == 0)) // top field
    {
        MV = mp4_state->mv;
    } else {
        MV = mp4_state->mv_field;
    }

    if ((y == 1) && (x == 1)) {
        if (block == 0)
            return mv0;
    }
    if (y == 1) {
        if (block == 0) {
            if (((interlaced_case == 2) || (interlaced_case == 3)) && p1_field) {
                mv.x = Div2Round(mp4_state->mv[y * mp4_state->MV_stride + x - 1][1].x +
                                 mp4_state->mv_field[y * mp4_state->MV_stride + x - 1][1].x);
                mv.y = Div2Round(mp4_state->mv[y * mp4_state->MV_stride + x - 1][1].y +
                                 mp4_state->mv_field[y * mp4_state->MV_stride + x - 1][1].y);
                return mv;
            } else {
                return MV[y * mp4_state->MV_stride + x - 1][1];
            }
        }
        if (block == 1)
            return MV[y * mp4_state->MV_stride + x][0];
    }

    switch (block) {
    case 0:
        vec1 = 1;
        yin1 = y;
        xin1 = x - 1;
        vec2 = 2;
        yin2 = y - 1;
        xin2 = x;
        vec3 = 2;
        yin3 = y - 1;
        xin3 = x + 1;
        break;
    case 1:
        vec1 = 0;
        yin1 = y;
        xin1 = x;
        vec2 = 3;
        yin2 = y - 1;
        xin2 = x;
        vec3 = 2;
        yin3 = y - 1;
        xin3 = x + 1;
        break;
    case 2:
        vec1 = 3;
        yin1 = y;
        xin1 = x - 1;
        vec2 = 0;
        yin2 = y;
        xin2 = x;
        vec3 = 1;
        yin3 = y;
        xin3 = x;
        break;
    default: // case 3
        vec1 = 2;
        yin1 = y;
        xin1 = x;
        vec2 = 0;
        yin2 = y;
        xin2 = x;
        vec3 = 1;
        yin3 = y;
        xin3 = x;
        break;
    }

    p1 = MV[yin1 * mp4_state->MV_stride + xin1][vec1];
    p2 = MV[yin2 * mp4_state->MV_stride + xin2][vec2];
    p3 = MV[yin3 * mp4_state->MV_stride + xin3][vec3];

    if ((interlaced_case == 2) || (interlaced_case == 3)) {
        if (p1_field) {
            p1.x = Div2Round(mp4_state->mv[yin1 * mp4_state->MV_stride + xin1][vec1].x +
                             mp4_state->mv_field[yin1 * mp4_state->MV_stride + xin1][vec1].x);
            p1.y = Div2Round(mp4_state->mv[yin1 * mp4_state->MV_stride + xin1][vec1].y +
                             mp4_state->mv_field[yin1 * mp4_state->MV_stride + xin1][vec1].y);
        }
        if (p2_field) {
            p2.x = Div2Round(mp4_state->mv[yin2 * mp4_state->MV_stride + xin2][vec2].x +
                             mp4_state->mv_field[yin2 * mp4_state->MV_stride + xin2][vec2].x);
            p2.y = Div2Round(mp4_state->mv[yin2 * mp4_state->MV_stride + xin2][vec2].y +
                             mp4_state->mv_field[yin2 * mp4_state->MV_stride + xin2][vec2].y);
        }
        if (p3_field) {
            p3.x = Div2Round(mp4_state->mv[yin3 * mp4_state->MV_stride + xin3][vec3].x +
                             mp4_state->mv_field[yin3 * mp4_state->MV_stride + xin3][vec3].x);
            p3.y = Div2Round(mp4_state->mv[yin3 * mp4_state->MV_stride + xin3][vec3].y +
                             mp4_state->mv_field[yin3 * mp4_state->MV_stride + xin3][vec3].y);
        }
    }

    mv.x = mmin(mmax(p1.x, p2.x), mmin(mmax(p2.x, p3.x), mmax(p1.x, p3.x)));
    mv.y = mmin(mmax(p1.y, p2.y), mmin(mmax(p2.y, p3.y), mmax(p1.y, p3.y)));
    return mv;
}

// purpose: motion vector prediction in case video packets are not used
// note: not tested, never used
motion_vector_t find_pmv_2(struct mp4_state *_mp4_state, int x, int y, int block)
{
    struct mp4_state *mp4_state = _mp4_state;

    motion_vector_t p1, p2, p3, mv;
    int xin1, xin2, xin3;
    int yin1, yin2, yin3;
    int vec1, vec2, vec3;

    x++;
    y++;

    if ((y == 1) && (x == 1)) {
        if (block == 0) {
            mv.x = mv.y = 0;
            return mv;
        }
    }
    if (y == 1) {
        if (block == 0)
            return mp4_state->mv[y * mp4_state->MV_stride + x - 1][1];
        if (block == 1)
            return mp4_state->mv[y * mp4_state->MV_stride + x][0];
    }

    switch (block) {
    case 0:
        vec1 = 1;
        yin1 = y;
        xin1 = x - 1;
        vec2 = 2;
        yin2 = y - 1;
        xin2 = x;
        vec3 = 2;
        yin3 = y - 1;
        xin3 = x + 1;
        break;
    case 1:
        vec1 = 0;
        yin1 = y;
        xin1 = x;
        vec2 = 3;
        yin2 = y - 1;
        xin2 = x;
        vec3 = 2;
        yin3 = y - 1;
        xin3 = x + 1;
        break;
    case 2:
        vec1 = 3;
        yin1 = y;
        xin1 = x - 1;
        vec2 = 0;
        yin2 = y;
        xin2 = x;
        vec3 = 1;
        yin3 = y;
        xin3 = x;
        break;
    default: // case 3
        vec1 = 2;
        yin1 = y;
        xin1 = x;
        vec2 = 0;
        yin2 = y;
        xin2 = x;
        vec3 = 1;
        yin3 = y;
        xin3 = x;
        break;
    }

    p1 = mp4_state->mv[yin1 * mp4_state->MV_stride + xin1][vec1];
    p2 = mp4_state->mv[yin2 * mp4_state->MV_stride + xin2][vec2];
    p3 = mp4_state->mv[yin3 * mp4_state->MV_stride + xin3][vec3];

    mv.x = mmin(mmax(p1.x, p2.x), mmin(mmax(p2.x, p3.x), mmax(p1.x, p3.x)));
    mv.y = mmin(mmax(p1.y, p2.y), mmin(mmax(p2.y, p3.y), mmax(p1.y, p3.y)));
    return mv;
}

int get_mv_data(mp4_stream_t *_ld)
{
    mp4_stream_t *ld = _ld;

    int code;

    if (getbits(ld, 1)) {
        return 0; // hor_mv_data == 0
    }

    code = showbits(ld, 12);

    if (code >= 512) {
        code = (code >> 8) - 2;
        flushbits(ld, MVtab0[code].len);
        return MVtab0[code].val;
    }

    if (code >= 128) {
        code = (code >> 2) - 32;
        flushbits(ld, MVtab1[code].len);
        return MVtab1[code].val;
    }

    code -= 4;

    flushbits(ld, MVtab2[code].len);
    return MVtab2[code].val;
}
