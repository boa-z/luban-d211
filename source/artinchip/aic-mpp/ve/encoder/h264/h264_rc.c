/*
 * Copyright (C) 2020-2026 ArtInChip Technology Co. Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Author: <qi.xu@artinchip.com>
 * Desc: h264 encoder rate control implementation
 *
 * Based on quadratic R-D model and linear MAD prediction model.
 * Supports CBR, VBR and fixed QP modes.
 */

#define LOG_TAG "h264_rc"
#include <math.h>
#include <string.h>
#include "h264_rc.h"
#include "h264_encoder.h"
#include "mpp_log.h"

#define H264_RC_THETA        1.3636f
#define H264_RC_OMEGA        0.9f
#define H264_RC_MIN_VALUE    4.0f
#define H264_RC_I_P_COEF     5

#ifndef ABS
#define ABS(x) ((x) >= 0 ? (x) : -(x))
#endif

#ifndef CLIP3
#define CLIP3(min, max, val) ((val) < (min) ? (min) : ((val) > (max) ? (max) : (val)))
#endif

#ifndef MIN
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#endif

#ifndef MAX
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#endif

/**
 * qp2qstep - convert QP to quantization step
 */
static double qp2qstep(int qp)
{
	int i;
	double step;
	double qp_to_step[6] = { 0.625, 0.6875, 0.8125, 0.875, 1.0, 1.125 };

	step = qp_to_step[qp % 6];
	for (i = 0; i < (qp / 6); i++)
		step *= 2;

	return step;
}

/**
 * qstep2qp - convert quantization step to QP
 */
static int qstep2qp(double qstep)
{
	int q_per = 0, q_rem = 0;

	if (qstep < qp2qstep(H264_RC_MIN_QP))
		return H264_RC_MIN_QP;
	else if (qstep > qp2qstep(H264_RC_MAX_QP))
		return H264_RC_MAX_QP;

	while (qstep > qp2qstep(5)) {
		qstep /= 2.0;
		q_per++;
	}

	if (qstep <= 0.65625)
		q_rem = 0;
	else if (qstep <= 0.75)
		q_rem = 1;
	else if (qstep <= 0.84375)
		q_rem = 2;
	else if (qstep <= 0.9375)
		q_rem = 3;
	else if (qstep <= 1.0625)
		q_rem = 4;
	else
		q_rem = 5;

	return (q_per * 6 + q_rem);
}

/**
 * rc_model_estimator - estimate R-D model parameters
 */
static void rc_model_estimator(struct h264_rc_ctx *rc, int model_size, int *reject)
{
	int true_size = model_size;
	int i;
	double a00 = 0.0, a01 = 0.0, a10 = 0.0, a11 = 0.0, a0 = 0.0, a1 = 0.0;
	double matrix_val;

	for (i = 0; i < model_size; i++) {
		if (reject[i])
			true_size--;
	}

	rc->x1 = rc->x2 = 0.0;

	if (true_size >= 1) {
		for (i = 0; i < model_size; i++) {
			if (!reject[i]) {
				a00 = a00 + 1.0;
				a01 += 1.0 / rc->reject_qp[i];
				a10 = a01;
				a11 += 1.0 / (rc->reject_qp[i] * rc->reject_qp[i]);
				a0 += rc->reject_qp[i] * rc->reject_rp[i];
				a1 += rc->reject_rp[i];
			}
		}

		matrix_val = a00 * a11 - a01 * a10;
		if (ABS(matrix_val) > 0.000001) {
			double det_threshold = 0.0;
			/* When sample qsteps are close, matrix is ill-conditioned.
			 * Fallback to linear model (x2=0) for stability.
			 */
			if (true_size >= 2) {
				double min_qp = rc->reject_qp[0];
				double max_qp = rc->reject_qp[0];
				for (i = 1; i < model_size; i++) {
					if (!reject[i]) {
						if (rc->reject_qp[i] < min_qp)
							min_qp = rc->reject_qp[i];
						if (rc->reject_qp[i] > max_qp)
							max_qp = rc->reject_qp[i];
					}
				}
				/* When qstep values are too clustered (ratio < 1.3),
				 * the least-squares matrix is near-singular and x1/x2
				 * estimates diverge. Force linear model (x2=0) to
				 * keep the model stable.
				 */
				if (max_qp < min_qp * 1.3) {
					rc->x1 = a0 / a00;
					rc->x2 = 0.0;
					logi("RC_RD: qstep cluster (%.1f/%.1f=%.2f) → linear model",
						max_qp, min_qp, max_qp / min_qp);
					/* Early return to skip quadratic solve and smoothing */
					rc->prev_x1 = rc->x1;
					rc->prev_x2 = rc->x2;
					return;
				}
				det_threshold = a00 * a00 * 0.01 / (max_qp * max_qp);
				if (max_qp < min_qp * 1.1)
					det_threshold = a00 * a00 * 0.1 / (max_qp * max_qp);
			}
			if (ABS(matrix_val) < det_threshold) {
				/* Ill-conditioned: use linear model x2=0 */
				rc->x1 = a0 / a00;
				rc->x2 = 0.0;
				logi("RC_RD: linear fallback (det=%.6g < thr=%.6g)", matrix_val, det_threshold);
			} else {
				rc->x1 = (a0 * a11 - a1 * a01) / matrix_val;
				rc->x2 = (a1 * a00 - a0 * a10) / matrix_val;
			}
		} else {
			rc->x1 = rc->prev_x1;
			rc->x2 = rc->prev_x2;
		}
	}

	/* Constrain model coefficients to prevent instability.
	 * x1 must be non-negative (negative has no physical meaning in R-D model).
	 * Limit change to +/-50% of previous value for smooth convergence.
	 */
	if (rc->x1 < 0) {
		rc->x1 = rc->prev_x1;
		logi("RC_RD: x1 clipped from negative to prev_x1=%.2f", rc->x1);
	}
	if (rc->prev_x1 > 0 && ABS(rc->x1 - rc->prev_x1) > rc->prev_x1 * 0.5) {
		rc->x1 = rc->prev_x1 * 0.7 + rc->x1 * 0.3;
		logi("RC_RD: x1 smoothed, prev=%.2f, new=%.2f", rc->prev_x1, rc->x1);
	}
	if (rc->prev_x2 > 0 && ABS(rc->x2 - rc->prev_x2) > rc->prev_x2 * 0.5) {
		rc->x2 = rc->prev_x2 * 0.7 + rc->x2 * 0.3;
		logi("RC_RD: x2 smoothed, prev=%.2f, new=%.2f", rc->prev_x2, rc->x2);
	}

	rc->prev_x1 = rc->x1;
	rc->prev_x2 = rc->x2;
}

/**
 * mad_model_estimator - estimate MAD prediction model parameters
 */
static void mad_model_estimator(struct h264_rc_ctx *rc, int model_size, int *reject)
{
	int true_size = model_size;
	int i;
	double one_mad = 0.0;
	int estimate_x2 = 0;
	double matrix_val;
	double a00 = 0.0, a01 = 0.0, a10 = 0.0, a11 = 0.0, a0 = 0.0, a1 = 0.0;

	for (i = 0; i < model_size; i++) {
		if (reject[i])
			true_size--;
	}

	rc->mad_c1 = rc->mad_c2 = 0.0;

	for (i = 0; i < model_size; i++) {
		if (!reject[i])
			one_mad = rc->mad_history[i];
	}

	for (i = 0; i < model_size; i++) {
		if ((rc->mad_history[i] != one_mad) && !reject[i])
			estimate_x2 = 1;
		if (!reject[i])
			rc->mad_c1 += rc->mad_history[i] / (rc->pmad_history[i] * true_size);
	}

	if ((true_size >= 1) && estimate_x2) {
		for (i = 0; i < model_size; i++) {
			if (!reject[i]) {
				a00 = a00 + 1.0;
				a01 += rc->pmad_history[i];
				a10 = a01;
				a11 += rc->pmad_history[i] * rc->pmad_history[i];
				a0 += rc->mad_history[i];
				a1 += rc->mad_history[i] * rc->pmad_history[i];
			}
		}

		matrix_val = a00 * a11 - a01 * a10;
		if (ABS(matrix_val) > 0.000001) {
			rc->mad_c2 = (a0 * a11 - a1 * a01) / matrix_val;
			rc->mad_c1 = (a1 * a00 - a0 * a10) / matrix_val;
		} else {
			rc->mad_c1 = a0 / a01;
			rc->mad_c2 = 0.0;
		}
	}

	rc->prev_mad_c1 = rc->mad_c1;
	rc->prev_mad_c2 = rc->mad_c2;
}

/**
 * update_mad_model - update MAD prediction model
 */
static void update_mad_model(struct h264_rc_ctx *rc)
{
	int i;
	int rejected_count = 0;
	double standard = 0.0, threshold;
	int m_nc = rc->coded_p_frame;
	int reject[H264_RC_WINDOW_SIZE];
	double err[H264_RC_WINDOW_SIZE];
	int model_size;

	if (rc->coded_p_frame <= 0)
		return;

	for (i = (H264_RC_WINDOW_SIZE - 2); i > 0; i--) {
		rc->pmad_history[i] = rc->pmad_history[i - 1];
		rc->mad_history[i] = rc->mad_history[i - 1];
	}
	rc->pmad_history[0] = rc->this_frame_mad;
	rc->mad_history[0] = rc->this_frame_mad;

	rc->mad_c1 = rc->prev_mad_c1;
	rc->mad_c2 = rc->prev_mad_c2;

	model_size = (rc->this_frame_mad > rc->prev_frame_mad)
		? (int)((float)(H264_RC_WINDOW_SIZE - 1) * rc->prev_frame_mad / rc->this_frame_mad)
		: (int)((float)(H264_RC_WINDOW_SIZE - 1) * rc->this_frame_mad / rc->prev_frame_mad);
	model_size = CLIP3(1, (m_nc - 1), model_size);
	model_size = MIN(model_size, MIN(20, rc->mad_size + 1));

	rc->mad_size = model_size;

	for (i = 0; i < (H264_RC_WINDOW_SIZE - 1); i++)
		reject[i] = 0;

	rc->prev_frame_mad = rc->this_frame_mad;

	mad_model_estimator(rc, model_size, reject);

	for (i = 0; i < model_size; i++) {
		err[i] = rc->mad_c1 * rc->pmad_history[i] + rc->mad_c2 - rc->mad_history[i];
		standard += (err[i] * err[i]);
	}

	threshold = (model_size == 2) ? 0 : sqrt(standard / model_size);
	rejected_count = 0;
	for (i = 0; i < model_size; i++) {
		if (fabs(err[i]) > threshold) {
			reject[i] = 1;
			rejected_count++;
		}
	}
	reject[0] = 0;
	if (rejected_count > 0)
		logi("RC_MAD: rejected=%d/%d, threshold=%.4f", rejected_count, model_size, threshold);

	mad_model_estimator(rc, model_size, reject);
	logi("RC_MAD: model_size=%d, c1=%.4f, c2=%.4f, mad=%.4f",
		model_size, rc->mad_c1, rc->mad_c2, rc->this_frame_mad);
}

/**
 * update_rc_model - update R-D model
 */
static void update_rc_model(struct h264_rc_ctx *rc)
{
	int model_size;
	int i;
	int rd_rejected_count = 0;
	double standard = 0.0, threshold;
	int m_nc;
	int reject[H264_RC_WINDOW_SIZE];
	double err[H264_RC_WINDOW_SIZE];

	m_nc = rc->coded_p_frame;

	rc->pre_header_bits = 0;
	for (i = (H264_RC_WINDOW_SIZE - 2); i > 0; i--) {
		rc->reject_qp[i] = rc->reject_qp[i - 1];
		rc->reject_rp[i] = rc->reject_rp[i - 1];
	}
	rc->reject_qp[0] = qp2qstep(rc->curr_qp);
	rc->reject_rp[0] = (double)rc->last_frame_bits / rc->this_frame_mad;

	rc->x1 = rc->prev_x1;
	rc->x2 = rc->prev_x2;

	model_size = (rc->this_frame_mad > rc->prev_frame_mad)
		? (int)(rc->prev_frame_mad / rc->this_frame_mad * (H264_RC_WINDOW_SIZE - 1))
		: (int)(rc->this_frame_mad / rc->prev_frame_mad * (H264_RC_WINDOW_SIZE - 1));
	model_size = CLIP3(1, m_nc, model_size);
	model_size = MIN(model_size, rc->model_size + 1);
	model_size = MIN(model_size, (H264_RC_WINDOW_SIZE - 1));

	rc->model_size = model_size;

	for (i = 0; i < (H264_RC_WINDOW_SIZE - 1); i++)
		reject[i] = 0;

	rc_model_estimator(rc, model_size, reject);

	model_size = rc->model_size;
	for (i = 0; i < (int)model_size; i++) {
		err[i] = rc->x1 / rc->reject_qp[i] + rc->x2 / (rc->reject_qp[i] * rc->reject_qp[i]) - rc->reject_rp[i];
		standard += err[i] * err[i];
	}
	threshold = (model_size == 2) ? 0 : sqrt(standard / model_size);
	rd_rejected_count = 0;
	for (i = 0; i < (int)model_size; i++) {
		if (fabs(err[i]) > threshold) {
			reject[i] = 1;
			rd_rejected_count++;
		}
	}
	reject[0] = 0;
	if (rd_rejected_count > 0)
		logi("RC_RD: rejected=%d/%d, threshold=%.4f", rd_rejected_count, model_size, threshold);

	rc_model_estimator(rc, model_size, reject);
	logi("RC_RD: model_size=%d, x1=%.2f, x2=%.2f, qp_step=%.4f, rp=%.2f",
		model_size, rc->x1, rc->x2, rc->reject_qp[0], rc->reject_rp[0]);

	if (rc->coded_p_frame > 1)
		update_mad_model(rc);
	else
		rc->pmad_history[0] = rc->this_frame_mad;
}

/**
 * update_model_qp - calculate QP from R-D model
 */
static void update_model_qp(struct h264_rc_ctx *rc, int bits)
{
	double tmp_val, qstep;

	tmp_val = rc->this_frame_mad * rc->x1 * rc->this_frame_mad * rc->x1
		+ 4 * rc->x2 * rc->this_frame_mad * bits;

	if ((rc->x2 == 0.0) || (tmp_val < 0)
		|| ((sqrt(tmp_val) - rc->x1 * rc->this_frame_mad) <= 0.0))
		qstep = (float)(rc->x1 * rc->this_frame_mad / (double)bits);
	else
		qstep = (float)((2 * rc->x2 * rc->this_frame_mad)
			/ (sqrt(tmp_val) - rc->x1 * rc->this_frame_mad));

	rc->curr_qp = qstep2qp(qstep);
}

/**
 * calculate_initial_qp - calculate initial QP based on bpp
 *
 * Applies resolution-based complexity factor and narrow-QP-range
 * adjustment before mapping bpp → QP. The mapping is tuned for
 * 720p@30fps (resolution_factor=1.05):
 *   1Mbps → QP=40, 4Mbps → QP=25, 10Mbps → QP=20
 */
static int calculate_initial_qp(struct h264_rc_ctx *rc)
{
	double bpp, eff_bpp;
	int qp;
	double complexity_factor = 1.0;

	bpp = 1.0 * rc->params.target_bps /
		(rc->params.frame_rate * rc->params.width * rc->params.height);

	/* Resolution-based complexity adjustment */
	int total_pixels = rc->params.width * rc->params.height;
	if (total_pixels > 1500000)
		complexity_factor = 1.15;
	else if (total_pixels > 600000)
		complexity_factor = 1.05;

	/* Narrow QP range needs more conservative (higher) initial QP */
	int qp_range = rc->params.qp_max - rc->params.qp_min;
	if (qp_range < 15)
		complexity_factor *= 1.1;

	eff_bpp = bpp * complexity_factor;

	if (eff_bpp <= 0.018)
		qp = 46;
	else if (eff_bpp <= 0.038)
		qp = 46 - (int)((eff_bpp - 0.018) * (6.0 / 0.020));
	else if (eff_bpp <= 0.152)
		qp = 40 - (int)((eff_bpp - 0.038) * (15.0 / 0.114));
	else if (eff_bpp <= 0.380)
		qp = 25 - (int)((eff_bpp - 0.152) * (5.0 / 0.228));
	else
		qp = 15;

	return CLIP3(rc->params.qp_min, rc->params.qp_max, qp);
}

/* ---- helper functions for update_qp_rc ---- */

static void rc_qp_first_frame(struct h264_rc_ctx *rc)
{
	rc->curr_qp = rc->params.qp_init;
	rc->last_qp = rc->params.qp_init - 1;
	rc->last_idr_frame = rc->frame_count;
	rc->base_qp = rc->curr_qp;
	rc->gop_avg_p_qp = 0;
	rc->gop_p_qp_sum = 0;
	rc->startup_frame_count = 0;
	logi("RC: Initial QP=%d, base_qp=%d", rc->curr_qp, rc->base_qp);
}

static void rc_update_base_qp(struct h264_rc_ctx *rc)
{
	if (!(rc->frame_count > 0 && rc->num_p_picture > 0))
		return;

	int calculated_qp = calculate_initial_qp(rc);
	int avg_p_qp = rc->gop_p_qp_sum / rc->num_p_picture;

	rc->base_qp = (calculated_qp * 3 + avg_p_qp * 2) / 5;

	if (rc->bitrate_status != 1) {
		int max_base_qp = rc->params.qp_max - 5;
		if (max_base_qp > 40)
			max_base_qp = 40;
		rc->base_qp = MIN(rc->base_qp, max_base_qp);
	}

	rc->base_qp = CLIP3(rc->params.qp_min, rc->params.qp_max, rc->base_qp);
	logi("RC: New GOP base_qp=%d (calculated=%d, avg_p=%d, blended)",
		rc->base_qp, calculated_qp, avg_p_qp);
}

static int rc_i_frame_qp(struct h264_rc_ctx *rc)
{
	if (rc->frame_count == 0)
		return rc->params.qp_init;

	int adaptive_i_offset = rc->i_qp_offset;

	if (rc->buffer_fullness > rc->params.target_bps * 0.2) {
		adaptive_i_offset = rc->i_qp_offset - 1;
		logi("RC: Buffer surplus, I-frame offset %d -> %d (higher QP)",
			rc->i_qp_offset, adaptive_i_offset);
	} else if (rc->buffer_fullness < -rc->params.target_bps * 0.3) {
		adaptive_i_offset = rc->i_qp_offset + 1;
		logi("RC: Buffer deficit, I-frame offset %d -> %d (lower QP)",
			rc->i_qp_offset, adaptive_i_offset);
	}

	int curr_qp = rc->base_qp - adaptive_i_offset;

	if (rc->prev_i_frame_bits > 0) {
		long long max_i_frame = rc->params.target_bps * 35 / 100;
		double size_ratio = (double)rc->prev_i_frame_bits / max_i_frame;
		if (size_ratio > 1.5) {
			int extra_qp = (int)((size_ratio - 1.0) * 4);
			extra_qp = CLIP3(1, 6, extra_qp);
			curr_qp = MIN(rc->params.qp_max, curr_qp + extra_qp);
			logi("RC: I-frame feedback: prev=%.0f%% of budget, qp+=%d",
				size_ratio * 100.0, extra_qp);
		} else if (size_ratio < 0.3) {
			int extra_qp = (int)((0.3 - size_ratio) * 3);
			extra_qp = CLIP3(1, 3, extra_qp);
			curr_qp = MAX(rc->params.qp_min, curr_qp - extra_qp);
			logi("RC: I-frame feedback: prev=%.0f%% of budget, qp-=%d",
				size_ratio * 100.0, extra_qp);
		}
	}

	logi("RC: I-frame QP=%d (base_qp=%d, offset=%d)",
		curr_qp, rc->base_qp, adaptive_i_offset);
	return curr_qp;
}

static int rc_first_p_frame(struct h264_rc_ctx *rc)
{
	int curr_qp;

	if (rc->frame_count == 0) {
		curr_qp = rc->base_qp;
	} else {
		curr_qp = MIN(rc->last_qp + 1, rc->params.qp_max);
		if (rc->buffer_fullness > rc->params.target_bps * 0.3)
			curr_qp = MIN(curr_qp + 2, rc->params.qp_max);
	}

	rc->target_bits_per_frame = (int)((rc->effective_target_bps / rc->params.frame_rate) / 1.3);
	logi("RC: First P frame, qp=%d (last_qp=%d, base_qp=%d)",
		curr_qp, rc->last_qp, rc->base_qp);
	return curr_qp;
}

static int rc_startup_feedback(struct h264_rc_ctx *rc)
{
	rc->startup_frame_count++;
	int qp = rc->prev_p_qp;

	if (rc->last_frame_bits > 0 && rc->target_bits_per_frame > 0) {
		double ratio = (double)rc->last_frame_bits / rc->target_bits_per_frame;
		int qp_adjust = 0;

		if (ratio > 2.5)
			qp_adjust = 4;
		else if (ratio > 2.0)
			qp_adjust = 3;
		else if (ratio > 1.5)
			qp_adjust = 2;
		else if (ratio > 1.2)
			qp_adjust = 1;
		else if (ratio < 0.4)
			qp_adjust = -4;
		else if (ratio < 0.55)
			qp_adjust = -3;
		else if (ratio < 0.7)
			qp_adjust = -2;
		else if (ratio < 0.9)
			qp_adjust = -1;

		qp = rc->prev_p_qp + qp_adjust;
		logi("RC: Startup feedback, ratio=%.2f, qp_adj=%d, qp=%d->%d",
			ratio, qp_adjust, rc->prev_p_qp, qp);
	}

	return qp;
}

static int rc_normal_phase(struct h264_rc_ctx *rc)
{
	rc->x1 = rc->prev_x1;
	rc->x2 = rc->prev_x2;
	rc->mad_c1 = rc->prev_mad_c1;
	rc->mad_c2 = rc->prev_mad_c2;

	int max_qstep = rc->params.qp_max_step;
	int qp = rc->prev_p_qp;
	int hp = rc->pre_header_bits;

	rc->this_frame_mad = rc->mad_c1 * rc->pmad_history[0] + rc->mad_c2;

	int qp_before_clip = qp;
	if (rc->target_bits_per_frame < 0) {
		qp = qp + max_qstep;
		qp = CLIP3(rc->params.qp_min, rc->params.qp_max, qp);
		logi("RC: Normal neg target, qp=%d (from %d)", qp, rc->prev_p_qp);
	} else {
		int bits = rc->target_bits_per_frame - hp;
		bits = MAX(bits, (int)(rc->effective_target_bps
			/ (H264_RC_MIN_VALUE * rc->params.frame_rate)));
		update_model_qp(rc, bits);
		qp = rc->curr_qp;
		qp_before_clip = qp;
		qp = CLIP3(rc->params.qp_min, rc->params.qp_max, qp);
		logi("RC: Normal model, x1=%.1f, x2=%.1f, mad=%.4f, pred_mad=%.4f, bits=%d, qp_model=%d",
			rc->x1, rc->x2, rc->this_frame_mad, rc->pmad_history[0], bits, qp_before_clip);
	}

	if (rc->buffer_fullness < -rc->params.target_bps * 0.3) {
		qp -= 1;
		logi("RC: Buffer underfill comp, qp=%d", qp);
	}

	return qp;
}

static void rc_p_frame_clip(struct h264_rc_ctx *rc)
{
	int qp_range = (rc->startup_frame_count < 5) ? 10 : 4;
	if (rc->buffer_fullness > rc->params.target_bps * 0.5) {
		qp_range = 8;
		logi("RC: High buffer %.0f, widen qp_range to %d",
			(double)rc->buffer_fullness, qp_range);
	}
	rc->curr_qp = CLIP3(rc->base_qp - qp_range, rc->base_qp + qp_range, rc->curr_qp);

	int max_qp_step = 2;
	if (rc->slice_type == H264_SLICE_I) {
		max_qp_step = 8;
	} else if (rc->buffer_fullness > rc->params.target_bps * 0.3) {
		max_qp_step = 4;
	}
	rc->curr_qp = CLIP3(rc->last_qp - max_qp_step, rc->last_qp + max_qp_step, rc->curr_qp);

	rc->curr_qp = CLIP3(rc->params.qp_min, rc->params.qp_max, rc->curr_qp);
}

/**
 * update_qp_rc - update QP for rate control
 */
static int update_qp_rc(struct h264_rc_ctx *rc)
{
	if (rc->frame_count == 0) {
		rc_qp_first_frame(rc);
	} else if (rc->slice_type == H264_SLICE_I) {
		rc_update_base_qp(rc);
		rc->curr_qp = rc_i_frame_qp(rc);

		rc->last_idr_frame = rc->frame_count;
		rc->pre_last_qp = rc->last_qp;
		rc->last_qp = rc->curr_qp;
		rc->prev_p_qp = rc->curr_qp;
		rc->target_bits_per_frame = (int)((rc->effective_target_bps
			/ rc->params.frame_rate) * H264_RC_I_P_COEF);
		rc->num_p_picture = 0;
		rc->gop_p_qp_sum = 0;
		rc->startup_frame_count = 0;
	} else if (rc->slice_type == H264_SLICE_P) {
		if (rc->num_p_picture == 0) {
			rc->curr_qp = rc_first_p_frame(rc);
		} else {
			if (rc->startup_frame_count < 5)
				rc->curr_qp = rc_startup_feedback(rc);
			else
				rc->curr_qp = rc_normal_phase(rc);

			if (rc->buffer_fullness > rc->params.target_bps * 0.3 &&
			    rc->curr_qp >= rc->base_qp + 3) {
				int new_base_qp = MIN(rc->base_qp + 2, rc->params.qp_max - 5);
				if (new_base_qp > rc->base_qp) {
					rc->base_qp = new_base_qp;
					logi("RC: Raising base_qp to %d due to persistent buffer overflow",
						rc->base_qp);
				}
			}

			rc_p_frame_clip(rc);
		}
		rc->pre_last_qp = rc->last_qp;
		rc->last_qp = rc->curr_qp;
		rc->prev_p_qp = rc->curr_qp;
		rc->gop_p_qp_sum += rc->curr_qp;
	}

	return rc->curr_qp;
}

/**
 * rc_init_frame - initialize frame level rate control
 */
static void rc_init_frame(struct h264_rc_ctx *rc)
{
	/* Use effective target for bit allocation; may differ from params.target_bps
	 * when the original target is detected as unreachable.
	 */
	long long eff_target = rc->effective_target_bps;

	if ((rc->frame_count % rc->params.gop_size) == 0) {
		long long allocated_bits;
		int np;
		int m = 1;
		int n = rc->params.gop_size;

		np = 1 + ((n - 2) / m);
		(void)np;

		rc->low_bound = (int)(rc->remaining_bits + eff_target / rc->params.frame_rate);
		rc->up_bound1 = (int)(rc->remaining_bits + (eff_target * 2.048));

		allocated_bits = eff_target;
		if (rc->remaining_bits > 2 * allocated_bits)
			rc->remaining_bits = 2 * allocated_bits;
		else if (rc->remaining_bits < -2 * allocated_bits)
			rc->remaining_bits = -2 * allocated_bits;

		rc->remaining_bits += allocated_bits;
	}

	if (rc->slice_type == H264_SLICE_P) {
		/* Enhanced GOP bit allocation with progressive adjustment.
		 * Distribute bits more evenly across the GOP while considering buffer state.
		 * This prevents early frames from consuming too many bits and causing
		 * quality degradation in later frames.
		 */
		int remaining_frames_in_gop = rc->params.gop_size - rc->gop_frame_count;
		if (remaining_frames_in_gop <= 0)
			remaining_frames_in_gop = 1;

		/* Calculate base target bits per frame using remaining budget */
		double base_target = (double)rc->remaining_bits / remaining_frames_in_gop;

		/* Progressive buffer compensation: apply stronger correction as we approach GOP end.
		 * This ensures we don't run out of bits at the end of the GOP.
		 */
		double gop_progress = (double)rc->gop_frame_count / rc->params.gop_size;
		double buffer_factor = 1.0;

		if (rc->buffer_fullness > 0) {
			/* We have used MORE bits than target (over budget).
			 * Need to REDUCE target to compensate and avoid overflow.
			 */
			double buffer_ratio = (double)rc->buffer_fullness / eff_target;
			buffer_factor = 1.0 - 0.20 * buffer_ratio;
		} else if (rc->buffer_fullness < 0) {
			/* We have used FEWER bits than target (under budget).
			 * Need to INCREASE target to use more bits and get closer to target bitrate.
			 */
			double deficit_ratio = (double)(-rc->buffer_fullness) / eff_target;
			/* Apply stronger increase as GOP progresses to ensure we use allocated bits */
			double progress_weight = 1.0 + 0.3 * gop_progress;
			buffer_factor = 1.0 + 0.20 * deficit_ratio * progress_weight;
		}

		/* Tighter bounds for better stability */
		buffer_factor = CLIP3(0.75, 1.25, buffer_factor);

		rc->target_bits_per_frame = (long long)(base_target * buffer_factor + 0.5);

		/* Dynamic bounds based on GOP position and remaining budget.
		 * Allow more flexibility early in GOP, tighten constraints later.
		 * When remaining budget is low, reduce min_bits to avoid overdraft.
		 */
		long long nominal_bits = (long long)(eff_target / rc->params.frame_rate);
		long long min_bits, max_bits;
		long long available_budget = rc->remaining_bits;

		if (gop_progress < 0.5) {
			/* First half of GOP: wider range */
			min_bits = nominal_bits / 3;
			max_bits = nominal_bits * 3;
		} else {
			/* Second half of GOP: tighter range to ensure smooth ending */
			min_bits = nominal_bits / 2;
			max_bits = nominal_bits * 2;
		}

		/* If remaining budget is tight, allow lower min_bits to prevent overdraft.
		 * This is critical when early frames consumed too many bits.
		 */
		if (available_budget > 0 && available_budget < remaining_frames_in_gop * min_bits) {
			min_bits = available_budget / remaining_frames_in_gop;
			if (min_bits < nominal_bits / 10)
				min_bits = nominal_bits / 10;
		} else if (available_budget <= 0) {
			/* Budget depleted: force minimal allocation to trigger QP increase */
			min_bits = nominal_bits / 10;
		}

		rc->target_bits_per_frame = CLIP3(min_bits, max_bits, rc->target_bits_per_frame);
		logi("RC_BITS: base=%.1f, factor=%.3f, progress=%.2f, min=%lld, max=%lld, target=%lld",
			base_target, buffer_factor, gop_progress, min_bits, max_bits,
			rc->target_bits_per_frame);
	}

	rc->curr_qp = update_qp_rc(rc);
	rc->curr_qp = CLIP3(rc->params.qp_min, rc->params.qp_max, rc->curr_qp);
}

/**
 * rc_update_pict - update after frame encoding
 */
static void rc_update_pict(struct h264_rc_ctx *rc, int nbits, int mad)
{
	int complexity;
	int bits_of_delta;

	rc->last_frame_bits = nbits;
	rc->this_frame_mad = (double)mad / (double)(rc->params.width * rc->params.height);

	complexity = (int)floor(nbits * rc->curr_qp + 0.5);

	if (rc->slice_type == H264_SLICE_P) {
		rc->weight_p = complexity;
		rc->coded_p_frame++;
		rc->num_p_picture++;
	}

	/* Use effective target for delta calculation when bitrate is unreachable */
	int target_for_delta = rc->effective_target_bps;
	bits_of_delta = nbits - (int)floor(target_for_delta / rc->params.frame_rate + 0.5f);
	rc->remaining_bits -= nbits;

	/* I-frame budget check: if I-frame consumed >35% of GOP budget,
	 * immediately boost base_qp to prevent QP ramp across the GOP.
	 * This is critical for quality continuity — without it, P-frames
	 * slowly climb from low to high QP, causing visible quality drift.
	 * Also save I-frame statistics for next GOP's QP feedback.
	 */
	if (rc->slice_type == H264_SLICE_I) {
		rc->prev_i_frame_bits = nbits;
		rc->prev_i_frame_qp = rc->curr_qp;

		long long gop_budget = (long long)rc->params.target_bps;
		double i_ratio = (double)nbits / gop_budget;
		if (i_ratio > 0.25 && rc->base_qp < rc->params.qp_max - 3) {
			int boost = (int)((i_ratio - 0.25) * 15);
			if (boost < 2) boost = 2;
			if (boost > 12) boost = 12;
			rc->base_qp = MIN(rc->params.qp_max - 2, rc->base_qp + boost);
			logi("RC: I-frame %.0f%% of GOP budget, boosting base_qp by %d to %d",
				i_ratio * 100.0, boost, rc->base_qp);
		} else if (i_ratio < 0.08 && rc->base_qp > rc->params.qp_min + 2) {
			rc->base_qp -= 1;
			logi("RC: Simple content (I-frame %.0f%% of GOP), base_qp-1 to %d",
				i_ratio * 100.0, rc->base_qp);
		}
	}

	/* Reachable bitrate detection (P frames only) */
	if (rc->slice_type == H264_SLICE_P) {
		int nominal_bits = target_for_delta / rc->params.frame_rate;
		double actual_ratio = (nominal_bits > 0) ? (double)nbits / nominal_bits : 1.0;

		if (rc->curr_qp >= rc->params.qp_max - 1 && actual_ratio > 1.5) {
			/* QP near max but actual bits still significantly above target.
			 * Use 1.5x instead of 3.0x so that persistent overshoot at QP max
			 * is detected even when the overshoot ratio is moderate.
			 */
			rc->consecutive_qp_max_cnt++;
			rc->consecutive_qp_min_cnt = 0;
		} else if (actual_ratio < 0.5) {
			int qp_at_low_end = (rc->curr_qp <= rc->params.qp_min + 1) ||
				(rc->startup_frame_count >= 5 && rc->curr_qp <= rc->base_qp - 2);
			if (qp_at_low_end) {
				/* QP at/near min but still below target, or simple content where
				 * QP is already below base_qp but can't fill the bit budget.
				 */
				rc->consecutive_qp_min_cnt++;
				rc->consecutive_qp_max_cnt = 0;
			} else {
				rc->consecutive_qp_max_cnt = 0;
				rc->consecutive_qp_min_cnt = 0;
			}
		}

		/* Unreachable target detection.
		 * When QP at max and still overshooting, the target bitrate is below
		 * the hardware encoder's minimum output. Do NOT raise effective_target_bps
		 * (that would make the overshoot appear smaller and weaken corrective action).
		 * Instead, mark the target as unreachable so subsequent GOPs use QP_max
		 * aggressively. effective_target_bps stays at params.target_bps so that
		 * buffer_fullness honestly reflects the true deviation from target.
		 */
		if (rc->consecutive_qp_max_cnt >= 3) {
			rc->bitrate_status = 1;
			long long new_target = (long long)(rc->effective_target_bps * 1.08);
			if (new_target <= rc->params.target_bps * 3)
				rc->effective_target_bps = new_target;
			if (rc->effective_target_bps > rc->params.target_bps)
				rc->effective_target_bps = rc->params.target_bps;
			rc->consecutive_qp_max_cnt = 0;
			logi("RC: QP_max overshoot, raising effective_target to %lld (target=%d)",
				rc->effective_target_bps, rc->params.target_bps);
		} else if (rc->consecutive_qp_min_cnt >= 5) {
			rc->bitrate_status = 2; /* target too high / unreachable */
			rc->effective_target_bps = (long long)(rc->effective_target_bps * 0.85);
			if (rc->effective_target_bps < rc->params.target_bps * 0.3)
				rc->effective_target_bps = rc->params.target_bps * 0.3;
			rc->consecutive_qp_min_cnt = 0;
			logi("RC: Target too high, effective_target_bps adjusted to %lld",
				rc->effective_target_bps);
		}
	}

	rc->buffer_fullness += bits_of_delta;
	/* Widen buffer fullness limit from +/-1x to +/-3x target_bps
	 * so that cross-GOP accumulated deviation is preserved and
	 * RC can react to long-term trends instead of being blind
	 * to sustained over/under-shoot.
	 */
	long long buffer_limit = rc->params.target_bps * 3;
	if (rc->buffer_fullness < -buffer_limit)
		rc->buffer_fullness = -buffer_limit;
	else if (rc->buffer_fullness > buffer_limit)
		rc->buffer_fullness = buffer_limit;

	rc->low_bound -= (int)bits_of_delta;
	rc->up_bound1 -= (int)bits_of_delta;
	rc->up_bound2 = (int)(H264_RC_OMEGA * rc->up_bound1);

	logi("RC_BUF: delta=%d, buffer=%lld, remain=%lld, low=%d, up1=%d, up2=%d",
		bits_of_delta, rc->buffer_fullness, rc->remaining_bits,
		rc->low_bound, rc->up_bound1, rc->up_bound2);

	if (rc->slice_type == H264_SLICE_P)
		update_rc_model(rc);
}

int h264_rc_init(struct h264_rc_ctx *rc, struct h264_rc_params *params)
{
	int i;

	if (!rc || !params)
		return -1;

	memset(rc, 0, sizeof(struct h264_rc_ctx));

	rc->params = *params;

	if (rc->params.mode == H264_RC_MODE_FIX_QP) {
		rc->enabled = 0;
		return 0;
	}

	rc->enabled = 1;
	logi("rc init: mode=%d, target_bps=%d, enabled=%d",
		rc->params.mode, rc->params.target_bps, rc->enabled);

	if (rc->params.qp_min < H264_RC_MIN_QP)
		rc->params.qp_min = H264_RC_MIN_QP;
	if (rc->params.qp_max > H264_RC_MAX_QP)
		rc->params.qp_max = H264_RC_MAX_QP;
	if (rc->params.qp_max_step <= 0)
		rc->params.qp_max_step = H264_RC_QP_STEP_MAX;
	if (rc->params.gop_size <= 0)
		rc->params.gop_size = H264_RC_GOP_SIZE;

	/* Smart QP min adjustment for high bitrate scenarios.
	 * If target bitrate is very high (> 15Mbps for 720p), allow lower QP to achieve better quality.
	 * This prevents the encoder from being unable to reach the target bitrate.
	 */
	double bpp_target = (double)rc->params.target_bps /
		(rc->params.frame_rate * rc->params.width * rc->params.height);
	if (bpp_target > 0.8 && rc->params.qp_min > 10) {
		/* For very high bitrate, reduce qp_min to allow higher quality */
		int original_qp_min = rc->params.qp_min;
		rc->params.qp_min = MAX(H264_RC_MIN_QP, rc->params.qp_min - 5);
		logi("RC: High bitrate detected (bpp=%.2f), reducing qp_min from %d to %d",
			bpp_target, original_qp_min, rc->params.qp_min);
	}

	if (rc->params.qp_init <= 0)
		rc->params.qp_init = calculate_initial_qp(rc);

	rc->curr_qp = rc->params.qp_init;

	rc->prev_frame_mad = 1.0;
	rc->this_frame_mad = 1.0;
	rc->target_bits_per_frame = 0;
	rc->low_bound = 0;
	rc->up_bound1 = 0x7fffffff;
	rc->up_bound2 = 0x7fffffff;
	rc->weight_p = 0.0;
	rc->weight_b = 0.0;
	rc->ave_wp = 0.0;
	rc->ave_wb = 0.0;

	/* Better initial x1 estimation based on resolution and target bitrate.
	 * The R-D model: bits = x1 * mad / qstep (linear approximation with x2=0).
	 * Use the initial QP to estimate a reasonable qstep, then derive x1
	 * from the expected operating point. This converges much faster than
	 * the old fixed value of target_bps * 0.6 which was off by 50x for
	 * low-bitrate 720p content.
	 */
	double est_mad = 0.05;  /* typical normalized MAD for natural content */
	double est_qstep = qp2qstep(rc->params.qp_init);
	double est_bits_per_frame = (double)rc->params.target_bps / rc->params.frame_rate;
	rc->prev_x1 = est_bits_per_frame * est_qstep / est_mad;
	if (rc->prev_x1 < 1000.0)
		rc->prev_x1 = 1000.0;

	rc->prev_x2 = 0.0;
	rc->prev_mad_c1 = 1.0;
	rc->prev_mad_c2 = 0.0;

	for (i = 0; i < H264_RC_WINDOW_SIZE; i++) {
		rc->reject_qp[i] = 0;
		rc->reject_rp[i] = 0.0;
		rc->pmad_history[i] = 0.0;
	}

	rc->model_size = 0;
	rc->mad_size = 0;
	rc->num_p_picture = 0;
	rc->coded_p_frame = 0;
	rc->remaining_bits = 0;
	rc->buffer_fullness = 0;

	/* Initialize reachable bitrate detection */
	rc->consecutive_qp_max_cnt = 0;
	rc->consecutive_qp_min_cnt = 0;
	rc->effective_target_bps = rc->params.target_bps;
	rc->bitrate_status = 0;

	/* Initialize base_qp and GOP statistics */
	rc->base_qp = rc->params.qp_init;
	rc->gop_avg_p_qp = 0;
	rc->gop_p_qp_sum = 0;
	rc->startup_frame_count = 0;

	logi("RC: Initial QP=%d, base_qp=%d (calculated from bitrate)",
		rc->params.qp_init, rc->base_qp);

	return 0;
}

void h264_rc_destroy(struct h264_rc_ctx *rc)
{
	if (rc)
		memset(rc, 0, sizeof(struct h264_rc_ctx));
}

int h264_rc_start_frame(struct h264_rc_ctx *rc, int slice_type)
{
	if (!rc || !rc->enabled)
		return -1;

	rc->slice_type = slice_type;
	rc_init_frame(rc);

	logi("RC_START: frame=%d, type=%s, qp=%d, target_bits=%lld, remaining_bits=%lld, gop_frame=%d/%d",
		rc->frame_count,
		slice_type == H264_SLICE_I ? "I" : "P",
		rc->curr_qp,
		rc->target_bits_per_frame,
		rc->remaining_bits,
		rc->gop_frame_count,
		rc->params.gop_size);

	return rc->curr_qp;
}

void h264_rc_end_frame(struct h264_rc_ctx *rc, int frame_bits, int mad)
{
	if (!rc || !rc->enabled)
		return;

	rc_update_pict(rc, frame_bits, mad);

	logi("RC_END:   frame=%d, actual_bits=%d, target_bits=%lld, diff=%lld, qp=%d, remaining_bits=%lld, buffer_fullness=%lld",
		rc->frame_count,
		frame_bits,
		rc->target_bits_per_frame,
		(long long)frame_bits - rc->target_bits_per_frame,
		rc->curr_qp,
		rc->remaining_bits,
		rc->buffer_fullness);

	rc->frame_count++;
	rc->gop_frame_count++;

	if (rc->gop_frame_count >= rc->params.gop_size)
		rc->gop_frame_count = 0;
}
