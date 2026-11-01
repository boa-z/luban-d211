/*
 * Copyright (C) 2020-2025 ArtInChip Technology Co. Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 *  author: <xiaodong.zhao@artinchip.com>
 *  Desc: mpeg4 mblock interface
 *
 */

#ifndef _MP4_MBLOCK_H_
#define _MP4_MBLOCK_H_

typedef struct {
  int val, len;
} VLCtabMb;

void dump_mb_xy_for_trace(struct mp4_state * mp4_state);

#endif // _MP4_MBLOCK_H_
extern int get_ssc(reference_t * ref);
extern int macroblock(reference_t * ref);
extern int macroblock_i_vop(reference_t * ref);
extern int macroblock_p_vop(reference_t * ref);
extern int getgobhdr(reference_t * ref,int gob_index);
extern int block();
extern void mark_packet_boundary (struct mp4_state * _mp4_state, int mbnum);



