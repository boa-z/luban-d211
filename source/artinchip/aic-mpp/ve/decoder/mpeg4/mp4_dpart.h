/*
 * Copyright (C) 2020-2025 ArtInChip Technology Co. Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 *  author: <xiaodong.zhao@artinchip.com>
 *  Desc: mpeg4 dpart interface
 *
 */

#ifndef _MP4_DPART_
#define _MP4_DPART_

#include "mp4_vars.h"

#define DC_MARKER     0x0006b001
#define MOTION_MARKER 0x0001f001

extern int data_partitioned_i_vop(reference_t *ref);
extern int data_partitioned_p_vop(reference_t *ref);

#endif // _MP4_DPART_

