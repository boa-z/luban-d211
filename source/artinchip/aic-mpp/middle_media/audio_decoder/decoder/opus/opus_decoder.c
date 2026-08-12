/*
 * Copyright (C) 2020-2025 ArtInChip Technology Co. Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 *  author: <xiaodong.zhao@artinchip.com>
 *  Desc: opus decoder interface
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
#include "opus/opus.h"
#include "opus/opus_types.h"
#include "opus/opus_multistream.h"


#define OD_BUF_SIZE (2 * 120 * 48)

/**The maximum number of channels in an Ogg Opus stream.*/
#define OPUS_CHANNEL_COUNT_MAX (255)

#define OP_NCHANNELS_MAX (8)

#define OP_MIN(_a, _b) ((_a) < (_b) ? (_a) : (_b))
#define OP_MAX(_a, _b) ((_a) > (_b) ? (_a) : (_b))
#define OP_CLAMP(_lo, _x, _hi) (OP_MAX(_lo, OP_MIN(_x, _hi)))

struct opus_head {
    /**
     The Ogg Opus format version, in the range 0...255.
     The top 4 bits represent a "major" version, and the bottom four bits
     represent backwards-compatible "minor" revisions.
     The current specification describes version 1.
     This library will recognize versions up through 15 as backwards compatible
     with the current specification.
     An earlier draft of the specification described a version 0, but the only
     difference between version 1 and version 0 is that version 0 did
     not specify the semantics for handling the version field.
    */
    int version;
    /* The number of channels, in the range 1...255. */
    int channel_count;
    /* The number of samples that should be discarded from the beginning of the stream. */
    unsigned int pre_skip;
    /**
     The sampling rate of the original input.
     All Opus audio is coded at 48 kHz, and should also be decoded at 48 kHz
     for playback (unless the target hardware does not support this sampling
     rate).
     However, this field may be used to resample the audio back to the original
     sampling rate, for example, when saving the output to a file.
    */
    opus_uint32 input_sample_rate;
    /**
     The gain to apply to the decoded output, in dB, as a Q8 value in the range
     -32768...32767.
     The <tt>libopusfile</tt> API will automatically apply this gain to the
     decoded output before returning it, scaling it by
     <code>pow(10,output_gain/(20.0*256))</code>.
     You can adjust this behavior with op_set_gain_offset().
    */
    int output_gain;
    /**
     The channel mapping family, in the range 0...255.
     Channel mapping family 0 covers mono or stereo in a single stream.
     Channel mapping family 1 covers 1 to 8 channels in one or more streams,
     using the Vorbis speaker assignments.
     Channel mapping family 255 covers 1 to 255 channels in one or more
     streams, but without any defined speaker assignment.
    */
    int mapping_family;
    /* The number of Opus streams in each Ogg packet, in the range 1...255. */
    int stream_count;
    /**
     The number of coupled Opus streams in each Ogg packet, in the range
     0...127.
     This must satisfy <code>0 <= coupled_count <= stream_count</code> and
     <code>coupled_count + stream_count <= 255</code>.
     The coupled streams appear first, before all uncoupled streams, in an Ogg
     Opus packet.
    */
    int coupled_count;
    /**
     The mapping from coded stream channels to output channels.
     Let <code>index=mapping[k]</code> be the value for channel <code>k</code>.
     If <code>index<2*coupled_count</code>, then it refers to the left channel
     from stream <code>(index/2)</code> if even, and the right channel from
     stream <code>(index/2)</code> if odd.
     Otherwise, it refers to the output of the uncoupled stream
     <code>(index-coupled_count)</code>.
    */
    unsigned char mapping[OPUS_CHANNEL_COUNT_MAX];
};

struct opus_tags {
    char **user_comments; /* The array of comment string vectors. */
    int *comment_lengths; /**An array of the corresponding length of each vector, in bytes.*/
    int comments;         /**The total number of comment streams.*/
    char *vendor;         /**The null-terminated vendor string. This identifies the software used to encode the stream.*/
};

struct opus_decoder {
    struct aic_audio_decoder decoder;
    struct mpp_packet *curr_packet;

    OpusMSDecoder *od; /*Central working state for the packet-to-PCM decoder.*/
    struct opus_head opus_head;
    struct opus_tags opus_tags;
    opus_int16 od_buf[OD_BUF_SIZE];
    int od_buf_size;
    int od_buf_pos;

    int es_cnt;
    int frame_duration;
    int opus_header_init_flag;
    int frame_id;
    int frame_count;
};

/**
  Matrices for downmixing from the supported channel counts to stereo.
  The matrices with 5 or more channels are normalized to a total volume of 2.0,
  since most mixes sound too quiet if normalized to 1.0 (as there is generally
  little volume in the side/rear channels).
  Hence we keep the coefficients in Q14, so the downmix values won't overflow a
   32-bit number.
*/
static const opus_int16 OP_STEREO_DOWNMIX_Q14[OP_NCHANNELS_MAX - 2][OP_NCHANNELS_MAX][2] = {
    /*3.0*/
    {
        {9598, 0}, {6786, 6786}, {0, 9598}
    },
    /*quadrophonic*/
    {
        {6924, 0}, {0, 6924}, {5996, 3464}, {3464, 5996}
    },
    /*5.0*/
    {
        {10666, 0}, {7537, 7537}, {0, 10666}, {9234, 5331}, {5331, 9234}
    },
    /*5.1*/
    {
        {8668, 0}, {6129, 6129}, {0, 8668}, {7507, 4335}, {4335, 7507}, {6129, 6129}
    },
    /*6.1*/
    {
        {7459, 0}, {5275, 5275}, {0, 7459}, {6460, 3731}, {3731, 6460}, {4568, 4568}, {5275, 5275}
    },
    /*7.1*/
    {
        {6368, 0}, {4502, 4502}, {0, 6368}, {5515, 3183}, {3183, 5515}, {5515, 3183}, {3183, 5515}, {4502, 4502}
    }
};

static unsigned op_parse_uint16le(const unsigned char *_data)
{
    return _data[0] | _data[1] << 8;
}

static int op_parse_int16le(const unsigned char *_data)
{
    int ret;
    ret = _data[0] | _data[1] << 8;
    return (ret ^ 0x8000) - 0x8000;
}

static opus_uint32 op_parse_uint32le(const unsigned char *_data)
{
    return _data[0] | (opus_uint32)_data[1] << 8 |
          (opus_uint32)_data[2] << 16 | (opus_uint32)_data[3] << 24;
}

int opus_head_parse(struct opus_head *_head, unsigned char *_data, int _len)
{
    struct opus_head head = {0};

    if ((NULL == _head) || (NULL == _data) || (_len <= 0))  {
        loge("invalid parameter [%p/%p/%d]\n", _head, _data, _len);
        return -1;
    }

    if (_len < 8) {
        return -1;
    }

    if (memcmp(_data, "OpusHead", 8) != 0) {
        return -1;
    }

    if (_len < 9) {
        return -1;
    }

    head.version = _data[8];
    if (head.version > 15) {
        return -1;
    }

    if (_len < 19) {
        return -1;
    }

    head.channel_count = _data[9];
    head.pre_skip = (unsigned int)op_parse_uint16le(_data + 10);
    head.input_sample_rate = op_parse_uint32le(_data + 12);
    head.output_gain = op_parse_int16le(_data + 16);
    head.mapping_family = _data[18];
    if (head.mapping_family == 0) {
        if (head.channel_count < 1 || head.channel_count > 2) {
            return -1;
        }

        if (head.version <= 1 && _len > 19) {
            return -1;
        }

        head.stream_count = 1;
        head.coupled_count = head.channel_count - 1;
        if (_head != NULL) {
            _head->mapping[0] = 0;
            _head->mapping[1] = 1;
        }
    } else if (head.mapping_family == 1) {
        size_t size;
        int    ci;
        if (head.channel_count < 1 || head.channel_count > 8) {
            return -1;
        }

        size = 21 + head.channel_count;
        if (_len < size || (head.version <= 1 && _len > size)) {
            return -1;
        }

        head.stream_count = _data[19];
        if (head.stream_count < 1) {
            return -1;
        }

        head.coupled_count = _data[20];
        if (head.coupled_count > head.stream_count) {
            return -1;
        }

        for (ci = 0; ci < head.channel_count; ci++) {
            if (_data[21 + ci] >= head.stream_count + head.coupled_count && _data[21 + ci] != 255) {
                return -1;
            }
        }

        if (_head != NULL) {
            memcpy(_head->mapping, _data + 21, head.channel_count);
        }
    /**
      General purpose players should not attempt to play back content with
      channel mapping family 255.
    */
    } else if (head.mapping_family == 255) {
        return -1;
    /* No other channel mapping families are currently defined. */
    } else {
        return -1;
    }

    if (_head != NULL) {
        memcpy(_head, &head, head.mapping - (unsigned char *)&head);
    }

    return 0;
}

static char *op_strdup_with_len(const char *_s, int _len)
{
    size_t size;
    char *ret;

    size = sizeof(*ret) * (_len + 1);
    if (size < _len) {
        return NULL;
    }

    ret = (char *)mpp_alloc(size);
    if (ret != NULL) {
        memcpy(ret, _s, sizeof(*ret) * _len);
        ret[_len] = '\0';
    }

    return ret;
}

static int op_tags_ensure_capacity(struct opus_tags *_tags, int _ncomments)
{
    char **user_comments;
    int *comment_lengths;
    int cur_ncomments;
    int size;

    if (_ncomments >= (size_t)INT_MAX) {
        return -1;
    }

    size = sizeof(*_tags->comment_lengths) * (_ncomments + 1);
    if (size / sizeof(*_tags->comment_lengths) != _ncomments + 1) {
        return -1;
    }

    cur_ncomments = _tags->comments;
    /**
      We only support growing.
      Trimming requires cleaning up the allocated strings in the old space, and
      is best handled separately if it's ever needed.
    */
    if (_ncomments < cur_ncomments) {
        return -1;
    }

    comment_lengths = (int *)mpp_realloc(_tags->comment_lengths, size);
    if (comment_lengths == NULL) {
        return -1;
    }

    if (_tags->comment_lengths == NULL) {
        if (cur_ncomments != 0) {
            return -1;
        }
        comment_lengths[cur_ncomments] = 0;
    }

    comment_lengths[_ncomments] = comment_lengths[cur_ncomments];
    _tags->comment_lengths = comment_lengths;
    size = sizeof(*_tags->user_comments) * (_ncomments + 1);
    if (size / sizeof(*_tags->user_comments) != _ncomments + 1) {
        return -1;
    }

    user_comments = (char **)mpp_realloc(_tags->user_comments, size);
    if (user_comments == NULL) {
        return -1;
    }

    if (_tags->user_comments == NULL) {
        if (cur_ncomments != 0) {
            return -1;
        }
        user_comments[cur_ncomments] = NULL;
    }
    user_comments[_ncomments] = user_comments[cur_ncomments];
    _tags->user_comments = user_comments;

    return 0;
}

void opus_tags_clear(struct opus_tags *_tags)
{
    int ncomments;
    int ci;

    if (NULL == _tags) {
        loge("invalid parameter [%p]", _tags);
        return;
    }

    ncomments = _tags->comments;

    if (_tags->user_comments != NULL) {
        ncomments++;
    } else {
        if (ncomments != 0) {
            return;
        }
    }

    for (ci = ncomments; ci-- > 0;) {
        if (_tags->user_comments) {
            if (_tags->user_comments[ci]) {
                mpp_free(_tags->user_comments[ci]);
                _tags->user_comments[ci] = NULL;
            }
        }
    }

    if (_tags->user_comments) {
        mpp_free(_tags->user_comments);
        _tags->user_comments = NULL;
    }

    if (_tags->comment_lengths) {
        mpp_free(_tags->comment_lengths);
        _tags->comment_lengths = NULL;
    }

    if (_tags->vendor) {
        mpp_free(_tags->vendor);
        _tags->vendor = NULL;
    }
}

static int opus_tags_parse(struct opus_tags *_tags, unsigned char *_data, int _len)
{
    opus_uint32 count;
    int len;
    int ncomments;
    int ci;
    int ret;

    if ((NULL == _tags) || (NULL == _data) || (_len <= 0)) {
        loge("invalid parameter [%p/%p/%d]\n", _tags, _data, _len);
        return -1;
    }

    len = _len;
    if (len < 8) {
        return -1;
    }

    if (memcmp(_data, "OpusTags", 8) != 0) {
        return -1;
    }

    if (len < 16) {
        return -1;
    }

    _data += 8;
    len -= 8;
    count = op_parse_uint32le(_data);
    _data += 4;
    len -= 4;
    if (count > len) {
        return -1;
    }

    _tags->vendor = op_strdup_with_len((char *)_data, count);
    if (_tags->vendor == NULL) {
        return -1;
    }

    _data += count;
    len -= count;
    if (len < 4) {
        return -1;
    }

    count = op_parse_uint32le(_data);
    _data += 4;
    len -= 4;
    /*Check to make sure there's minimally sufficient data left in the packet.*/
    if (count > len >> 2) {
        return -1;
    }

    /*Check for overflow (the API limits this to an int).*/
    if (count > (opus_uint32)INT_MAX - 1) {
        return -1;
    }

    ret = op_tags_ensure_capacity(_tags, count);
    if (ret < 0) {
        return ret;
    }

    ncomments = (int)count;
    for (ci = 0; ci < ncomments; ci++) {
        /* Check to make sure there's minimally sufficient data left in the packet. */
        if ((ncomments - ci) > len >> 2) {
            return -1;
        }

        count = op_parse_uint32le(_data);
        _data += 4;
        len -= 4;
        if (count > len) {
            return -1;
        }

        /* Check for overflow (the API limits this to an int). */
        if (count > (opus_uint32)INT_MAX) {
            return -1;
        }

        if (_tags != NULL) {
            _tags->user_comments[ci] = op_strdup_with_len((char *)_data, count);
            if (_tags->user_comments[ci] == NULL) {
                return -1;
            }
            _tags->comment_lengths[ci] = (int)count;
            _tags->comments = ci + 1;
            /*Needed by opus_tags_clear() if we fail before parsing the (optional)
               binary metadata.*/
            _tags->user_comments[ci + 1] = NULL;
        }
        _data += count;
        len -= count;
    }

    if (len > 0 && (_data[0] & 1)) {
        if (len > (opus_uint32)INT_MAX) {
            return -1;
        }

        if (_tags != NULL) {
            _tags->user_comments[ncomments] = (char *)mpp_alloc(len);
            if (_tags->user_comments[ncomments] == NULL) {
                return -1;
            }
            memcpy(_tags->user_comments[ncomments], _data, len);
            _tags->comment_lengths[ncomments] = (int)len;
        }
    }

    return 0;
}

static int op_get_packet_duration(const unsigned char *_data, int _len)
{
    int nframes;
    int frame_size;
    int nsamples;

    if ((NULL ==  _data) || (_len <= 0)) {
        loge("invalid parameter [%p/%d]", _data, _len);
        return -1;
    }

    nframes = opus_packet_get_nb_frames(_data, _len);
    if (nframes < 0) {
        return -1;
    }

    frame_size = opus_packet_get_samples_per_frame(_data, 48000);
    nsamples = nframes * frame_size;
    if (nsamples > 120 * 48) {
        return -1;
    }

    return nsamples;
}

int __opus_decode_init(struct aic_audio_decoder *decoder, struct aic_audio_decode_config *config)
{
    struct opus_decoder *opus_decoder = NULL;

    if ((NULL == decoder) || (NULL == config)) {
        loge("invalid parameter [%p/%p]", decoder, config);
        return -1;
    }

    opus_decoder = (struct opus_decoder *)decoder;
    opus_decoder->decoder.pm = audio_pm_create(config);
    opus_decoder->frame_count = config->frame_count;

    opus_decoder->es_cnt = 0;
    opus_decoder->opus_header_init_flag = 0;

    opus_decoder->frame_id = 0;

    opus_decoder->od_buf_pos = 0;
    opus_decoder->od_buf_size = sizeof(opus_int16) * OD_BUF_SIZE;	// for 2 channel pcm data

    return 0;
}

int __opus_decode_destroy(struct aic_audio_decoder *decoder)
{
    struct opus_decoder *opus_decoder = (struct opus_decoder *)decoder;

    if (NULL == opus_decoder) {
        loge("invalid parameter [%p]", opus_decoder);
        return -1;
    }

    opus_multistream_decoder_destroy(opus_decoder->od);

    if (opus_decoder->decoder.pm) {
        audio_pm_destroy(opus_decoder->decoder.pm);
        opus_decoder->decoder.pm = NULL;
    }
    if (opus_decoder->decoder.fm) {
        audio_fm_destroy(opus_decoder->decoder.fm);
        opus_decoder->decoder.fm = NULL;
    }

    mpp_free(opus_decoder);

    return 0;
}

static void print_opus_info(struct opus_head *opus_head)
{
    if (NULL == opus_head) {
        return;
    }

    logd("-------------------------opus info----------------------\n");
    logd("version:%d\n", opus_head->version);
    logd("ch:%d\n", opus_head->channel_count);
    logd("pre_skip:%d\n", opus_head->pre_skip);
    logd("rate:%d\n", opus_head->input_sample_rate);
    logd("output_gain:%d\n", opus_head->output_gain);
    logd("mapping_family:%d\n", opus_head->mapping_family);
    logd("stream_count:%d\n", opus_head->stream_count);
    logd("coupled_count:%d\n", opus_head->coupled_count);
    logd("-------------------------------------------------------\n");
}

static int opus_header_init(struct opus_decoder *opus_decoder)
{
    int ret, size;
    struct opus_head* opus_head = NULL;
    unsigned char *data = NULL;

    if (NULL == opus_decoder) {
        loge("invalid parameter!");
        return -1;
    }

    opus_head = &opus_decoder->opus_head;
    data = opus_decoder->curr_packet->data;
    size = opus_decoder->curr_packet->size;

    if (0 == opus_decoder->opus_header_init_flag) {
        ret = opus_head_parse(opus_head, data, size);
        if (ret >= 0) {
            print_opus_info(opus_head);
            opus_decoder->opus_header_init_flag = 1;
        }
    } else if (1 == opus_decoder->opus_header_init_flag) {
        ret = opus_tags_parse(&opus_decoder->opus_tags, data, size);
        if (ret < 0) {
            loge("opus_tags_parse error, ret:%d\n", ret);
            opus_tags_clear(&opus_decoder->opus_tags);
        } else {
            logd("Encoded by: %s\n", opus_decoder->opus_tags.vendor);
            opus_decoder->opus_header_init_flag = 2;

            opus_decoder->od = opus_multistream_decoder_create(48000, opus_head->channel_count,
                    opus_head->stream_count, opus_head->coupled_count, opus_head->mapping, &ret);
            if (opus_decoder->od == NULL) {
                loge("opus_multistream_decoder_create error! ret:%d\n", ret);
                return -1;
            }
        }
    }

    audio_pm_enqueue_empty_packet(opus_decoder->decoder.pm, opus_decoder->curr_packet);

    return 0;
}


static int opus_output_pcm(struct opus_decoder *opus_decoder, int sample_num)
{
    int pcm_data_size = 0, channel = 0;
    struct aic_audio_frame *frame = NULL;

    if ((NULL == opus_decoder) || (sample_num <= 0)) {
        loge("invalid parameter [%p %d]", opus_decoder, sample_num);
        return -1;
    }

    frame = audio_fm_dequeue_empty_frame(opus_decoder->decoder.fm);
    if (NULL == frame) {
        loge("audio_fm_dequeue_empty_frame fail!");
        return -1;
    }

    frame->channels = opus_decoder->opus_head.channel_count;
    frame->sample_rate = opus_decoder->opus_head.input_sample_rate;
    frame->pts = opus_decoder->frame_id * opus_decoder->frame_duration;
    frame->bits_per_sample = 16;
    frame->id = opus_decoder->frame_id++;

    if (opus_decoder->curr_packet->flag & PACKET_FLAG_EOS) {
        frame->flag |= PACKET_FLAG_EOS;
        logd("opus_decoder last packet!!!!\n");
    }

    channel = opus_decoder->opus_head.channel_count;
    if (channel > 2) {
        channel = 2;
    }
    pcm_data_size = sample_num * 2 * channel;
    if (opus_decoder->opus_head.channel_count <= 2) {
        memcpy(frame->data, opus_decoder->od_buf, pcm_data_size);
    } else {
        opus_int16 *dst = (opus_int16 *)frame->data;
        for (int i = 0; i < sample_num; i++) {
            opus_int32 l = 0;
            opus_int32 r = 0;
            int ci;
            int ch = opus_decoder->opus_head.channel_count;
            for (ci = 0; ci < ch; ci++) {
                opus_int32 s;
                int index = ch * i + ci;
                if (index < OD_BUF_SIZE) {
                    s = opus_decoder->od_buf[index];
                    l += OP_STEREO_DOWNMIX_Q14[ch - 3][ci][0] * s;
                    r += OP_STEREO_DOWNMIX_Q14[ch - 3][ci][1] * s;
                } else {
                    loge("od_buf too smal, need:%d actual:%d", index, OD_BUF_SIZE);
                }
            }
            /*TODO: For 5 or more channels, we should do soft clipping here.*/
            dst[(i << 1) + 0] = (opus_int16)OP_CLAMP(-32768, l + (8192 >> 14), 32767);
            dst[(i << 1) + 1] = (opus_int16)OP_CLAMP(-32768, r + (8192 >> 14), 32767);
        }
    }

    if (audio_fm_enqueue_ready_frame(opus_decoder->decoder.fm, frame) != 0) {
        loge("audio_fm_enqueue_ready_frame fail!\n");
    }

    return 0;
}

int __opus_decode_frame(struct aic_audio_decoder *decoder)
{
    int sample_per_frame, size, duration;
    unsigned char *data = NULL;
    struct audio_frame_manager_cfg cfg = {0};
    struct opus_decoder *opus_decoder = (struct opus_decoder *)decoder;

    if (NULL == opus_decoder) {
        loge("invalid parameter [%p]", opus_decoder);
        return -1;
    }

    if (0 == audio_pm_get_ready_packet_num(opus_decoder->decoder.pm)) {
        return DEC_NO_READY_PACKET;
    }

    if ((opus_decoder->decoder.fm) && (audio_fm_get_empty_frame_num(opus_decoder->decoder.fm)) == 0) {
        return DEC_NO_EMPTY_FRAME;
    }

    opus_decoder->curr_packet = audio_pm_dequeue_ready_packet(opus_decoder->decoder.pm);
    if (!opus_decoder->curr_packet) {
        return DEC_NO_READY_PACKET;
    }

    // process header
    if (2 != opus_decoder->opus_header_init_flag) {
        return opus_header_init(opus_decoder);
    }

    // start decode
    data = opus_decoder->curr_packet->data;
    size = opus_decoder->curr_packet->size;
    duration = op_get_packet_duration(data, size);
    sample_per_frame = opus_multistream_decode(opus_decoder->od, data, size, opus_decoder->od_buf, duration, 0);
    if (sample_per_frame < 0) {
        loge("decode error!\n");
        return DEC_ERR_NULL_PTR;
    }

    if (NULL == opus_decoder->decoder.fm) {
        int channel = opus_decoder->opus_head.channel_count;
        if (channel > 2) {      // only support 2 channel
            channel = 2;
        }
        cfg.bits_per_sample = 16;
        cfg.samples_per_frame = sample_per_frame * channel;
        cfg.frame_count = opus_decoder->frame_count;
        opus_decoder->decoder.fm = audio_fm_create(&cfg);
        if (opus_decoder->decoder.fm == NULL) {
            loge("audio_fm_create fail!!!\n");
            return DEC_ERR_NULL_PTR;
        }

        opus_decoder->frame_duration = sample_per_frame * 1000 * 1000 / opus_decoder->opus_head.input_sample_rate;
    }

    opus_output_pcm(opus_decoder, sample_per_frame);

    audio_pm_enqueue_empty_packet(opus_decoder->decoder.pm, opus_decoder->curr_packet);

    return DEC_OK;
}

int __opus_decode_control(struct aic_audio_decoder *decoder, int cmd, void *param)
{
    return 0;
}

int __opus_decode_reset(struct aic_audio_decoder *decoder)
{
    return 0;
}

struct aic_audio_decoder_ops opus_decoder = {
    .name       = "opus",
    .init       = __opus_decode_init,
    .destroy    = __opus_decode_destroy,
    .decode     = __opus_decode_frame,
    .control    = __opus_decode_control,
    .reset      = __opus_decode_reset,
};

struct aic_audio_decoder *create_opus_decoder()
{
    struct opus_decoder *s = (struct opus_decoder *)mpp_alloc(sizeof(struct opus_decoder));
    if (NULL == s)
        return NULL;

    memset(s, 0, sizeof(struct opus_decoder));
    s->decoder.ops = &opus_decoder;

    return &s->decoder;
}
