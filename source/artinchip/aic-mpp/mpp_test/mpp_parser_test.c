/*
 * Copyright (C) 2020-2026 ArtInChip Technology Co. Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Desc: Generic parser test - demux any supported format, optionally
 *       dump raw ES to file.  Supports single-file and batch-directory
 *       modes.
 *
 * Usage: parser_test -i <input> [-o <output>] [-v|-a] [-V] [-t <dir>]
 *   -i   input file
 *   -t   directory of test files (batch mode)
 *   -o   output raw ES to this file (no default, only dumps when set)
 *   -v   dump video stream only (default: video)
 *   -a   dump audio stream only
 *   -V   verbose: print first 16 bytes of each dumped frame
 *   -h   help
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <dirent.h>
#include <getopt.h>
#include <inttypes.h>

#include "aic_parser.h"
#include "aic_stream.h"
#include "mpp_log.h"
#include "mpp_mem.h"

#define PARSER_FILE_MAX_NUM      128
#define PARSER_FILE_PATH_MAX_LEN 256
#define PACKET_MAX_SIZE          (512 * 1024)

struct parser_file_list {
	char *file_path[PARSER_FILE_MAX_NUM];
	int file_num;
};

struct parser_context {
	struct aic_parser *parser;
	struct parser_file_list files;
	char out_file[256];
	int output_en;
	int output_type;
	int verbose;
	int fd;
	struct aic_parser_av_media_info media_info;
};

static const char *vdec_type_str(int type)
{
	switch (type) {
	case MPP_CODEC_VIDEO_DECODER_H264:   return "H264";
	case MPP_CODEC_VIDEO_DECODER_MPEG12: return "MPEG12";
	case MPP_CODEC_VIDEO_DECODER_MPEG4:  return "MPEG4";
	case MPP_CODEC_VIDEO_DECODER_MJPEG:  return "MJPEG";
	default: return "unknown";
	}
}

static const char *adec_type_str(int type)
{
	switch (type) {
	case MPP_CODEC_AUDIO_DECODER_MP3: return "MP3";
	case MPP_CODEC_AUDIO_DECODER_AAC: return "AAC";
	default: return "unknown";
	}
}

static void print_help(const char *prog)
{
	printf("Usage: %s -i <input> [options]\n", prog);
	printf("Options:\n"
	       "  -i <file>   input media file\n"
	       "  -t <dir>    batch mode: test all files in directory\n"
	       "  -o <file>   dump raw ES to output file (no default)\n"
	       "  -v          dump video stream only (default)\n"
	       "  -a          dump audio stream only\n"
	       "  -V          verbose: print first 16 bytes of each dumped frame\n"
	       "  -h          this help\n");
}

static int read_file(const char *path, struct parser_file_list *files)
{
	int len = strlen(path);

	if (len >= PARSER_FILE_PATH_MAX_LEN) {
		loge("file path too long\n");
		return -1;
	}
	files->file_path[0] = (char *)mpp_alloc(len + 1);
	if (!files->file_path[0])
		return -1;
	strncpy(files->file_path[0], path, len);
	files->file_path[0][len] = '\0';
	files->file_num = 1;
	return 0;
}

static int read_dir(const char *path, struct parser_file_list *files)
{
	struct dirent *ent;
	char *ptr;
	int len;

	DIR *dir = opendir(path);
	if (!dir) {
		loge("opendir %s failed\n", path);
		return -1;
	}

	while ((ent = readdir(dir))) {
		if (!strncmp(ent->d_name, ".", 2) || !strncmp(ent->d_name, "..", 3))
			continue;

		ptr = strrchr(ent->d_name, '.');
		if (!ptr)
			continue;

		len = strlen(path) + 1 + strlen(ent->d_name);
		if (len >= PARSER_FILE_PATH_MAX_LEN)
			continue;

		files->file_path[files->file_num] = (char *)mpp_alloc(len + 1);
		if (!files->file_path[files->file_num])
			continue;

		snprintf(files->file_path[files->file_num], len + 1,
			 "%s/%s", path, ent->d_name);
		files->file_num++;
		if (files->file_num >= PARSER_FILE_MAX_NUM)
			break;
	}
	closedir(dir);
	return 0;
}

static int parse_options(struct parser_context *ctx, int argc, char **argv)
{
	int opt;

	optind = 0;
	while ((opt = getopt(argc, argv, "i:t:o:vaVh")) != -1) {
		switch (opt) {
		case 'i':
			read_file(optarg, &ctx->files);
			break;
		case 't':
			read_dir(optarg, &ctx->files);
			break;
		case 'o':
			strncpy(ctx->out_file, optarg, sizeof(ctx->out_file) - 1);
			ctx->out_file[sizeof(ctx->out_file) - 1] = '\0';
			ctx->output_en = 1;
			break;
		case 'v':
			ctx->output_type = MPP_MEDIA_TYPE_VIDEO;
			break;
		case 'a':
			ctx->output_type = MPP_MEDIA_TYPE_AUDIO;
			break;
		case 'V':
			ctx->verbose = 1;
			break;
		case 'h':
		default:
			print_help(argv[0]);
			return -1;
		}
	}

	if (ctx->files.file_num == 0) {
		print_help(argv[0]);
		return -1;
	}
	return 0;
}

static void print_media_info(const char *file,
			     struct aic_parser_av_media_info *info)
{
	printf("%s (%lld bytes)\n", file, (long long)info->file_size);

	if (info->has_video) {
		printf("\tVideo info:\n");
		printf("\t\tdec_type:\t\t%s\n",
		       vdec_type_str(info->video_stream.codec_type));
		printf("\t\twidth x height:\t\t%d x %d\n",
		       info->video_stream.width, info->video_stream.height);
	}
	if (info->has_audio) {
		for (int i = 0; i < info->audio_track_count; i++) {
			printf("\tAudio info:\n");
			printf("\t\tdec_type:\t\t%s\n",
			       adec_type_str(info->audio_stream[i].codec_type));
			printf("\t\tbit_width:\t\t%d\n",
			       info->audio_stream[i].bits_per_sample);
			printf("\t\tnb_channel:\t\t%d\n",
			       info->audio_stream[i].nb_channel);
			printf("\t\tsample_rate:\t\t%d\n",
			       info->audio_stream[i].sample_rate);
		}
	}
}

static void print_verbose(struct aic_parser_packet *pkt, int frame_no)
{
	int n = pkt->size < 16 ? pkt->size : 16;
	unsigned char *d = (unsigned char *)pkt->data;

	printf("[%c-frame %4d] size=%6d pts=%lld first=",
	       pkt->type == MPP_MEDIA_TYPE_VIDEO ? 'v' : 'a',
	       frame_no, pkt->size, (long long)pkt->pts);
	for (int i = 0; i < n; i++)
		printf("%02x", d[i]);
	printf("\n");
}

static void parser_write_file(struct parser_context *ctx,
			      struct aic_parser_packet *packet,
			      struct aic_parser_av_media_info *media_info,
			      int *v_frames, int *a_frames)
{
	if (ctx->fd < 0) {
		ctx->fd = open(ctx->out_file, O_CREAT | O_RDWR | O_TRUNC, 0666);
		if (ctx->fd < 0) {
			loge("open %s failed\n", ctx->out_file);
			return;
		}
		/* write extra data (SPS/PPS for H264, config for audio) */
		if (ctx->output_type == MPP_MEDIA_TYPE_VIDEO &&
		    media_info->video_stream.extra_data_size > 0) {
			write(ctx->fd, media_info->video_stream.extra_data,
			      media_info->video_stream.extra_data_size);
		} else if (ctx->output_type == MPP_MEDIA_TYPE_AUDIO &&
			   media_info->audio_stream[0].extra_data_size > 0) {
			write(ctx->fd, media_info->audio_stream[0].extra_data,
			      media_info->audio_stream[0].extra_data_size);
		}
	}

	if (packet->type != ctx->output_type)
		return;

	write(ctx->fd, packet->data, packet->size);

	if (packet->type == MPP_MEDIA_TYPE_VIDEO)
		(*v_frames)++;
	else
		(*a_frames)++;

	if (ctx->verbose)
		print_verbose(packet,
			      packet->type == MPP_MEDIA_TYPE_VIDEO ?
			      *v_frames : *a_frames);

	if (packet->flag & PACKET_EOS) {
		close(ctx->fd);
		ctx->fd = -1;
	}
}

static void process_parser_data(struct parser_context *ctx,
				struct aic_parser_av_media_info *media_info,
				const char *input_file)
{
	struct aic_parser_packet packet;
	char *buffer;
	int ret, v_frames = 0, a_frames = 0;

	if (!ctx->output_en)
		return;

	buffer = (char *)mpp_alloc(PACKET_MAX_SIZE);
	if (!buffer)
		return;

	memset(&packet, 0, sizeof(packet));

	/* batch mode: generate per-file output name from input filename */
	if (ctx->files.file_num > 1 && ctx->output_en) {
		const char *base = strrchr(input_file, '/');
		base = base ? base + 1 : input_file;
		const char *dot = strrchr(base, '.');
		int name_len = dot ? (int)(dot - base) : (int)strlen(base);
		snprintf(ctx->out_file, sizeof(ctx->out_file),
			 "%.*s.es", name_len, base);
	}

	printf("Demuxing -> %s ...\n", ctx->out_file);

	while (1) {
		ret = aic_parser_peek(ctx->parser, &packet);
		if (ret == PARSER_EOS) {
			printf("EOS reached.\n");
			break;
		}
		if (ret != PARSER_OK) {
			usleep(5000);
			continue;
		}

		if (packet.size > PACKET_MAX_SIZE) {
			loge("packet too large: %d, skip\n", packet.size);
			packet.data = mpp_alloc(packet.size);
			if (packet.data) {
				aic_parser_read(ctx->parser, &packet);
				mpp_free(packet.data);
			}
			continue;
		}

		packet.data = buffer;
		ret = aic_parser_read(ctx->parser, &packet);
		if (ret != PARSER_OK) {
			usleep(5000);
			continue;
		}

		parser_write_file(ctx, &packet, media_info,
				  &v_frames, &a_frames);

		if (packet.flag & PACKET_EOS)
			break;
	}

	printf("Done: %d video, %d audio frames.\n", v_frames, a_frames);
	mpp_free(buffer);
}

int main(int argc, char **argv)
{
	struct parser_context *ctx;
	struct aic_parser_av_media_info *media_info;
	int i;

	ctx = (struct parser_context *)mpp_alloc(sizeof(*ctx));
	if (!ctx) {
		loge("mpp_alloc failed\n");
		return -1;
	}
	memset(ctx, 0, sizeof(*ctx));
	ctx->output_type = MPP_MEDIA_TYPE_VIDEO;

	if (parse_options(ctx, argc, argv))
		goto exit;

	media_info = &ctx->media_info;

	for (i = 0; i < ctx->files.file_num; i++) {
		if (aic_parser_create((unsigned char *)ctx->files.file_path[i],
				      &ctx->parser) < 0) {
			loge("aic_parser_create %s failed\n",
			     ctx->files.file_path[i]);
			continue;
		}

		if (aic_parser_init(ctx->parser)) {
			loge("aic_parser_init failed\n");
			goto next_file;
		}

		memset(media_info, 0, sizeof(*media_info));
		if (aic_parser_get_media_info(ctx->parser, media_info)) {
			loge("aic_parser_get_media_info failed\n");
			goto next_file;
		}

		print_media_info(ctx->files.file_path[i], media_info);

		if (ctx->output_en) {
			ctx->fd = -1;
			process_parser_data(ctx, media_info,
					    ctx->files.file_path[i]);
		}

next_file:
		aic_parser_destroy(ctx->parser);
		ctx->parser = NULL;
	}

exit:
	for (i = 0; i < ctx->files.file_num; i++)
		mpp_free(ctx->files.file_path[i]);
	mpp_free(ctx);
	return 0;
}
