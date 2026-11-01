/*
 * Copyright (C) 2020-2025 ArtInChip Technology Co. Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 *  author: <xiaodong.zhao@artinchip.com>
 *  Desc: mpeg4 decoder util interface
 *
 */

#include "mp4_getbits.h"
#include "mp4_header.h"
#include "mp4_vars.h"

void decore_show_type(unsigned char *bmp, int stride, int prediction_type)
{
    int i, j;
    unsigned char b, g, r;
    if (prediction_type == B_VOP) {
        b = 0;
        g = 255;
        r = 0;
    } else if (prediction_type == I_VOP) {
        b = 255;
        g = 0;
        r = 0;
    } else if (prediction_type == S_VOP) {
        b = 128;
        g = 128;
        r = 128;
    } else {
        b = 0;
        g = 0;
        r = 0;
    }
    for (j = 0; j < 8; j++) {
        for (i = 0; i < 32; i++) {
            bmp[3 * i + 3 * j * stride] = b;
            bmp[3 * i + 3 * j * stride + 1] = g;
            bmp[3 * i + 3 * j * stride + 2] = r;
        }
    }
}

// return value: 1 restart decoding, 0 failure
int try_adjust_time_increment(mp4_stream_t *ld, struct mp4_state *mp4_state)
{
    int pos = bitpos(ld);

    pos -= 8 * ld->length;
    if (pos < 0)
        pos = -pos;
    if (pos > 128) {
        if (mp4_state->test_timeinc >= 0) {
            mp4_state->test_timeinc--;
            mp4_state->hdr.time_increment_resolution = 1 << mp4_state->test_timeinc;
            // initbits(ld, ld->startptr, ld->length);
            return 1;
        } else {
            if (mp4_state->test_timeinc == -1)
                mp4_state->hdr.time_increment_resolution = 15;
            return 0;
        }
    }

    return 0;
}

