/*
 * Copyright (C) 2020-2026 ArtInChip Technology Co. Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 *  author: <jun.ma@artinchip.com>
 *  Desc: aic_stream
 */
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include "aic_stream.h"
#include "aic_file_stream.h"
#ifdef HTTP_STREAM
#include "aic_http_stream.h"
#endif
#ifdef RTP_STREAM
#include "aic_rtp_stream.h"
#endif

enum AIC_STREAM_TYPE_E {
    STREAM_TYPE_NONE = 0,
    STREAM_TYPE_FILE,
    STREAM_TYPE_HTTP,
    STREAM_TYPE_CUSTOM,
    STREAM_TYPE_RTP,
};

static s32 parse_stream_type(const char *uri)
{
    s32 type = STREAM_TYPE_NONE;

    if (0 == strncmp(uri, "http", 4)) {
        type = STREAM_TYPE_HTTP;
    } else if (0 == strncmp(uri, "rtp", 3)) {
        type = STREAM_TYPE_RTP;
    } else {
        type = STREAM_TYPE_FILE;
    }

    return type;
}

s32 aic_stream_open(char *uri, struct aic_stream **stream, int flags)
{
    s32 ret = 0, stream_type = 0;

    // now only file_stream,if more,it shoud probe which stream
    stream_type = parse_stream_type(uri);

    if (STREAM_TYPE_FILE == stream_type)
        ret = file_stream_open(uri, stream, flags);
#ifdef HTTP_STREAM
    else if (STREAM_TYPE_HTTP == stream_type)
        ret = http_stream_open(uri, stream, flags);
#endif
#ifdef RTP_STREAM
    else if (STREAM_TYPE_RTP == stream_type)
        ret = rtp_stream_open(uri, stream, flags);
#endif

    return ret;
}

int aic_stream_skip(struct aic_stream *s, int len)
{
    return aic_stream_seek(s, len, SEEK_CUR);
}


void aic_stream_w8(struct aic_stream *s, int val)
{
    aic_stream_write(s, &val, 1);
}

void aic_stream_wl32(struct aic_stream *s, unsigned int val)
{
    aic_stream_w8(s, (uint8_t)val);
    aic_stream_w8(s, (uint8_t)(val >> 8));
    aic_stream_w8(s, (uint8_t)(val >> 16));
    aic_stream_w8(s, (uint8_t)(val >> 24));
}

void aic_stream_wb32(struct aic_stream *s, unsigned int val)
{
    aic_stream_w8(s, (uint8_t)(val >> 24));
    aic_stream_w8(s, (uint8_t)(val >> 16));
    aic_stream_w8(s, (uint8_t)(val >> 8));
    aic_stream_w8(s, (uint8_t)val);
}

void aic_stream_wl64(struct aic_stream *s, uint64_t val)
{
    aic_stream_wl32(s, (uint32_t)(val & 0xffffffff));
    aic_stream_wl32(s, (uint32_t)(val >> 32));
}

void aic_stream_wb64(struct aic_stream *s, uint64_t val)
{
    aic_stream_wb32(s, (uint32_t)(val >> 32));
    aic_stream_wb32(s, (uint32_t)(val & 0xffffffff));
}

void aic_stream_wl16(struct aic_stream *s, unsigned int val)
{
    aic_stream_w8(s, (uint8_t)val);
    aic_stream_w8(s, (uint8_t)(val >> 8));
}

void aic_stream_wb16(struct aic_stream *s, unsigned int val)
{
    aic_stream_w8(s, (uint8_t)(val >> 8));
    aic_stream_w8(s, (uint8_t)val);
}

void aic_stream_wl24(struct aic_stream *s, unsigned int val)
{
    aic_stream_wl16(s, val & 0xffff);
    aic_stream_w8(s, (uint8_t)(val >> 16));
}

void aic_stream_wb24(struct aic_stream *s, unsigned int val)
{
    aic_stream_wb16(s, (uint32_t)(val >> 8));
    aic_stream_w8(s, (uint8_t)val);
}

int aic_stream_r8(struct aic_stream *s)
{
    unsigned char val = 0;
    aic_stream_read(s, &val, 1);
    return val;
}

unsigned int aic_stream_rl16(struct aic_stream *s)
{
    unsigned int val;
    val = aic_stream_r8(s);
    val |= aic_stream_r8(s) << 8;
    return val;
}

unsigned int aic_stream_rl24(struct aic_stream *s)
{
    unsigned int val;
    val = aic_stream_rl16(s);
    val |= aic_stream_r8(s) << 16;
    return val;
}

unsigned int aic_stream_rl32(struct aic_stream *s)
{
    unsigned int val;
    val = aic_stream_rl16(s);
    val |= aic_stream_rl16(s) << 16;
    return val;
}

uint64_t aic_stream_rl64(struct aic_stream *s)
{
    uint64_t val;
    val = (uint64_t)aic_stream_rl32(s);
    val |= (uint64_t)aic_stream_rl32(s) << 32;
    return val;
}

unsigned int aic_stream_rb16(struct aic_stream *s)
{
    unsigned int val;
    val = aic_stream_r8(s) << 8;
    val |= aic_stream_r8(s);
    return val;
}

unsigned int aic_stream_rb24(struct aic_stream *s)
{
    unsigned int val;
    val = aic_stream_rb16(s) << 8;
    val |= aic_stream_r8(s);
    return val;
}
unsigned int aic_stream_rb32(struct aic_stream *s)
{
    unsigned int val;
    val = aic_stream_rb16(s) << 16;
    val |= aic_stream_rb16(s);
    return val;
}

uint64_t aic_stream_rb64(struct aic_stream *s)
{
    uint64_t val;
    val = (uint64_t)aic_stream_rb32(s) << 32;
    val |= (uint64_t)aic_stream_rb32(s);
    return val;
}
