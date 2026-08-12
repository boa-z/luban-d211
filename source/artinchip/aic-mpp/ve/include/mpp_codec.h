/*
 * Copyright (C) 2020-2026 ArtInChip Technology Co. Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 *  author: <qi.xu@artinchip.com>
 *  Desc: interface of decode libs
 */

#ifndef MPP_CODEC_H
#define MPP_CODEC_H

#include "mpp_dec_type.h"
#include "frame_manager.h"
#include "packet_manager.h"
#include "enc_frame_manager.h"
#include "enc_packet_manager.h"
#include "mpp_decoder.h"
#include "mpp_encoder.h"
#include "frame_allocator.h"

struct mpp_decoder {
	struct dec_ops *ops;
	struct packet_manager* pm;
	struct frame_manager* fm;
	struct frame_allocator* allocator;
	int type;
	int rotmir_flag; // only used for jpeg
	int hor_scale;   // only used for jpeg
	int ver_scale;   // only used for jpeg
	int crop_en;
	int crop_x;
	int crop_y;
	int crop_width;
	int crop_height;
	int output_x;
	int output_y;
};

struct mpp_encoder {
	struct enc_ops *ops;
	struct enc_frame_manager *fm;
	struct enc_packet_manager *pm;
	mpp_encoder_release_frame_callback release_cb;
	void *user_data;
};

struct dec_ops {
	const char *name;

	int (*init)(struct mpp_decoder *ctx, struct decode_config *config);
	int (*destory)(struct mpp_decoder *ctx);
	int (*decode)(struct mpp_decoder *ctx);
	int (*control)(struct mpp_decoder *ctx, int cmd, void *param);
	int (*reset)(struct mpp_decoder *ctx);
};

struct enc_ops {
	const char *name;

	int (*init)(struct mpp_encoder *ctx, struct encode_config *config);
	int (*destory)(struct mpp_encoder *ctx);
	int (*encode)(struct mpp_encoder *ctx, struct mpp_frame *frame, struct enc_packet *packet);
	int (*get_parameter)(struct mpp_encoder *ctx, enum mpp_enc_cmd_type cmd, void *param);
	int (*set_parameter)(struct mpp_encoder *ctx, enum mpp_enc_cmd_type cmd, void *param);
	int (*reset)(struct mpp_encoder *ctx);
};
#endif
