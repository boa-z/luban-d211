/*
 * Copyright (C) 2020-2026 ArtInChip Technology Co. Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 *  author: <jun.ma@artinchip.com>
 *  Desc: aic_audio_decoder interface
 */

#include "mpp_log.h"
#include "audio_decoder.h"

extern struct aic_audio_decoder* create_mp3_decoder();

#ifdef AAC_DECODER
extern struct aic_audio_decoder* create_aac_decoder();
#endif
#ifdef VORBIS_DECODER
extern struct aic_audio_decoder* create_vorbis_decoder();
#endif
#ifdef OPUS_DECODER
extern struct aic_audio_decoder* create_opus_decoder();
#endif
#ifdef AAC_ELD_DECODER
extern struct aic_audio_decoder* create_aac_eld_decoder();
#endif
#ifdef ALAC_DECODER
extern struct aic_audio_decoder* create_alac_decoder();
#endif
#ifdef APE_DECODER
extern struct aic_audio_decoder* create_ape_decoder();
#endif
#ifdef FLAC_DECODER
extern struct aic_audio_decoder* create_flac_decoder();
#endif

struct aic_audio_decoder* aic_audio_decoder_create(enum aic_audio_codec_type type)
{
	if(type == MPP_CODEC_AUDIO_DECODER_MP3)
		return create_mp3_decoder();
#ifdef AAC_DECODER
	else if(type == MPP_CODEC_AUDIO_DECODER_AAC)
		return create_aac_decoder();
#endif
#ifdef VORBIS_DECODER
	else if(type == MPP_CODEC_AUDIO_DECODER_VORBIS)
		return create_vorbis_decoder();
#endif
#ifdef OPUS_DECODER
	else if(type == MPP_CODEC_AUDIO_DECODER_OPUS)
		return create_opus_decoder();
#endif
#ifdef AAC_ELD_DECODER
	else if(type == MPP_CODEC_AUDIO_DECODER_AAC_ELD)
		return create_aac_eld_decoder();
#endif
#ifdef ALAC_DECODER
	else if(type == MPP_CODEC_AUDIO_DECODER_ALAC)
		return create_alac_decoder();
#endif
#ifdef APE_DECODER
	else if(type == MPP_CODEC_AUDIO_DECODER_APE)
		return create_ape_decoder();
#endif
#ifdef FLAC_DECODER
	else if(type == MPP_CODEC_AUDIO_DECODER_FLAC)
		return create_flac_decoder();
#endif

	return NULL;
}

void aic_audio_decoder_destroy(struct aic_audio_decoder* decoder)
{
	decoder->ops->destroy(decoder);
}

s32 aic_audio_decoder_init(struct aic_audio_decoder *decoder, struct aic_audio_decode_config *config)
{
	return decoder->ops->init(decoder, config);
}

s32 aic_audio_decoder_decode(struct aic_audio_decoder* decoder)
{
	if(decoder->pm && decoder->fm) {
		logw("render frame number: %d, empty frame number: %d", audio_fm_get_ready_frame_num(decoder->fm),
		  audio_fm_get_empty_frame_num(decoder->fm));
	}

	//if(decoder->pm && audio_pm_get_ready_packet_num(decoder->pm) == 0)
		//return DEC_NO_READY_PACKET;

	return decoder->ops->decode(decoder);
}

s32 aic_audio_decoder_control(struct aic_audio_decoder* decoder, int cmd, void *param)
{
	switch (cmd) {
	default:
		break;
	}
	return decoder->ops->control(decoder, cmd, param);
}

s32 aic_audio_decoder_reset(struct aic_audio_decoder* decoder)
{
	return decoder->ops->reset(decoder);
}

s32 aic_audio_decoder_get_packet(struct aic_audio_decoder* decoder, struct mpp_packet* packet, int size)
{
	if(decoder == NULL || packet == NULL)
		return DEC_ERR_NULL_PTR;
	if(audio_pm_get_empty_packet_num(decoder->pm) == 0)
		return DEC_NO_EMPTY_PACKET;

	return audio_pm_dequeue_empty_packet(decoder->pm, packet, size);
}

s32 aic_audio_decoder_put_packet(struct aic_audio_decoder* decoder, struct mpp_packet* packet)
{
	if(decoder == NULL || packet == NULL)
		return DEC_ERR_NULL_PTR;

	return audio_pm_enqueue_ready_packet(decoder->pm, packet);
}

s32 aic_audio_decoder_return_packet(struct aic_audio_decoder* decoder, struct mpp_packet* packet)
{
	if(decoder == NULL || packet == NULL)
		return DEC_ERR_NULL_PTR;

	return audio_pm_return_empty_packet(decoder->pm, packet);
}

s32 aic_audio_decoder_get_frame(struct aic_audio_decoder* decoder, struct aic_audio_frame *frame)
{
	if(decoder == NULL || frame == NULL)
		return DEC_ERR_NULL_PTR;

	if(decoder->fm == NULL) {
		//logw("frame manager not create now, wait");
		return DEC_ERR_FM_NOT_CREATE;
	}
	if(audio_fm_get_ready_frame_num(decoder->fm) == 0)
		return DEC_NO_RENDER_FRAME;

	return audio_fm_dequeue_ready_frame(decoder->fm, frame);
}

s32 aic_audio_decoder_put_frame(struct aic_audio_decoder* decoder, struct aic_audio_frame* frame)
{
	if(decoder == NULL || frame == NULL)
		return DEC_ERR_NULL_PTR;
	return audio_fm_enqueue_empty_frame(decoder->fm, frame);
}
