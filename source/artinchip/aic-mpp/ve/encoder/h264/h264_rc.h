/*
 * Copyright (C) 2020-2026 ArtInChip Technology Co. Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Author: <qi.xu@artinchip.com>
 * Desc: h264 encoder rate control
 */

#ifndef _H264_RC_H_
#define _H264_RC_H_

#include "mpp_codec.h"

#define H264_RC_MIN_QP        0
#define H264_RC_MAX_QP        51
#define H264_RC_QP_STEP_MAX   10
#define H264_RC_GOP_SIZE      30
#define H264_RC_WINDOW_SIZE   21

/* rate control mode */
enum h264_rc_mode {
	H264_RC_MODE_FIX_QP = 0,	/* fixed QP, no rate control */
	H264_RC_MODE_VBR = 1,		/* variable bitrate */
	H264_RC_MODE_CBR = 2,		/* constant bitrate */
};

/* rate control parameters */
struct h264_rc_params {
	enum h264_rc_mode mode;
	int target_bps;		/* target bitrate in bits per second */
	int max_bps;		/* max bitrate for VBR */
	int min_bps;		/* min bitrate for VBR */
	int frame_rate;		/* frame rate */
	int gop_size;		/* GOP size */

	/* QP parameters */
	int qp_init;		/* initial QP */
	int qp_min;		/* minimum QP */
	int qp_max;		/* maximum QP */
	int qp_max_step;	/* max QP change between frames */

	/* frame size */
	int width;
	int height;
};

/* rate control context */
struct h264_rc_ctx {
	struct h264_rc_params params;

	/* runtime state */
	int enabled;		/* rate control enabled */
	int frame_count;	/* encoded frame count */
	int gop_frame_count;	/* frame count in current GOP */

	/* current frame info */
	int slice_type;		/* current slice type: I or P */
	int curr_qp;		/* current frame QP */

	/* bitrate control */
	long long target_bits_per_frame;	/* target bits for each frame */
	long long remaining_bits;		/* remaining bits in GOP */
	long long buffer_fullness;		/* virtual buffer fullness */

	/* frame statistics */
	int last_frame_bits;	/* last frame encoded bits */
	int avg_frame_bits;	/* average frame bits */

	/* QP history for I frame QP calculation */
	int qp_history[H264_RC_WINDOW_SIZE];
	int qp_history_idx;
	int qp_history_num;

	/* R-D model parameters (for P frame) */
	double x1;		/* linear model coefficient */
	double x2;		/* quadratic model coefficient */
	double prev_x1;
	double prev_x2;

	/* MAD (Mean Absolute Difference) prediction */
	double mad_c1;		/* MAD linear coefficient */
	double mad_c2;		/* MAD constant */
	double prev_mad_c1;
	double prev_mad_c2;

	/* frame MAD history */
	double mad_history[H264_RC_WINDOW_SIZE];
	double pmad_history[H264_RC_WINDOW_SIZE];
	int mad_history_idx;

	/* model data for R-D estimation */
	double reject_qp[H264_RC_WINDOW_SIZE];
	double reject_rp[H264_RC_WINDOW_SIZE];
	int model_size;
	int mad_size;

	/* previous frame info */
	double prev_frame_mad;
	double this_frame_mad;
	int prev_qp;
	int prev_p_qp;
	int pre_last_qp;
	int last_qp;

	/* I frame specific */
	int last_idr_frame;	/* last IDR frame index */
	int i_qp_offset;
	int prev_i_frame_bits;	/* previous I-frame actual bits, for GOP budget feedback */
	int prev_i_frame_qp;	/* previous I-frame QP */

	/* header bits estimation */
	int pre_header_bits;

	/* bounds for target bits */
	int low_bound;
	int up_bound1;
	int up_bound2;

	/* complexity weights */
	double weight_p;
	double weight_b;
	double ave_wp;
	double ave_wb;

	/* P frame count */
	int num_p_picture;
	int coded_p_frame;

	/* base QP for GOP-level stability */
	int base_qp;			/* baseline QP for current GOP */
	int gop_avg_p_qp;		/* average P-frame QP in current GOP */
	int gop_p_qp_sum;		/* sum of P-frame QPs in current GOP */
	int startup_frame_count;	/* count of frames in startup phase */

	/* reachable bitrate detection */
	int consecutive_qp_max_cnt;	/* consecutive frames at qp_max */
	int consecutive_qp_min_cnt;	/* consecutive frames at qp_min */
	long long effective_target_bps;	/* adjusted target when original is unreachable */
	int bitrate_status;		/* 0=normal, 1=target too low, 2=target too high */
};

/**
 * h264_rc_init - initialize rate control
 * @rc: rate control context
 * @params: rate control parameters
 * @return: 0 on success, negative on failure
 */
int h264_rc_init(struct h264_rc_ctx *rc, struct h264_rc_params *params);

/**
 * h264_rc_destroy - destroy rate control
 * @rc: rate control context
 */
void h264_rc_destroy(struct h264_rc_ctx *rc);

/**
 * h264_rc_start_frame - called before encoding a frame
 * @rc: rate control context
 * @slice_type: slice type (H264_SLICE_I or H264_SLICE_P)
 * @return: recommended QP for this frame
 */
int h264_rc_start_frame(struct h264_rc_ctx *rc, int slice_type);

/**
 * h264_rc_end_frame - called after encoding a frame
 * @rc: rate control context
 * @frame_bits: actual encoded bits for this frame
 * @mad: frame MAD (Mean Absolute Difference) from hardware
 */
void h264_rc_end_frame(struct h264_rc_ctx *rc, int frame_bits, int mad);

/**
 * h264_rc_get_qp - get current frame QP
 * @rc: rate control context
 * @return: current QP value
 */
static inline int h264_rc_get_qp(struct h264_rc_ctx *rc)
{
	if (!rc || !rc->enabled)
		return -1;
	return rc->curr_qp;
}

/**
 * h264_rc_is_enabled - check if rate control is enabled
 * @rc: rate control context
 * @return: 1 if enabled, 0 otherwise
 */
static inline int h264_rc_is_enabled(struct h264_rc_ctx *rc)
{
	return rc ? rc->enabled : 0;
}

#endif /* _H264_RC_H_ */
