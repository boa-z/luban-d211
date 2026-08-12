/*
 * Copyright (C) 2020-2025 ArtInChip Technology Co. Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 *  author: <xiaodong.zhao@artinchip.com>
 *  Desc: mpeg4 block interface
 *
 */

#ifdef WIN32
#include <crtdbg.h>
#endif
#include <stdio.h>
#include <stdlib.h>

#include "mp4_block.h"
#include "mp4_predict.h"
#include "mp4_tables.h"
#include "mp4_vars.h"
#include "mp4_vld.h"
#include "mp4_vld_r.h"
#include "mp4_global.h"
#include "mp4_getbits.h"
#include "mpeg4_decoder.h"

// lookup table used for chroma blocks when modified quantization is in effect
const int g_new_quant_c[32] = {
    0,  1,  2,  3,  4,  5,  6,  6,  7,  8,  9,  9,  10, 10, 11, 11,
    12, 12, 12, 13, 13, 13, 14, 14, 14, 14, 14, 15, 15, 15, 15, 15
};

#ifdef COUNT_DCT
static inline int64_t read_counter()
{
    int64_t a;
    __asm__ __volatile__("rdtsc\n\t"
                         "movl %%eax, (%%ecx)\n\t"
                         "movl %%edx, 4(%%ecx)\n\t"
                         :
                         : "c"(&a)
                         : "eax", "edx");
    return a;
}
#endif

extern int dct_count, recon_count;
extern int64_t dct_time, recon_time;

// Note: unused, this code has been inlined
int intra_ac_vld(mp4_stream_t *_ld, int reversible_vlc_flag)
{
    mp4_stream_t *ld = _ld;
    event_t event;

    if (!reversible_vlc_flag) {
        event = vld_intra_dct(ld);
    } else {
        event = rvld_intra_dct(ld);
    }

    // to avoid new coeff to be decoded for this block
    if (event.run == -1)
        return 0;

    if (event.last == 1)
        return event.level;

    return event.level;
}

static inline void decode_dc_coefficients(mp4_stream_t *ld, struct mp4_state *mp4_state, short *block, int block_num)
{
    if (!mp4_state->hdr.short_video_header) {
        mp4_state->hdr.dc_scaler = dc_scaler(mp4_state->hdr.quantizer, block_num); // calculate DC scaler
    }

    if (!mp4_state->hdr.data_partitioning) {
        clearblock(block); // clearblock

        if (!mp4_state->hdr.short_video_header) {
            if (mp4_state->hdr.use_intra_dc_vlc) {
                int dct_dc_size, dct_dc_diff;
                if (block_num < 4) {
                    dct_dc_size = get_dc_size_lum(ld);
                    if (dct_dc_size != 0)
                        dct_dc_diff = get_dc_diff(ld, dct_dc_size);
                    else
                        dct_dc_diff = 0;
                    if (dct_dc_size > 8)
                        getbits1(ld); // marker bit
                } else {
                    dct_dc_size = get_dc_size_chr(ld);
                    if (dct_dc_size != 0)
                        dct_dc_diff = get_dc_diff(ld, dct_dc_size);
                    else
                        dct_dc_diff = 0;
                    if (dct_dc_size > 8)
                        getbits1(ld); // marker bit
                }

                block[0] = (short)dct_dc_diff;
            }
        } else if (!mp4_state->hdr.h263_aic) {
            block[0] = getbits(ld, 8);
            if (block[0] == 128)
                return;

            if (block[0] == 255) {
                block[0] = 128;
            }
            block[0] *= 8;
        }
    }
}

static inline void set_prediction_directions(mp4_stream_t *ld, struct mp4_state *mp4_state, short *block, int block_num)
{
    // Mask the prediciton direction when short header by Robert Yuan in Sept 13,2003
    if (!mp4_state->hdr.short_video_header) {
        if (!mp4_state->hdr.use_intra_dc_vlc)
            set_prediction_direction(mp4_state, block_num);
        else
            set_prediction_direction_intradc(mp4_state, block_num, &block[0]);
    } else if (mp4_state->hdr.h263_aic) {
        if (mp4_state->hdr.ac_pred_flag) {
            if (mp4_state->hdr.h263_aic_dir)
                mp4_state->coeff_pred.predict_dir = LEFT;
            else
                mp4_state->coeff_pred.predict_dir = TOP;
        } else {
            mp4_state->coeff_pred.predict_dir = NONE;
        }
    }
}

static inline int process_coded_data(mp4_stream_t *ld, struct mp4_state *mp4_state, short *block, int block_num, int coded)
{
    if (coded) {
        unsigned int *zigzag; // zigzag scan dir
        int i; // first AC coefficient position
        event_t event;

        if (mp4_state->hdr.ac_pred_flag == 1) {
            zigzag = (mp4_state->coeff_pred.predict_dir == TOP) ? alternate_horizontal_scan : alternate_vertical_scan;
        } else {
            zigzag = zig_zag_scan;
        }

        if ((mp4_state->hdr.interlaced) && (mp4_state->hdr.alternate_vertical_scan_flag))
            zigzag = alternate_vertical_scan;

        // default value (no use intra ac vld for dc value) is 1
        i = (mp4_state->hdr.short_video_header ? 1 : mp4_state->hdr.use_intra_dc_vlc ? 1 : 0);
        if (mp4_state->hdr.h263_aic)
            i = 0;
        mp4_state->mpeg_coef_matrix_no[block_num] = i;
        do {// event vld
            event = mp4_state->vld_intra_fun(ld);
            if (event.run == -1)
                return 0;

            i += event.run;

            if (i >= 64)
                return 0;

            block[zigzag[i]] = (short)event.level;
            i++;
        } while (!event.last);
    }

    return 1;
}

static inline void perform_dc_prediction(struct mp4_state *mp4_state, short *block, int block_num)
{
    // 2. DC prediction
    if ((!mp4_state->hdr.use_intra_dc_vlc) && (!mp4_state->hdr.short_video_header)) {
#ifdef MPEG4_DUMP_ENABLE
        fprintf(g_mpeg4_ctx->fp_mb_coeff, " intra mb & short_video_header=0, need DC pred\n");
#endif
        dc_recon(mp4_state, block_num, &block[0]);
    }
}

static inline void perform_ac_prediction(struct mp4_state *mp4_state, short *block, int block_num)
{
    // 3. AC prediction
    if (!mp4_state->hdr.short_video_header) {
#ifdef MPEG4_DUMP_ENABLE
        fprintf(g_mpeg4_ctx->fp_mb_coeff, " intra mb & short_video_header=0, need AC pred\n");
#endif
        mp4_state->hdr.intrablock_rescaled = ac_rescaling(mp4_state, block_num, &block[0]);
        if (!mp4_state->hdr.intrablock_rescaled) {
            ac_recon(mp4_state, block_num, &block[0]);
        }
        ac_store(mp4_state, block_num, &block[0]);
    }
}

static inline void store_mb_coefficients(struct mp4_state *mp4_state, short *block, int block_num)
{
    // store mb coeffs to mpeg_coef_matrix
    int dc_tmp = block[0];
    if (!mp4_state->hdr.short_video_header) {
        // DC coeff should
        block[0] = g_mpeg4_ctx->intra_dc_coeff;
    }
    mpeg_save_mb_coeff(mp4_state, block, block_num);
    if (!mp4_state->hdr.short_video_header) {
        block[0] = dc_tmp;
    }

#ifdef MPEG4_DUMP_ENABLE
    char pred_dir_name[4][8] = {"left", "top", "none", "both"};
    fprintf(g_mpeg4_ctx->fp_mb_coeff, "===> after DC/AC pred, ac_pred_flag: %d, predict_dir: %s\n", mp4_state->hdr.ac_pred_flag,
            pred_dir_name[mp4_state->coeff_pred.predict_dir]);
#endif
}

int block_intra(unsigned char *frame_data[], mp4_stream_t *_ld, struct mp4_state *_mp4_state, short *block, int block_num, int coded)
{
    struct mp4_state *mp4_state = _mp4_state;
    mp4_stream_t *ld = _ld;

    // 1. decode DC/AC coeff
    decode_dc_coefficients(ld, mp4_state, block, block_num);

    // Set prediction directions
    set_prediction_directions(ld, mp4_state, block, block_num);

    // Process coded data
    if (!process_coded_data(ld, mp4_state, block, block_num, coded))
        return 0;

#ifdef MPEG4_DUMP_ENABLE
    fprintf(g_mpeg4_ctx->fp_mb_coeff, "===> before pred\n");
    for (int i = 0; i < 8; i++) {
        for (int j = 0; j < 8; j++) {
            fprintf(g_mpeg4_ctx->fp_mb_coeff, "%5d ", block[8 * i + j]);
        }
        fprintf(g_mpeg4_ctx->fp_mb_coeff, "\n");
    }
#endif

    // 2. DC prediction
    perform_dc_prediction(mp4_state, block, block_num);

    // 3. AC prediction
    perform_ac_prediction(mp4_state, block, block_num);

    // Store coefficients
    store_mb_coefficients(mp4_state, block, block_num);

    return 1;
}

int block_inter(reference_t *ref, mp4_stream_t *_ld, struct mp4_state *_mp4_state, short block[64], int block_num, int coded)
{
    mp4_stream_t *ld = _ld;
    struct mp4_state *mp4_state = _mp4_state;
    event_t event;
    unsigned int *zigzag = zig_zag_scan; // zigzag scan dir
    int i;
    int iQ = mp4_state->hdr.quantizer;
    // unsigned long tmp;
    short coeff_block[64] = {0};

    if (mp4_state->hdr.modified_qantization && block_num >= 4)
        iQ = g_new_quant_c[iQ];

    clearblock(block); // clearblock
    mp4_state->mpeg_coef_matrix_no[block_num] = 0;

    if ((mp4_state->hdr.interlaced) && (mp4_state->hdr.alternate_vertical_scan_flag))
        zigzag = alternate_vertical_scan;

#ifdef MPEG4_DUMP_ENABLE
    fprintf(g_mpeg4_ctx->fp_mb_coeff, "block_inter, block num(%d), cbp: 0x%x\n", block_num, mp4_state->hdr.cbp);
#endif

    // 1. parse AC coeff, and inverse quant type
    if (mp4_state->hdr.quant_type == 0) {
        int q_scale = iQ;
        int q_2scale = q_scale << 1;
        int q_add = (q_scale & 1) ? q_scale : (q_scale - 1);

        i = 0;
        do {    // event vld
            int index;
            event = mp4_state->vld_inter_fun(ld);
            if (event.run == -1)
                return 0;

            i += event.run;
            if (i >= 64)
                return 0;

            index = zigzag[i];
            if (event.level > 0) {
                block[index] = (q_2scale * event.level) + q_add;
            } else {
                block[index] = (q_2scale * event.level) - q_add;
            }
            coeff_block[index] = event.level;

            if (block[zigzag[i]] > 2047)
                block[zigzag[i]] = 2047;
            else if (block[zigzag[i]] < -2048)
                block[zigzag[i]] = -2048;

            i++;
        } while (!event.last);
    } else {
        int k, m = 0;
        i = 0;

        // event vld
        do {
            event = mp4_state->vld_inter_fun(ld);
            if (event.run == -1)
                return 0;

            i += event.run;
            if (i >= 64)
                return 0;

            k = (event.level > 0) ? 1 : -1;
            coeff_block[zigzag[i]] = event.level;
            block[zigzag[i]] =
                ((2 * event.level + k) * iQ * mp4_state->hdr.nonintra_quant_matrix[zigzag[i]] + ((event.level < 0) ? 15 : 0)) >> 4;

            if (block[zigzag[i]] > 2047)
                block[zigzag[i]] = 2047;
            else if (block[zigzag[i]] < -2048)
                block[zigzag[i]] = -2048;
            // Mismatch control
            m ^= block[zigzag[i]];

            i++;
        } while (!event.last);

        if (!(m % 2))
            block[63] ^= 1;
    }

    mpeg_save_mb_coeff(mp4_state, coeff_block, block_num);

    return 1;
}

int get_dc_size_lum(mp4_stream_t *_ld)
{
    mp4_stream_t *ld = _ld;

    int code;

    if (showbits(ld, 11) == 1) {
        flushbits(ld, 11);
        return 12;
    }
    if (showbits(ld, 10) == 1) {
        flushbits(ld, 10);
        return 11;
    }
    if (showbits(ld, 9) == 1) {
        flushbits(ld, 9);
        return 10;
    }
    if (showbits(ld, 8) == 1) {
        flushbits(ld, 8);
        return 9;
    }
    if (showbits(ld, 7) == 1) {
        flushbits(ld, 7);
        return 8;
    }
    if (showbits(ld, 6) == 1) {
        flushbits(ld, 6);
        return 7;
    }
    if (showbits(ld, 5) == 1) {
        flushbits(ld, 5);
        return 6;
    }
    if (showbits(ld, 4) == 1) {
        flushbits(ld, 4);
        return 5;
    }

    code = showbits(ld, 3);

    if (code == 1) {
        flushbits(ld, 3);
        return 4;
    } else if (code == 2) {
        flushbits(ld, 3);
        return 3;
    } else if (code == 3) {
        flushbits(ld, 3);
        return 0;
    }

    code = showbits(ld, 2);

    if (code == 2) {
        flushbits(ld, 2);
        return 2;
    } else if (code == 3) {
        flushbits(ld, 2);
        return 1;
    }

    return 0;
}

int get_dc_size_chr(mp4_stream_t *_ld)
{
    mp4_stream_t *ld = _ld;

    if (showbits(ld, 12) == 1) {
        flushbits(ld, 12);
        return 12;
    }
    if (showbits(ld, 11) == 1) {
        flushbits(ld, 11);
        return 11;
    }
    if (showbits(ld, 10) == 1) {
        flushbits(ld, 10);
        return 10;
    }
    if (showbits(ld, 9) == 1) {
        flushbits(ld, 9);
        return 9;
    }
    if (showbits(ld, 8) == 1) {
        flushbits(ld, 8);
        return 8;
    }
    if (showbits(ld, 7) == 1) {
        flushbits(ld, 7);
        return 7;
    }
    if (showbits(ld, 6) == 1) {
        flushbits(ld, 6);
        return 6;
    }
    if (showbits(ld, 5) == 1) {
        flushbits(ld, 5);
        return 5;
    }
    if (showbits(ld, 4) == 1) {
        flushbits(ld, 4);
        return 4;
    }
    if (showbits(ld, 3) == 1) {
        flushbits(ld, 3);
        return 3;
    }

    return (3 - getbits(ld, 2));
}

int get_dc_diff(mp4_stream_t *_ld, int dct_dc_size)
{
    mp4_stream_t *ld = _ld;

    int code = getbits(ld, dct_dc_size);
    int msb = code >> (dct_dc_size - 1);

    if (msb == 0) {
        return (-1 * (code ^ ((1 << dct_dc_size) - 1)));
    } else {
        return code;
    }
}

int dc_scaler(int quant, int block_num)
{
    int type = (block_num < 4) ? 0 : 1;

    if (type == 0) {
        if (quant > 0 && quant < 5)
            return 8;
        else if (quant > 4 && quant < 9)
            return (2 * quant);
        else if (quant > 8 && quant < 25)
            return (quant + 8);
        else
            return (2 * quant - 16);
    } else {
        if (quant > 0 && quant < 5)
            return 8;
        else if (quant > 4 && quant < 25)
            return ((quant + 13) / 2);
        else
            return (quant - 6);
    }
}

void clearblock_generic (short *psBlock)
{
  uint32_t* pu32_b;
  int i;

    pu32_b = (uint32_t *) psBlock;

  for (i = 0; i < 4; i++)
  {
    pu32_b[0] = pu32_b[1] = pu32_b[2] = pu32_b[3] =
		pu32_b[4] = pu32_b[5] = pu32_b[6] = pu32_b[7] = 0;
    pu32_b += 8; // 16 coeff zeroed
  }
}

clearblockProcPtr clearblock;
