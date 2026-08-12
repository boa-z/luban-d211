/*
 * Copyright (C) 2020-2026 ArtInChip Technology Co. Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * author: <che.jiang@artinchip.com>
 * Desc: m3u - M3U8 parser for HLS
 */

#ifndef __M3U_H__
#define __M3U_H__

#include <stdint.h>

#define M3U_MAX_URL_LEN  1024
#define M3U_MAX_SEGMENTS 2048
#define M3U_MAX_VARIANTS 32

struct m3u_segment {
    char url[M3U_MAX_URL_LEN];
    int64_t duration;
    int sequence;
    int discontinuity; // 1 = preceded by #EXT-X-DISCONTINUITY, reset decoder
    int has_byterange; // 1 = has #EXT-X-BYTERANGE
    int64_t byterange_length;
    int64_t byterange_offset;   // -1 if not specified (contiguous)
    char program_date_time[64]; // #EXT-X-PROGRAM-DATE-TIME (empty if none)
};

struct m3u_variant {
    char url[M3U_MAX_URL_LEN];
    int bandwidth;
    int width;
    int height;
};

struct m3u_playlist {
    char base_url[M3U_MAX_URL_LEN];
    int version;
    int target_duration;
    int64_t media_sequence;
    int segment_count;
    int64_t total_duration;

    // segments is actual playable video/audio chunks
    struct m3u_segment segments[M3U_MAX_SEGMENTS];

    int variant_count;
    // variants is different quality/bitrate options
    struct m3u_variant variants[M3U_MAX_VARIANTS];

    int is_live;
    int is_master;
    int has_endlist;                // 1 = #EXT-X-ENDLIST found
    int allow_cache;                // 0 = #EXT-X-ALLOW-CACHE:NO, 1 = YES or not specified
    int playlist_type;              // 0=undefined, 1=EVENT, 2=VOD
    int64_t discontinuity_sequence; // #EXT-X-DISCONTINUITY-SEQUENCE

    // #EXT-X-KEY encryption support
    int has_encryption_key;
    char key_uri[M3U_MAX_URL_LEN];
    char key_method[16];
    char key_iv[33];

    // #EXT-X-MAP fMP4 init segment
    int has_init_segment;
    char init_segment_url[M3U_MAX_URL_LEN];
    int64_t init_segment_byterange_offset;
    int64_t init_segment_byterange_length;
};

typedef int (*m3u_read_line_cb)(void *userdata, char *line, int max_len);

int m3u_parse_stream(m3u_read_line_cb read_cb, void *userdata, const char *base_url,
                     struct m3u_playlist *pl);
int m3u_parse(const char *content, const char *base_url, struct m3u_playlist *pl);
void m3u_build_url(const char *base_url, const char *relative_url, char *full_url, int size);
int m3u_select_variant(struct m3u_playlist *pl, int bandwidth);

#endif
