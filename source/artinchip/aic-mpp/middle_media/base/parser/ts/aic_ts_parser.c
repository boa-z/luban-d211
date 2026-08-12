/*
 * Copyright (C) 2020-2026 ArtInChip Technology Co. Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Author: <che.jiang@artinchip.com>
 * Desc: aic ts parser
 */

#define LOG_TAG "ts_parse"

#include <malloc.h>
#include <string.h>
#include <stddef.h>
#include <fcntl.h>
#include <inttypes.h>
#include "aic_ts_parser.h"
#include "mpp_log.h"
#include "mpp_mem.h"
#include "mpp_dec_type.h"
#include "aic_stream.h"
#include "aic_parser.h"
#include "mpegts.h"

s32 ts_peek(struct aic_parser *parser, struct aic_parser_packet *pkt)
{
    struct aic_mpegts_parser *ts_parse_parser = (struct aic_mpegts_parser *)parser;

    return mpegts_peek_packet(ts_parse_parser, pkt);
}

s32 ts_read(struct aic_parser *parser, struct aic_parser_packet *pkt)
{
    struct aic_mpegts_parser *ts_parse_parser = (struct aic_mpegts_parser *)parser;

    return mpegts_read_packet(ts_parse_parser, pkt);
}

s32 ts_control(struct aic_parser *parser, enum parse_command cmd, void *param)
{
    struct aic_mpegts_parser *ts_parse_parser = (struct aic_mpegts_parser *)parser;

    return mpegts_control(ts_parse_parser, cmd, param);
}

s32 ts_get_media_info(struct aic_parser *parser,
                       struct aic_parser_av_media_info *media)
{
    int i;
    int64_t duration = 0;
    struct aic_av_audio_stream *audio_stream;
    struct aic_mpegts_parser *c = (struct aic_mpegts_parser *)parser;

    logi("================ media info =======================");
    media->audio_track_count = 0;
    for (i = 0; i < c->nb_streams; i++) {
        struct mpegts_stream_ctx *st = c->streams[i];
        if (st->codecpar.codec_type == MPP_MEDIA_TYPE_VIDEO) {
            media->has_video = 1;
            if (st->codecpar.codec_id == CODEC_ID_H264)
                media->video_stream.codec_type = MPP_CODEC_VIDEO_DECODER_H264;
            else if (st->codecpar.codec_id == CODEC_ID_MJPEG)
                media->video_stream.codec_type = MPP_CODEC_VIDEO_DECODER_MJPEG;
            else if (st->codecpar.codec_id == CODEC_ID_MPEG12)
                media->video_stream.codec_type = MPP_CODEC_VIDEO_DECODER_MPEG12;
            else if (st->codecpar.codec_id == CODEC_ID_MPEG4)
                media->video_stream.codec_type = MPP_CODEC_VIDEO_DECODER_MPEG4;
            else
                media->video_stream.codec_type = -1;

            media->video_stream.width = st->codecpar.width;
            media->video_stream.height = st->codecpar.height;
            media->video_stream.max_ref_frames = st->codecpar.max_ref_frames;
            media->video_stream.extra_data_size = 0;
            media->video_stream.extra_data = NULL;
            if (st->codecpar.extradata_size > 0) {
                media->video_stream.extra_data_size =
                    st->codecpar.extradata_size;
                media->video_stream.extra_data = st->codecpar.extradata;
            }
            logi("video codec_type: %d codec_id %d",
                 media->video_stream.codec_type, st->codecpar.codec_id);
            logi("video width: %d height %d",
                st->codecpar.width, st->codecpar.height);
            logi("video extra_data_size: %d\n", st->codecpar.extradata_size);
        } else if (st->codecpar.codec_type == MPP_MEDIA_TYPE_AUDIO) {
            media->has_audio = 1;
            audio_stream = &media->audio_stream[media->audio_track_count];
            if (st->codecpar.codec_id == CODEC_ID_MP3)
                audio_stream->codec_type = MPP_CODEC_AUDIO_DECODER_MP3;
            else if (st->codecpar.codec_id == CODEC_ID_AAC)
                audio_stream->codec_type = MPP_CODEC_AUDIO_DECODER_AAC;
            else
                audio_stream->codec_type = MPP_CODEC_AUDIO_DECODER_UNKOWN;

            audio_stream->bits_per_sample = st->codecpar.bits_per_coded_sample;
            audio_stream->nb_channel = st->codecpar.channels;
            audio_stream->sample_rate = st->codecpar.sample_rate;
            audio_stream->track_id = st->codecpar.audio_track_id;
            audio_stream->extra_data_size = 0;
            audio_stream->extra_data = NULL;
            if (st->codecpar.extradata_size > 0) {
                audio_stream->extra_data_size =
                    st->codecpar.extradata_size;
                audio_stream->extra_data = st->codecpar.extradata;
            }
            audio_stream->track_id = st->codecpar.audio_track_id;
            media->audio_track_count++;

            logi("track_id:%d", audio_stream->track_id);
            logi("audio codec_type: %d codec_id %d",
                 audio_stream->codec_type, st->codecpar.codec_id);
            logi("audio bits_per_sample: %d",
                 st->codecpar.bits_per_coded_sample);
            logi("audio channels: %d", st->codecpar.channels);
            logi("audio sample_rate: %d", st->codecpar.sample_rate);
            logi("audio extra_data_size: %d\n", st->codecpar.extradata_size);
        } else {
            logw("unknown stream(%d) type: %d", i, st->codecpar.codec_type);
        }

        if (st->duration > duration)
            duration = st->duration;
    }

    media->file_size = aic_stream_size(c->stream);
    media->seek_able = 1;
    media->duration = duration;

    return 0;
}

s32 ts_seek(struct aic_parser *parser, s64 time)
{
    struct aic_mpegts_parser *ts_parse_parser = (struct aic_mpegts_parser *)parser;

    return mpegts_seek_packet(ts_parse_parser, time);
}

s32 ts_parse_init(struct aic_parser *parser)
{
    struct aic_mpegts_parser *ts_parse_parser = (struct aic_mpegts_parser *)parser;

    if (mpegts_read_header(ts_parse_parser) < 0) {
        loge("ts_parse open failed");
        return -1;
    }

    return 0;
}

s32 ts_parse_destroy(struct aic_parser *parser)
{
    struct aic_mpegts_parser *ts_parser = (struct aic_mpegts_parser *)parser;
    if (parser == NULL) {
        return -1;
    }

    mpegts_read_close(ts_parser);
    aic_stream_close(ts_parser->stream);
    mpp_free(ts_parser);
    return 0;
}

s32 aic_ts_parser_create(unsigned char *uri, struct aic_parser **parser)
{
    s32 ret = 0;
    struct aic_mpegts_parser *ts_parser = NULL;

    ts_parser =
        (struct aic_mpegts_parser *)mpp_alloc(sizeof(struct aic_mpegts_parser));
    if (ts_parser == NULL) {
        loge("mpp_alloc aic_parser failed!!!!!\n");
        ret = -1;
        goto exit;
    }
    memset(ts_parser, 0, sizeof(struct aic_mpegts_parser));

    if (aic_stream_open((char *)uri, &ts_parser->stream, O_RDONLY) < 0) {
        loge("stream open fail");
        ret = -1;
        goto exit;
    }
    if (0 == strncmp((char *)uri, "rtp", 3))
        ts_parser->audio_delay_parse = 1;

    ts_parser->base.get_media_info = ts_get_media_info;
    ts_parser->base.peek = ts_peek;
    ts_parser->base.read = ts_read;
    ts_parser->base.control = ts_control;
    ts_parser->base.destroy = ts_parse_destroy;
    ts_parser->base.seek = ts_seek;
    ts_parser->base.init = ts_parse_init;

    *parser = &ts_parser->base;

    return ret;

exit:
    if (ts_parser) {
        if (ts_parser->stream)
            aic_stream_close(ts_parser->stream);
        mpp_free(ts_parser);
    }
    return ret;
}
