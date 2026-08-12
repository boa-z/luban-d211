/*
 * Copyright (C) 2020-2025 ArtInChip Technology Co. Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 *  author: <xiaodong.zhao@artinchip.com>
 *  Desc: mpeg4 global interface
 *
 */

#include "mp4_global.h"
#include "mp4_vars.h"
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

int do_idct_mode;
unsigned long mp4_frame_ctr;

int mpeg_test4_init(void)
{
    mp4_frame_ctr = 0;

    do_idct_mode = T2_IDCT_MODE;

    srand((unsigned)time(NULL));

    return 1;
}

