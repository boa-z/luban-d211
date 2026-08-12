/*
 * Copyright (C) 2020-2026 ArtInChip Technology Co. Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Author: <qi.xu@artinchip.com>
 * Desc: PSNR and SSIM quality metrics for encoder debugging
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "mpp_dec_type.h"
#include "ve_buffer.h"
#include "h264_encoder.h"
#include "h264_quality_metrics.h"

#ifdef H264_ENABLE_QUALITY_METRICS

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/**
 * Calculate PSNR between two YUV frames
 * @orig: original frame buffers (Y, U, V)
 * @recon: reconstructed frame buffers (Y, U, V)
 * @width: frame width
 * @height: frame height
 * @uv_interleave: whether UV is interleaved (NV12/NV21)
 * @psnr_y: output PSNR for Y component
 * @psnr_u: output PSNR for U component
 * @psnr_v: output PSNR for V component
 * @psnr_avg: output average PSNR
 * @return: 0 on success, -1 on failure
 */
int calculate_psnr(unsigned char *orig[3], unsigned char *recon[3],
		   int width, int height, int uv_interleave,
		   double *psnr_y, double *psnr_u, double *psnr_v, double *psnr_avg)
{
	int y_size = width * height;
	int uv_width = width / 2;
	int uv_height = height / 2;
	int uv_size = uv_width * uv_height;
	int i;
	double mse_y = 0.0, mse_u = 0.0, mse_v = 0.0;
	double diff;

	if (!orig || !recon || !orig[0] || !recon[0])
		return -1;

	/* Calculate MSE for Y component */
	for (i = 0; i < y_size; i++) {
		diff = (double)orig[0][i] - (double)recon[0][i];
		mse_y += diff * diff;
	}
	mse_y /= y_size;

	/* Calculate MSE for U and V components */
	if (uv_interleave) {
		/* NV12/NV21: UV interleaved in buf[1] */
		for (i = 0; i < uv_size; i++) {
			/* U component (even indices) */
			diff = (double)orig[1][i * 2] - (double)recon[1][i * 2];
			mse_u += diff * diff;
			/* V component (odd indices) */
			diff = (double)orig[1][i * 2 + 1] - (double)recon[1][i * 2 + 1];
			mse_v += diff * diff;
		}
	} else {
		/* YUV420P: separate U and V planes */
		for (i = 0; i < uv_size; i++) {
			if (orig[1] && recon[1]) {
				diff = (double)orig[1][i] - (double)recon[1][i];
				mse_u += diff * diff;
			}
			if (orig[2] && recon[2]) {
				diff = (double)orig[2][i] - (double)recon[2][i];
				mse_v += diff * diff;
			}
		}
	}
	mse_u /= uv_size;
	mse_v /= uv_size;

	/* Calculate PSNR (peak signal value = 255 for 8-bit) */
	if (mse_y < 1e-10)
		*psnr_y = 100.0; /* Perfect match */
	else
		*psnr_y = 10.0 * log10(255.0 * 255.0 / mse_y);

	if (mse_u < 1e-10)
		*psnr_u = 100.0;
	else
		*psnr_u = 10.0 * log10(255.0 * 255.0 / mse_u);

	if (mse_v < 1e-10)
		*psnr_v = 100.0;
	else
		*psnr_v = 10.0 * log10(255.0 * 255.0 / mse_v);

	/* Weighted average PSNR (4:2:0 format: Y has 4x the samples of U or V) */
	*psnr_avg = (4.0 * (*psnr_y) + (*psnr_u) + (*psnr_v)) / 6.0;

	return 0;
}

/**
 * Calculate SSIM between two YUV frames
 * Uses simplified SSIM calculation on Y component only for performance
 * @orig: original frame buffers (Y, U, V)
 * @recon: reconstructed frame buffers (Y, U, V)
 * @width: frame width
 * @height: frame height
 * @ssim: output SSIM value (0-1, higher is better)
 * @return: 0 on success, -1 on failure
 */
int calculate_ssim(unsigned char *orig[3], unsigned char *recon[3],
		   int width, int height, double *ssim)
{
	int i, j;
	double ssim_sum = 0.0;
	int block_count = 0;

	/* SSIM constants */
	const double C1 = (0.01 * 255) * (0.01 * 255);
	const double C2 = (0.03 * 255) * (0.03 * 255);
	const int window_size = 8; /* 8x8 window */

	if (!orig || !recon || !orig[0] || !recon[0])
		return -1;

	/* Calculate SSIM using sliding window approach */
	for (i = 0; i <= height - window_size; i += window_size) {
		for (j = 0; j <= width - window_size; j += window_size) {
			double mu_x = 0.0, mu_y = 0.0;
			double sigma_x2 = 0.0, sigma_y2 = 0.0, sigma_xy = 0.0;
			int k, l;
			int pixel_count = 0;

			/* Calculate means */
			for (k = 0; k < window_size; k++) {
				for (l = 0; l < window_size; l++) {
					int idx = (i + k) * width + (j + l);
					mu_x += orig[0][idx];
					mu_y += recon[0][idx];
					pixel_count++;
				}
			}
			mu_x /= pixel_count;
			mu_y /= pixel_count;

			/* Calculate variances and covariance */
			for (k = 0; k < window_size; k++) {
				for (l = 0; l < window_size; l++) {
					int idx = (i + k) * width + (j + l);
					double diff_x = orig[0][idx] - mu_x;
					double diff_y = recon[0][idx] - mu_y;
					sigma_x2 += diff_x * diff_x;
					sigma_y2 += diff_y * diff_y;
					sigma_xy += diff_x * diff_y;
				}
			}
			sigma_x2 /= pixel_count;
			sigma_y2 /= pixel_count;
			sigma_xy /= pixel_count;

			/* Calculate SSIM for this window */
			double numerator = (2 * mu_x * mu_y + C1) * (2 * sigma_xy + C2);
			double denominator = (mu_x * mu_x + mu_y * mu_y + C1) *
					     (sigma_x2 + sigma_y2 + C2);
			ssim_sum += numerator / denominator;
			block_count++;
		}
	}

	if (block_count > 0)
		*ssim = ssim_sum / block_count;
	else
		*ssim = 0.0;

	return 0;
}

/**
 * Print quality metrics to log
 */
void print_quality_metrics(int frame_num, double psnr_y, double psnr_u,
			   double psnr_v, double psnr_avg, double ssim)
{
	logi("QUALITY: frame=%d, PSNR_Y=%.2f, PSNR_U=%.2f, PSNR_V=%.2f, "
	     "PSNR_AVG=%.2f, SSIM=%.4f",
	     frame_num, psnr_y, psnr_u, psnr_v, psnr_avg, ssim);
}

#else /* H264_ENABLE_QUALITY_METRICS not defined */

/* Stub implementations when feature is disabled */
int calculate_psnr(unsigned char *orig[3], unsigned char *recon[3],
		   int width, int height, int uv_interleave,
		   double *psnr_y, double *psnr_u, double *psnr_v, double *psnr_avg)
{
	return 0;
}

int calculate_ssim(unsigned char *orig[3], unsigned char *recon[3],
		   int width, int height, double *ssim)
{
	return 0;
}

void print_quality_metrics(int frame_num, double psnr_y, double psnr_u,
			   double psnr_v, double psnr_avg, double ssim)
{
	(void)frame_num;
	(void)psnr_y;
	(void)psnr_u;
	(void)psnr_v;
	(void)psnr_avg;
	(void)ssim;
}

#endif /* H264_ENABLE_QUALITY_METRICS */
