/*
 * Copyright (C) 2020-2025 ArtInChip Technology Co. Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 *  author: <xiaodong.zhao@artinchip.com>
 *  Desc: vorbis decoder interface
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
#include <vorbis/codec.h>

#define SAMPLES_PER_FRAME 1024

enum vorbis_decode_state {
    VORBIS_PARSE_HEADER,
    VORBIS_DECODE_FRAME
};

struct vorbis_decoder {
    struct aic_audio_decoder decoder;
    struct mpp_packet* curr_packet;

    ogg_packet          op; /* one raw packet of data for decode */
    vorbis_info         vi; /* struct that stores all the static vorbis bitstream settings */
    vorbis_comment      vc; /* struct that stores all the bitstream user comments */
    vorbis_dsp_state    vd; /* central working state for the packet->PCM decoder */
    vorbis_block        vb; /* local working space for packet->PCM decode */

    int es_cnt;
    int frame_duration;
    enum vorbis_decode_state state;

    int frame_id;
    int sample_num;
    char pcm_buf[SAMPLES_PER_FRAME << 3];

    int frame_count;
};

int __vorbis_decode_init(struct aic_audio_decoder *decoder, struct aic_audio_decode_config *config)
{
    struct vorbis_decoder *vorbis_decoder = (struct vorbis_decoder *)decoder;

    vorbis_decoder->decoder.pm = audio_pm_create(config);
    vorbis_decoder->frame_count = config->frame_count;

    /**
    extract the initial header from the first page and verify that the
    Ogg bitstream is in fact Vorbis data

    I handle the initial header first instead of just having the code
    read all three Vorbis headers at once because reading the initial
    header is an easy way to identify a Vorbis bitstream and it's
    useful to see that functionality seperated out.
    */
    vorbis_info_init(&vorbis_decoder->vi);
    vorbis_comment_init(&vorbis_decoder->vc);

    // vorbis_decoder->vorbis_header_init_flag = 0;
    vorbis_decoder->es_cnt = 0;
    vorbis_decoder->state = VORBIS_PARSE_HEADER;

    vorbis_decoder->frame_id = 0;
    vorbis_decoder->sample_num = 0;

    return 0;
}

int __vorbis_decode_destroy(struct aic_audio_decoder *decoder)
{
    struct vorbis_decoder *vorbis_decoder = (struct vorbis_decoder *)decoder;
    if (!vorbis_decoder) {
        return -1;
    }

    if (VORBIS_DECODE_FRAME == vorbis_decoder->state) {
        vorbis_block_clear(&vorbis_decoder->vb);
        vorbis_dsp_clear(&vorbis_decoder->vd);
    }
    vorbis_comment_clear(&vorbis_decoder->vc);
    vorbis_info_clear(&vorbis_decoder->vi);  /* must be called last */

    if (vorbis_decoder->decoder.pm) {
        audio_pm_destroy(vorbis_decoder->decoder.pm);
        vorbis_decoder->decoder.pm = NULL;
    }
    if (vorbis_decoder->decoder.fm) {
        audio_fm_destroy(vorbis_decoder->decoder.fm);
        vorbis_decoder->decoder.fm = NULL;
    }

    mpp_free(vorbis_decoder);

    return 0;

}

static int vorbis_output_pcm(struct vorbis_decoder *vorbis_decoder)
{
    int pcm_data_size = 0;
    struct aic_audio_frame *frame;

    if (vorbis_decoder->sample_num < SAMPLES_PER_FRAME) {
        return -1;
    }

    frame = audio_fm_dequeue_empty_frame(vorbis_decoder->decoder.fm);
    if (NULL == frame) {
        loge("audio_fm_dequeue_empty_frame fail!");
        return -1;
    }

    frame->channels = vorbis_decoder->vi.channels;
    frame->sample_rate = vorbis_decoder->vi.rate;
    frame->pts = vorbis_decoder->frame_id * vorbis_decoder->frame_duration;
    frame->bits_per_sample = 16;
    frame->id = vorbis_decoder->frame_id++;

    if (vorbis_decoder->curr_packet->flag & PACKET_FLAG_EOS) {
        frame->flag |= PACKET_FLAG_EOS;
        logd("vorbis_decoder last packet!!!!\n");
    }

    pcm_data_size = SAMPLES_PER_FRAME * 2 * vorbis_decoder->vi.channels;
    memcpy(frame->data, vorbis_decoder->pcm_buf, pcm_data_size);
    vorbis_decoder->sample_num -= SAMPLES_PER_FRAME;
    if (vorbis_decoder->sample_num > 0) {
        memmove(vorbis_decoder->pcm_buf, vorbis_decoder->pcm_buf + pcm_data_size, vorbis_decoder->sample_num * 4);
    }

    if (audio_fm_enqueue_ready_frame(vorbis_decoder->decoder.fm, frame) != 0) {
        loge("audio_fm_enqueue_ready_frame fail!\n");
    }

    return 0;
}

static int vorbis_parse_header(struct vorbis_decoder *vorbis_decoder)
{
    int ret;

    ret = vorbis_synthesis_headerin(&vorbis_decoder->vi, &vorbis_decoder->vc, &vorbis_decoder->op);
    if (ret == 0) {
        // check if process all headers
        if (vorbis_decoder->es_cnt >= 3) {
            // init decoder
            if (vorbis_synthesis_init(&vorbis_decoder->vd, &vorbis_decoder->vi) == 0) {
                vorbis_block_init(&vorbis_decoder->vd, &vorbis_decoder->vb);
                vorbis_decoder->state = VORBIS_DECODE_FRAME;
                logi("Vorbis decoder fully initialized");
            } else {
                loge("vorbis_synthesis_init failed!");
                ret = DEC_ERR_NOT_SUPPORT;
            }
        }
    } else {
        loge("vorbis_synthesis_headerin failed for header %d, ret=%d", vorbis_decoder->es_cnt, ret);
        ret = DEC_ERR_NOT_SUPPORT;
    }
    audio_pm_enqueue_empty_packet(vorbis_decoder->decoder.pm, vorbis_decoder->curr_packet);

    return ret;
}

int __vorbis_decode_frame(struct aic_audio_decoder *decoder)
{
    int ret, samples;
    float **pcm;
    struct audio_frame_manager_cfg cfg;
    struct vorbis_decoder *vorbis_decoder = (struct vorbis_decoder *)decoder;

    if (vorbis_decoder->vi.channels > 2) {
        loge("vorbis decoder only support 2 channel,current channel:%d", vorbis_decoder->vi.channels);
        return DEC_ERR_NOT_SUPPORT;
    }

    if (vorbis_decoder->sample_num >= SAMPLES_PER_FRAME) {
        return vorbis_output_pcm(vorbis_decoder);
    }

    if (0 == audio_pm_get_ready_packet_num(vorbis_decoder->decoder.pm)) {
        return DEC_NO_READY_PACKET;
    }

    if ((vorbis_decoder->decoder.fm) && (audio_fm_get_empty_frame_num(vorbis_decoder->decoder.fm)) == 0) {
        return DEC_NO_EMPTY_FRAME;
    }

    vorbis_decoder->curr_packet = audio_pm_dequeue_ready_packet(vorbis_decoder->decoder.pm);
    if (!vorbis_decoder->curr_packet) {
        return DEC_NO_READY_PACKET;
    }

    vorbis_decoder->op.packet = vorbis_decoder->curr_packet->data;
    vorbis_decoder->op.bytes = vorbis_decoder->curr_packet->size;
    vorbis_decoder->op.b_o_s = (vorbis_decoder->es_cnt == 0) ? 1 : 0;  // first frame set b_o_s
    vorbis_decoder->op.e_o_s = 0;
    vorbis_decoder->op.granulepos = -1;
    vorbis_decoder->op.packetno = vorbis_decoder->es_cnt;
    vorbis_decoder->es_cnt++;

    // process header
    if (VORBIS_PARSE_HEADER == vorbis_decoder->state) {
        return vorbis_parse_header(vorbis_decoder);
    }

    // decode frame
    if (VORBIS_DECODE_FRAME == vorbis_decoder->state) {
        ret = vorbis_synthesis(&vorbis_decoder->vb, &vorbis_decoder->op);
        if (ret == 0) {
            if (0 != vorbis_synthesis_blockin(&vorbis_decoder->vd, &vorbis_decoder->vb)) {
                loge("vorbis_synthesis_blockin error: %d", ret);
            }
        } else {
            loge("vorbis_synthesis error: %d", ret);
        }

        while ((samples = vorbis_synthesis_pcmout(&vorbis_decoder->vd, &pcm)) > 0) {
            int i, j, bout;

            if (samples > SAMPLES_PER_FRAME) {
                bout = SAMPLES_PER_FRAME;
            } else {
                bout = samples;
            }

            ogg_int16_t *data = (ogg_int16_t *)(vorbis_decoder->pcm_buf + vorbis_decoder->sample_num * 4);
            /* convert floats to 16 bit signed ints (host order) and interleave */
            for (i=0; i < vorbis_decoder->vi.channels; i++) {
                ogg_int16_t *ptr = data + i;
                float *mono = pcm[i];
                for (j = 0; j < bout; j++) {
#if 1
                    // int val=floor(mono[j]*32767.f+.5f);
                    int val = (int)(mono[j] * 32767.0f);
#else /* optional dither */
                    int val=mono[j]*32767.f+drand48()-0.5f;
#endif
                    /* might as well guard against clipping */
                    if (val > 32767) {
                        val = 32767;
                    }
                    if (val < -32768) {
                        val=-32768;
                    }
                    *ptr = val;
                    ptr += vorbis_decoder->vi.channels;
                }
            }
            vorbis_decoder->sample_num += bout;

            /* tell libvorbis how many samples we actually consumed */
            vorbis_synthesis_read(&vorbis_decoder->vd, bout);

            if (NULL == vorbis_decoder->decoder.fm) {
                cfg.bits_per_sample = 16;
                cfg.samples_per_frame = SAMPLES_PER_FRAME * vorbis_decoder->vi.channels;
                cfg.frame_count = vorbis_decoder->frame_count;
                vorbis_decoder->decoder.fm = audio_fm_create(&cfg);
                if (vorbis_decoder->decoder.fm == NULL) {
                    loge("audio_fm_create fail!!!\n");
                    return DEC_ERR_NULL_PTR;
                }

                vorbis_decoder->frame_duration = SAMPLES_PER_FRAME * 1000 * 1000 / vorbis_decoder->vi.rate;
            }

           if (vorbis_decoder->sample_num >= SAMPLES_PER_FRAME) {
                vorbis_output_pcm(vorbis_decoder);
            }
        }
    }

    audio_pm_enqueue_empty_packet(vorbis_decoder->decoder.pm, vorbis_decoder->curr_packet);

    return DEC_OK;
}

int __vorbis_decode_control(struct aic_audio_decoder *decoder, int cmd, void *param)
{
    return 0;
}

int __vorbis_decode_reset(struct aic_audio_decoder *decoder)
{
    return 0;
}

struct aic_audio_decoder_ops vorbis_decoder = {
    .name           = "vorbis",
    .init           = __vorbis_decode_init,
    .destroy        = __vorbis_decode_destroy,
    .decode         = __vorbis_decode_frame,
    .control        = __vorbis_decode_control,
    .reset          = __vorbis_decode_reset,
};

struct aic_audio_decoder* create_vorbis_decoder()
{
    struct vorbis_decoder *s = (struct vorbis_decoder*)mpp_alloc(sizeof(struct vorbis_decoder));
    if(s == NULL)
        return NULL;

    memset(s, 0, sizeof(struct vorbis_decoder));
    s->decoder.ops = &vorbis_decoder;

    return &s->decoder;
}
