/*
 * Copyright (C) 2020-2026 ArtInChip Technology Co. Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 *  author: <jianye.liang@artinchip.com>
 *  Desc: MPEG-PS demuxer internal header
 */

#ifndef __MPG_H__
#define __MPG_H__

#include "aic_parser.h"
#include "aic_tag.h"

#define PACK_START_CODE          0x000001BA
#define SYSTEM_HEADER_START_CODE 0x000001BB
#define PADDING_STREAM           0x000001BE
#define PRIVATE_STREAM_2         0x000001BF
#define PRIVATE_STREAM_1         0x000001BD
#define AUDIO_ID                 0xC0
#define VIDEO_ID                 0xE0
#define AC3_ID                   0x80
#define DTS_ID                   0x88
#define LPCM_ID                  0xA0
#define SUB_ID                   0x20

#define MPG_PACK_SIZE            2048
#define MPG_MAX_STREAMS          4
#define MPG_MAX_PES_SIZE         (256 * 1024)
#define MPG_PROBE_SIZE           2048

struct mpg_stream_ctx {
	int index;
	int64_t duration;
	struct aic_codec_param codecpar;
	unsigned char *pes_buf;
	int pes_buf_size;
	int pes_data_len;
	int64_t cur_pts;
	int64_t cur_dts;
};

struct mpg_context {
	int64_t file_size;
	int nb_streams;
	int video_stream_index;
	int audio_stream_index;
	int is_mpeg2;
	int last_peeked_stream;
	int last_read_stream;
	struct mpg_stream_ctx *streams[MPG_MAX_STREAMS];
};

struct aic_mpg_parser {
	struct aic_parser base;
	struct aic_stream *stream;
	struct mpg_context ctx;
};

int mpg_read_header(struct aic_mpg_parser *s);
void mpg_read_close(struct aic_mpg_parser *s);
int mpg_peek_packet(struct aic_mpg_parser *s, struct aic_parser_packet *pkt);
int mpg_read_packet(struct aic_mpg_parser *s, struct aic_parser_packet *pkt);

#endif /* __MPG_H__ */
