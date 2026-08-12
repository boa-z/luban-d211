/*
 * Copyright (C) 2020-2026 ArtInChip Technology Co. Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 *  author: <jianye.liang@artinchip.com>
 *  Desc: aic mpg parser
 */
#define LOG_TAG "mpg_parse"

#include <malloc.h>
#include <string.h>
#include <fcntl.h>
#include <inttypes.h>
#include "aic_mpg_parser.h"
#include "mpp_log.h"
#include "mpp_mem.h"
#include "mpp_dec_type.h"
#include "aic_stream.h"
#include "aic_parser.h"
#include "mpg.h"

static s32 mpg_peek(struct aic_parser *parser, struct aic_parser_packet *pkt)
{
	struct aic_mpg_parser *p = (struct aic_mpg_parser *)parser;
	return mpg_peek_packet(p, pkt);
}

static s32 mpg_read(struct aic_parser *parser, struct aic_parser_packet *pkt)
{
	struct aic_mpg_parser *p = (struct aic_mpg_parser *)parser;
	return mpg_read_packet(p, pkt);
}

static s32 mpg_get_media_info(struct aic_parser *parser,
			      struct aic_parser_av_media_info *media)
{
	int i;
	struct aic_mpg_parser *p = (struct aic_mpg_parser *)parser;

	logi("================ media info =======================");
	media->audio_track_count = 0;
	media->duration = 0;
	for (i = 0; i < p->ctx.nb_streams; i++) {
		struct mpg_stream_ctx *st = p->ctx.streams[i];
		if (st->codecpar.codec_type == MPP_MEDIA_TYPE_VIDEO) {
			media->has_video = 1;
			if (st->codecpar.codec_id == CODEC_ID_H264)
				media->video_stream.codec_type = MPP_CODEC_VIDEO_DECODER_H264;
			else if (st->codecpar.codec_id == CODEC_ID_MPEG12)
				media->video_stream.codec_type = MPP_CODEC_VIDEO_DECODER_MPEG12;
			else
				media->video_stream.codec_type = -1;

			media->video_stream.width  = st->codecpar.width;
			media->video_stream.height = st->codecpar.height;
			if (st->codecpar.extradata_size > 0) {
				media->video_stream.extra_data_size = st->codecpar.extradata_size;
				media->video_stream.extra_data = st->codecpar.extradata;
			}
			logi("video codec_type: %d codec_id %d",
			     media->video_stream.codec_type, st->codecpar.codec_id);
			logi("video width: %d, height: %d", st->codecpar.width, st->codecpar.height);
		} else if (st->codecpar.codec_type == MPP_MEDIA_TYPE_AUDIO) {
			struct aic_av_audio_stream *audio =
				&media->audio_stream[media->audio_track_count];
			media->has_audio = 1;
			if (st->codecpar.codec_id == CODEC_ID_MP3)
				audio->codec_type = MPP_CODEC_AUDIO_DECODER_MP3;
			else if (st->codecpar.codec_id == CODEC_ID_AAC)
				audio->codec_type = MPP_CODEC_AUDIO_DECODER_AAC;
			else
				audio->codec_type = MPP_CODEC_AUDIO_DECODER_UNKOWN;

			audio->bits_per_sample = st->codecpar.bits_per_coded_sample;
			audio->nb_channel = st->codecpar.channels;
			audio->sample_rate = st->codecpar.sample_rate;
			if (st->codecpar.extradata_size > 0) {
				audio->extra_data_size = st->codecpar.extradata_size;
				audio->extra_data = st->codecpar.extradata;
			}
			audio->track_id = st->codecpar.audio_track_id;
			media->audio_track_count++;
			logi("audio sample_rate: %d, channels: %d",
			     st->codecpar.sample_rate, st->codecpar.channels);
		}
	}

	media->file_size = aic_stream_size(p->stream);
	media->seek_able = 0;

	return 0;
}

static s32 mpg_parse_init(struct aic_parser *parser)
{
	struct aic_mpg_parser *p = (struct aic_mpg_parser *)parser;
	if (mpg_read_header(p) < 0) {
		loge("mpg_parse open failed");
		return -1;
	}
	return 0;
}

static s32 mpg_parse_control(struct aic_parser *parser, enum parse_command cmd, void *params)
{
	return 0;
}

static s32 mpg_parse_seek(struct aic_parser *parser, s64 time)
{
	return 0;
}

static s32 mpg_parse_destroy(struct aic_parser *parser)
{
	struct aic_mpg_parser *p = (struct aic_mpg_parser *)parser;
	if (!parser)
		return -1;

	mpg_read_close(p);
	aic_stream_close(p->stream);
	mpp_free(p);
	return 0;
}

s32 aic_mpg_parser_create(unsigned char *uri, struct aic_parser **parser)
{
	s32 ret = 0;
	struct aic_mpg_parser *p = NULL;

	p = (struct aic_mpg_parser *)mpp_alloc(sizeof(struct aic_mpg_parser));
	if (!p) {
		loge("mpp_alloc aic_mpg_parser failed");
		ret = -1;
		goto exit;
	}
	memset(p, 0, sizeof(struct aic_mpg_parser));

	if (aic_stream_open((char *)uri, &p->stream, O_RDONLY) < 0) {
		loge("stream open fail");
		ret = -1;
		goto exit;
	}

	p->base.get_media_info = mpg_get_media_info;
	p->base.peek           = mpg_peek;
	p->base.read           = mpg_read;
	p->base.control        = mpg_parse_control;
	p->base.destroy        = mpg_parse_destroy;
	p->base.seek           = mpg_parse_seek;
	p->base.init           = mpg_parse_init;

	*parser = &p->base;
	return ret;

exit:
	if (p) {
		if (p->stream)
			aic_stream_close(p->stream);
		mpp_free(p);
	}
	return ret;
}
