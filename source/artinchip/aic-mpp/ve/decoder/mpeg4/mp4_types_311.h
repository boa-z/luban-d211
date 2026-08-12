/*
 * Copyright (C) 2020-2025 ArtInChip Technology Co. Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 *  author: <xiaodong.zhao@artinchip.com>
 *  Desc: mpeg4 types 311 interface
 *
 */

#ifndef TYPES_311_H
#define TYPES_311_H
#include "mp4_decore.h"
#include "mp4_vars.h"
#include "mp4_vld.h"

#include "mp4_getbits.h"
#define VLD_TABLE_DIM 6
struct item;
struct mv_item;

#ifdef WIN32
#pragma warning(disable : 4514) // unreferenced inline function has been removed
#pragma warning(disable : 4127) // conditional expression is constant
#endif

#ifndef WIN32
#define __forceinline inline
#endif

typedef struct item item_t;
typedef struct mv_item mv_item_t;

struct ShortVector {
    short x;
    short y;
};

struct item {
    event_t value;
    char length;
};

struct mv_item {
    struct ShortVector value;
    short length;
};

static inline event_t get_event_311(mp4_stream_t *ld, const item_t *items, const int *indices)
{
    short dimension = VLD_TABLE_DIM;
    while (1) {
        unsigned int val = indices[showbits(ld, dimension)];
        if (!(val & 0xFFFF0000)) {
            flushbits(ld, items[val].length);
            return items[val].value;
        }
        flushbits(ld, dimension);
        dimension = val >> 16;
        indices += (val & 0xFFFF);
    }
}

static inline short get_short_311(mp4_stream_t *ld, const int *indices)
{
    short dimension = VLD_TABLE_DIM;
    while (1) {
        unsigned int val = indices[showbits(ld, dimension)];
        if (!(val & 0xFFFF0000)) {
            flushbits(ld, val >> 10);
            return val & 0x3FF;
        }
        flushbits(ld, dimension);
        dimension = val >> 16;
        indices += (val & 0xFFFF);
    }
}

static inline const struct ShortVector *get_motionvector_311(mp4_stream_t *ld, const mv_item_t *items,
                                                             const int *indices)
{
    short dimension = VLD_TABLE_DIM;
    while (1) {
        unsigned int val = indices[showbits(ld, dimension)];
        if (!(val & 0xFFFF0000)) {
            flushbits(ld, items[val].length);
            return &items[val].value;
        }
        flushbits(ld, dimension);
        dimension = val >> 16;
        indices += (val & 0xFFFF);
    }
}

#endif
