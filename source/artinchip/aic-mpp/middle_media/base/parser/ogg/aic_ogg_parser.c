/*
 * Copyright (C) 2020-2025 ArtInChip Technology Co. Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 *  author: <xiaodong.zhao@artinchip.com>
 *  Desc: aic_ogg_parser
 */

#include <malloc.h>
#include <string.h>
#include <stddef.h>
#include <fcntl.h>
#include "aic_mov_parser.h"
#include "mpp_log.h"
#include "mpp_mem.h"
#include "mpp_dec_type.h"
#include "aic_stream.h"
#include "ogg_parser.h"
#include "aic_ogg_parser.h"

s32 ogg_peek(struct aic_parser * parser, struct aic_parser_packet *pkt)
{
    struct aic_ogg_parser *ogg_parser = (struct aic_ogg_parser *)parser;
    return ogg_peek_packet(ogg_parser,pkt);
}

s32 ogg_read(struct aic_parser * parser, struct aic_parser_packet *pkt)
{
    struct aic_ogg_parser *ogg_parser = (struct aic_ogg_parser *)parser;
    return ogg_read_packet(ogg_parser,pkt);
}

s32 ogg_get_media_info(struct aic_parser *parser, struct aic_parser_av_media_info *media)
{
    struct aic_ogg_parser *ogg_parser = (struct aic_ogg_parser *)parser;
    media->has_video = 0;
    media->has_audio = 1;
    media->file_size = ogg_parser->ctx.filesize;
    media->duration = ogg_parser->duration;
    media->audio_stream[0].codec_type = ogg_parser->code_type;
    media->audio_stream[0].bits_per_sample = 16;
    if (MPP_CODEC_AUDIO_DECODER_VORBIS == ogg_parser->code_type) {
        media->audio_stream[0].nb_channel = ogg_parser->vorbis_header.channels;
        media->audio_stream[0].sample_rate = ogg_parser->vorbis_header.sample_rate;
    } else if (MPP_CODEC_AUDIO_DECODER_OPUS == ogg_parser->code_type) {
        media->audio_stream[0].nb_channel = ogg_parser->opus_header.channels;
        media->audio_stream[0].sample_rate = ogg_parser->opus_header.sample_rate;
    }

    media->audio_track_count = 1;

    return 0;
}

s32 ogg_seek(struct aic_parser *parser, s64 time)
{
    struct aic_ogg_parser *ogg_parser = (struct aic_ogg_parser *)parser;
    return ogg_seek_packet(ogg_parser, time);
}

s32 ogg_init(struct aic_parser *parser)
{
    struct aic_ogg_parser *ogg_parser = (struct aic_ogg_parser *)parser;

    if (ogg_parse_header(ogg_parser)) {
        loge("ogg init failed");
        return -1;
    }

    return 0;
}

s32 opus_init(struct aic_parser *parser)
{
    struct aic_ogg_parser *ogg_parser = (struct aic_ogg_parser *)parser;

    if (opus_parse_header(ogg_parser)) {
        loge("ogg init failed");
        return -1;
    }

    return 0;
}

s32 ogg_destroy(struct aic_parser *parser)
{
    struct aic_ogg_parser *ogg_parser = (struct aic_ogg_parser *)parser;

    if (ogg_parser == NULL) {
        return -1;
    }

    ogg_close(ogg_parser);
    aic_stream_close(ogg_parser->stream);
    mpp_free(ogg_parser);

    return 0;
}

s32 aic_ogg_parser_create(unsigned char *uri, struct aic_parser **parser)
{
    s32 ret = 0;
    struct aic_ogg_parser *ogg_parser = NULL;

    ogg_parser = (struct aic_ogg_parser *)mpp_alloc(sizeof(struct aic_ogg_parser));
    if (ogg_parser == NULL) {
        loge("mpp_alloc aic_parser failed!!!!!\n");
        ret = -1;
        goto exit;
    }
    memset(ogg_parser, 0, sizeof(struct aic_ogg_parser));
    ogg_parser->ctx.state = OGG_STATE_READ;

    if (aic_stream_open((char *)uri, &ogg_parser->stream, O_RDONLY) < 0) {
        loge("stream open fail");
        ret = -1;
        goto exit;
    }

    ogg_parser->base.get_media_info = ogg_get_media_info;
    ogg_parser->base.peek = ogg_peek;
    ogg_parser->base.read = ogg_read;
    ogg_parser->base.control = NULL;
    ogg_parser->base.destroy = ogg_destroy;
    ogg_parser->base.seek = ogg_seek;
    ogg_parser->base.init = ogg_init;
    ogg_parser->code_type = MPP_CODEC_AUDIO_DECODER_VORBIS;
    *parser = &ogg_parser->base;

    return ret;

exit:
    if (ogg_parser->stream) {
        aic_stream_close(ogg_parser->stream);
    }
    if (ogg_parser) {
        mpp_free(ogg_parser);
    }

    return ret;
}


s32 aic_opus_parser_create(unsigned char *uri, struct aic_parser **parser)
{
    s32 ret = 0;
    struct aic_ogg_parser *ogg_parser = NULL;

    ogg_parser = (struct aic_ogg_parser *)mpp_alloc(sizeof(struct aic_ogg_parser));
    if (ogg_parser == NULL) {
        loge("mpp_alloc aic_parser failed!!!!!\n");
        ret = -1;
        goto exit;
    }
    memset(ogg_parser, 0, sizeof(struct aic_ogg_parser));
    ogg_parser->ctx.state = OGG_STATE_READ;

    if (aic_stream_open((char *)uri, &ogg_parser->stream, O_RDONLY) < 0) {
        loge("stream open fail");
        ret = -1;
        goto exit;
    }

    ogg_parser->base.get_media_info = ogg_get_media_info;
    ogg_parser->base.peek = ogg_peek;
    ogg_parser->base.read = ogg_read;
    ogg_parser->base.control = NULL;
    ogg_parser->base.destroy = ogg_destroy;
    ogg_parser->base.seek = ogg_seek;
    ogg_parser->base.init = opus_init;
    ogg_parser->code_type = MPP_CODEC_AUDIO_DECODER_OPUS;
    *parser = &ogg_parser->base;

    return ret;

exit:
    if (ogg_parser->stream) {
        aic_stream_close(ogg_parser->stream);
    }
    if (ogg_parser) {
        mpp_free(ogg_parser);
    }

    return ret;
}
