/*
 * Copyright (C) 2020-2025 ArtInChip Technology Co. Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 *  author: <xiaodong.zhao@artinchip.com>
 *  Desc: mpeg4 mblock bvop interface
 *
 */

#include <string.h>
#include <stdlib.h>
#include "mp4_block.h"
#include "mp4_global.h"
#include "mp4_mblock.h"
#include "mp4_mblock_util.h"
#include "mp4_recon.h"
#include "mp4_vars.h"
#include "mp4_getbits.h"
#include "mpeg4_decoder.h"

extern int DQtab[4];
extern int gNewTAB_DQUANT_MQ[32][2];
#define MCBPC_CBPC(d)      (((d) >> 8) & 0x3)

#define CBPY_INTRA(d)      (((d) >> 12) & 0xf)
#define CBPY_INTER(d)      (((d) >> 8) & 0xf)
#define CBPY_BITS(d)       ((d) & 0xff)

#define MBTYPE_MVDFW(x)    (((x) & 8) ? 1 : 0)
#define MBTYPE_MVDBW(x)    (((x) & 4) ? 1 : 0)
#define MBTYPE_CBPC(x)     (((x) & 2) ? 1 : 0)
#define MBTYPE_CBPY(x)     (((x) & 2) ? 1 : 0)
#define MBTYPE_DQUANT(x)   (((x) & 1) ? 1 : 0)

/* MBTYPE table for B pictures
 *     PredictionType   bits
 *                7-4    3-0
 */
#define B_DIRECT_SKIPPED   1
#define B_DIRECT           2
#define B_DIRECT_Q         3
#define B_FORWARD_SKIPPED  4
#define B_FORWARD          5
#define B_FORWARD_Q        6
#define B_BACKWARD_SKIPPED 7
#define B_BACKWARD         8
#define B_BACKWARD_Q       9
#define B_BIDIR_SKIPPED    10
#define B_BIDIR            11
#define B_BIDIR_Q          12
#define B_INTRA            13
#define B_INTRA_Q          14

#define MBTYPE_ENTRY(p, b) (((p & 0xF) << 4) | (b & 0xF))

const unsigned char gNewTAB_MBTYPE_B[128] = {

    // 0000000 - 0
    MBTYPE_ENTRY(0, 7),

    // 0000001 - 1
    MBTYPE_ENTRY(B_INTRA_Q, 7),

    // 000001x - 2 to 3
    MBTYPE_ENTRY(B_INTRA, 6),
    MBTYPE_ENTRY(B_INTRA, 6),

    // 00001xx - 4 to 7
    MBTYPE_ENTRY(B_BIDIR_Q, 5),
    MBTYPE_ENTRY(B_BIDIR_Q, 5),
    MBTYPE_ENTRY(B_BIDIR_Q, 5),
    MBTYPE_ENTRY(B_BIDIR_Q, 5),

    // 0001xxx - 8 to 15
    MBTYPE_ENTRY(B_DIRECT_Q, 4),
    MBTYPE_ENTRY(B_DIRECT_Q, 4),
    MBTYPE_ENTRY(B_DIRECT_Q, 4),
    MBTYPE_ENTRY(B_DIRECT_Q, 4),
    MBTYPE_ENTRY(B_DIRECT_Q, 4),
    MBTYPE_ENTRY(B_DIRECT_Q, 4),
    MBTYPE_ENTRY(B_DIRECT_Q, 4),
    MBTYPE_ENTRY(B_DIRECT_Q, 4),

    // 00100xx - 16 to 19
    MBTYPE_ENTRY(B_BIDIR_SKIPPED, 5),
    MBTYPE_ENTRY(B_BIDIR_SKIPPED, 5),
    MBTYPE_ENTRY(B_BIDIR_SKIPPED, 5),
    MBTYPE_ENTRY(B_BIDIR_SKIPPED, 5),

    // 00101xx - 20 to 23
    MBTYPE_ENTRY(B_BIDIR, 5),
    MBTYPE_ENTRY(B_BIDIR, 5),
    MBTYPE_ENTRY(B_BIDIR, 5),
    MBTYPE_ENTRY(B_BIDIR, 5),

    // 00110xx - 24 to 27
    MBTYPE_ENTRY(B_FORWARD_Q, 5),
    MBTYPE_ENTRY(B_FORWARD_Q, 5),
    MBTYPE_ENTRY(B_FORWARD_Q, 5),
    MBTYPE_ENTRY(B_FORWARD_Q, 5),

    // 00111xx - 28 to 31
    MBTYPE_ENTRY(B_BACKWARD_Q, 5),
    MBTYPE_ENTRY(B_BACKWARD_Q, 5),
    MBTYPE_ENTRY(B_BACKWARD_Q, 5),
    MBTYPE_ENTRY(B_BACKWARD_Q, 5),

    // 010xxxx - 32 to 47
    MBTYPE_ENTRY(B_BACKWARD_SKIPPED, 3),
    MBTYPE_ENTRY(B_BACKWARD_SKIPPED, 3),
    MBTYPE_ENTRY(B_BACKWARD_SKIPPED, 3),
    MBTYPE_ENTRY(B_BACKWARD_SKIPPED, 3),
    MBTYPE_ENTRY(B_BACKWARD_SKIPPED, 3),
    MBTYPE_ENTRY(B_BACKWARD_SKIPPED, 3),
    MBTYPE_ENTRY(B_BACKWARD_SKIPPED, 3),
    MBTYPE_ENTRY(B_BACKWARD_SKIPPED, 3),
    MBTYPE_ENTRY(B_BACKWARD_SKIPPED, 3),
    MBTYPE_ENTRY(B_BACKWARD_SKIPPED, 3),
    MBTYPE_ENTRY(B_BACKWARD_SKIPPED, 3),
    MBTYPE_ENTRY(B_BACKWARD_SKIPPED, 3),
    MBTYPE_ENTRY(B_BACKWARD_SKIPPED, 3),
    MBTYPE_ENTRY(B_BACKWARD_SKIPPED, 3),
    MBTYPE_ENTRY(B_BACKWARD_SKIPPED, 3),
    MBTYPE_ENTRY(B_BACKWARD_SKIPPED, 3),

    // 011xxxx - 48 to 63
    MBTYPE_ENTRY(B_BACKWARD, 3),
    MBTYPE_ENTRY(B_BACKWARD, 3),
    MBTYPE_ENTRY(B_BACKWARD, 3),
    MBTYPE_ENTRY(B_BACKWARD, 3),
    MBTYPE_ENTRY(B_BACKWARD, 3),
    MBTYPE_ENTRY(B_BACKWARD, 3),
    MBTYPE_ENTRY(B_BACKWARD, 3),
    MBTYPE_ENTRY(B_BACKWARD, 3),
    MBTYPE_ENTRY(B_BACKWARD, 3),
    MBTYPE_ENTRY(B_BACKWARD, 3),
    MBTYPE_ENTRY(B_BACKWARD, 3),
    MBTYPE_ENTRY(B_BACKWARD, 3),
    MBTYPE_ENTRY(B_BACKWARD, 3),
    MBTYPE_ENTRY(B_BACKWARD, 3),
    MBTYPE_ENTRY(B_BACKWARD, 3),
    MBTYPE_ENTRY(B_BACKWARD, 3),

    // 100xxxx - 64 to 79
    MBTYPE_ENTRY(B_FORWARD_SKIPPED, 3),
    MBTYPE_ENTRY(B_FORWARD_SKIPPED, 3),
    MBTYPE_ENTRY(B_FORWARD_SKIPPED, 3),
    MBTYPE_ENTRY(B_FORWARD_SKIPPED, 3),
    MBTYPE_ENTRY(B_FORWARD_SKIPPED, 3),
    MBTYPE_ENTRY(B_FORWARD_SKIPPED, 3),
    MBTYPE_ENTRY(B_FORWARD_SKIPPED, 3),
    MBTYPE_ENTRY(B_FORWARD_SKIPPED, 3),
    MBTYPE_ENTRY(B_FORWARD_SKIPPED, 3),
    MBTYPE_ENTRY(B_FORWARD_SKIPPED, 3),
    MBTYPE_ENTRY(B_FORWARD_SKIPPED, 3),
    MBTYPE_ENTRY(B_FORWARD_SKIPPED, 3),
    MBTYPE_ENTRY(B_FORWARD_SKIPPED, 3),
    MBTYPE_ENTRY(B_FORWARD_SKIPPED, 3),
    MBTYPE_ENTRY(B_FORWARD_SKIPPED, 3),
    MBTYPE_ENTRY(B_FORWARD_SKIPPED, 3),

    // 101xxxx - 80 to 95
    MBTYPE_ENTRY(B_FORWARD, 3),
    MBTYPE_ENTRY(B_FORWARD, 3),
    MBTYPE_ENTRY(B_FORWARD, 3),
    MBTYPE_ENTRY(B_FORWARD, 3),
    MBTYPE_ENTRY(B_FORWARD, 3),
    MBTYPE_ENTRY(B_FORWARD, 3),
    MBTYPE_ENTRY(B_FORWARD, 3),
    MBTYPE_ENTRY(B_FORWARD, 3),
    MBTYPE_ENTRY(B_FORWARD, 3),
    MBTYPE_ENTRY(B_FORWARD, 3),
    MBTYPE_ENTRY(B_FORWARD, 3),
    MBTYPE_ENTRY(B_FORWARD, 3),
    MBTYPE_ENTRY(B_FORWARD, 3),
    MBTYPE_ENTRY(B_FORWARD, 3),
    MBTYPE_ENTRY(B_FORWARD, 3),
    MBTYPE_ENTRY(B_FORWARD, 3),

    // 11xxxxx - 96 to 127
    MBTYPE_ENTRY(B_DIRECT, 2),
    MBTYPE_ENTRY(B_DIRECT, 2),
    MBTYPE_ENTRY(B_DIRECT, 2),
    MBTYPE_ENTRY(B_DIRECT, 2),
    MBTYPE_ENTRY(B_DIRECT, 2),
    MBTYPE_ENTRY(B_DIRECT, 2),
    MBTYPE_ENTRY(B_DIRECT, 2),
    MBTYPE_ENTRY(B_DIRECT, 2),
    MBTYPE_ENTRY(B_DIRECT, 2),
    MBTYPE_ENTRY(B_DIRECT, 2),
    MBTYPE_ENTRY(B_DIRECT, 2),
    MBTYPE_ENTRY(B_DIRECT, 2),
    MBTYPE_ENTRY(B_DIRECT, 2),
    MBTYPE_ENTRY(B_DIRECT, 2),
    MBTYPE_ENTRY(B_DIRECT, 2),
    MBTYPE_ENTRY(B_DIRECT, 2),
    MBTYPE_ENTRY(B_DIRECT, 2),
    MBTYPE_ENTRY(B_DIRECT, 2),
    MBTYPE_ENTRY(B_DIRECT, 2),
    MBTYPE_ENTRY(B_DIRECT, 2),
    MBTYPE_ENTRY(B_DIRECT, 2),
    MBTYPE_ENTRY(B_DIRECT, 2),
    MBTYPE_ENTRY(B_DIRECT, 2),
    MBTYPE_ENTRY(B_DIRECT, 2),
    MBTYPE_ENTRY(B_DIRECT, 2),
    MBTYPE_ENTRY(B_DIRECT, 2),
    MBTYPE_ENTRY(B_DIRECT, 2),
    MBTYPE_ENTRY(B_DIRECT, 2),
    MBTYPE_ENTRY(B_DIRECT, 2),
    MBTYPE_ENTRY(B_DIRECT, 2),
    MBTYPE_ENTRY(B_DIRECT, 2),
    MBTYPE_ENTRY(B_DIRECT, 2),

};

// Macroblock type defines
#define D_MBTYPE_INTRA            0
#define D_MBTYPE_INTRA_Q          1
#define D_MBTYPE_FORWARD_SKIPPED  2
#define D_MBTYPE_FORWARD          3
#define D_MBTYPE_FORWARD_Q        4
#define D_MBTYPE_FORWARD_4V       5  // AP mode
#define D_MBTYPE_FORWARD_Q_4V     6  // AP mode with quant
#define D_MBTYPE_UPWARD_SKIPPED   7  // EI or EP pictures
#define D_MBTYPE_UPWARD           8  // EI or EP pictures
#define D_MBTYPE_UPWARD_Q         9  // EI or EP pictures
#define D_MBTYPE_BACKWARD_SKIPPED 10 // true B pictures
#define D_MBTYPE_BACKWARD         11 // true B pictures
#define D_MBTYPE_BACKWARD_Q       12 // true B pictures
#define D_MBTYPE_BIDIR_SKIPPED    13 // EP or true B pictures
#define D_MBTYPE_BIDIR            14 // EP or true B pictures
#define D_MBTYPE_BIDIR_Q          15 // EP or true B pictures
#define D_MBTYPE_DIRECT_SKIPPED   16 // true B pictures
#define D_MBTYPE_DIRECT           17 // true B pictures
#define D_MBTYPE_DIRECT_Q         18 // true B pictures

static const uint32_t MapMBType[] = {
    INTRA_MODE,    // D_MBTYPE_INTRA
    INTRA_MODE,    // D_MBTYPE_INTRA_Q
    FORWARD_MODE,  // D_MBTYPE_FORWARD_SKIPPED
    FORWARD_MODE,  // D_MBTYPE_FORWARD
    FORWARD_MODE,  // D_MBTYPE_FORWARD_Q
    FORWARD_MODE,  // D_MBTYPE_FORWARD_4V
    FORWARD_MODE,  // D_MBTYPE_FORWARD_Q_4V
    FORWARD_MODE,  // D_MBTYPE_UPWARD_SKIPPED
    FORWARD_MODE,  // D_MBTYPE_UPWARD
    FORWARD_MODE,  // D_MBTYPE_UPWARD_Q
    BACKWARD_MODE, // D_MBTYPE_BACKWARD_SKIPPED
    BACKWARD_MODE, // D_MBTYPE_BACKWARD
    BACKWARD_MODE, // D_MBTYPE_BACKWARD_Q
    BIDIR_MODE,    // D_BTYPE_BIDIR_SKIPPED
    BIDIR_MODE,    // D_MBTYPE_BIDIR
    BIDIR_MODE,    // D_MBTYPE_BIDIR_Q
    DIRECT_MODE,   // D_MBTYPE_DIRECT_SKIPPED
    DIRECT_MODE,   // D_BTYPE_DIRECT
    DIRECT_MODE,   // D_MBTYPE_DIRECT_Q
};

#define B_PREDICTION_TYPE(t) ((uint32_t)(BPredictMap[t][1]))
#define B_FIELD_FLAGS(t)     ((uint32_t)(BPredictMap[t][0]))

static const unsigned char BPredictMap[15][2] = {
    {0x0, D_MBTYPE_DIRECT_SKIPPED},
    {0x0, D_MBTYPE_DIRECT_SKIPPED},   // B_DIRECT_SKIPPED    1
    {0x2, D_MBTYPE_DIRECT},           // B_DIRECT            2
    {0x3, D_MBTYPE_DIRECT_Q},         // B_DIRECT_Q          3
    {0x8, D_MBTYPE_FORWARD_SKIPPED},  // B_FORWARD_SKIPPED   4
    {0xA, D_MBTYPE_FORWARD},          // B_FORWARD           5
    {0xB, D_MBTYPE_FORWARD_Q},        // B_FORWARD_Q         6
    {0x4, D_MBTYPE_BACKWARD_SKIPPED}, // B_BACKWARD_SKIPPED  7
    {0x6, D_MBTYPE_BACKWARD},         // B_BACKWARD          8
    {0x7, D_MBTYPE_BACKWARD_Q},       // B_BACKWARD_Q        9
    {0xC, D_MBTYPE_BIDIR_SKIPPED},    // B_BIDIR_SKIPPED    10
    {0xE, D_MBTYPE_BIDIR},            // B_BIDIR            11
    {0xF, D_MBTYPE_BIDIR_Q},          // B_BIDIR_Q          12
    {0x2, D_MBTYPE_INTRA},            // B_INTRA            13
    {0x3, D_MBTYPE_INTRA_Q}           // B_INTRA_Q          14
};

/**
 *
 **/

int get_modb(mp4_stream_t *ld);
int get_mb_type(mp4_stream_t *ld);
int get_cbpb(mp4_stream_t *ld);
int get_dbquant(mp4_stream_t *ld);
int get_mb_h263b_type(mp4_stream_t *ld, int *stuffing);

/**
field=0: frame
field=1: top field
field=2: bottom field
**/
int set_mv_b(mp4_stream_t *ld, struct mp4_state *mp4_state, int mb_xpos, int mb_ypos, int mode, int field);
int find_pmv_B(struct mp4_state *mp4_state, int x, int y, int block, int comp);
int set_mv_263b(mp4_stream_t *_ld, struct mp4_state *_mp4_state, int mb_xpos, int mb_ypos, int mode);
extern motion_vector_t h263_find_pmv(struct mp4_state *mp4_state, int x, int y, int block, int mode);


#define _co_located_not_coded(mb_xpos, mb_ypos) mp4_state->codedmap[mb_ypos][mb_xpos]


#include <stdarg.h>

extern void interlaced_information_bvop(mp4_stream_t *ld, struct mp4_state *mp4_state);
extern void rescue_predict(struct mp4_state *_mp4_state);

int macroblock_b_vop(reference_t *ref)
{
    mp4_stream_t *ld = ref->ld;
    struct mp4_state *mp4_state = ref->mp4_state;

    int mb_xpos = mp4_state->hdr.mb_xpos;
    int mb_ypos = mp4_state->hdr.mb_ypos;

    dump_mb_xy_for_trace(mp4_state);

    int j;
    if (mb_xpos == 0x2c && mb_ypos == 01 && mp4_frame_ctr == 476)
        j = 0;
    // vector predictor update, $7.6.8
    if (mb_xpos == 0) {
        mp4_state->mv_pfor[0].x = mp4_state->mv_pfor[0].y = 0;
        mp4_state->mv_pfor[1].x = mp4_state->mv_pfor[1].y = 0;
        mp4_state->mv_pback[0].x = mp4_state->mv_pback[0].y = 0;
        mp4_state->mv_pback[1].x = mp4_state->mv_pback[1].y = 0;
    }

    if ((mp4_state->codedmap[mb_ypos * mp4_state->codedmap_stride + mb_xpos] != 1) || // co_located_not_coded
        (mp4_state->hdr.old_prediction_type == S_VOP)) {
        mp4_state->hdr.modb = get_modb(ld); // modb
        if (mp4_state->hdr.modb != MODB_1) {
            mp4_state->hdr.mb_type = get_mb_type(ld); // mb_type

            if (mp4_state->hdr.modb == MODB_00) {
                mp4_state->hdr.cbpb = get_cbpb(ld); // cbpb
            } else {
                mp4_state->hdr.cbpb = 0;
            }
            mp4_state->hdr.cbp = mp4_state->hdr.cbpb; // fixme: do we need both cbp and cbpb? interlaced_information() refers to cbp.

            if (1) // !scalability
            {
                if ((mp4_state->hdr.mb_type != MB_TYPE_1) && (mp4_state->hdr.cbpb != 0)) {
                    mp4_state->hdr.dbquant = get_dbquant(ld); // dbquant
                    mp4_state->hdr.quantizer += mp4_state->hdr.dbquant;

                    if (mp4_state->hdr.quantizer > 31)
                        mp4_state->hdr.quantizer = 31;
                    else if (mp4_state->hdr.quantizer < 1)
                        mp4_state->hdr.quantizer = 1;
                } else {
                    mp4_state->hdr.dbquant = 0;
                }
                if (mp4_state->hdr.interlaced)
                // attention
                // we don't want to overwrite fieldpredictedmap!
                {
                    interlaced_information_bvop(ld, mp4_state);
                    if (mp4_state->hdr.mb_type == MB_TYPE_1) {
                        // in DIRECT mode, prediction mode ( interlaced/frame or progressive/field ) is determined
                        // by prediction mode of co-located macroblock.
                        mp4_state->hdr.field_prediction =
                            mp4_state->fieldpredictedmap[(mb_ypos + 1) * mp4_state->fieldpredictedmap_stride + mb_xpos + 1];
                        mp4_state->hdr.forward_top_field_reference =
                            mp4_state->fieldrefmap[2 * mb_xpos + mb_ypos * mp4_state->fieldrefmap_stride];
                        mp4_state->hdr.forward_bottom_field_reference =
                            mp4_state->fieldrefmap[2 * mb_xpos + mb_ypos * mp4_state->fieldrefmap_stride + 1];
                        mp4_state->hdr.backward_top_field_reference = 0;
                        mp4_state->hdr.backward_bottom_field_reference = 1;
                    }
                }

                if ((mp4_state->hdr.mb_type == MB_TYPE_01) || (mp4_state->hdr.mb_type == MB_TYPE_0001)) {
                    if (mp4_state->hdr.interlaced && mp4_state->hdr.field_prediction) {
                        set_mv_b(ld, mp4_state, mb_xpos, mb_ypos, FORWARD_MODE, 1);
                        set_mv_b(ld, mp4_state, mb_xpos, mb_ypos, FORWARD_MODE, 2);
                    } else {
                        set_mv_b(ld, mp4_state, mb_xpos, mb_ypos, FORWARD_MODE, 0);
                    }
                }
                if ((mp4_state->hdr.mb_type == MB_TYPE_01) || (mp4_state->hdr.mb_type == MB_TYPE_001)) {
                    if (mp4_state->hdr.interlaced && mp4_state->hdr.field_prediction) {
                        set_mv_b(ld, mp4_state, mb_xpos, mb_ypos, BACKWARD_MODE, 1);
                        set_mv_b(ld, mp4_state, mb_xpos, mb_ypos, BACKWARD_MODE, 2);
                    } else {
                        set_mv_b(ld, mp4_state, mb_xpos, mb_ypos, BACKWARD_MODE, 0);
                        // fixme: here I read the field motion vectors, I need to put them in a separate structure
                    }
                }
                if (mp4_state->hdr.mb_type == MB_TYPE_1) {
#ifdef MPEG4_DUMP_ENABLE
                    fprintf(g_mpeg4_ctx->fp_mb_info, "  direct mode");
#endif
                    set_mv_b(ld, mp4_state, mb_xpos, mb_ypos, DIRECT_MODE, 0);
                }
            }

            reconstruct_bvop(ref, mp4_state->hdr.mb_type);

            for (j = 0; j < 6; j++) {
                int coded = mp4_state->hdr.cbpb & (1 << (5 - j));
                mp4_state->mpeg_coef_matrix_no[j] = 0;

                if (coded) {
                    block_inter(ref, ld, mp4_state, &ld->block[0], j, (coded != 0));
                    // addblockInter(ref, &ld->block[0], j, mb_xpos, mb_ypos);
                }
            }
        } else {
            mp4_state->hdr.field_prediction =
                mp4_state->fieldpredictedmap[(mb_ypos + 1) * mp4_state->fieldpredictedmap_stride + mb_xpos + 1];
            mp4_state->hdr.forward_top_field_reference = mp4_state->fieldrefmap[2 * mb_xpos + mb_ypos * mp4_state->fieldrefmap_stride];
            mp4_state->hdr.forward_bottom_field_reference =
                mp4_state->fieldrefmap[2 * mb_xpos + mb_ypos * mp4_state->fieldrefmap_stride + 1];
            mp4_state->hdr.backward_top_field_reference = 0;
            mp4_state->hdr.backward_bottom_field_reference = 1;
            mp4_state->mv[(mb_ypos + 1) * mp4_state->MV_stride + mb_xpos + 1][4].x = 0;
            mp4_state->mv[(mb_ypos + 1) * mp4_state->MV_stride + mb_xpos + 1][4].y = 0;

            reconstruct_bvop(ref, DIRECT_MODE);
        }
    } else {
#ifdef MPEG4_DUMP_ENABLE
        fprintf(g_mpeg4_ctx->fp_mb_info, "  not coded mb");
        fprintf(g_mpeg4_ctx->fp_mb_info, "  mv (0, 0), forward pred");
#endif
        // not coded
        mp4_state->mv[(mb_ypos + 1) * mp4_state->MV_stride + mb_xpos + 1][4].x = 0;
        mp4_state->mv[(mb_ypos + 1) * mp4_state->MV_stride + mb_xpos + 1][4].y = 0;
        mp4_state->hdr.field_prediction = 0;

        reconstruct_bvop(ref, FORWARD_MODE);
    }

    // mpeg_qp_buf[mp4_state->hdr.mb_ypos][mp4_state->hdr.mb_xpos] = (unsigned char)mp4_state->hdr.quantizer;

    if (mp4_state->hdr.mb_xpos < (mp4_state->mb_width - 1)) {
        mp4_state->hdr.mb_xpos++;
    } else {
        mp4_state->hdr.mb_ypos++;
        mp4_state->hdr.mb_xpos = 0;
    }

    return 1;
}

int macroblock_h263_bvop(reference_t *ref)
{
    mp4_stream_t *ld = ref->ld;
    struct mp4_state *mp4_state = ref->mp4_state;

    int mb_xpos = mp4_state->hdr.mb_xpos;
    int mb_ypos = mp4_state->hdr.mb_ypos;
    int j;
    int is_stuffing;
    int bGetDQUANT = 0;
    int bGetCBPC = 0;
    int bGetMVDBW = 0;
    int bGetMVDFW = 0;
    uint32_t uResult;
    memset(mp4_state->mpeg_coef_matrix, 0, 6 * 64 * sizeof(unsigned long));

    if (mb_xpos < (mp4_state->mb_width - 1)) {
        if (!mb_xpos)
            mp4_state->hdr.gob_edge.ileft = 1;
    }

Read_not_coded:
    mp4_state->hdr.not_coded = getbits(ld, 1);
    if (!mp4_state->hdr.not_coded) {
        uResult = get_mb_h263b_type(ld, &is_stuffing); // mb_type
        if (is_stuffing)
            goto Read_not_coded;
        bGetDQUANT = bGetCBPC = bGetMVDFW = bGetMVDBW = 0;

        mp4_state->hdr.mb_type = B_PREDICTION_TYPE(uResult);

        bGetDQUANT = MBTYPE_DQUANT(B_FIELD_FLAGS(uResult));
        bGetMVDFW = MBTYPE_MVDFW(B_FIELD_FLAGS(uResult));
        bGetMVDBW = MBTYPE_MVDBW(B_FIELD_FLAGS(uResult));
        bGetCBPC = (MBTYPE_CBPC(B_FIELD_FLAGS(uResult)) ? 1 : 0);

        mp4_state->hdr.mb_type = MapMBType[mp4_state->hdr.mb_type];

        // INTRA_MODE: present in intra MB's when advanced intra mode used
        if ((INTRA_MODE == mp4_state->hdr.mb_type) && (!mp4_state->hdr.short_video_header || mp4_state->hdr.h263_aic)) {
            mp4_state->hdr.ac_pred_flag = getbits(ld, 1);
            if (mp4_state->hdr.h263_aic && mp4_state->hdr.ac_pred_flag)
                mp4_state->hdr.h263_aic_dir = getbits(ld, 1); // 1: AINTRA_Mode_horiz, 0:AINTRA_MODE_VERT
        } else {
            mp4_state->hdr.ac_pred_flag = 0;
        }
        // CBPC ----------------------------------------------------
        if (bGetCBPC) {
            mp4_state->hdr.cbpc = 0;
            if (getbits(ld, 1)) {
                if (getbits(ld, 1)) {
                    mp4_state->hdr.cbpc = (getbits(ld, 1) ? 2 : 3);
                } else {
                    mp4_state->hdr.cbpc = 1;
                }
            }
            mp4_state->hdr.cbpy = get_cbpy(ld, INTRA_MODE == mp4_state->hdr.mb_type); // cbpy
            mp4_state->hdr.modb = MODB_00;
        } else {
            mp4_state->hdr.cbpc = 0;
            mp4_state->hdr.cbpy = 0;
            mp4_state->hdr.modb = MODB_01;
        }

        mp4_state->hdr.cbp = mp4_state->hdr.cbpb = (mp4_state->hdr.cbpy << 2) | mp4_state->hdr.cbpc;

        if (bGetDQUANT) {

            if (mp4_state->hdr.modified_qantization) {

                if (getbits(ld, 1)) {
                    mp4_state->hdr.dquant = gNewTAB_DQUANT_MQ[mp4_state->hdr.quantizer][getbits(ld, 1)];
                    mp4_state->hdr.quantizer += mp4_state->hdr.dquant;

                } else {
                    int m_tmp;
                    m_tmp = mp4_state->hdr.quantizer;
                    mp4_state->hdr.quantizer = getbits(ld, 5);
                    mp4_state->hdr.dquant = mp4_state->hdr.quantizer - m_tmp;
                }
            } else {
                mp4_state->hdr.dquant = getbits(ld, 2);
                mp4_state->hdr.quantizer += DQtab[mp4_state->hdr.dquant];
                if (mp4_state->hdr.quantizer > 31)
                    mp4_state->hdr.quantizer = 31;
                else if (mp4_state->hdr.quantizer < 1)
                    mp4_state->hdr.quantizer = 1;
            }
        } else {
            mp4_state->hdr.dquant = 0;
        }
        if (!bGetMVDFW)
            memset(&mp4_state->mv[(mb_ypos + 1) * mp4_state->MV_stride + mb_xpos + 1][0], 0, 6 * sizeof(motion_vector_t));
        if (!bGetMVDBW)
            memset(&mp4_state->mv_back[(mb_ypos + 1) * mp4_state->MV_stride + mb_xpos + 1][0], 0, 6 * sizeof(motion_vector_t));

        if (bGetMVDFW) {
            set_mv_263b(ld, mp4_state, mb_xpos, mb_ypos, FORWARD_MODE);
        }
        if (bGetMVDBW) {
            set_mv_263b(ld, mp4_state, mb_xpos, mb_ypos, BACKWARD_MODE);
        }

        if (mp4_state->hdr.mb_type == DIRECT_MODE) {
            mp4_state->mv[(mb_ypos + 1) * mp4_state->MV_stride + mb_xpos + 1][4].x = 0;
            mp4_state->mv[(mb_ypos + 1) * mp4_state->MV_stride + mb_xpos + 1][4].y = 0;
            mp4_state->mv[(mb_ypos + 1) * mp4_state->MV_stride + mb_xpos + 1][5].x = 0;
            mp4_state->mv[(mb_ypos + 1) * mp4_state->MV_stride + mb_xpos + 1][5].y = 0;
            mp4_state->mp4_mv_data[0] = 0;
            mp4_state->mp4_mv_data[1] = 0;
            mp4_state->mp4_mv_res[0] = 0;
            mp4_state->mp4_mv_res[1] = 0;
        }

        if (mp4_state->hdr.mb_type == INTRA_MODE) {
            mp4_state->modemap[(mb_ypos + 1) * mp4_state->modemap_stride + mb_xpos + 1] = INTRA;

            rescue_predict(mp4_state);
            // texture decoding add
            for (j = 0; j < 6; j++) {
                int coded = mp4_state->hdr.cbp & (1 << (5 - j));

                block_intra(mp4_state->frame_to_decode, ld, mp4_state, &ld->block[0], j, (coded != 0));
            }
        } else {
            mp4_state->modemap[(mb_ypos + 1) * mp4_state->modemap_stride + mb_xpos + 1] = INTER;

            reconstruct_bvop(ref, mp4_state->hdr.mb_type);

            for (j = 0; j < 6; j++) {
                int coded = mp4_state->hdr.cbpb & (1 << (5 - j));
                mp4_state->mpeg_coef_matrix_no[j] = 0;

                if (coded) {
                    block_inter(ref, ld, mp4_state, &ld->block[0], j, (coded != 0));
                    // addblockInter(ref, &ld->block[0], j, mb_xpos, mb_ypos);
                }
            }
        }
    } else {
        mp4_state->hdr.modb = MODB_1;
        mp4_state->modemap[(mb_ypos + 1) * mp4_state->modemap_stride + mb_xpos + 1] = NOT_CODED;

        mp4_state->hdr.field_prediction = mp4_state->fieldpredictedmap[(mb_ypos + 1) * mp4_state->fieldpredictedmap_stride + mb_xpos + 1];
        mp4_state->hdr.forward_top_field_reference = mp4_state->fieldrefmap[2 * mb_xpos + mb_ypos * mp4_state->fieldrefmap_stride];
        mp4_state->hdr.forward_bottom_field_reference = mp4_state->fieldrefmap[2 * mb_xpos + mb_ypos * mp4_state->fieldrefmap_stride + 1];
        mp4_state->hdr.backward_top_field_reference = 0;
        mp4_state->hdr.backward_bottom_field_reference = 1;

        mp4_state->mv[(mb_ypos + 1) * mp4_state->MV_stride + mb_xpos + 1][4].x = 0;
        mp4_state->mv[(mb_ypos + 1) * mp4_state->MV_stride + mb_xpos + 1][4].y = 0;
        mp4_state->mv[(mb_ypos + 1) * mp4_state->MV_stride + mb_xpos + 1][5].x = 0;
        mp4_state->mv[(mb_ypos + 1) * mp4_state->MV_stride + mb_xpos + 1][5].y = 0;

        reconstruct_bvop(ref, DIRECT_MODE);
        memset(&mp4_state->mv[(mb_ypos + 1) * mp4_state->MV_stride + mb_xpos + 1][0], 0, 4 * sizeof(motion_vector_t));
        memset(&mp4_state->mv_back[(mb_ypos + 1) * mp4_state->MV_stride + mb_xpos + 1][0], 0, 4 * sizeof(motion_vector_t));
    }
    if (mp4_state->hdr.gob_edge.ileft)
        mp4_state->hdr.gob_edge.ileft--;
    if (mp4_state->hdr.gob_edge.iabove)
        mp4_state->hdr.gob_edge.iabove--;
    mp4_state->quant_store[(mp4_state->hdr.mb_ypos + 1) * mp4_state->quant_store_stride + mp4_state->hdr.mb_xpos + 1] =
        mp4_state->hdr.quantizer;
    // mpeg_qp_buf[mp4_state->hdr.mb_ypos][mp4_state->hdr.mb_xpos] = (unsigned char)mp4_state->hdr.quantizer;

    if (mp4_state->hdr.mb_xpos < (mp4_state->mb_width - 1)) {
        mp4_state->hdr.mb_xpos++;
    } else {
        mp4_state->hdr.mb_ypos++;
        mp4_state->hdr.mb_xpos = 0;
    }

    return 1;
};

int macroblock_b_vop_finish(reference_t *ref)
{
    struct mp4_state *mp4_state = ref->mp4_state;

    while (mp4_state->hdr.mba < mp4_state->hdr.mba_size) {
        // (not coded)
        mp4_state->mv[(mp4_state->hdr.mb_ypos + 1) * mp4_state->MV_stride + mp4_state->hdr.mb_xpos + 1][4].x = 0;
        mp4_state->mv[(mp4_state->hdr.mb_ypos + 1) * mp4_state->MV_stride + mp4_state->hdr.mb_xpos + 1][4].y = 0;
        mp4_state->hdr.field_prediction = 0;

        reconstruct_bvop(ref, FORWARD_MODE);

        mp4_state->hdr.mba++;
        if (mp4_state->hdr.mb_xpos < (mp4_state->mb_width - 1)) {
            mp4_state->hdr.mb_xpos++;
        } else {
            mp4_state->hdr.mb_ypos++;
            mp4_state->hdr.mb_xpos = 0;
        }
    }

    return 1;
}

int get_modb(mp4_stream_t *ld)
{
    int code = showbits(ld, 2);

    switch (code) {
    case 0:
        getbits(ld, 2);
        return MODB_00;
    case 1:
        getbits(ld, 2);
        return MODB_01;
    default:
        getbits(ld, 1);
        return MODB_1;
    }
}

tab_type MBTYPEtab[16] = {
    {-1, 0},         {MB_TYPE_0001, 4}, {MB_TYPE_001, 3}, {MB_TYPE_001, 3}, {MB_TYPE_01, 2}, {MB_TYPE_01, 2},
    {MB_TYPE_01, 2}, {MB_TYPE_01, 2},   {MB_TYPE_1, 1},   {MB_TYPE_1, 1},   {MB_TYPE_1, 1},  {MB_TYPE_1, 1},
    {MB_TYPE_1, 1},  {MB_TYPE_1, 1},    {MB_TYPE_1, 1},   {MB_TYPE_1, 1}
};

int get_mb_type(mp4_stream_t *ld)
{
    int code = showbits(ld, 4);

    flushbits(ld, MBTYPEtab[code].len);
    return MBTYPEtab[code].val;
}

int get_mb_h263b_type(mp4_stream_t *ld, int *stuffing)
{
    int code = showbits(ld, 7);

    *stuffing = 0;
    flushbits(ld, gNewTAB_MBTYPE_B[code] & 0x0f);
    if (code == 0) {
        code = getbits(ld, 2);
        if (1 == code)
            *stuffing = 1;
    }
    return gNewTAB_MBTYPE_B[code] >> 4;
}

int get_cbpb(mp4_stream_t *ld)
{
    // no transparent blocks -> coded block bit pattern is 6 bits
    int code = getbits(ld, 6);
    return code;
}

int get_dbquant(mp4_stream_t *ld)
{
    int code = showbits(ld, 2);

    switch (code) {
    case 0:
    case 1:
        getbits(ld, 1);
        return 0;
    case 2:
        getbits(ld, 2);
        return -2;
    case 3:
        getbits(ld, 2);
        return 2;
    }

    return 0; // this line should is never reached
}

/**
 * field 0: frame mode
 * field 1: top
 * field 2: bottom
 */
int set_mv_b(mp4_stream_t *_ld, struct mp4_state *_mp4_state, int mb_xpos, int mb_ypos, int mode, int field)
{
    mp4_stream_t *ld = _ld;
    struct mp4_state *mp4_state = _mp4_state;

    int vop_fcode, scale_fac;
    int hor_mv_data, ver_mv_data, hor_mv_res, ver_mv_res;
    int high, low, range;

    int mvd_x, mvd_y;
    motion_vector_t pmv, mv;

    motion_vector_t(*MV_array)[6] = (field == 2) ? mp4_state->mv_field : mp4_state->mv;

    if (mode == DIRECT_MODE) {
        vop_fcode = 1; // scale_fac == 1, residual will not be decoded
    } else if (mode == FORWARD_MODE) {
        vop_fcode = mp4_state->hdr.fcode_for;
    } else {
        vop_fcode = mp4_state->hdr.fcode_back;
    }

    scale_fac = 1 << (vop_fcode - 1);
    high = (32 * scale_fac) - 1;
    low = ((-32) * scale_fac);
    range = (64 * scale_fac);

    hor_mv_res = 0;
    ver_mv_res = 0;

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

    if (mp4_state->hdr.quarter_pixel == 1) {
        // do nothing here, mvd_x and mvd_y will be
        // in quarter pixel resolution
    }

    if (mode == FORWARD_MODE) {
        pmv = mp4_state->mv_pfor[(field == 2) ? 1 : 0];
    } else if (mode == BACKWARD_MODE) {
        pmv = mp4_state->mv_pback[(field == 2) ? 1 : 0];
    } else if (mode == DIRECT_MODE) {
        // in this case the predictors are zero and the delta vector is extracted (MVDx, MVDy)
        pmv.x = pmv.y = 0;
    }

    mv.x = pmv.x + mvd_x;

    if (mv.x < low)
        mv.x += range;
    if (mv.x > high)
        mv.x -= range;

    if (field)
        mv.y = 2 * (pmv.y / 2 + mvd_y);
    else
        mv.y = pmv.y + mvd_y;

    if (mv.y < low)
        mv.y += range;
    if (mv.y > high)
        mv.y -= range; // [review] is this correct for interlaced macroblocks?

    // store mv and update vector predictor, $7.6.8
    if (mode == FORWARD_MODE) {
        MV_array[(mb_ypos + 1) * mp4_state->MV_stride + mb_xpos + 1][4] = mv;
        if (field == 0)
            mp4_state->mv_pfor[0] = mp4_state->mv_pfor[1] = mv;
        else
            mp4_state->mv_pfor[field - 1] = mv;
    } else if (mode == BACKWARD_MODE) {
        MV_array[(mb_ypos + 1) * mp4_state->MV_stride + mb_xpos + 1][5] = mv;
        if (field == 0)
            mp4_state->mv_pback[0] = mp4_state->mv_pback[1] = mv;
        else
            mp4_state->mv_pback[field - 1] = mv;
    } else if (mode == DIRECT_MODE) {
        // directmode is stored in MV[comp][4]
        MV_array[(mb_ypos + 1) * mp4_state->MV_stride + mb_xpos + 1][4] = mv;
    }
    return 1;
}

int set_mv_263b(mp4_stream_t *_ld, struct mp4_state *_mp4_state, int mb_xpos, int mb_ypos, int mode)
{
    mp4_stream_t *ld = _ld;
    struct mp4_state *mp4_state = _mp4_state;

    int vop_fcode, scale_fac;
    int hor_mv_data, ver_mv_data, hor_mv_res, ver_mv_res;
    int high, low, range;

    int mvd_x, mvd_y;
    motion_vector_t pmv, mv;
    int i;

    if (mode == DIRECT_MODE) {
        vop_fcode = 1; // scale_fac == 1, residual will not be decoded
    } else if (mode == FORWARD_MODE) {
        vop_fcode = mp4_state->hdr.fcode_for;
    } else {
        vop_fcode = mp4_state->hdr.fcode_back;
    }

    scale_fac = 1 << (vop_fcode - 1);
    high = (32 * scale_fac) - 1;
    low = ((-32) * scale_fac);
    range = (64 * scale_fac);

    hor_mv_res = 0;
    ver_mv_res = 0;

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

    if (mp4_state->hdr.quarter_pixel == 1) {
        // do nothing here, mvd_x and mvd_y will be
        // in quarter pixel resolution
    }

    if (mode == FORWARD_MODE) {
        pmv = h263_find_pmv(mp4_state, mb_xpos, mb_ypos, 0, FORWARD_MODE);
    } else if (mode == BACKWARD_MODE) {
        pmv = h263_find_pmv(mp4_state, mb_xpos, mb_ypos, 0, BACKWARD_MODE);
    } else if (mode == DIRECT_MODE) {
        // in this case the predictors are zero and the delta vector is extracted (MVDx, MVDy)
        pmv.x = pmv.y = 0;
    }

    mv.x = pmv.x + mvd_x;

    if (mv.x < low)
        mv.x += range;
    if (mv.x > high)
        mv.x -= range;

    mv.y = pmv.y + mvd_y;

    if (mv.y < low)
        mv.y += range;
    if (mv.y > high)
        mv.y -= range;

    if (mode == FORWARD_MODE) {
        for (i = 0; i < 4; i++) {
            mp4_state->mv[(mb_ypos + 1) * mp4_state->MV_stride + mb_xpos + 1][i].x = mv.x;
            mp4_state->mv[(mb_ypos + 1) * mp4_state->MV_stride + mb_xpos + 1][i].y = mv.y;
        }
        mp4_state->mv[(mb_ypos + 1) * mp4_state->MV_stride + mb_xpos + 1][4].x = mv.x;
        mp4_state->mv[(mb_ypos + 1) * mp4_state->MV_stride + mb_xpos + 1][4].y = mv.y;
    } else if (mode == BACKWARD_MODE) {
        for (i = 0; i < 4; i++) {
            mp4_state->mv_back[(mb_ypos + 1) * mp4_state->MV_stride + mb_xpos + 1][i].x = mv.x;
            mp4_state->mv_back[(mb_ypos + 1) * mp4_state->MV_stride + mb_xpos + 1][i].y = mv.y;
        }
        mp4_state->mv[(mb_ypos + 1) * mp4_state->MV_stride + mb_xpos + 1][5].x = mv.x;
        mp4_state->mv[(mb_ypos + 1) * mp4_state->MV_stride + mb_xpos + 1][5].y = mv.y;
    }

    return 1;
}
