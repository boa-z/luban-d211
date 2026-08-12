/*
 * Copyright (C) 2020-2026 ArtInChip Technology Co. Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 *  author: <xiaodong.zhao@artinchip.com>
 *  Desc: alac decoder interface
 */

#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <inttypes.h>
#include <string.h>
#include <stdlib.h>

#include "mpp_dec_type.h"
#include "mpp_mem.h"
#include "mpp_list.h"
#include "mpp_log.h"

#include "audio_decoder.h"
#include "alac.h"

#define N_CHANNEL               2
#define N_ALAC_SAMPLE           352 /* period size 352 samples */
#define ALAC_PCM_BUFF_SIZE      (2 * N_CHANNEL * N_ALAC_SAMPLE) // 1408

struct alac_decoder {
    struct aic_audio_decoder decoder;
    struct mpp_packet *curr_packet;

    alac_file *alac_phandle;
    ALACSpecificConfig alac_config;

    int es_cnt;
    int frame_id;
    int frame_count;
};

static int alac_get_fmtp_info(ALACSpecificConfig *config, const char *fmtp)
{
    int intarr[12];
    char *original;
    char *strptr;
    int i;

    /* Parse fmtp string to integers */
    original = strptr = strdup(fmtp);
    for (i = 0; i < 12; i++) {
        if (strptr == NULL) {
            return -1;
        }
        intarr[i] = atoi(strsep(&strptr, " "));
    }
    free(original);
    original = strptr = NULL;

    /* Fill the config struct */
    config->frameLength = intarr[1];
    config->compatibleVersion = intarr[2];
    config->bitDepth = intarr[3];
    config->pb = intarr[4];
    config->mb = intarr[5];
    config->kb = intarr[6];
    config->numChannels = intarr[7];
    config->maxRun = intarr[8];
    config->maxFrameBytes = intarr[9];
    config->avgBitRate = intarr[10];
    config->sampleRate = intarr[11];

    /* Validate supported audio types */
    if (config->bitDepth != 16) {
        return -2;
    }
    if (config->numChannels != 2) {
        return -3;
    }

    return 0;
}

#define SET_UINT16(buf, value)do {\
    (buf)[0] = (unsigned char)((value) >> 8);\
    (buf)[1] = (unsigned char)(value);\
            }while(0)

#define SET_UINT32(buf, value)do {\
    (buf)[0] = (unsigned char)((value) >> 24);\
    (buf)[1] = (unsigned char)((value) >> 16);\
    (buf)[2] = (unsigned char)((value) >> 8);\
    (buf)[3] = (unsigned char)(value);\
            }while(0)

static void alac_set_decoder_info(alac_file *alac, ALACSpecificConfig *config)
{
    unsigned char decoder_info[48];
    memset(decoder_info, 0, sizeof(decoder_info));

    /* Construct decoder info buffer */
    SET_UINT32(&decoder_info[24], config->frameLength);
    decoder_info[28] = config->compatibleVersion;
    decoder_info[29] = config->bitDepth;
    decoder_info[30] = config->pb;
    decoder_info[31] = config->mb;
    decoder_info[32] = config->kb;
    decoder_info[33] = config->numChannels;
    SET_UINT16(&decoder_info[34], config->maxRun);
    SET_UINT32(&decoder_info[36], config->maxFrameBytes);
    SET_UINT32(&decoder_info[40], config->avgBitRate);
    SET_UINT32(&decoder_info[44], config->sampleRate);
    alac_set_info(alac, (char *) decoder_info);
}

int __alac_decode_init(struct aic_audio_decoder *decoder, struct aic_audio_decode_config *config)
{
    struct audio_frame_manager_cfg frm_cfg = {0};
    struct alac_decoder *alac_decoder = NULL;
    int ret;

    if ((NULL == decoder) || (NULL == config)) {
        loge("invalid parameter [%p/%p]", decoder, config);
        return -1;
    }

    alac_decoder = (struct alac_decoder *)decoder;
    alac_decoder->decoder.pm = audio_pm_create(config);
    alac_decoder->frame_count = config->frame_count;

    alac_decoder->es_cnt = 0;
    alac_decoder->frame_id = 0;

    ret = alac_get_fmtp_info(&alac_decoder->alac_config, "96 352 0 16 40 10 14 2 255 0 0 44100");
    if (ret) {
        loge("alac_get_fmtp_info failed! %d\n", ret);
        goto err;
    }

    alac_decoder->alac_phandle = alac_create(alac_decoder->alac_config.bitDepth, alac_decoder->alac_config.numChannels);
    if (alac_decoder->alac_phandle == NULL) {
        loge("alacDecoder create faild!\n");
        goto err;
    }

    alac_set_decoder_info(alac_decoder->alac_phandle, &alac_decoder->alac_config);

    frm_cfg.bits_per_sample = 16;
    frm_cfg.samples_per_frame = N_ALAC_SAMPLE * N_CHANNEL;
    frm_cfg.frame_count = config->frame_count;
    alac_decoder->decoder.fm = audio_fm_create(&frm_cfg);
    if (alac_decoder->decoder.fm == NULL) {
        loge("alac audio_fm_create fail!!!\n");
        goto err;
    }

    return 0;
err:
    if (alac_decoder->alac_phandle) {
        alac_free(alac_decoder->alac_phandle);
        alac_decoder->alac_phandle = NULL;
    }
    return -1;
}

int __alac_decode_destroy(struct aic_audio_decoder *decoder)
{
    struct alac_decoder *alac_decoder = (struct alac_decoder *)decoder;

    if (NULL == alac_decoder) {
        loge("invalid parameter [%p]", alac_decoder);
        return -1;
    }

    if (alac_decoder->decoder.pm) {
        audio_pm_destroy(alac_decoder->decoder.pm);
        alac_decoder->decoder.pm = NULL;
    }
    if (alac_decoder->decoder.fm) {
        audio_fm_destroy(alac_decoder->decoder.fm);
        alac_decoder->decoder.fm = NULL;
    }

    if (alac_decoder->alac_phandle) {
        alac_free(alac_decoder->alac_phandle);
        alac_decoder->alac_phandle = NULL;
    }

    mpp_free(alac_decoder);

    return 0;
}

int __alac_decode_frame(struct aic_audio_decoder *decoder)
{
    struct alac_decoder *alac_decoder = (struct alac_decoder *)decoder;
    int len = 0, ret = DEC_OK;
    struct aic_audio_frame *frame = NULL;
    unsigned char *data = NULL;

    if (NULL == alac_decoder) {
        loge("invalid parameter [%p]", alac_decoder);
        return -1;
    }

    if (0 == audio_pm_get_ready_packet_num(alac_decoder->decoder.pm)) {
        return DEC_NO_READY_PACKET;
    }

    if ((alac_decoder->decoder.fm) && (audio_fm_get_empty_frame_num(alac_decoder->decoder.fm)) == 0) {
        return DEC_NO_EMPTY_FRAME;
    }

    alac_decoder->curr_packet = audio_pm_dequeue_ready_packet(alac_decoder->decoder.pm);
    if (!alac_decoder->curr_packet) {
        return DEC_NO_READY_PACKET;
    }

    // start decode
    data = alac_decoder->curr_packet->data;
    // size = alac_decoder->curr_packet->size;
    frame = audio_fm_dequeue_empty_frame(alac_decoder->decoder.fm);
    alac_decode_frame(alac_decoder->alac_phandle, data, frame->data, &len);
    if (ALAC_PCM_BUFF_SIZE != len) {
        loge("alac decode error!");
        audio_fm_enqueue_empty_frame(alac_decoder->decoder.fm, frame);
        ret = DEC_ERR_NOT_SUPPORT;
    }

    audio_pm_enqueue_empty_packet(alac_decoder->decoder.pm, alac_decoder->curr_packet);

    if (audio_fm_enqueue_ready_frame(alac_decoder->decoder.fm, frame) != 0) {
        loge("please check code, why!!!\n");
        return DEC_ERR_NULL_PTR;
    }

    return ret;
}

int __alac_decode_control(struct aic_audio_decoder *decoder, int cmd, void *param)
{
    return 0;
}

int __alac_decode_reset(struct aic_audio_decoder *decoder)
{
    return 0;
}

struct aic_audio_decoder_ops alac_decoder = {
    .name       = "alac",
    .init       = __alac_decode_init,
    .destroy    = __alac_decode_destroy,
    .decode     = __alac_decode_frame,
    .control    = __alac_decode_control,
    .reset      = __alac_decode_reset,
};

struct aic_audio_decoder *create_alac_decoder()
{
    struct alac_decoder *s = (struct alac_decoder *)mpp_alloc(sizeof(struct alac_decoder));
    if (NULL == s)
        return NULL;

    memset(s, 0, sizeof(struct alac_decoder));
    s->decoder.ops = &alac_decoder;

    return &s->decoder;
}
