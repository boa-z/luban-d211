/*
 * Copyright (C) 2020-2025 ArtInChip Technology Co. Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 *  author: <xiaodong.zhao@artinchip.com>
 *  Desc: mpeg4 init interface
 *
 */

#include "mp4_block.h"
#include "mp4_portab.h"
#include "mp4_vld.h"
#include "mp4_vld_r.h"
#include "mp4_recon.h"

void init_platform(int *flag_sse, int *flag_3dnow)
{
    *flag_sse = 0;
    *flag_3dnow = 0;

    clearblock = clearblock_generic;

    decore_cleanup = decore_cleanup_generic;
}
