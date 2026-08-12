/*
 * Copyright (C) 2020-2026 ArtInChip Technology Co. Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Author: <qi.xu@artinchip.com>
 * Desc: PSNR and SSIM quality metrics header for encoder debugging
 */

#ifndef H264_QUALITY_METRICS_H
#define H264_QUALITY_METRICS_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Enable/disable quality metrics calculation.
 * Define this macro to enable PSNR/SSIM calculation for debugging.
 * This feature should be disabled in production builds for performance.
 */
// #define H264_ENABLE_QUALITY_METRICS

/**
 * Calculate PSNR between two YUV frames
 */
int calculate_psnr(unsigned char *orig[3], unsigned char *recon[3],
		   int width, int height, int uv_interleave,
		   double *psnr_y, double *psnr_u, double *psnr_v, double *psnr_avg);

/**
 * Calculate SSIM between two YUV frames
 */
int calculate_ssim(unsigned char *orig[3], unsigned char *recon[3],
		   int width, int height, double *ssim);

/**
 * Print quality metrics to log
 */
void print_quality_metrics(int frame_num, double psnr_y, double psnr_u,
			   double psnr_v, double psnr_avg, double ssim);

#ifdef __cplusplus
}
#endif

#endif /* H264_QUALITY_METRICS_H */
