/*
 * Copyright (C) 2020-2025 ArtInChip Technology Co. Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 *  author: <xiaodong.zhao@artinchip.com>
 *  Desc: mpeg4 mblock bvop interface
 *
 */

#ifndef MP4_MBLOCK_BVOP_H
#define MP4_MBLOCK_BVOP_H

extern int macroblock_b_vop(reference_t * ref);
extern int macroblock_b_vop_finish(reference_t *ref);

#endif
