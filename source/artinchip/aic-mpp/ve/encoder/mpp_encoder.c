/*
 * Copyright (C) 2020-2026 ArtInChip Technology Co. Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Author: <qi.xu@artinchip.com>
 * Desc:  mpp_encoder interface
 */

#include "mpp_dec_type.h"
#include "mpp_encoder.h"
#include "mpp_codec.h"
#include "mpp_log.h"
#include "enc_frame_manager.h"
#include "enc_packet_manager.h"

extern struct mpp_encoder *create_jpeg_encoder();
#ifdef H264_ENCODER_ENABLE
extern struct mpp_encoder *create_h264_encoder();
#endif

struct mpp_encoder *mpp_encoder_create(enum mpp_codec_type type)
{
	struct mpp_encoder *encoder = NULL;

	if (type == MPP_CODEC_VIDEO_ENCODER_MJPEG)
		encoder = create_jpeg_encoder();
#ifdef H264_ENCODER_ENABLE
	else if (type == MPP_CODEC_VIDEO_ENCODER_H264)
		encoder = create_h264_encoder();
#endif
	if (!encoder)
		return NULL;

	encoder->fm = enc_frame_manager_create();
	if (!encoder->fm) {
		encoder->ops->destory(encoder);
		return NULL;
	}

	encoder->release_cb = NULL;
	encoder->user_data = NULL;

	return encoder;
}

void mpp_encoder_destory(struct mpp_encoder *encoder)
{
	if (encoder == NULL)
		return;

	if (encoder->fm)
		enc_frame_manager_destroy(encoder->fm);

	if (encoder->pm)
		enc_pm_destroy(encoder->pm);

	encoder->ops->destory(encoder);
}

int mpp_encoder_init(struct mpp_encoder *encoder, struct encode_config *config)
{
	if (encoder == NULL || config == NULL)
		return ENC_ERR_NULL_PTR;

	return encoder->ops->init(encoder, config);
}

int mpp_encoder_put_frame(struct mpp_encoder* encoder, struct mpp_frame* frame)
{
	if (encoder == NULL || frame == NULL)
		return ENC_ERR_NULL_PTR;

	return enc_frame_buffer_put(encoder->fm, frame);
}

int mpp_encoder_set_callback(struct mpp_encoder* encoder,
	mpp_encoder_release_frame_callback callback, void* user_data)
{
	if (encoder == NULL)
		return ENC_ERR_NULL_PTR;

	encoder->release_cb = callback;
	encoder->user_data = user_data;

	enc_frame_set_release_callback(encoder->fm, callback, user_data);

	return 0;
}

int mpp_encoder_encode(struct mpp_encoder* encoder)
{
	struct mpp_frame *frame;
	struct enc_packet enc_pkt;
	int ret;

	if (encoder == NULL)
		return ENC_ERR_NULL_PTR;

	frame = enc_frame_buffer_get(encoder->fm);
	if (!frame)
		return ENC_ERR_NULL_PTR;

	ret = enc_pm_dequeue_empty_packet(encoder->pm, &enc_pkt, 1024 * 1024);
	if (ret < 0) {
		loge("enc_pm_dequeue_empty_packet ret: %d", ret);
		enc_frame_buffer_return(encoder->fm, frame);
		return ENC_ERR_NULL_PTR;
	}

	ret = encoder->ops->encode(encoder, frame, &enc_pkt);

	if (ret == 0)
		enc_pm_enqueue_ready_packet(encoder->pm, &enc_pkt);

	enc_frame_buffer_return(encoder->fm, frame);

	return ret;
}

int mpp_encoder_get_packet(struct mpp_encoder* encoder, struct mpp_packet *packet)
{
	if (encoder == NULL || packet == NULL)
		return ENC_ERR_NULL_PTR;

	return enc_pm_dequeue_ready_packet(encoder->pm, packet);
}

int mpp_encoder_put_packet(struct mpp_encoder* encoder, struct mpp_packet *packet)
{
	if (encoder == NULL || packet == NULL)
		return ENC_ERR_NULL_PTR;

	return enc_pm_enqueue_empty_packet(encoder->pm, packet);
}

int mpp_encoder_set_parameter(struct mpp_encoder* encoder, enum mpp_enc_cmd_type cmd, void* param)
{
	if (encoder == NULL || param == NULL)
		return ENC_ERR_NULL_PTR;

	return encoder->ops->set_parameter(encoder, cmd, param);
}

int mpp_encoder_get_parameter(struct mpp_encoder* encoder, enum mpp_enc_cmd_type cmd, void* param)
{
	if (encoder == NULL || param == NULL)
		return ENC_ERR_NULL_PTR;

	return encoder->ops->get_parameter(encoder, cmd, param);
}

int mpp_encoder_reset(struct mpp_encoder *encoder)
{
	if (encoder == NULL)
		return ENC_ERR_NULL_PTR;

	enc_pm_reset(encoder->pm);

	return encoder->ops->reset(encoder);
}
