/*
 * Copyright (C) 2020-2025 ArtInChip Technology Co. Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 *   author: <xiaodong.zhao@artinchip.com>
 *  Desc: mpeg vld context
 *
 */

#ifndef _MP4_VLD_H_
#define _MP4_VLD_H_

#include "mp4_vars.h"

#define ESCAPE 7167

extern tab_type tableB16_1[];
extern tab_type tableB16_2[];
extern tab_type tableB16_3[];

extern tab_type tableB17_1[];
extern tab_type tableB17_2[];
extern tab_type tableB17_3[];

extern tab_type tableI2_1[];
extern tab_type tableI2_2[];
extern tab_type tableI2_3[];

extern event_t vld_event(mp4_stream_t *_ld, int intraFlag);
extern event_t vld_intra_dct(mp4_stream_t *_ld);
extern event_t vld_rmg2_intra_dct(mp4_stream_t *_ld);
extern event_t vld_intra_aic_dct(mp4_stream_t *_ld);
extern event_t vld_inter_dct(mp4_stream_t *_ld);
extern event_t vld_inter_mq_dct(mp4_stream_t *_ld);
extern event_t vld_shv_dct(mp4_stream_t *_ld);

#endif // _MP4_VLD_H_
