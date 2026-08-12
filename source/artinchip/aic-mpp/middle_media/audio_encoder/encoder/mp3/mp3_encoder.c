/*
 * Copyright (C) 2020-2026 ArtInChip Technology Co. Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Author: <che.jiang@artinchip.com>
 * Desc: mp3_encoder
 */
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <inttypes.h>

#include "mpp_dec_type.h"
#include "mpp_mem.h"
#include "mpp_list.h"
#include "mpp_log.h"
#include "lame/lame.h"

#include "audio_encoder.h"

//576 or 1152 is mp3 integer mult frame size, with higher efficiency
#define PCM_SAMPLES_PER_CHANNEL 1152

struct mp3_audio_encoder {
    struct aic_audio_encoder encoder;
    int bitrate;
    int channels;
    int sample_rate;
    int bits_per_sample;
    int packet_buffer_size;
    unsigned char* packet_buffer;
    lame_global_flags *lame;
};


int __mp3_encode_init(struct aic_audio_encoder *encoder, struct aic_audio_encode_config *config)
{
    struct mp3_audio_encoder *mp3_encoder = (struct mp3_audio_encoder *)encoder;
    struct audio_frame_manager_cfg frm_cfg = {0};
    struct aic_audio_decode_config dec_cfg = {0};
    int max_frame_size;

    lame_global_flags *lame = NULL;
    int ret = 0;

    if (NULL == mp3_encoder) {
        loge("mp3_encoder is null.");
        return -1;
    }

    lame = lame_init();
    if (NULL == lame) {
        loge("lame_init failed.");
        return -1;
    }

    mp3_encoder->channels = config->channels;
    mp3_encoder->sample_rate = config->sample_rate;
    mp3_encoder->bits_per_sample = config->bits_per_sample;

    lame_set_in_samplerate(lame, config->sample_rate);
    lame_set_num_channels(lame, config->channels);
    lame_set_out_samplerate(lame, config->sample_rate);

    lame_set_VBR(lame, vbr_off);
    lame_set_brate(lame, 128);
    if (config->sample_rate >= 48000)
        lame_set_brate(lame, 192);
    else if (config->sample_rate <= 22050)
        lame_set_brate(lame, 96);
    lame_set_quality(lame, 5);

    if (config->channels == 1)
        lame_set_mode(lame, MONO);

    //should be disable some feature to avoid mad decode lost sync issue
    lame_set_bWriteVbrTag(lame, 0);
    lame_set_disable_reservoir(lame, 1);
    lame_set_write_id3tag_automatic(lame, 0);

    ret = lame_init_params(lame);
    if (ret < 0) {
        loge("lame_init_params failed: %d\n", ret);
        goto error;
    }

    max_frame_size = PCM_SAMPLES_PER_CHANNEL * config->channels * sizeof(short) * 1.25;
    if (max_frame_size < config->packet_buffer_size) {
        mp3_encoder->packet_buffer_size = config->packet_buffer_size;
        dec_cfg.packet_buffer_size = config->packet_buffer_size;
    } else {
        mp3_encoder->packet_buffer_size = max_frame_size;
        dec_cfg.packet_buffer_size = max_frame_size;
    }
    dec_cfg.packet_count = config->packet_count + 1;
    dec_cfg.frame_count = config->frame_count;
    mp3_encoder->encoder.pm = audio_pm_create(&dec_cfg);
    if (NULL == mp3_encoder->encoder.pm) {
        loge("audio_pm_create error.");
        goto error;
    }

    frm_cfg.bits_per_sample = config->bits_per_sample;
    frm_cfg.samples_per_frame = PCM_SAMPLES_PER_CHANNEL * config->channels;
    frm_cfg.frame_count = config->frame_count;
    mp3_encoder->encoder.fm = audio_fm_create(&frm_cfg);
    if (NULL == mp3_encoder->encoder.fm) {
        loge("audio_fm_create fail.\n");
        goto error;
    }

    mp3_encoder->packet_buffer = mpp_alloc(dec_cfg.packet_buffer_size);
    if (NULL == mp3_encoder->packet_buffer) {
        loge("malloc packet buffer fail, size %d.\n", dec_cfg.packet_buffer_size);
        goto error;
    }

    mp3_encoder->lame = lame;
    return 0;

error:
    if (mp3_encoder->packet_buffer) {
        mpp_free(mp3_encoder->packet_buffer);
        mp3_encoder->packet_buffer = NULL;
    }
    if (mp3_encoder->encoder.fm) {
        audio_fm_destroy(mp3_encoder->encoder.fm);
        mp3_encoder->encoder.fm = NULL;
    }
    if (mp3_encoder->encoder.pm) {
        audio_pm_destroy(mp3_encoder->encoder.pm);
        mp3_encoder->encoder.pm = NULL;
    }

    if (lame)
        lame_close(lame);

    return -1;
}

int __mp3_encode_destroy(struct aic_audio_encoder *encoder)
{
    struct mp3_audio_encoder *mp3_encoder = (struct mp3_audio_encoder *)encoder;

    if (NULL == mp3_encoder) {
        loge("mp3_encoder is null.");
        return -1;
    }
    if (mp3_encoder->encoder.fm) {
        audio_fm_destroy(mp3_encoder->encoder.fm);
        mp3_encoder->encoder.fm = NULL;
    }
    if (mp3_encoder->encoder.pm) {
        audio_pm_destroy(mp3_encoder->encoder.pm);
        mp3_encoder->encoder.pm = NULL;
    }
    if (mp3_encoder->packet_buffer) {
        mpp_free(mp3_encoder->packet_buffer);
        mp3_encoder->packet_buffer = NULL;
    }
    if (mp3_encoder->lame) {
        lame_close(mp3_encoder->lame);
        mp3_encoder->lame = NULL;
    }

    return 0;
}

int __mp3_encode_frame(struct aic_audio_encoder *encoder)
{
    struct mp3_audio_encoder *mp3_encoder = (struct mp3_audio_encoder *)encoder;
    struct mpp_packet packet = {0};
    struct aic_audio_frame frame;
    int samples, imp3_bytes;

    if (NULL == mp3_encoder) {
        loge("mp3_encoder is null.");
        return ENC_ERR_NULL_PTR;

    }
    if (0 == mp3_encoder->channels || mp3_encoder->bits_per_sample < 8) {
        loge("mp3_encoder wrong param: channels %d, bits_per_sample %d.",
            mp3_encoder->channels, mp3_encoder->bits_per_sample);
        return ENC_ERR_NOT_SUPPORT;
    }

    if (0 == audio_fm_get_ready_frame_num(mp3_encoder->encoder.fm))
        return ENC_NO_READY_FRAME;

    if (0 == audio_pm_get_empty_packet_num(mp3_encoder->encoder.pm))
        return ENC_NO_EMPTY_PACKET;

    if (0 != audio_fm_dequeue_ready_frame(mp3_encoder->encoder.fm, &frame)) {
        loge("mp3 encoder get pcm buf fail!");
        return ENC_NO_READY_FRAME;
    }

    samples = frame.size / (mp3_encoder->channels * (mp3_encoder->bits_per_sample / 8));

    if (mp3_encoder->channels == 2) {
        // stereo interleaved mode (L,R,L,R...) [citation:10]
        imp3_bytes = lame_encode_buffer_interleaved(
            mp3_encoder->lame,
            (short *)frame.data,  // interleaved PCM data
            samples,           // sample count
            (unsigned char *)mp3_encoder->packet_buffer,
            mp3_encoder->packet_buffer_size);
    } else {
        // mono [citation:10]
        imp3_bytes = lame_encode_buffer(
            mp3_encoder->lame,
            (short *)frame.data, (short *)frame.data,
            samples,          // sample count
            (unsigned char *)mp3_encoder->packet_buffer,
            mp3_encoder->packet_buffer_size);
    }

    audio_fm_enqueue_empty_frame(mp3_encoder->encoder.fm, &frame);
    if (imp3_bytes < 0 || imp3_bytes > mp3_encoder->packet_buffer_size) {
        loge("mp3 encoder fail: imp3_bytes %d, buffer_size %d!",
            imp3_bytes, mp3_encoder->packet_buffer_size);
        return ENC_ERR_INTERNAL;
    } else if (imp3_bytes == 0) {
        logi("mp3 encoder next frame!");
        return ENC_OK;
    }

    logd("frame_size %d, samples %d, imp3_bytes %d!", frame.size, samples, imp3_bytes);

    if (0 != audio_pm_dequeue_empty_packet(mp3_encoder->encoder.pm, &packet, imp3_bytes)) {
        loge("mp3 encoder get empty buf fail: imp3_bytes %d!", imp3_bytes);
        return ENC_NO_EMPTY_PACKET;
    }
    packet.flag &= ~FRAME_FLAG_EOS;
    memcpy(packet.data, mp3_encoder->packet_buffer, imp3_bytes);
    audio_pm_enqueue_ready_packet(mp3_encoder->encoder.pm, &packet);

    // process end frame encode
    if (frame.flag & FRAME_FLAG_EOS) {
        imp3_bytes = lame_encode_flush(
            mp3_encoder->lame,
            mp3_encoder->packet_buffer,
            mp3_encoder->packet_buffer_size);

        logd("process the end frame, lame_encode_flush return %d bytes.", imp3_bytes);
        if (imp3_bytes > 0) {
            if (0 != audio_pm_dequeue_empty_packet(mp3_encoder->encoder.pm, &packet, imp3_bytes)) {
                loge("mp3 encoder get empty buf fail: imp3_bytes %d!", imp3_bytes);
                return ENC_NO_EMPTY_PACKET;
            }
            memcpy(packet.data, mp3_encoder->packet_buffer, imp3_bytes);
            packet.size = imp3_bytes;
            audio_pm_enqueue_ready_packet(mp3_encoder->encoder.pm, &packet);
            packet.flag |= FRAME_FLAG_EOS;
        }
    }
    return ENC_OK;
}

int __mp3_encode_control(struct aic_audio_encoder *encoder, int cmd, void *param)
{
    if ((NULL == encoder) || (cmd < MPP_ENCODER_CMD) || (NULL == param)) {
        loge("invalid parameter [%p/%d/%p]", encoder, cmd, param);
        return -1;
    }

    switch (cmd) {
    case MPP_ENCODER_GET_SAMPLE_NUM:
        *(int *)param = PCM_SAMPLES_PER_CHANNEL;
        break;
    default:
        loge("unsupport cmd:%d", cmd);
        return -1;
    }

    return 0;
}

int __mp3_encode_reset(struct aic_audio_encoder *encoder)
{
    return 0;
}

struct aic_audio_encoder_ops mp3_encoder = {
    .name       = "mp3",
    .init       = __mp3_encode_init,
    .destroy    = __mp3_encode_destroy,
    .encode     = __mp3_encode_frame,
    .control    = __mp3_encode_control,
    .reset      = __mp3_encode_reset,
};

struct aic_audio_encoder *create_mp3_encoder()
{
    struct mp3_audio_encoder *s = (struct mp3_audio_encoder *)mpp_alloc(sizeof(struct mp3_audio_encoder));
    if (s == NULL) {
        loge("mpp_alloc error!!!!\n");
        return NULL;
    }
    memset(s, 0, sizeof(struct mp3_audio_encoder));
    s->encoder.ops = &mp3_encoder;

    return &s->encoder;
}
