/*
 * Copyright (C) 2020-2025 ArtInChip Technology Co. Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 *  author: <xiaodong.zhao@artinchip.com>
 *  Desc: aic_audio_encoder interface
 */

#ifndef _AUDIO_DECODER_H_
#define _AUDIO_DECODER_H_

#include "mpp_dec_type.h"
#include "aic_audio_encoder.h"
#include "audio_packet_manager.h"
#include "audio_frame_manager.h"

struct aic_audio_encoder {
    struct aic_audio_encoder_ops *ops;
    struct audio_packet_manager* pm;
    struct audio_frame_manager* fm;
};

struct aic_audio_encoder_ops {
    const char *name;
    s32 (*init)(struct aic_audio_encoder *encoder, struct aic_audio_encode_config *config);
    s32 (*destroy)(struct aic_audio_encoder *encoder);
    s32 (*encode)(struct aic_audio_encoder *encoder);
    s32 (*control)(struct aic_audio_encoder *encoder, int cmd, void *param);
    s32 (*reset)(struct aic_audio_encoder *encoder);
};

#endif
