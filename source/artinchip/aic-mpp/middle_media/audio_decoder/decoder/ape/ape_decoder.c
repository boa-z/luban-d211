/*
 * Copyright (C) 2020-2026 ArtInChip Technology Co. Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Author: <xiaodong.zhao@artinchip.com>
 * Desc: ape_decoder interface
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

#include <ape/decoder.h>

#define BLOCKS_PER_LOOP     1152

#define INPUT_CHUNKSIZE      (20 * 1024)
#define APE_PCM_BUF_SIZE     (BLOCKS_PER_LOOP << 2)
#define APE_MIN_DECODE_SIZE  2048

// #define ENABLE_APE_PCM_DUMP 1

enum ape_decode_state_e {
	APE_STATE_PARSE_HEADER = 0,
	APE_STATE_PARSE_SEEK_TABLE,
	APE_STATE_SEEK_TO_FIRST_FRAME,
	APE_STATE_DECODE_FRAME,
};

#ifndef MIN
#define MIN(a,b) ((a) < (b) ? (a) : (b))
#endif

struct ape_audio_decoder {
	struct aic_audio_decoder decoder;
	struct mpp_packet *curr_packet;

	struct ape_ctx_t ape_ctx;

	unsigned char es_buf[INPUT_CHUNKSIZE];
	int es_data_size;

	int32_t decoded0[BLOCKS_PER_LOOP];
	int32_t decoded1[BLOCKS_PER_LOOP];

	unsigned short pcm_buf[BLOCKS_PER_LOOP << 1];
	unsigned int pcm_samples;

	int first_byte;

	enum ape_decode_state_e state;
	int total_skip_size;
	int seek_table_offset;

	unsigned int frame_id;
	unsigned int frame_count;
	int eos;
	int num_blocks;
	int frame_size;
	int packet_size;
#ifdef ENABLE_APE_PCM_DUMP
	FILE *fp_ape_pcm;
#endif
};

#ifdef ENABLE_APE_PCM_DUMP
void init_pcm_file(struct ape_audio_decoder *ape_decoder) {
	char file_path[64] = "/mnt/sdcard/testAPE.pcm";
	ape_decoder->fp_ape_pcm = fopen(file_path, "wb");
	if (!ape_decoder->fp_ape_pcm) {
		loge("fopen file failed!");
	}
}
#endif

int __ape_decode_init(struct aic_audio_decoder *decoder, struct aic_audio_decode_config *config)
{
	struct ape_audio_decoder *ape_decoder = (struct ape_audio_decoder *)decoder;
	struct audio_frame_manager_cfg cfg;

	ape_decoder->decoder.pm = audio_pm_create(config);
	if (!ape_decoder->decoder.pm) {
		loge("audio_pm_create ape failed.\n");
		return -1;
	}
	ape_decoder->frame_count = config->frame_count;
	ape_decoder->frame_id = 0;
	ape_decoder->pcm_samples = 0;
	ape_decoder->es_data_size = 0;
	ape_decoder->ape_ctx.cur_frame = 0;
	ape_decoder->total_skip_size = 0;
	ape_decoder->seek_table_offset = 0;
	ape_decoder->eos = 0;
	ape_decoder->first_byte = 3;
	ape_decoder->state = APE_STATE_PARSE_HEADER;
	ape_decoder->num_blocks = 0;
	ape_decoder->frame_size = 0;
	ape_decoder->packet_size = 1024;

	cfg.bits_per_sample = 16;
	cfg.samples_per_frame = BLOCKS_PER_LOOP * 4 + 512;   // use to malloc frame buf
	cfg.frame_count = ape_decoder->frame_count;
	ape_decoder->decoder.fm = audio_fm_create(&cfg);
	if (ape_decoder->decoder.fm == NULL) {
		loge("audio_fm_create ape fail!!!\n");
		goto failed;
	}
#ifdef ENABLE_APE_PCM_DUMP
	init_pcm_file(ape_decoder);
#endif
	return 0;

failed:
	if (ape_decoder->decoder.pm) {
		audio_pm_destroy(ape_decoder->decoder.pm);
		ape_decoder->decoder.pm = NULL;
	}

	return -1;
}

int __ape_decode_destroy(struct aic_audio_decoder *decoder)
{
	struct ape_audio_decoder *ape_decoder = (struct ape_audio_decoder *)decoder;
	if (!ape_decoder) {
		return -1;
	}

	if (ape_decoder->decoder.fm) {
		audio_fm_destroy(ape_decoder->decoder.fm);
		ape_decoder->decoder.fm = NULL;
	}

	if (ape_decoder->decoder.pm) {
		audio_pm_destroy(ape_decoder->decoder.pm);
		ape_decoder->decoder.pm = NULL;
	}

	if (ape_decoder->ape_ctx.frames) {
		mpp_free(ape_decoder->ape_ctx.frames);
	}
#ifdef ENABLE_APE_PCM_DUMP
	if (ape_decoder->fp_ape_pcm) {
		fclose(ape_decoder->fp_ape_pcm);
	}
#endif
	mpp_free(ape_decoder);

	return 0;

}

/*return consumed size*/
static int ape_input_data(struct ape_audio_decoder *ape_decoder, char *buf, int size)
{
	if ((NULL == ape_decoder) || (NULL == buf) || (size <= 0)) {
		loge("invalid parameter! [%p/%p/%d]\n", ape_decoder, buf, size);
		return -1;
	}

	if (size > INPUT_CHUNKSIZE - ape_decoder->es_data_size) {
		loge("may drop es data! %d/%d\n", size, INPUT_CHUNKSIZE - ape_decoder->es_data_size);
		size = INPUT_CHUNKSIZE - ape_decoder->es_data_size;
	}

	if (size > 0) {
		memcpy(ape_decoder->es_buf + ape_decoder->es_data_size, buf, size);
		ape_decoder->es_data_size += size;
	}

	return size;
}

static int ape_decode(struct ape_audio_decoder *ape_decoder, unsigned char *buf, int *size)
{
	int used, i, ret;
	struct ape_ctx_t *ape_ctx = NULL;
	int shift = 0, round_val = 0;

	if ((NULL == ape_decoder) || (NULL == buf) || (NULL == size)) {
		loge("<%s:%d> Invalid parameter!\n", __func__, __LINE__);
		return -1;
	}

	ape_ctx = &ape_decoder->ape_ctx;

	if (ape_ctx->new_frame) {
		ape_decoder->frame_size = ape_ctx->frames[ape_ctx->cur_frame].size;

		/* Calculate how many blocks there are in this frame */
		if (ape_ctx->cur_frame == (ape_ctx->totalframes - 1)) {
			ape_decoder->num_blocks = ape_ctx->finalframeblocks;
		} else {
			ape_decoder->num_blocks = ape_ctx->blocksperframe;
		}

		ape_ctx->currentframeblocks = ape_decoder->num_blocks;

		used = 0;
		init_frame_decoder(ape_ctx, buf, &ape_decoder->first_byte, &used);

		if (*size < used) {
			loge("critical error! size:%d used:%d\n", *size, used);
			return -1;
		}

		memmove(buf, buf + used, *size - used);
		*size -= used;
		ape_decoder->frame_size -= used;
		if (ape_decoder->frame_size < 0)
			ape_decoder->frame_size = 0;

		ape_ctx->new_frame = 0;
	}

	int min_required = MIN(ape_decoder->frame_size, 10240);
	if (min_required < APE_MIN_DECODE_SIZE) {
		min_required = APE_MIN_DECODE_SIZE;
	}
	if (*size < min_required) {
		return 1;
	}

	int sub_blocks = MIN(BLOCKS_PER_LOOP, ape_decoder->num_blocks);
	ret = decode_chunk(ape_ctx, buf, &ape_decoder->first_byte,
					&used, ape_decoder->decoded0,
					ape_decoder->decoded1, sub_blocks);
	if (ret < 0) {
		loge("decode err, used:%d\n", used);
		return ret;
	}

	if (*size < used) {
		loge("critical error! size:%d used:%d sub_block:%d block:%d\n", *size, used, sub_blocks, ape_decoder->num_blocks);
		return -1;
	}

	memmove(buf, buf + used, *size - used);
	*size -= used;
	ape_decoder->frame_size -= used;
	if (ape_decoder->frame_size < 0)
		ape_decoder->frame_size = 0;

	/* Convert the output samples to WAV format and write to output file */
	shift = ape_ctx->bps - 16;
	round_val = shift > 0 ? (1 << (shift - 1)) : 0;
	if (shift < 0) {
		shift = 0;
	}
	for (i = 0; i < sub_blocks; i++) {
		if (1 == ape_ctx->channels) {
			unsigned int idx = ape_decoder->pcm_samples;
			if (idx >= (BLOCKS_PER_LOOP << 1)) {
				logw("pcm_buf index out of range: %d", idx);
				break;
			}
			if (ape_ctx->bps == 8) {
				ape_decoder->pcm_buf[idx] = (ape_decoder->decoded0[i] + 0x80) & 0xff;
			} else {
				ape_decoder->pcm_buf[idx] = (unsigned short)((ape_decoder->decoded0[i] + round_val) >> shift);
			}
		} else if (2 == ape_ctx->channels) {
			unsigned int idx = ape_decoder->pcm_samples * 2;
			if (idx + 1 >= (BLOCKS_PER_LOOP << 1)) {
				logw("pcm_buf index out of range: %d", idx + 1);
				break;
			}
			if (ape_ctx->bps == 8) {
				ape_decoder->pcm_buf[idx] = (ape_decoder->decoded0[i] + 0x80) & 0xff;
				ape_decoder->pcm_buf[idx + 1] = (ape_decoder->decoded1[i] + 0x80) & 0xff;
			} else {
				ape_decoder->pcm_buf[idx] = (unsigned short)((ape_decoder->decoded0[i] + round_val) >> shift);
				ape_decoder->pcm_buf[idx + 1] = (unsigned short)((ape_decoder->decoded1[i] + round_val) >> shift);
			}
		}
		ape_decoder->pcm_samples++;
	}

	/* Decrement the block count */
	ape_decoder->num_blocks -= sub_blocks;
	if (ape_decoder->num_blocks <= 0) {
		ape_ctx->new_frame = 1;
		ape_ctx->cur_frame++;
	}

	return 0;
}

static int ape_output_pcm(struct ape_audio_decoder *ape_decoder, struct aic_audio_frame *frame, u64 pts)
{
	int size;

	if (!ape_decoder || !frame) {
		loge("invalid parameter [%p/%p]\n", ape_decoder, frame);
		return DEC_ERR_NULL_PTR;
	}

	if (ape_decoder->pcm_samples < BLOCKS_PER_LOOP) {
		frame->channels = 0;
		frame->sample_rate = 0;
		frame->pts = 0;
		frame->bits_per_sample = 0;
		frame->id = 0;
		frame->flag = 0;
		frame->size = 0;

		return DEC_NO_RENDER_FRAME;
	}

	size = BLOCKS_PER_LOOP * 2 * ape_decoder->ape_ctx.channels;
	memcpy(frame->data, ape_decoder->pcm_buf, size);
#ifdef ENABLE_APE_PCM_DUMP
	if (ape_decoder->fp_ape_pcm) {
		size_t write_size = fwrite(ape_decoder->pcm_buf, 1, size, ape_decoder->fp_ape_pcm);
		if (write_size != size) {
			loge("write_size != pcm_data_size\n");
			return DEC_ERR_NOT_SUPPORT;
		} else {
			printf("channels = %d, sample_rate = %d, pcm_samples = %d\n",
			frame->channels, frame->sample_rate, ape_decoder->pcm_samples);
		}
		fflush(ape_decoder->fp_ape_pcm);
	}
#endif
	ape_decoder->pcm_samples -= BLOCKS_PER_LOOP;
	if (ape_decoder->pcm_samples > 0) {
		memmove(ape_decoder->pcm_buf,
				ape_decoder->pcm_buf + BLOCKS_PER_LOOP * ape_decoder->ape_ctx.channels,
				ape_decoder->pcm_samples * 2 * ape_decoder->ape_ctx.channels);
	}

	frame->channels = ape_decoder->ape_ctx.channels;
	frame->sample_rate = ape_decoder->ape_ctx.samplerate;
	frame->pts = pts;
	frame->bits_per_sample = 16;
	frame->id = ape_decoder->frame_id++;
	frame->flag = ape_decoder->eos ? FRAME_FLAG_EOS : 0;
	frame->size = size;

	return 0;
}

int __ape_decode_frame(struct aic_audio_decoder *decoder)
{
	int ret = 0, packet_num = 0;
	struct aic_audio_frame *frame = NULL;
	struct ape_audio_decoder *ape_decoder = (struct ape_audio_decoder *)decoder;
	u64 pts = 0;

	if (INPUT_CHUNKSIZE - ape_decoder->es_data_size > ape_decoder->packet_size) {
		packet_num = audio_pm_get_ready_packet_num(ape_decoder->decoder.pm);
		if (packet_num == 0) {
			loge("no packet!\n");
			return DEC_NO_READY_PACKET;
		}

		ape_decoder->curr_packet = audio_pm_dequeue_ready_packet(ape_decoder->decoder.pm);
		if (!ape_decoder->curr_packet) {
			loge("get packet error!\n");
			return DEC_NO_READY_PACKET;
		}

		ape_decoder->eos = ape_decoder->curr_packet->flag;

		ape_input_data(ape_decoder, ape_decoder->curr_packet->data, ape_decoder->curr_packet->size);
		pts = ape_decoder->curr_packet->pts;
		int pkt_size = ape_decoder->curr_packet->size;
		audio_pm_enqueue_empty_packet(ape_decoder->decoder.pm, ape_decoder->curr_packet);
		ape_decoder->curr_packet = NULL;

		if (ape_decoder->packet_size != pkt_size) {
			ape_decoder->packet_size = pkt_size;
		}
	}

	if (APE_STATE_PARSE_HEADER == ape_decoder->state) {
		ret = ape_parseheaderbuf(&ape_decoder->ape_ctx, ape_decoder->es_buf, ape_decoder->es_data_size);
		if (ret >= 0) {
			ape_decoder->seek_table_offset = ret;
			ape_decoder->state = APE_STATE_PARSE_SEEK_TABLE;
		} else {
			loge("parser header error!\n");
			return DEC_OK;
		}
	}

	if (APE_STATE_PARSE_SEEK_TABLE == ape_decoder->state) {
		ret = ape_parse_seek_table(&ape_decoder->ape_ctx,
								   ape_decoder->es_buf,
								   ape_decoder->es_data_size,
								   ape_decoder->seek_table_offset);
		if (0 == ret) {
			ape_decoder->state = APE_STATE_SEEK_TO_FIRST_FRAME;
			ape_decoder->ape_ctx.new_frame = 1;
		} else {
			loge("ape_parse_seek_table error!\n");
			return DEC_OK;
		}
	}

	/* skip junk data, seek to first frame */
	if (APE_STATE_SEEK_TO_FIRST_FRAME == ape_decoder->state) {
		if (ape_decoder->ape_ctx.firstframe > ape_decoder->total_skip_size) {
			int skip = ape_decoder->ape_ctx.firstframe - ape_decoder->total_skip_size;
			if (skip > ape_decoder->es_data_size) {
				ape_decoder->total_skip_size += ape_decoder->es_data_size;
				ape_decoder->es_data_size = 0;
			} else {
				ape_decoder->total_skip_size += skip;
				memmove(ape_decoder->es_buf, ape_decoder->es_buf + skip, ape_decoder->es_data_size - skip);
				ape_decoder->es_data_size = ape_decoder->es_data_size - skip;
			}
		} else {
			ape_decoder->state = APE_STATE_DECODE_FRAME;
		}
	}

	if ((APE_STATE_DECODE_FRAME == ape_decoder->state) && (ape_decoder->pcm_samples < BLOCKS_PER_LOOP)) {
		int dec_ret = ape_decode(ape_decoder, ape_decoder->es_buf, &ape_decoder->es_data_size);
		if (dec_ret < 0) {
			loge("ape_decode error, skip frame %d\n", ape_decoder->ape_ctx.cur_frame);
			ape_decoder->ape_ctx.cur_frame++;
			ape_decoder->ape_ctx.new_frame = 1;
			ape_decoder->num_blocks = 0;
			ape_decoder->frame_size = 0;
			ape_decoder->es_data_size = 0;
			ape_decoder->pcm_samples = 0;
		}
	}

	if (ape_decoder->ape_ctx.cur_frame == ape_decoder->ape_ctx.totalframes) {
		ape_decoder->eos = 1;
	}

	/* output pcm data */
	if (ape_decoder->pcm_samples >= BLOCKS_PER_LOOP) {

		if (!ape_decoder->decoder.fm) {
			loge("frame manager not create!\n");
			return DEC_ERR_FM_NOT_CREATE;
		}

		frame = audio_fm_dequeue_empty_frame(ape_decoder->decoder.fm);
		if (!frame) {
			loge("no empty frame!\n");
			return DEC_NO_EMPTY_FRAME;
		}

		ape_output_pcm(ape_decoder, frame, pts);
		audio_fm_enqueue_ready_frame(ape_decoder->decoder.fm, frame);
	}

	return DEC_OK;
}

int __ape_decode_control(struct aic_audio_decoder *decoder, int cmd, void *param)
{
	return 0;
}

int __ape_decode_reset(struct aic_audio_decoder *decoder)
{
	struct ape_audio_decoder *ape_decoder = (struct ape_audio_decoder *)decoder;

	audio_pm_reset(ape_decoder->decoder.pm);
	audio_fm_reset(ape_decoder->decoder.fm);
	return 0;
}

struct aic_audio_decoder_ops ape_decoder = {
	.name           = "ape",
	.init           = __ape_decode_init,
	.destroy        = __ape_decode_destroy,
	.decode         = __ape_decode_frame,
	.control        = __ape_decode_control,
	.reset          = __ape_decode_reset,
};

struct aic_audio_decoder* create_ape_decoder(void)
{
	struct ape_audio_decoder *s = (struct ape_audio_decoder*)mpp_alloc(sizeof(struct ape_audio_decoder));
	if (s == NULL) {
		return NULL;
	}
	memset(s, 0, sizeof(struct ape_audio_decoder));
	s->decoder.ops = &ape_decoder;
	return &s->decoder;
}
