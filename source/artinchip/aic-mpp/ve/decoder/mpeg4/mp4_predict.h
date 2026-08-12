/*
 * Copyright (C) 2020-2025 ArtInChip Technology Co. Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 *  author: <xiaodong.zhao@artinchip.com>
 *  Desc: mpeg4 predict interface
 *
 */

#ifndef _MP4_PREDICT_H_
#define _MP4_PREDICT_H_

#include "mp4_vars.h"

#define BOTH 3;
#define NONE 2
#define TOP  1
#define LEFT 0


extern void set_prediction_direction(struct mp4_state *mp4_state, int block_num);
extern void set_prediction_direction_intradc(struct mp4_state *mp4_state, int block_num, short *dc_value);

extern void dc_recon(struct mp4_state *_mp4_state, int block_num, short *dc_value);
extern void ac_recon(struct mp4_state *_mp4_state, int block_num, short *psBlock);
extern int ac_rescaling(struct mp4_state *_mp4_state, int block_num, short *psBlock);
extern void ac_store(struct mp4_state *_mp4_state, int block_num, short *psBlock);
extern void advanced_intra_prediction(struct mp4_state *_mp4_state, int block_num, short *psBlock);

extern void rescue_predict(struct mp4_state *_mp4_state);

#endif // _MP4_PREDICT_H_
