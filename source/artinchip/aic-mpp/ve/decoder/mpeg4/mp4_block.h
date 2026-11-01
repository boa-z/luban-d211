/*
 * Copyright (C) 2020-2025 ArtInChip Technology Co. Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 *  author: <xiaodong.zhao@artinchip.com>
 *  Desc: mpeg4 block interface
 *
 */

#ifndef _MP4_BLOCK_H_
#define _MP4_BLOCK_H_

#include "mp4_vars.h"

typedef void (clearblockProc) (int16_t*);
typedef clearblockProc* clearblockProcPtr;

extern clearblockProc clearblock_generic;
extern clearblockProcPtr clearblock;

extern int block_intra(unsigned char * frame_ref[], mp4_stream_t * _ld, struct mp4_state * _mp4_state, short * block, int block_num, int coded);
extern int block_inter(reference_t * ref, mp4_stream_t * ld, struct mp4_state * mp4_state, short block[64], int block_num, int coded);

extern int block_intra_311(unsigned char* frame_ref[], mp4_stream_t * _ld, struct mp4_state * _mp4_state, short * block, int block_num, int coded);
extern int block_inter_311(mp4_stream_t * _ld, struct mp4_state * _mp4_state, short * block, int block_num);

extern int get_dc_size_lum(mp4_stream_t * _ld);
extern int get_dc_size_chr(mp4_stream_t * _ld);
extern int get_dc_diff(mp4_stream_t * _ld, int dct_dc_size);

extern int dc_scaler(int quantizer, int block_num);
extern int intra_ac_vld(mp4_stream_t * _ld, int reversible_vlc_flag);

#endif // _MP4_BLOCK_H_

