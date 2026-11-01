/*
 * Copyright (C) 2020-2025 ArtInChip Technology Co. Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 *  author: <xiaodong.zhao@artinchip.com>
 *  Desc: mpeg4 getbits interface
 *
 */

#ifndef _MP4_GETBITS_H_
#define _MP4_GETBITS_H_

#include "mp4_vars.h"

void next_start_code(mp4_stream_t *_ld, struct mp4_state *_mp4_state);

#define _SWAP(a, b) (b = ((a[0] << 24) | (a[1] << 16) | (a[2] << 8) | a[3]))

static inline void initbits(mp4_stream_t *_ld, const unsigned char *stream, int length)
{
    mp4_stream_t *ld = _ld;

    ld->incnt = 0;
    ld->bitcnt = 0;

    ld->startptr = ld->rdptr = stream;
    ld->length = length;

    _SWAP(ld->rdptr, ld->bit_a);
    ld->rdptr += 4;
    _SWAP(ld->rdptr, ld->bit_b);
    ld->rdptr += 4;
}

/* advance by n bits */
static inline void flushbits(mp4_stream_t *ld, int n)
{

    if ((ld->rdptr - ld->startptr) > ld->length + 32)
        return;

    ld->bitcnt += n;
    if (ld->bitcnt >= 32) {
        ld->bit_a = ld->bit_b;

        _SWAP(ld->rdptr, ld->bit_b);
        ld->rdptr += 4;
        ld->bitcnt -= 32;
    }
}

/* read n bits */
static inline unsigned int showbits(mp4_stream_t *ld, int n)
{
    int nbit = (n + ld->bitcnt) - 32;

    if (nbit > 0) {
        // The bits are on both ints
        return (((ld->bit_a & (0xFFFFFFFFU >> (ld->bitcnt))) << nbit) | (ld->bit_b >> (32 - nbit)));

    } else {
        int rbit = 32 - ld->bitcnt;
        return (ld->bit_a & (0xFFFFFFFFU >> (ld->bitcnt))) >> (rbit - n);
    }
}

static inline unsigned int showbits1(mp4_stream_t *ld)
{
    if (ld->bit_a & (0x80000000U >> ld->bitcnt))
        return 1;
    else
        return 0;
}

// returns absolute bis position inside the stream
static inline unsigned int bitpos(mp4_stream_t *ld)
{
    return 8 * (ld->rdptr - ld->startptr) + ld->bitcnt - 64;
}

static inline unsigned int getbits(mp4_stream_t *ld, int n)
{
    unsigned int l = showbits(ld, n);
    flushbits(ld, n);
    return l;
}

static inline unsigned int getbits1(mp4_stream_t *ld)
{
    unsigned int l = showbits1(ld);
    flushbits(ld, 1);
    return l;
}

/* Get remaining bits in the stream */
static inline unsigned int get_bits_left(mp4_stream_t *ld)
{
    return (ld->length * 8) - bitpos(ld);
}

#endif /* _MP4_GETBITS_H_ */
