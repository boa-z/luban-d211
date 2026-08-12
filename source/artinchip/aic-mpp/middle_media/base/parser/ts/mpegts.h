/*
 * Copyright (C) 2020-2026 ArtInChip Technology Co. Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Author: <che.jiang@artinchip.com>
 * Desc: mpegts demuxer api
 */


#ifndef _MPEGTS_H
#define _MPEGTS_H

#include "aic_parser.h"
#include "aic_tag.h"

struct mpegts_stream_ctx {
    void *priv_data;
    int index;
    int64_t duration;
    struct aic_codec_param codecpar;
    int program_num;
    int pmt_stream_idx;
    int stream_identifier;
    int audio_info_set;
};

#define MPEGTS_MAX_TRACK_NUM 8
struct aic_mpegts_parser {
    struct aic_parser base;
    struct aic_stream *stream;
    void *priv_data;
    int nb_streams;
    int audio_delay_parse;
    int nb_audio_track;
    int audio_track_init_cnt;
    int audio_pid[MPEGTS_MAX_TRACK_NUM];
    struct mpegts_stream_ctx *streams[MPEGTS_MAX_TRACK_NUM];
};

int mpegts_read_header(struct aic_mpegts_parser *s);
int mpegts_read_close(struct aic_mpegts_parser *s);
int mpegts_peek_packet(struct aic_mpegts_parser *c, struct aic_parser_packet *pkt);
int mpegts_seek_packet(struct aic_mpegts_parser *c, s64 pts);
int mpegts_read_packet(struct aic_mpegts_parser *s, struct aic_parser_packet *pkt);
int mpegts_control(struct aic_mpegts_parser *s, enum parse_command cmd, void *params);
#endif
