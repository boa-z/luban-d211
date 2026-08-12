/*
 * Copyright (C) 2020-2025 ArtInChip Technology Co. Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Author: <xiaodong.zhao@artinchip.com>
 * Desc: aac_encoder
 */
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <inttypes.h>

#include "mpp_dec_type.h"
#include "mpp_mem.h"
#include "mpp_list.h"
#include "mpp_log.h"
#include "fdk-aac/aacenc_lib.h"

#include "audio_encoder.h"

struct aac_audio_encoder {
    struct aic_audio_encoder encoder;
    HANDLE_AACENCODER handle;
    int bitrate;        // 64000
    int aot;            // 2
    int afterburner;    // 1
    int eld_sbr;        // 0
    int vbr;            // 0
    int channels;
    int sample_rate;
    int16_t *convert_buf;
    char outbuf[2048];
    AACENC_InfoStruct info;
};


int __aac_encode_init(struct aic_audio_encoder *encoder, struct aic_audio_encode_config *config)
{
    int size;
    CHANNEL_MODE mode;
    struct audio_frame_manager_cfg frm_cfg = {0};
    struct aic_audio_decode_config dec_cfg = {0};
    struct aac_audio_encoder *aac_encoder = (struct aac_audio_encoder *)encoder;

    if (NULL == aac_encoder) {
        loge("invalid parameter [%p]", aac_encoder);
        return -1;
    }

    // set default parameter
    aac_encoder->bitrate = 64000;
    aac_encoder->aot = 2;
    aac_encoder->afterburner = 1;
    aac_encoder->eld_sbr = 0;
    aac_encoder->vbr = 0;

    aac_encoder->channels = config->channels;
    aac_encoder->sample_rate = config->sample_rate;

    switch (aac_encoder->channels) {
    case 1:
        mode = MODE_1;
        break;
    case 2:
        mode = MODE_2;
        break;
    case 3:
        mode = MODE_1_2;
        break;
    case 4:
        mode = MODE_1_2_1;
        break;
    case 5:
        mode = MODE_1_2_2;
        break;
    case 6:
        mode = MODE_1_2_2_1;
        break;
    default:
        loge("Unsupported WAV channels %d", aac_encoder->channels);
        return -1;
    }

    if (aacEncOpen(&aac_encoder->handle, 0, aac_encoder->channels) != AACENC_OK) {
        loge("Unable to open encoder\n");
        return 1;
    }
    if (aacEncoder_SetParam(aac_encoder->handle, AACENC_AOT, aac_encoder->aot) != AACENC_OK) {
        loge("Unable to set the AOT\n");
        return 1;
    }
    if (aac_encoder->aot == 39 && aac_encoder->eld_sbr) {
        if (aacEncoder_SetParam(aac_encoder->handle, AACENC_SBR_MODE, 1) != AACENC_OK) {
            loge("Unable to set SBR mode for ELD\n");
            return 1;
        }
    }
    if (aacEncoder_SetParam(aac_encoder->handle, AACENC_SAMPLERATE, aac_encoder->sample_rate) != AACENC_OK) {
        loge("Unable to set the sample rate\n");
        return 1;
    }
    if (aacEncoder_SetParam(aac_encoder->handle, AACENC_CHANNELMODE, mode) != AACENC_OK) {
        loge("Unable to set the channel mode\n");
        return 1;
    }
    if (aacEncoder_SetParam(aac_encoder->handle, AACENC_CHANNELORDER, 1) != AACENC_OK) {
        loge("Unable to set the wav channel order\n");
        return 1;
    }
    if (aac_encoder->vbr) {
        if (aacEncoder_SetParam(aac_encoder->handle, AACENC_BITRATEMODE, aac_encoder->vbr) != AACENC_OK) {
            loge("Unable to set the VBR bitrate mode\n");
            return 1;
        }
    } else {
        if (aacEncoder_SetParam(aac_encoder->handle, AACENC_BITRATE, aac_encoder->bitrate) != AACENC_OK) {
            loge("Unable to set the bitrate\n");
            return 1;
        }
    }
    if (aacEncoder_SetParam(aac_encoder->handle, AACENC_TRANSMUX, TT_MP4_ADTS) != AACENC_OK) {
        loge("Unable to set the ADTS transmux\n");
        return 1;
    }
    if (aacEncoder_SetParam(aac_encoder->handle, AACENC_AFTERBURNER, aac_encoder->afterburner) != AACENC_OK) {
        loge("Unable to set the afterburner mode\n");
        return 1;
    }
    if (aacEncEncode(aac_encoder->handle, NULL, NULL, NULL, NULL) != AACENC_OK) {
        loge("Unable to initialize the encoder\n");
        return 1;
    }
    if (aacEncInfo(aac_encoder->handle, &aac_encoder->info) != AACENC_OK) {
        loge("Unable to get the encoder info\n");
        return 1;
    }

    dec_cfg.packet_buffer_size = config->packet_buffer_size;
    dec_cfg.packet_count = config->packet_count;
    dec_cfg.frame_count = config->frame_count;
    aac_encoder->encoder.pm = audio_pm_create(&dec_cfg);
    if (NULL == aac_encoder->encoder.pm) {
        loge("audio_pm_create error!");
        goto audio_pm_create_fail;
    }

    frm_cfg.bits_per_sample = config->bits_per_sample;
    frm_cfg.samples_per_frame = aac_encoder->info.frameLength * aac_encoder->channels;
    frm_cfg.frame_count = config->frame_count;
    aac_encoder->encoder.fm = audio_fm_create(&frm_cfg);
    if (NULL == aac_encoder->encoder.fm) {
        loge("audio_fm_create fail!!!\n");
        goto audio_fm_create_fail;
    }

    size = aac_encoder->info.frameLength * aac_encoder->channels * sizeof(int16_t);
    aac_encoder->convert_buf = (int16_t *)mpp_alloc(size);
    if (NULL == aac_encoder->convert_buf) {
        goto alloc_convert_buf_fail;
    }

    return 0;

alloc_convert_buf_fail:
    audio_fm_destroy(aac_encoder->encoder.fm);
audio_fm_create_fail:
    audio_pm_destroy(aac_encoder->encoder.pm);
audio_pm_create_fail:
    aacEncClose(&aac_encoder->handle);

    return -1;
}

int __aac_encode_destroy(struct aic_audio_encoder *encoder)
{
    struct aac_audio_encoder *aac_encoder = (struct aac_audio_encoder *)encoder;

    if (NULL == aac_encoder) {
        loge("invalid parameter!");
        return -1;
    }

    audio_pm_destroy(aac_encoder->encoder.pm);
    audio_fm_destroy(aac_encoder->encoder.fm);
    aacEncClose(&aac_encoder->handle);
    mpp_free(aac_encoder);
    if (aac_encoder->convert_buf) {
        mpp_free(aac_encoder->convert_buf);
        aac_encoder->convert_buf = NULL;
    }

    return 0;
}

int __aac_encode_frame(struct aic_audio_encoder *encoder)
{
    int ret = 0, i, samples;
    AACENC_InArgs in_args = {0};
    AACENC_OutArgs out_args = { 0 };
    AACENC_BufDesc in_buf = { 0 }, out_buf = { 0 };
    int in_identifier = IN_AUDIO_DATA;
    int in_elem_size, in_size;
    int out_size, out_elem_size;
    int out_identifier = OUT_BITSTREAM_DATA;
    uint8_t* input_buf;
    void *in_ptr, *out_ptr;
    struct mpp_packet packet = {0};
    struct aic_audio_frame frame;
    struct aac_audio_encoder *aac_encoder = (struct aac_audio_encoder *)encoder;

    if (NULL == aac_encoder) {
        loge("invalid parameter!");
        return -1;
    }

    if (audio_fm_get_ready_frame_num(aac_encoder->encoder.fm) == 0) {
        return DEC_NO_READY_PACKET;
    }

    if (audio_pm_get_empty_packet_num(aac_encoder->encoder.pm) == 0) {
        return DEC_NO_EMPTY_FRAME;
    }

    ret = audio_fm_dequeue_ready_frame(aac_encoder->encoder.fm, &frame);
    if (0 != ret) {
        loge("aac encoder get pcm buf fail!");
        return -1;
    }

    input_buf = (uint8_t*)frame.data;
    samples = aac_encoder->info.frameLength * aac_encoder->channels;
    for (i = 0; i < samples; i++) {
        const uint8_t* in = &input_buf[2*i];
        aac_encoder->convert_buf[i] = in[0] | (in[1] << 8);
    }

    in_ptr = (void *)aac_encoder->convert_buf;
    in_size = samples * sizeof(int16_t);
    in_elem_size = 2;

    in_args.numInSamples = samples;
    in_buf.numBufs = 1;
    in_buf.bufs = &in_ptr;
    in_buf.bufferIdentifiers = &in_identifier;
    in_buf.bufSizes = &in_size;
    in_buf.bufElSizes = &in_elem_size;

    out_ptr = (void *)aac_encoder->outbuf;
    out_size = sizeof(aac_encoder->outbuf);
    out_elem_size = 1;
    out_buf.numBufs = 1;
    out_buf.bufs = &out_ptr;
    out_buf.bufferIdentifiers = &out_identifier;
    out_buf.bufSizes = &out_size;
    out_buf.bufElSizes = &out_elem_size;

    ret = aacEncEncode(aac_encoder->handle, &in_buf, &out_buf, &in_args, &out_args);
    if (AACENC_OK != ret) {
        if (AACENC_ENCODE_EOF == ret) {
            loge("eof");
        }
        return -1;
    }

    ret = audio_fm_enqueue_empty_frame(aac_encoder->encoder.fm, &frame);
    if (DEC_OK != ret) {
        loge("audio_fm_enqueue_empty_frame fail");
        return -1;
    }

    ret = audio_pm_dequeue_empty_packet(aac_encoder->encoder.pm, &packet, out_args.numOutBytes);
    if (DEC_OK != ret) {
        loge("get es packet fail, remove current es frame, size:%d", out_args.numOutBytes);
        return -1;
    }
    memcpy(packet.data, aac_encoder->outbuf, out_args.numOutBytes);
    audio_pm_enqueue_ready_packet(aac_encoder->encoder.pm, &packet);

    return DEC_OK;
}

int __aac_encode_control(struct aic_audio_encoder *encoder, int cmd, void *param)
{
    struct aac_audio_encoder *aac_encoder;

    if ((NULL == encoder) || (cmd < MPP_ENCODER_CMD) || (NULL == param)) {
        loge("invalid parameter [%p/%d/%p]", encoder, cmd, param);
        return -1;
    }

    aac_encoder = (struct aac_audio_encoder *)encoder;

    switch (cmd) {
    case MPP_ENCODER_GET_SAMPLE_NUM:
        *(int *)param = aac_encoder->info.frameLength;
        break;
    default:
        loge("unsupport cmd:%d", cmd);
        return -1;
    }

    return 0;
}

int __aac_encode_reset(struct aic_audio_encoder *encoder)
{
    struct aac_audio_encoder *aac_encoder = (struct aac_audio_encoder *)encoder;

    if (NULL == aac_encoder) {
        loge("invalid parameter!");
        return -1;
    }

    audio_pm_reset(aac_encoder->encoder.pm);
    audio_fm_reset(aac_encoder->encoder.fm);

    return 0;
}

struct aic_audio_encoder_ops aac_encoder = {
    .name       = "aac",
    .init       = __aac_encode_init,
    .destroy    = __aac_encode_destroy,
    .encode     = __aac_encode_frame,
    .control    = __aac_encode_control,
    .reset      = __aac_encode_reset,
};

struct aic_audio_encoder *create_aac_encoder()
{
    struct aac_audio_encoder *s = (struct aac_audio_encoder *)mpp_alloc(sizeof(struct aac_audio_encoder));
    if (s == NULL) {
        loge("mpp_alloc error!!!!\n");
        return NULL;
    }
    memset(s, 0, sizeof(struct aac_audio_encoder));
    s->encoder.ops = &aac_encoder;

    return &s->encoder;
}
