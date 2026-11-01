/*
 * Copyright (C) 2020-2025 ArtInChip Technology Co. Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 *  author: <xiaodong.zhao@artinchip.com>
 *  Desc: mpeg4 recon interface
 *
 */

#ifndef _MP4_RECON_H_
#define _MP4_RECON_H_

#include "mp4_vars.h"

void reconstruct(reference_t *ref);
void reconstruct_bvop(reference_t *ref, int mode);


typedef void(reconCompAccurateProc)(unsigned char *src, unsigned char *dst, int stride, int stride_dst, int xh, int yh,
                                    int rounding);
typedef reconCompAccurateProc *reconCompAccurateProcPtr;


typedef void(reconCompAffineProc)(unsigned char *src, unsigned char *dst, int lx, int ly, int lx_dst, int px, int py,
                                  affine_transform_t *ptrans, int warping_accuracy, int rounding);
typedef reconCompAffineProc *reconCompAffineProcPtr;


extern void (*const set_gmc_mv_pointers[4])(struct mp4_state *);

#endif // _MP4_RECON_H_
