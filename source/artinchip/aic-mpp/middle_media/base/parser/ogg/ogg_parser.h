/*
 * Copyright (C) 2020-2025 ArtInChip Technology Co. Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 *  author: <xiaodong.zhao@artinchip.com>
 *  Desc: ogg
 */

#ifndef __OGG_PARSER_H__
#define __OGG_PARSER_H__

#include <unistd.h>

#include "ogg/ogg.h"
#include "aic_parser.h"

enum ogg_parse_state {
    OGG_STATE_READ,
    OGG_STATE_PARSE
};

#pragma pack(push, 1)
typedef struct {
    char capture_pattern[4];  // "OggS"
    uint8_t version;
    uint8_t header_type;
    uint64_t granule_position;
    uint32_t bitstream_serial_number;
    uint32_t page_sequence_number;
    uint32_t checksum;
    uint8_t page_segments;
} ogg_page_header_t;

typedef struct {
    uint8_t packet_type;
    char vorbis_str[6];  // "vorbis"
    uint32_t vorbis_version;
    uint8_t channels;
    uint32_t sample_rate;
    int32_t bitrate_maximum;
    int32_t bitrate_nominal;
    int32_t bitrate_minimum;
    uint8_t blocksize_0:4;
    uint8_t blocksize_1:4;
    uint8_t framing_flag;
} vorbis_header_t;
#pragma pack(pop)

struct opus_head
{
    int version;
    int channels;
    unsigned int pre_skip;
    unsigned int sample_rate;
    int output_gain;
    int mapping_family;
    int stream_count;
    int coupled_count;
};

struct ogg_dec_context {
    ogg_sync_state   oy;    /* sync and verify incoming physical bitstream */
    ogg_stream_state os;    /* take physical pages, weld into a logical stream of packets */
    ogg_page         og;    /* one Ogg bitstream page. Vorbis packets are inside */
    ogg_packet       op;    /* one raw packet of data for decode */
    enum ogg_parse_state state;
    int64_t filesize;
};

struct aic_ogg_parser {
    struct aic_parser base;
    struct aic_stream* stream;
    vorbis_header_t vorbis_header;
    struct opus_head opus_header;
    struct ogg_dec_context ctx;
    unsigned first_packet_pos;
    uint64_t duration;  // us
    uint32_t frame_id;
    enum aic_audio_codec_type code_type;
};

int ogg_parse_header(struct aic_ogg_parser *s);
int opus_parse_header(struct aic_ogg_parser *s);
int ogg_close(struct aic_ogg_parser *s);
int ogg_peek_packet(struct aic_ogg_parser *s, struct aic_parser_packet *pkt);
int ogg_seek_packet(struct aic_ogg_parser *s, s64 seek_time);
int ogg_read_packet(struct aic_ogg_parser *s, struct aic_parser_packet *pkt);

#endif
