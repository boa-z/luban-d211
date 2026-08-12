/*
 * Copyright (C) 2020-2026 ArtInChip Technology Co. Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Author: <qi.xu@artinchip.com>
 * Desc: encoder test demo - encode YUV file to JPEG/H264
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <getopt.h>

#include "mpp_encoder.h"
#include "mpp_codec.h"
#include <math.h>
#include "mpp_log.h"
#include "dma_allocator.h"

#undef LOG_TAG
#define LOG_TAG "encoder_test"

struct test_config {
	char *input_file;
	char *output_file;
	int width;
	int height;
	enum mpp_codec_type codec_type;
	int frame_count;
	int quality;
	/* rate control parameters */
	enum MPP_ENC_RC_MODE rc_mode;
	int target_bps;
	int max_bps;
	int min_bps;
	int qp_min;
	int qp_max;
	int qp_init;
	int frame_rate;
};

static void print_usage(const char *prog)
{
	printf("Usage: %s [options]\n", prog);
	printf("Options:\n");
	printf("  -i, --input <file>      Input YUV file path (required)\n");
	printf("  -o, --output <file>     Output file path (required)\n");
	printf("  -w, --width <num>       Image width (required)\n");
	printf("  -h, --height <num>      Image height (required)\n");
	printf("  -c, --codec <type>      Codec type: jpeg or h264 (default: jpeg)\n");
	printf("  -n, --frames <num>      Number of frames to encode (default: 1)\n");
	printf("  -q, --quality <num>     JPEG quality 1-100 (default: 85)\n");
	printf("\nH264 Rate Control Options:\n");
	printf("  --rc-mode <mode>        Rate control mode: fixqp, cbr, vbr (default: fixqp)\n");
	printf("  --target-bps <bps>      Target bitrate in bps (default: 4000000)\n");
	printf("  --max-bps <bps>         Max bitrate in bps (VBR only)\n");
	printf("  --min-bps <bps>         Min bitrate in bps (VBR only)\n");
	printf("  --qp-min <0-51>         Min QP (default: 20)\n");
	printf("  --qp-max <0-51>         Max QP (default: 45)\n");
	printf("  --qp-init <0-51>        Initial QP (default: auto)\n");
	printf("  --fps <num>             Frame rate for bitrate stats (default: 30)\n");
	printf("  --help                  Show this help message\n");

	printf("\nExample:\n");
	printf("  %s -i input.yuv -o output.jpg -w 1920 -h 1080 -c jpeg\n", prog);
	printf("  %s -i input.yuv -o output.h264 -w 1920 -h 1080 -c h264 -n 30\n", prog);
	printf("  %s -i input.yuv -o output.h264 -w 1920 -h 1080 -c h264 -n 30 --rc-mode cbr --target-bps 4000000\n", prog);
	printf("  %s -i input.yuv -o output.h264 -w 1920 -h 1080 -c h264 -n 30 --rc-mode vbr --target-bps 4000000 --max-bps 8000000 --min-bps 2000000\n", prog);
}

static int parse_codec_arg(struct test_config *cfg, const char *arg)
{
	if (strcmp(arg, "jpeg") == 0 || strcmp(arg, "mjpeg") == 0) {
		cfg->codec_type = MPP_CODEC_VIDEO_ENCODER_MJPEG;
	} else if (strcmp(arg, "h264") == 0) {
		cfg->codec_type = MPP_CODEC_VIDEO_ENCODER_H264;
	} else {
		loge("Unknown codec type: %s", arg);
		return -1;
	}
	return 0;
}

static int parse_rc_mode_arg(struct test_config *cfg, const char *arg)
{
	if (strcmp(arg, "fixqp") == 0) {
		cfg->rc_mode = MPP_ENC_RC_MODE_FIX_QP;
	} else if (strcmp(arg, "cbr") == 0) {
		cfg->rc_mode = MPP_ENC_RC_MODE_CBR;
	} else if (strcmp(arg, "vbr") == 0) {
		cfg->rc_mode = MPP_ENC_RC_MODE_VBR;
	} else {
		loge("Unknown rc mode: %s", arg);
		return -1;
	}
	return 0;
}

static int parse_qp_arg(const char *arg, const char *name, int *qp_field)
{
	int val = atoi(arg);

	if (val < 0 || val > 51) {
		loge("%s must be between 0 and 51", name);
		return -1;
	}
	*qp_field = val;
	return 0;
}

static int parse_validate_required(struct test_config *cfg, const char *prog)
{
	if (!cfg->input_file || !cfg->output_file || cfg->width <= 0 || cfg->height <= 0) {
		loge("Missing required arguments");
		print_usage(prog);
		return -1;
	}
	return 0;
}

static int parse_args(int argc, char *argv[], struct test_config *cfg)
{
	static struct option long_options[] = {
		{"input", required_argument, 0, 'i'},
		{"output", required_argument, 0, 'o'},
		{"width", required_argument, 0, 'w'},
		{"height", required_argument, 0, 'h'},
		{"codec", required_argument, 0, 'c'},
		{"frames", required_argument, 0, 'n'},
		{"quality", required_argument, 0, 'q'},
		{"rc-mode", required_argument, 0, 1},
		{"target-bps", required_argument, 0, 2},
		{"max-bps", required_argument, 0, 3},
		{"min-bps", required_argument, 0, 4},
		{"qp-min", required_argument, 0, 5},
		{"qp-max", required_argument, 0, 6},
		{"qp-init", required_argument, 0, 7},
		{"fps", required_argument, 0, 8},
		{"help", no_argument, 0, 0},
		{0, 0, 0, 0}
	};
	int opt;
	int option_index = 0;

	cfg->codec_type = MPP_CODEC_VIDEO_ENCODER_MJPEG;
	cfg->frame_count = 1;
	cfg->quality = 85;
	cfg->rc_mode = MPP_ENC_RC_MODE_FIX_QP;
	cfg->target_bps = 4000000;
	cfg->max_bps = 0;
	cfg->min_bps = 0;
	cfg->qp_min = 1;
	cfg->qp_max = 50;
	cfg->qp_init = -1;
	cfg->frame_rate = 30;

	while ((opt = getopt_long(argc, argv, "i:o:w:h:c:n:q:", long_options, &option_index)) != -1) {
		switch (opt) {
		case 'i':
			cfg->input_file = optarg;
			break;
		case 'o':
			cfg->output_file = optarg;
			break;
		case 'w':
			cfg->width = atoi(optarg);
			break;
		case 'h':
			cfg->height = atoi(optarg);
			break;
		case 'c':
			if (parse_codec_arg(cfg, optarg) < 0)
				return -1;
			break;
		case 'n':
			cfg->frame_count = atoi(optarg);
			break;
		case 'q':
			cfg->quality = atoi(optarg);
			if (cfg->quality < 1 || cfg->quality > 100) {
				loge("Quality must be between 1 and 100");
				return -1;
			}
			break;
		case 1: // --rc-mode
			if (parse_rc_mode_arg(cfg, optarg) < 0)
				return -1;
			break;
		case 2: // --target-bps
			cfg->target_bps = atoi(optarg);
			break;
		case 3: // --max-bps
			cfg->max_bps = atoi(optarg);
			break;
		case 4: // --min-bps
			cfg->min_bps = atoi(optarg);
			break;
		case 5: // --qp-min
			if (parse_qp_arg(optarg, "qp-min", &cfg->qp_min) < 0)
				return -1;
			break;
		case 6: // --qp-max
			if (parse_qp_arg(optarg, "qp-max", &cfg->qp_max) < 0)
				return -1;
			break;
		case 7: // --qp-init
			if (parse_qp_arg(optarg, "qp-init", &cfg->qp_init) < 0)
				return -1;
			break;
		case 8: // --fps
			cfg->frame_rate = atoi(optarg);
			if (cfg->frame_rate <= 0) {
				loge("fps must be positive");
				return -1;
			}
			break;
		case 0:
			print_usage(argv[0]);
			return -1;
		default:
			print_usage(argv[0]);
			return -1;
		}
	}

	return parse_validate_required(cfg, argv[0]);
}

static void release_frame_callback(struct mpp_frame* frame, void* user_data)
{
	// frame buffer is managed by dma allocator, nothing to do here
	logd("release frame callback, pts: %lld", frame->pts);
}

static void log_config_info(struct test_config *cfg)
{
	logi("Input file: %s", cfg->input_file);
	logi("Output file: %s", cfg->output_file);
	logi("Resolution: %dx%d", cfg->width, cfg->height);
	logi("Codec type: %s", cfg->codec_type == MPP_CODEC_VIDEO_ENCODER_MJPEG ? "JPEG" : "H264");
	logi("Frame count: %d", cfg->frame_count);
	logi("Quality: %d", cfg->quality);

	if (cfg->codec_type == MPP_CODEC_VIDEO_ENCODER_H264) {
		const char *rc_mode_str[] = {"FIX_QP", "CBR", "VBR"};
		logi("RC mode: %s", rc_mode_str[cfg->rc_mode]);
		if (cfg->rc_mode != MPP_ENC_RC_MODE_FIX_QP) {
			logi("Target bps: %d", cfg->target_bps);
			if (cfg->max_bps > 0)
				logi("Max bps: %d", cfg->max_bps);
			if (cfg->min_bps > 0)
				logi("Min bps: %d", cfg->min_bps);
			logi("QP range: [%d, %d]", cfg->qp_min, cfg->qp_max);
			if (cfg->qp_init >= 0)
				logi("Initial QP: %d", cfg->qp_init);
			logi("Frame rate: %d", cfg->frame_rate);
		}
	}
}

static int open_io_files(struct test_config *cfg, FILE **input_fp, FILE **output_fp)
{
	*input_fp = fopen(cfg->input_file, "rb");
	if (!*input_fp) {
		loge("Failed to open input file: %s", cfg->input_file);
		return -1;
	}

	*output_fp = fopen(cfg->output_file, "wb");
	if (!*output_fp) {
		loge("Failed to open output file: %s", cfg->output_file);
		fclose(*input_fp);
		*input_fp = NULL;
		return -1;
	}

	return 0;
}

static int setup_dma_buffers(int width, int height, int *dma_fd,
			     int dmabuf_fd[], unsigned char *vir_addr[], int size[])
{
	int i;

	size[0] = width * height;
	size[1] = width * height / 4;
	size[2] = width * height / 4;

	*dma_fd = dmabuf_device_open();
	if (*dma_fd < 0) {
		loge("Failed to open dmabuf device");
		return -1;
	}

	for (i = 0; i < 3; i++) {
		dmabuf_fd[i] = dmabuf_alloc(*dma_fd, size[i]);
		if (dmabuf_fd[i] < 0) {
			loge("Failed to allocate dmabuf %d", i);
			goto fail;
		}
		vir_addr[i] = dmabuf_mmap(dmabuf_fd[i], size[i]);
		if (!vir_addr[i]) {
			loge("Failed to mmap dmabuf %d", i);
			goto fail;
		}
	}

	return 0;

fail:
	for (i = 0; i < 3; i++) {
		if (vir_addr[i]) {
			dmabuf_munmap(vir_addr[i], size[i]);
			vir_addr[i] = NULL;
		}
		if (dmabuf_fd[i] >= 0) {
			dmabuf_free(dmabuf_fd[i]);
			dmabuf_fd[i] = -1;
		}
	}
	dmabuf_device_close(*dma_fd);
	*dma_fd = -1;
	return -1;
}

static int create_and_config_encoder(struct test_config *cfg,
				     struct mpp_encoder **encoder)
{
	struct encode_config enc_cfg;

	*encoder = mpp_encoder_create(cfg->codec_type);
	if (!*encoder) {
		loge("Failed to create encoder");
		return -1;
	}

	memset(&enc_cfg, 0, sizeof(enc_cfg));
	enc_cfg.packet_buffer_size = 2 * 1024 * 1024;

	if (mpp_encoder_init(*encoder, &enc_cfg) < 0) {
		loge("Failed to init encoder");
		mpp_encoder_destory(*encoder);
		*encoder = NULL;
		return -1;
	}

	mpp_encoder_set_callback(*encoder, release_frame_callback, NULL);

	if (cfg->codec_type == MPP_CODEC_VIDEO_ENCODER_H264 &&
	    cfg->rc_mode != MPP_ENC_RC_MODE_FIX_QP) {
		struct mpp_enc_h264_rc_mode rc_mode;
		rc_mode.rc_mode = cfg->rc_mode;
		rc_mode.target_bps = cfg->target_bps;
		rc_mode.max_bps = cfg->max_bps > 0 ? cfg->max_bps : cfg->target_bps * 1.2;
		rc_mode.min_bps = cfg->min_bps > 0 ? cfg->min_bps : cfg->target_bps * 0.8;
		rc_mode.min_mb_qp = cfg->qp_min;
		rc_mode.max_mb_qp = cfg->qp_max;
		mpp_encoder_set_parameter(*encoder, ENC_CMD_H264_RC_MODE, &rc_mode);

		if (cfg->qp_init >= 0) {
			mpp_encoder_set_parameter(*encoder, MPP_ENC_SET_QP, &cfg->qp_init);
		}
	}

	if (cfg->codec_type == MPP_CODEC_VIDEO_ENCODER_MJPEG) {
		mpp_encoder_set_parameter(*encoder, ENC_CMD_JPEG_QUALITY, &cfg->quality);
	}

	return 0;
}

static int encode_frames(struct test_config *cfg, struct mpp_encoder *encoder,
			 FILE *input_fp, FILE *output_fp,
			 int dmabuf_fd[], unsigned char *vir_addr[], int size[],
			 int *encoded_count, long long *total_size, int *frame_bits)
{
	struct mpp_frame frame;
	struct mpp_packet packet;
	int frame_size = cfg->width * cfg->height * 3 / 2;
	unsigned char *yuv_buffer;
	int i;
	int ret;

	yuv_buffer = (unsigned char *)malloc(frame_size);
	if (!yuv_buffer) {
		loge("Failed to allocate temp YUV buffer");
		return -1;
	}

	for (i = 0; i < cfg->frame_count; i++) {
		ret = fread(yuv_buffer, 1, frame_size, input_fp);
		if (ret < frame_size) {
			if (feof(input_fp)) {
				logi("End of input file, encoded %d frames", i);
				break;
			}
			loge("Failed to read YUV frame");
			free(yuv_buffer);
			return -1;
		}

		memcpy(vir_addr[0], yuv_buffer, size[0]);
		memcpy(vir_addr[1], yuv_buffer + size[0], size[1]);
		memcpy(vir_addr[2], yuv_buffer + size[0] + size[1], size[2]);

		dmabuf_sync(dmabuf_fd[0], CACHE_CLEAN);
		dmabuf_sync(dmabuf_fd[1], CACHE_CLEAN);
		dmabuf_sync(dmabuf_fd[2], CACHE_CLEAN);

		memset(&frame, 0, sizeof(frame));
		frame.buf.format = MPP_FMT_YUV420P;
		frame.buf.size.width = cfg->width;
		frame.buf.size.height = cfg->height;
		frame.buf.stride[0] = cfg->width;
		frame.buf.stride[1] = cfg->width / 2;
		frame.buf.stride[2] = cfg->width / 2;
		frame.buf.buf_type = MPP_DMA_BUF_FD;
		frame.buf.fd[0] = dmabuf_fd[0];
		frame.buf.fd[1] = dmabuf_fd[1];
		frame.buf.fd[2] = dmabuf_fd[2];
		frame.pts = i;

		ret = mpp_encoder_put_frame(encoder, &frame);
		if (ret < 0) {
			loge("Failed to put frame to encoder");
			free(yuv_buffer);
			return -1;
		}

		ret = mpp_encoder_encode(encoder);
		if (ret < 0) {
			loge("Failed to encode frame %d, ret: %d", i, ret);
			free(yuv_buffer);
			return -1;
		}

		ret = mpp_encoder_get_packet(encoder, &packet);
		if (ret < 0) {
			loge("Failed to get packet");
			free(yuv_buffer);
			return -1;
		}

		if (packet.size > 0) {
			ret = fwrite(packet.data, 1, packet.size, output_fp);
			if (ret != packet.size) {
				loge("Failed to write output file");
				free(yuv_buffer);
				return -1;
			}
			*total_size += packet.size;
			frame_bits[*encoded_count] = packet.size * 8;
			(*encoded_count)++;
			logi("Frame %d encoded, size: %d bytes", i, packet.size);
		}

		mpp_encoder_put_packet(encoder, &packet);
	}

	free(yuv_buffer);
	return 0;
}

static void print_rc_stats(struct test_config *cfg, int encoded_count,
			   long long total_size, int frame_bits[])
{
	double target_bps = cfg->target_bps;
	double actual_avg_bps = (double)total_size * 8.0 *
				cfg->frame_rate / encoded_count;
	double avg_deviation = fabs(actual_avg_bps - target_bps) /
			       target_bps * 100.0;
	int max_frame_bits = frame_bits[0];
	int min_frame_bits = frame_bits[0];
	int max_inter_diff = 0;
	double max_inter_frame_ratio = 0.0;
	int max_instant_bps = frame_bits[0] * cfg->frame_rate;
	int min_instant_bps = frame_bits[0] * cfg->frame_rate;
	int j;

	for (j = 10; j < encoded_count; j++) {
		if ((j % 30) == 0 || (j % 30) == 1)
			continue;
		int instant_bps = frame_bits[j] * cfg->frame_rate;
		int inter_diff = abs(frame_bits[j] - frame_bits[j - 1]);
		double inter_ratio = (double)inter_diff / frame_bits[j - 1] * 100.0;

		if (instant_bps > max_instant_bps)
			max_instant_bps = instant_bps;
		if (instant_bps < min_instant_bps)
			min_instant_bps = instant_bps;
		if (frame_bits[j] > max_frame_bits)
			max_frame_bits = frame_bits[j];
		if (frame_bits[j] < min_frame_bits)
			min_frame_bits = frame_bits[j];
		if (inter_diff > max_inter_diff)
			max_inter_diff = inter_diff;
		if (inter_ratio > max_inter_frame_ratio)
			max_inter_frame_ratio = inter_ratio;
	}

	logi("========== Rate Control Statistics ==========");
	logi("Target bps:      %.0f", target_bps);
	logi("Actual avg bps:  %.0f (deviation: %.2f%%)",
	     actual_avg_bps, avg_deviation);
	logi("Instant bps range: [%d, %d] (fluctuation: %.2f%% ~ +%.2f%% of target)",
	     min_instant_bps, max_instant_bps,
	     (min_instant_bps - target_bps) * 100.0 / target_bps,
	     (max_instant_bps - target_bps) * 100.0 / target_bps);
	logi("Frame bits range:  [%d, %d] (fluctuation: %.2f%% ~ +%.2f%% of target-per-frame)",
	     min_frame_bits, max_frame_bits,
	     (min_frame_bits - target_bps / cfg->frame_rate) * 100.0 /
	     (target_bps / cfg->frame_rate),
	     (max_frame_bits - target_bps / cfg->frame_rate) * 100.0 /
	     (target_bps / cfg->frame_rate));
	logi("Max inter-frame fluctuation: %.2f%% (relative to previous frame)",
	     max_inter_frame_ratio);
	logi("=============================================");
}

int main(int argc, char *argv[])
{
	struct test_config cfg;
	struct mpp_encoder *encoder = NULL;
	FILE *input_fp = NULL;
	FILE *output_fp = NULL;
	int dma_fd = -1;
	int dmabuf_fd[3] = {-1, -1, -1};
	unsigned char *vir_addr[3] = {NULL};
	int size[3];
	int encoded_frame_count = 0;
	long long total_output_size = 0;
	int *frame_bits = NULL;
	int i;
	int ret = -1;

	if (parse_args(argc, argv, &cfg) < 0)
		return -1;

	log_config_info(&cfg);

	if (open_io_files(&cfg, &input_fp, &output_fp) < 0)
		return -1;

	if (setup_dma_buffers(cfg.width, cfg.height, &dma_fd, dmabuf_fd,
			      vir_addr, size) < 0)
		goto cleanup;

	if (create_and_config_encoder(&cfg, &encoder) < 0)
		goto cleanup;

	frame_bits = (int *)malloc(cfg.frame_count * sizeof(int));
	if (!frame_bits) {
		loge("Failed to allocate frame bits buffer");
		goto cleanup;
	}

	if (encode_frames(&cfg, encoder, input_fp, output_fp,
			  dmabuf_fd, vir_addr, size,
			  &encoded_frame_count, &total_output_size, frame_bits) < 0)
		goto cleanup;

	logi("Encoding completed successfully!");
	logi("Total output file size: %lld bytes (%.2f KB, %.2f MB)",
	     total_output_size,
	     (double)total_output_size / 1024,
	     (double)total_output_size / (1024 * 1024));

	if (cfg.codec_type == MPP_CODEC_VIDEO_ENCODER_H264 &&
	    cfg.rc_mode != MPP_ENC_RC_MODE_FIX_QP &&
	    encoded_frame_count > 0) {
		print_rc_stats(&cfg, encoded_frame_count, total_output_size, frame_bits);
	}

	ret = 0;

cleanup:
	if (encoder)
		mpp_encoder_destory(encoder);
	if (input_fp)
		fclose(input_fp);
	if (output_fp)
		fclose(output_fp);
	if (frame_bits)
		free(frame_bits);
	for (i = 0; i < 3; i++) {
		if (vir_addr[i])
			dmabuf_munmap(vir_addr[i], size[i]);
		if (dmabuf_fd[i] >= 0)
			dmabuf_free(dmabuf_fd[i]);
	}
	if (dma_fd >= 0)
		dmabuf_device_close(dma_fd);

	return ret;
}
