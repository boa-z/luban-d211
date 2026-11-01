/*
 * Copyright (C) 2020-2026 ArtInChip Technology Co. Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 *  author: <xiaodong.zhao@artinchip.com>
 *  Desc: aic_audio_encoder interface
 */

#include "mpp_log.h"
#include "audio_encoder.h"

#ifdef MP3_ENCODER
extern struct aic_audio_encoder *create_mp3_encoder();
#endif
#ifdef AAC_ENCODER
extern struct aic_audio_encoder* create_aac_encoder();
#endif

struct aic_audio_encoder *aic_audio_encoder_create(enum aic_audio_codec_type type)
{
#ifdef MP3_ENCODER
    if(type == MPP_CODEC_AUDIO_ENCODER_MP3) {
        return create_mp3_encoder();
    }
#endif
#ifdef AAC_ENCODER
    if(type == MPP_CODEC_AUDIO_ENCODER_AAC) {
        return create_aac_encoder();
    }
#endif

    return NULL;
}

void aic_audio_encoder_destroy(struct aic_audio_encoder* encoder)
{
    encoder->ops->destroy(encoder);
}

s32 aic_audio_encoder_init(struct aic_audio_encoder *encoder, struct aic_audio_encode_config *config)
{
    return encoder->ops->init(encoder, config);
}

s32 aic_audio_encoder_encode(struct aic_audio_encoder* encoder)
{
    return encoder->ops->encode(encoder);
}

s32 aic_audio_encoder_control(struct aic_audio_encoder* encoder, int cmd, void *param)
{
    switch (cmd) {
    default:
        break;
    }
    return encoder->ops->control(encoder, cmd, param);
}

s32 aic_audio_encoder_reset(struct aic_audio_encoder* encoder)
{
    return encoder->ops->reset(encoder);
}

struct mpp_packet* aic_audio_encoder_get_es_package(struct aic_audio_encoder* encoder)
{
    if(encoder == NULL)
        return NULL;

    if (0 == audio_pm_get_ready_packet_num(encoder->pm)) {
        return NULL;
    }

    return audio_pm_dequeue_ready_packet(encoder->pm);
}

s32 aic_audio_encoder_back_es_packet(struct aic_audio_encoder* encoder, struct mpp_packet* packet)
{
    if(encoder == NULL || packet == NULL)
        return DEC_ERR_NULL_PTR;

    return audio_pm_enqueue_empty_packet(encoder->pm, packet);
}

struct aic_audio_frame *aic_audio_encoder_get_pcm_frame(struct aic_audio_encoder* encoder)
{

    if(encoder == NULL)
        return NULL;

    if(encoder->fm == NULL) {
        return NULL;
    }
    if(audio_fm_get_empty_frame_num(encoder->fm) == 0)
        return NULL;

    return audio_fm_dequeue_empty_frame(encoder->fm);
}

s32 aic_audio_encoder_put_pcm_frame(struct aic_audio_encoder* encoder, struct aic_audio_frame* frame)
{
    if(encoder == NULL || frame == NULL)
        return DEC_ERR_NULL_PTR;

    return audio_fm_enqueue_ready_frame(encoder->fm, frame);
}


