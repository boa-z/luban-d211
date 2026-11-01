/*
 * Copyright (C) 2020-2026 ArtInChip Technology Co. Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 *  author: <xiaodong.zhao@artinchip.com>
 *  Desc: audio encoder test demo
 */

#include <inttypes.h>
#include <string.h>
#include <malloc.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdlib.h>
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/types.h>
#include <pthread.h>

#include "aic_audio_encoder.h"
#include "aic_audio_decoder.h"
#include "mpp_log.h"
#include "wavreader.h"

struct audio_encoder_ctx {
    struct aic_audio_encoder *encoder;
    void *wav;
    FILE *fp_out;
    int format;
    int sample_rate;
    int channels;
    int bits_per_sample;
    int sample_num;
};

struct audio_encoder_type_desc {
    int type;
    char str[16];
};

struct audio_encoder_type_desc g_audio_type_desc[] = {
    {MPP_CODEC_AUDIO_ENCODER_AAC, "aac"},
    {MPP_CODEC_AUDIO_ENCODER_MP3, "mp3"},
};

static int get_audio_enc_type(const char* outfile)
{
    char* ptr = NULL;
    int i;

    if (strcmp(outfile, ".") == 0 || strcmp(outfile, "..") == 0)
        return MPP_CODEC_AUDIO_ENCODER_UNKOWN;

    ptr = strrchr(outfile, '.');
    if (ptr == NULL)
        return MPP_CODEC_AUDIO_ENCODER_UNKOWN;

    for (i = 0; i < sizeof(g_audio_type_desc) / sizeof(g_audio_type_desc[0]); i++) {
        if (strcmp(ptr + 1, g_audio_type_desc[i].str) == 0) {
            return g_audio_type_desc[i].type;
        }
    }

    loge("unsupport audio encoder type:%s.", ptr);
    return MPP_CODEC_AUDIO_ENCODER_UNKOWN;
}

static void* encode_thread(void *p)
{
    struct audio_encoder_ctx *encoder_ctx = (struct audio_encoder_ctx *)p;
    struct aic_audio_frame *frame;
    struct mpp_packet *pkt;
    int read, ret, end_flag;
    int pcm_size;

    while (1) {
        frame = aic_audio_encoder_get_pcm_frame(encoder_ctx->encoder);
        if (NULL == frame) {
            loge("aic_audio_encoder_get_pcm_frame fail");
            break;
        }
        pcm_size = encoder_ctx->sample_num * encoder_ctx->channels * encoder_ctx->bits_per_sample / 8;
        read = wav_read_data(encoder_ctx->wav, frame->data, pcm_size, &end_flag);
        if (0 == read) {
            printf("read wav file over!\n");
            break;
        } else if (read < 0) {
            loge("read wav file fail!");
            break;
        }

        if (end_flag)
            frame->flag |= FRAME_FLAG_EOS;

        ret = aic_audio_encoder_put_pcm_frame(encoder_ctx->encoder, frame);
        if (0 != ret) {
            loge("aic_audio_encoder_put_pcm_frame fail!");
            break;
        }

        ret = aic_audio_encoder_encode(encoder_ctx->encoder);
        if (0 != ret) {
            loge("aic_audio_encoder_encode fail!");
            break;
        }

        pkt = aic_audio_encoder_get_es_package(encoder_ctx->encoder);
        if (NULL == pkt) {
            loge("get es package fail!");
            continue;
        }

        ret = fwrite(pkt->data, 1, pkt->size, encoder_ctx->fp_out);
        if (ret != pkt->size) {
            loge("fwrite fail!");
            break;
        }

        ret = aic_audio_encoder_back_es_packet(encoder_ctx->encoder, pkt);
        if (0 != ret) {
            loge("aic_audio_encoder_back_es_packet fail!");
            break;
        }

        if (pkt->flag & PACKET_FLAG_EOS)
            break;
    }

    return (void *)0;
}


int main(int argc ,char *argv[])
{
    int enc_type = MPP_CODEC_AUDIO_ENCODER_UNKOWN;
    const char *infile = NULL, *outfile = NULL;
    struct aic_audio_encode_config cfg = {0};
    struct audio_encoder_ctx encoder_ctx = {0};
    pthread_t encode_thread_id = 0;
    int ret = -1;

    if (argc < 3) {
        printf("Usage [input.wav] [output file]\n");
        return -1;
    }

    infile = argv[1];
    outfile = argv[2];

    enc_type = get_audio_enc_type(outfile);
    if (enc_type == MPP_CODEC_AUDIO_ENCODER_UNKOWN)
        return -1;

    encoder_ctx.wav = wav_read_open(infile);
    if (!encoder_ctx.wav) {
        fprintf(stderr, "Unable to open wav file %s\n", infile);
        return 1;
    }
    if (!wav_get_header(encoder_ctx.wav, &encoder_ctx.format, &encoder_ctx.channels, &encoder_ctx.sample_rate, &encoder_ctx.bits_per_sample, NULL)) {
        fprintf(stderr, "Bad wav file %s\n", infile);
        return 1;
    }

    printf("\nwave file param:\n");
    printf("\tformat:%d\n", encoder_ctx.format);
    printf("\tchannels:%d\n", encoder_ctx.channels);
    printf("\tsample_rate:%d\n", encoder_ctx.sample_rate);
    printf("\tbits_per_sample:%d\n\n", encoder_ctx.bits_per_sample);

    encoder_ctx.encoder = aic_audio_encoder_create(enc_type);
    if (NULL == encoder_ctx.encoder) {
        loge("aic_audio_encoder_create encoder fail!");
        goto release_wav;
    }
    cfg.packet_buffer_size = 8 * 1024;
    cfg.packet_count = 2;
    cfg.frame_count = 2;
    cfg.channels = encoder_ctx.channels;
    cfg.sample_rate = encoder_ctx.sample_rate;
    cfg.bits_per_sample = encoder_ctx.bits_per_sample;

    ret = aic_audio_encoder_init(encoder_ctx.encoder, &cfg);
    if (0 != ret) {
        loge("aic_audio_encoder_init fail!");
        goto release_encoder;
    }

    ret =aic_audio_encoder_control(encoder_ctx.encoder, MPP_ENCODER_GET_SAMPLE_NUM, &encoder_ctx.sample_num);
    if ((0 != ret) || (0 == encoder_ctx.sample_num)) {
        loge("get sample num fail!");
        goto release_encoder;

    }

    encoder_ctx.fp_out = fopen(outfile, "w+");
    if (NULL == encoder_ctx.fp_out) {
        loge("open file:%s fail!", outfile);
        goto release_encoder;
    }

    ret = pthread_create(&encode_thread_id, NULL, encode_thread, &encoder_ctx);
    if (0 < ret) {
        loge("create encode_thread fail!\n");
        goto release_fp_out;
    }

    pthread_join(encode_thread_id, NULL);

release_fp_out:
    fclose(encoder_ctx.fp_out);
    encoder_ctx.fp_out = NULL;
release_encoder:
    aic_audio_encoder_destroy(encoder_ctx.encoder);
release_wav:
    wav_read_close(encoder_ctx.wav);

    return ret;
}
