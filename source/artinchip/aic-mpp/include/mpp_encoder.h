/*
 * Copyright (C) 2020-2026 ArtInChip Technology Co. Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 *  author: <qi.xu@artinchip.com>
 *  Desc: mpp encoder
 */

#ifndef __MPP_ENCODER_H__
#define __MPP_ENCODER_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <video/mpp_types.h>
#include "mpp_dec_type.h"

enum MPP_ENC_RC_MODE {
	MPP_ENC_RC_MODE_FIX_QP = 0,
	MPP_ENC_RC_MODE_CBR = 1,
	MPP_ENC_RC_MODE_VBR = 2,
};

enum MPP_ENC_GOP_MODE {
	MPP_ENC_GOP_MODE_IPPP = 0,
	MPP_ENC_GOP_MODE_I = 1,
};

struct mpp_enc_h264_rc_mode {
	enum MPP_ENC_RC_MODE rc_mode;
	int target_bps;
	int max_bps;
	int min_bps;

	int min_mb_qp;
	int max_mb_qp;
};

enum mpp_enc_cmd_type {
	// jpeg
	ENC_CMD_JPEG_QUALITY = 0x100,

	// h264
	// struct mpp_enc_h264_rc_mode
	ENC_CMD_H264_RC_MODE = 0x204,

	// common
	MPP_ENC_GET_QP = 0x300,
	MPP_ENC_SET_QP = 0x301,
};

struct mpp_encoder;

/**
 * struct encode_config - encode config
 */
struct encode_config {
	int packet_buffer_size;	// packet buffer size in pm
};

/**
 * mpp_encoder_create - create encoder (h264/jpeg/png ...)
 * @type: encoder type
 */
struct mpp_encoder* mpp_encoder_create(enum mpp_codec_type type);

/**
 * mpp_encoder_destory - destory encoder
 * @encoder: mpp_encoder context
 */
void mpp_encoder_destory(struct mpp_encoder* encoder);

/**
 * mpp_encoder_init - init encoder
 * @encoder: mpp_encoder context
 * @config: configuration of encoder
 */
int mpp_encoder_init(struct mpp_encoder *encoder, struct encode_config *config);

int mpp_encoder_put_frame(struct mpp_encoder* encoder, struct mpp_frame* frame);

/**
 * mpp_encoder_set_callback - set callback for release frame
 * @encoder: mpp_encoder context
 * @callback: callback function
 * @user_data: user data passed to callback function
 */
typedef void (*mpp_encoder_release_frame_callback)(struct mpp_frame* frame, void* user_data);

/**
 * mpp_encoder_set_callback - set callback for release frame
 * @encoder: mpp_encoder context
 * @callback: callback function
 * @user_data: user data passed to callback function
 * @return: 0 on success, negative error code on failure
 */
int mpp_encoder_set_callback(struct mpp_encoder* encoder, mpp_encoder_release_frame_callback callback, void* user_data);

/**
 * mpp_encoder_encode - encode one packet
 * @encoder: mpp_encoder context
 * @return: 0 on success, negative error code on failure
 */
int mpp_encoder_encode(struct mpp_encoder* encoder);

/**
 * mpp_encoder_get_packet - get encoded packet from encoder
 * @encoder: mpp_encoder context
 * @packet: output packet containing encoded data
 * @return: 0 on success, negative error code on failure
 */
int mpp_encoder_get_packet(struct mpp_encoder* encoder, struct mpp_packet *packet);

/**
 * mpp_encoder_put_packet - return packet to encoder for reuse
 * @encoder: mpp_encoder context
 * @packet: packet to be returned to encoder
 * @return: 0 on success, negative error code on failure
 */
int mpp_encoder_put_packet(struct mpp_encoder* encoder, struct mpp_packet *packet);

/**
 * mpp_encoder_set_parameter - set a parameter to mpp_encoder
 * @encoder: mpp_encoder context
 * @cmd: command type, see enum mpp_enc_cmd_type
 * @param: pointer to command-specific parameter structure
 * @return: 0 on success, negative error code on failure
 */
int mpp_encoder_set_parameter(struct mpp_encoder* encoder, enum mpp_enc_cmd_type cmd, void* param);

/**
 * mpp_encoder_get_parameter - get a parameter from mpp_encoder
 * @encoder: mpp_encoder context
 * @cmd: command type, see enum mpp_enc_cmd_type
 * @param: pointer to command-specific parameter structure to store the result
 * @return: 0 on success, negative error code on failure
 */
int mpp_encoder_get_parameter(struct mpp_encoder* encoder, enum mpp_enc_cmd_type cmd, void* param);

/**
 * mpp_encoder_reset - reset mpp_encoder
 * @encoder: mpp_encoder context
 */
int mpp_encoder_reset(struct mpp_encoder* encoder);

/**
 * mpp_encode_jpeg - encode one jpeg frame
 * @frame: the frame need be encoded
 * @quality: encode quality, 1~100
 * @dma_buf_fd: fd of output dma_buf to save jpeg data
 * @buf_len: the length of output buffer
 * @len: the length of encoded jpeg data
 */
int mpp_encode_jpeg(struct mpp_frame* frame, int quality, int dma_buf_fd, int buf_len, int *len);

#ifdef __cplusplus
}
#endif

#endif
