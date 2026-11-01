/*
 * Copyright (C) 2020-2025 ArtInChip Technology Co. Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Author: <xiaodong.zhao@artinchip.com>
 * Desc: aac eld decoder, using fdk-aac lib
 */
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <inttypes.h>

#include "mpp_dec_type.h"
#include "mpp_mem.h"
#include "mpp_list.h"
#include "mpp_log.h"

#include "audio_decoder.h"
#include "fdk-aac/aacdecoder_lib.h"

#define N_AACELD_SAMPLE         480 /* period size 480 samples */
#define N_CHANNEL               2
#define AACELD_PCM_BUFF_SIZE    (2 * N_CHANNEL * N_AACELD_SAMPLE) // 1920

/* AAC-ELD ASC config binary data */
// aac-eld 44.1kHz channel = 2
// |0xF8, 0xE8, 0x50, 0x00
// |11111 000111 | 0100 | 0010 | 1 0000 0000 0000
// |  32   +  7  |   4  |   2  |
static unsigned char eld_conf[] = { 0xF8, 0xE8, 0x50, 0x00 };
static unsigned char *conf[] = { eld_conf };                   //for aac eld config
static unsigned int conf_len = sizeof(eld_conf);

struct aac_eld_audio_decoder {
    struct aic_audio_decoder decoder;
    HANDLE_AACDECODER aaceld_handle;
    TRANSPORT_TYPE transport_fmt;
    CStreamInfo *stream_info;
    unsigned int nr_of_ayers;
    int fdk_flags;
    struct mpp_packet *curr_packet;
    int frame_id;
    int aac_type;
};

int __aac_eld_decode_init(struct aic_audio_decoder *decoder, struct aic_audio_decode_config *config)
{
    struct aac_eld_audio_decoder *aac_decoder = NULL;
    struct audio_frame_manager_cfg frm_cfg = {0};
    int ret;

    if ((NULL == decoder) || (NULL == config)) {
        loge("invalid parameter!");
        return DEC_ERR_NULL_PTR;
    }

    aac_decoder = (struct aac_eld_audio_decoder *)decoder;
    aac_decoder->decoder.pm = audio_pm_create(config);

    aac_decoder->fdk_flags = 0;
    aac_decoder->transport_fmt = 0;     //raw
    aac_decoder->nr_of_ayers = 1;       // 1 layer
    aac_decoder->aaceld_handle = aacDecoder_Open(aac_decoder->transport_fmt, aac_decoder->nr_of_ayers);
    if (NULL == aac_decoder->aaceld_handle) {
        loge("fdk-aac open fail!");
        goto err;
    }

    ret = aacDecoder_ConfigRaw(aac_decoder->aaceld_handle, conf, &conf_len);
    if (ret != AAC_DEC_OK) {
        loge("Unable to set configRaw\n");
        goto err;
    }

    aac_decoder->stream_info = aacDecoder_GetStreamInfo(aac_decoder->aaceld_handle);
    if (aac_decoder->stream_info == NULL) {
        loge("aacDecoder_GetStreamInfo failed!\n");
        goto err;
    }

    frm_cfg.bits_per_sample = 16;
    frm_cfg.samples_per_frame = N_AACELD_SAMPLE * N_CHANNEL;
    frm_cfg.frame_count = config->frame_count;
    aac_decoder->decoder.fm = audio_fm_create(&frm_cfg);
    if (aac_decoder->decoder.fm == NULL) {
        loge("audio_fm_create fail!!!\n");
        goto err;
    }

    return 0;
err:
    if (aac_decoder->aaceld_handle) {
        aacDecoder_Close(aac_decoder->aaceld_handle);
        aac_decoder->aaceld_handle = NULL;
    }

    if (aac_decoder->decoder.pm) {
        audio_pm_destroy(aac_decoder->decoder.pm);
    }

    return -1;
}

int __aac_eld_decode_destroy(struct aic_audio_decoder *decoder)
{
    struct aac_eld_audio_decoder *aac_decoder = NULL;

    if (NULL == decoder) {
        loge("invalid parameter!");
        return DEC_ERR_NULL_PTR;
    }

    aac_decoder = (struct aac_eld_audio_decoder *)decoder;

    audio_pm_destroy(aac_decoder->decoder.pm);
    audio_fm_destroy(aac_decoder->decoder.fm);
    aacDecoder_Close(aac_decoder->aaceld_handle);
    mpp_free(aac_decoder);

    return 0;
}

int __aac_eld_decode_frame(struct aic_audio_decoder *decoder)
{
    struct aac_eld_audio_decoder *aac_decoder = NULL;
    struct aic_audio_frame *frame = NULL;
    unsigned char *input_buf[1] = {0};
    unsigned int valid_size = 0;
    unsigned int size = 0;
    s32 ret = 0;
    int len = 0;

    if (NULL == decoder) {
        loge("invalid parameter!");
        return DEC_ERR_NULL_PTR;
    }

    aac_decoder = (struct aac_eld_audio_decoder *)decoder;

    if (audio_pm_get_ready_packet_num(aac_decoder->decoder.pm) == 0) {
        return DEC_NO_READY_PACKET;
    }

    if (audio_fm_get_empty_frame_num(aac_decoder->decoder.fm) == 0) {
        return DEC_NO_EMPTY_FRAME;
    }

    aac_decoder->curr_packet = audio_pm_dequeue_ready_packet(aac_decoder->decoder.pm);
    input_buf[0] = aac_decoder->curr_packet->data;
    size = valid_size = aac_decoder->curr_packet->size;

    /* step 1 -> fill aac_data_buf to decoder's internal buf */
    ret = aacDecoder_Fill(aac_decoder->aaceld_handle, input_buf, &size, &valid_size);
    if (ret != AAC_DEC_OK) {
        loge("Fill failed: %x\n", ret);
        goto aacDecoder_Fill_err;
    }

    frame = audio_fm_dequeue_empty_frame(aac_decoder->decoder.fm);
    /* step 2 -> call decoder function */
    ret = aacDecoder_DecodeFrame(aac_decoder->aaceld_handle, (INT_PCM *)frame->data, AACELD_PCM_BUFF_SIZE, aac_decoder->fdk_flags);
    if (ret != AAC_DEC_OK) {
        loge("aacDecoder_DecodeFrame : 0x%x -- inputsize: %d\n", ret, size);
        goto DecodeFrame_err;
    }

    /* step 3 -> get frame size */
    CStreamInfo *info = aacDecoder_GetStreamInfo(aac_decoder->aaceld_handle);
    logi("aacDecoder frame size: %d\n", info->frameSize);
    logi("aacDecoder sample rate: %d\n", info->sampleRate);
    logi("aacDecoder number channel: %d\n", info->numChannels);
    logi("aacDecoder aac sample rate: %d\n", info->aacSampleRate);

    len = info->frameSize * 4;
    if (len == 0) {
        loge("aacDecoder frame size is zero.\n");
        goto DecodeFrame_err;
    } else if (len != AACELD_PCM_BUFF_SIZE) {
        loge("aacDecoder_DecodeFrame size %d is erro.\n", len);
        goto DecodeFrame_err;
    }

    frame->channels = info->numChannels;
    frame->sample_rate = info->sampleRate;
    frame->pts = aac_decoder->curr_packet->pts;
    frame->bits_per_sample = 16;
    frame->id = aac_decoder->frame_id++;

    if (aac_decoder->curr_packet->flag & PACKET_FLAG_EOS) {
        frame->flag |= PACKET_FLAG_EOS;
        logd("aac_decoder last packet!!!!\n");
    }

    audio_pm_enqueue_empty_packet(aac_decoder->decoder.pm, aac_decoder->curr_packet);

    if (audio_fm_enqueue_ready_frame(aac_decoder->decoder.fm, frame) != 0) {
        loge("please check code, why!!!\n");
        return DEC_ERR_NULL_PTR;
    }

    return DEC_OK;

DecodeFrame_err:
    audio_fm_enqueue_empty_frame(aac_decoder->decoder.fm, frame);
aacDecoder_Fill_err:
    audio_pm_enqueue_empty_packet(aac_decoder->decoder.pm, aac_decoder->curr_packet);

    return -1;
}

int __aac_eld_decode_control(struct aic_audio_decoder *decoder, int cmd, void *param)
{
    return 0;
}

int __aac_eld_decode_reset(struct aic_audio_decoder *decoder)
{
    struct aac_eld_audio_decoder *aac_decoder = NULL;

    if (NULL == decoder) {
        loge("invalid parameter!");
        return DEC_ERR_NULL_PTR;
    }

    aac_decoder = (struct aac_eld_audio_decoder *)decoder;
    audio_pm_reset(aac_decoder->decoder.pm);
    audio_fm_reset(aac_decoder->decoder.fm);
    return 0;
}

struct aic_audio_decoder_ops aac_eld_decoder = {
    .name       = "aac",
    .init       = __aac_eld_decode_init,
    .destroy    = __aac_eld_decode_destroy,
    .decode     = __aac_eld_decode_frame,
    .control    = __aac_eld_decode_control,
    .reset      = __aac_eld_decode_reset,
};

struct aic_audio_decoder *create_aac_eld_decoder()
{
    struct aac_eld_audio_decoder *s = (struct aac_eld_audio_decoder *)mpp_alloc(sizeof(struct aac_eld_audio_decoder));
    if (s == NULL) {
        loge("mpp_alloc error!!!!\n");
        return NULL;
    }
    memset(s, 0, sizeof(struct aac_eld_audio_decoder));
    s->decoder.ops = &aac_eld_decoder;
    return &s->decoder;
}
