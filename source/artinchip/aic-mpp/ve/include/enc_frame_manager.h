/*
 * Copyright (C) 2020-2026 ArtInChip Technology Co. Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 *  author: <qi.xu@artinchip.com>
 *  Desc:  encoder frame buffer manager
 */

#ifndef ENC_FRAME_MANAGER_H
#define ENC_FRAME_MANAGER_H

#include "mpp_dec_type.h"
#include "mpp_list.h"

struct enc_frame_manager;

typedef void (*enc_fm_release_callback)(struct mpp_frame* frame, void* user_data);

struct enc_frame_manager* enc_frame_manager_create(void);
int enc_frame_manager_destroy(struct enc_frame_manager *fm);
int enc_frame_buffer_put(struct enc_frame_manager *fm, struct mpp_frame* frame);
struct mpp_frame* enc_frame_buffer_get(struct enc_frame_manager *fm);
int enc_frame_buffer_return(struct enc_frame_manager *fm, struct mpp_frame* frame);
void enc_frame_set_release_callback(struct enc_frame_manager *fm,
    enc_fm_release_callback callback, void* user_data);

#endif
