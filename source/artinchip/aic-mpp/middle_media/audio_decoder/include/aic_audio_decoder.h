/*
 * Copyright (C) 2020-2026 ArtInChip Technology Co. Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 *  author: <jun.ma@artinchip.com>
 *  Desc: aic_audio_decoder interface
 */


#ifndef _AIC_AUDIO_DECODER_H_
#define _AIC_AUDIO_DECODER_H_

#include "mpp_dec_type.h"
#include "audio_frame_manager.h"
#include "audio_packet_manager.h"

struct aic_audio_decoder;

/**
 * aic_audio_decoder_create - create decoder (mp3 ...)
 * @type: decoder type
 */
struct aic_audio_decoder* aic_audio_decoder_create(enum aic_audio_codec_type type);

/**
 * aic_audio_decoder_destory - destory decoder
 * @decoder: aic_audio_decoder context
 */
void aic_audio_decoder_destroy(struct aic_audio_decoder* decoder);

/**
 * aic_audio_decoder_init - init decoder
 * @decoder: aic_audio_decoder context
 * @config: configuration of decoder
 */
s32 aic_audio_decoder_init(struct aic_audio_decoder *decoder, struct aic_audio_decode_config *config);

/**
 * aic_audio_decoder_decode - decode one packet
 * @decoder: aic_audio_decoder context
 */
s32 aic_audio_decoder_decode(struct aic_audio_decoder* decoder);

/**
 * aic_audio_decoder_get_packet - get an empty packet from decoder
 * @decoder: aic_audio_decoder context
 * @packet: the packet from aic_audio_decoder
 * @size: data size of this packet
 */
s32 aic_audio_decoder_get_packet(struct aic_audio_decoder* decoder, struct mpp_packet* packet, int size);

/**
 * aic_audio_decoder_put_packet - put the packet to decoder
 * @decoder: aic_audio_decoder context
 * @packet: the packet filled by application
 */
s32 aic_audio_decoder_put_packet(struct aic_audio_decoder* decoder, struct mpp_packet* packet);

/**
 * aic_audio_decoder_return_packet - return the packet to empty list (bypass decode)
 * @decoder: aic_audio_decoder context
 * @packet: the packet to be returned to empty pool
 */
s32 aic_audio_decoder_return_packet(struct aic_audio_decoder* decoder, struct mpp_packet* packet);

/**
 * aic_audio_decoder_get_frame - get a display frame from decoder
 * @decoder: aic_audio_decoder context
 * @frame: one decoded frame to be sent to sound
 */
s32 aic_audio_decoder_get_frame(struct aic_audio_decoder* decoder, struct aic_audio_frame* frame);

/**
 * aic_audio_decoder_put_frame - return the frame to decoder
 * @decoder: aic_audio_decoder context
 * @frame: the frame which get from aic_audio_decoder_get_frame to be return
 */
s32 aic_audio_decoder_put_frame(struct aic_audio_decoder* decoder, struct aic_audio_frame* frame);

/**
 * aic_audio_decoder_control - send a control command (like, set/get parameter) to aic_audio_decoder
 * @decoder: aic_audio_decoder context
 * @cmd: command name see in aic_audio_decoder.h
 * @param: command data
 */
s32 aic_audio_decoder_control(struct aic_audio_decoder* decoder, s32 cmd, void* param);

/**
 * aic_audio_decoder_reset - reset aic_audio_decoder
 * @decoder: aic_audio_decoder context
 */
s32 aic_audio_decoder_reset(struct aic_audio_decoder* decoder);

#endif
