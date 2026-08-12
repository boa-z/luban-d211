/*
 * Copyright (C) 2020-2026 ArtInChip Technology Co. Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 *  author: <che.jiang@artinchip.com>
 *  Desc: muxer demo — mux MJPEG/H264 + MP3/AAC into MP4/TS
 */

#include <string.h>
#include <malloc.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/types.h>
#include <signal.h>
#include <dirent.h>
#include <inttypes.h>
#include <time.h>

#define LOG_DEBUG
#define MAX_BUF_SIZE (512 * 1024)
#define MAX_AUDIO_BUF_SIZE (32 * 1024)
#define FRAME_DURATION 40 /* ms, for 25 fps (MJPEG fallback) */
#define LOOP_SEC 60

#include "mpp_dec_type.h"
#include "mpp_list.h"
#include "mpp_log.h"
#include "mpp_mem.h"
#include "aic_muxer.h"
#include "aic_parser.h"

struct demo_config {
	char vin_path[256];
	char ain_path[256];
	char out_path[256];
	int codec_type;
	int acodec_type;
	int audio_dur;
	enum aic_muxer_type muxer_type;
	struct aic_av_media_info info;
};

static void print_help(const char *prog)
{
	printf("Compile time: %s\n", __TIME__);
	printf("Usage: %s [options]:\n"
	       "\t-v     video input file  (.jpg / .264)\n"
	       "\t-a     audio input file  (.mp3)\n"
	       "\t-o     output file (.mp4 / .ts)\n"
	       "\t-h     help\n\n"
	       "Example:\n"
	       "  %s -v /mnt/test.264 -a /mnt/test.mp3 -o /mnt/test.mp4\n",
	       prog, prog);
}

/* ================================================================
 *  Dimension probe — JPEG (SOF scan) / H264 (aic_parser)
 * ================================================================ */

static int probe_jpeg_dimensions(const char *path, int *w, int *h)
{
	uint8_t buf[4096];
	int fd, n, i;

	fd = open(path, O_RDONLY);
	if (fd < 0)
		return -1;
	n = read(fd, buf, sizeof(buf));
	close(fd);
	if (n < 10)
		return -1;

	for (i = 0; i < n - 9; i++) {
		if (buf[i] != 0xFF)
			continue;
		int marker = buf[i + 1];
		if (marker >= 0xC0 && marker <= 0xC3) {
			*h = (buf[i + 5] << 8) | buf[i + 6];
			*w = (buf[i + 7] << 8) | buf[i + 8];
			return 0;
		}
		if (marker != 0xD8 && marker != 0xD9)
			i += 2 + ((buf[i + 2] << 8) | buf[i + 3]) - 1;
	}
	return -1;
}

static int parser_video_config(struct demo_config *cfg)
{
	if (cfg->codec_type == MPP_CODEC_VIDEO_ENCODER_H264) {
		struct aic_parser *parser = NULL;
		struct aic_parser_av_media_info parser_info;

		if (aic_parser_create((unsigned char *)cfg->vin_path, &parser) <
		    0)
			return -1;
		aic_parser_init(parser);
		aic_parser_get_media_info(parser, &parser_info);

		cfg->info.has_video = 1;
		cfg->info.duration = parser_info.duration;
		cfg->info.video_stream = parser_info.video_stream;
		cfg->info.video_stream.codec_type = cfg->codec_type;
		if (!cfg->info.video_stream.frame_rate)
			cfg->info.video_stream.frame_rate = 25;

		aic_parser_destroy(parser);
	} else if (cfg->codec_type == MPP_CODEC_VIDEO_ENCODER_MJPEG) {
		cfg->info.has_video = 1;
		cfg->info.video_stream.codec_type = cfg->codec_type;
		cfg->info.video_stream.frame_rate = 25;
		probe_jpeg_dimensions(cfg->vin_path,
				      &cfg->info.video_stream.width,
				      &cfg->info.video_stream.height);
	} else {
		return -1;
	}

	logi("video: %dx%d, fps=%d, codec=%d", cfg->info.video_stream.width,
	     cfg->info.video_stream.height, cfg->info.video_stream.frame_rate,
	     cfg->codec_type);
	return 0;
}

static int parser_audio_config(struct demo_config *cfg)
{
	struct aic_parser_av_media_info parser_info;
	struct aic_parser *parser = NULL;
	int sr, spf;

	if (cfg->acodec_type != MPP_CODEC_AUDIO_ENCODER_MP3)
		return -1;

	if (aic_parser_create((unsigned char *)cfg->ain_path, &parser) < 0)
		return -1;
	aic_parser_init(parser);
	aic_parser_get_media_info(parser, &parser_info);
	aic_parser_destroy(parser);
	if (parser_info.audio_stream[0].sample_rate <= 0) {
		cfg->info.has_audio = 0;
		return -1;
	}
	cfg->info.has_audio = 1;
	cfg->info.audio_stream[0] = parser_info.audio_stream[0];
	cfg->info.audio_stream[0].codec_type = cfg->acodec_type;
	cfg->info.audio_track_count = 1;

	/* MP3 frame duration:
		*   MPEG-1 (sr >= 32k): 1152 samples/frame
		*   MPEG-2 (sr <  32k):  576 samples/frame */
	sr = parser_info.audio_stream[0].sample_rate;
	spf = sr >= 32000 ? 1152 : 576;
	cfg->audio_dur = spf * 1000 / sr;

	logi("audio frame dur: %d ms (sr=%d, spf=%d)", cfg->audio_dur, sr, spf);
	logi("audio: sr=%d, ch=%d, codec=%d",
	     cfg->info.audio_stream[0].sample_rate,
	     cfg->info.audio_stream[0].nb_channel, cfg->acodec_type);

	return 0;
}

/* ================================================================
 *  Argument parsing
 * ================================================================ */

static int parse_args(int argc, char *argv[], struct demo_config *cfg)
{
	char *ptr;

	memset(cfg, 0, sizeof(*cfg));
	cfg->acodec_type = MPP_CODEC_AUDIO_ENCODER_UNKOWN;

	optind = 0;
	while (1) {
		int opt = getopt(argc, argv, "v:a:o:W:H:h");
		if (opt == -1)
			break;
		switch (opt) {
		case 'v':
			snprintf(cfg->vin_path, sizeof(cfg->vin_path), "%s",
				 optarg);
			break;
		case 'a':
			snprintf(cfg->ain_path, sizeof(cfg->ain_path), "%s",
				 optarg);
			break;
		case 'o':
			snprintf(cfg->out_path, sizeof(cfg->out_path), "%s",
				 optarg);
			break;
		case 'h':
			print_help(argv[0]);
			return 1;
		default:
			break;
		}
	}

	if (strlen(cfg->vin_path) == 0 || strlen(cfg->out_path) == 0) {
		loge("param error: need -v and -o");
		print_help(argv[0]);
		return -1;
	}

	ptr = strrchr(cfg->out_path, '.');
	if (!ptr) {
		loge("Invalid output url");
		return -1;
	}
	if (strncmp(ptr + 1, "ts", 2) == 0) {
		cfg->muxer_type = AIC_MUXER_TYPE_TS;
	} else if (strncmp(ptr + 1, "mp4", 3) == 0) {
		cfg->muxer_type = AIC_MUXER_TYPE_MP4;
	} else {
		loge("Unsupport muxer type: %s.", ptr + 1);
		return -1;
	}

	ptr = strrchr(cfg->vin_path, '.');
	if (!ptr) {
		loge("invalid input suffix");
		return -1;
	}
	if (strncmp(ptr + 1, "264", 3) == 0) {
		cfg->codec_type = MPP_CODEC_VIDEO_ENCODER_H264;
	} else if (strncmp(ptr + 1, "jpg", 3) == 0) {
		cfg->codec_type = MPP_CODEC_VIDEO_ENCODER_MJPEG;
	} else {
		loge("Unsupport video type: %s.", ptr + 1);
		return -1;
	}

	if (strlen(cfg->ain_path) > 0) {
		ptr = strrchr(cfg->ain_path, '.');
		if (!ptr) {
			loge("Invalid audio suffix");
			return -1;
		}
		if (strncmp(ptr + 1, "mp3", 3) == 0) {
			cfg->acodec_type = MPP_CODEC_AUDIO_ENCODER_MP3;
		} else {
			loge("Unsupport audio type: %s.", ptr + 1);
			return -1;
		}
	}
	return 0;
}

/* ================================================================
 *  Muxer helpers
 * ================================================================ */

static struct aic_muxer *create_muxer(const struct demo_config *cfg)
{
	struct aic_muxer *muxer = NULL;
	aic_muxer_create((unsigned char *)cfg->out_path, &muxer,
			 cfg->muxer_type);
	if (!muxer)
		loge("aic_muxer_create error");
	return muxer;
}

/* ================================================================
 *  MJPEG path
 * ================================================================ */
static int do_mjpeg_mux(struct aic_muxer *muxer, struct aic_av_media_info *info,
			const struct demo_config *cfg)
{
	struct aic_parser *aparser = NULL;
	struct aic_parser_packet apkt;
	struct aic_av_packet packet, apacket;
	time_t start_time, last_log = 0;
	uint8_t *abuf = NULL;
	int fd = -1, size, vframe = 0, aframe = 0, audio_eos = 0;
	int64_t vpts = 0, apts = 0;
	int audio_dur = cfg->audio_dur;
	int ret = -1;

	memset(&packet, 0, sizeof(packet));
	packet.type = MPP_MEDIA_TYPE_VIDEO;

	/* read the one JPEG frame */
	fd = open(cfg->vin_path, O_RDONLY);
	if (fd < 0) {
		loge("open %s error", cfg->vin_path);
		return -1;
	}
	size = lseek(fd, 0, SEEK_END);
	lseek(fd, 0, SEEK_SET);

	packet.data = mpp_alloc(size);
	if (!packet.data) {
		loge("mpp_alloc error");
		goto out;
	}
	if (read(fd, packet.data, size) <= 0) {
		loge("read jpeg failed");
		goto out;
	}
	packet.size = size;
	packet.duration = FRAME_DURATION;
	close(fd);
	fd = -1;

	/* ---- audio parser ---- */
	if (info->has_audio) {
		if (aic_parser_create((unsigned char *)cfg->ain_path,
				      &aparser) < 0) {
			loge("audio parser create failed");
			goto out;
		}
		aic_parser_init(aparser);
		abuf = mpp_alloc(MAX_AUDIO_BUF_SIZE);
		if (!abuf)
			goto out;
		memset(&apkt, 0, sizeof(apkt));
		memset(&apacket, 0, sizeof(apacket));
		aic_parser_peek(aparser, &apkt);
	}

	aic_muxer_init(muxer, info);
	aic_muxer_write_header(muxer);

	logi("MJPEG loop: %dx%d, remux for %ds...", info->video_stream.width,
	     info->video_stream.height, LOOP_SEC);
	start_time = time(NULL);
	while (1) {
		time_t now = time(NULL);
		int dur_ms =
			(info->duration > 0 ? info->duration : LOOP_SEC) * 1000;
		if (vpts >= dur_ms)
			break;

		/* ---- video: repeat JPEG ---- */
		packet.pts = packet.dts = vpts;
		aic_muxer_write_packet(muxer, &packet);
		vframe++;
		vpts += FRAME_DURATION;

		/* ---- audio: write frames until apts >= vpts ---- */
		while (info->has_audio && !audio_eos && apts < vpts) {
			int rc = aic_parser_peek(aparser, &apkt);
			if (rc == PARSER_EOS || apkt.size <= 0) {
				audio_eos = 1;
				break;
			}
			apkt.data = abuf;
			aic_parser_read(aparser, &apkt);

			apacket.type = MPP_MEDIA_TYPE_AUDIO;
			apacket.data = apkt.data;
			apacket.size = apkt.size;
			apacket.pts = apacket.dts = apts;
			apacket.duration = audio_dur;
			apacket.stream_index = 1;
			aic_muxer_write_packet(muxer, &apacket);
			aframe++;
			apts += audio_dur;
		}

		if (now != last_log) {
			logi("frame:%d  elapsed:%lds", vframe,
			     now - start_time);
			last_log = now;
		}
	}

	logi("MJPEG done: %d vframes, %d aframes in %lds", vframe, aframe,
	     time(NULL) - start_time);
	ret = 0;
out:
	mpp_free(packet.data);
	mpp_free(abuf);
	if (fd > 0)
		close(fd);
	if (aparser)
		aic_parser_destroy(aparser);
	return ret;
}

/* ================================================================
 *  H264 path — with optional MP3 audio interleaving
 *  Sync strategy: write whichever stream has the lower PTS.
 * ================================================================ */
static int do_h264_mux(struct aic_muxer *muxer, struct aic_av_media_info *info,
		       const struct demo_config *cfg)
{
	struct aic_parser *vparser = NULL, *aparser = NULL;
	struct aic_parser_packet vpkt, apkt;
	struct aic_av_packet out_pkt;
	uint8_t *vbuf = NULL, *abuf = NULL;
	int vframe_dur = info->video_stream.frame_rate > 0 ?
				 1000 / info->video_stream.frame_rate :
				 FRAME_DURATION;
	int vframe = 0, aframe = 0, audio_eos = 0;
	int64_t vpts = 0, apts = 0;
	int audio_dur = 0, ret = -1;

	/* ---- video parser ---- */
	if (aic_parser_create((unsigned char *)cfg->vin_path, &vparser) < 0) {
		loge("video parser create failed");
		return -1;
	}
	aic_parser_init(vparser);

	vbuf = mpp_alloc(MAX_BUF_SIZE);
	if (!vbuf)
		goto out;

	/* ---- audio parser ---- */
	if (info->has_audio) {
		if (aic_parser_create((unsigned char *)cfg->ain_path,
				      &aparser) < 0) {
			loge("audio parser create failed");
			goto out;
		}
		aic_parser_init(aparser);
		abuf = mpp_alloc(MAX_AUDIO_BUF_SIZE);
		if (!abuf)
			goto out;
		audio_dur = cfg->audio_dur > 0 ? cfg->audio_dur : 72;
	}

	memset(&out_pkt, 0, sizeof(out_pkt));
	aic_muxer_init(muxer, info);
	aic_muxer_write_header(muxer);

	/* peek first packets to get initial PTS */
	memset(&vpkt, 0, sizeof(vpkt));
	aic_parser_peek(vparser, &vpkt);

	if (info->has_audio) {
		memset(&apkt, 0, sizeof(apkt));
		aic_parser_peek(aparser, &apkt);
	}

	while (1) {
		/*
		 * Sync rule: write one video frame, then write audio
		 * frames until their PTS catches up to the next video PTS.
		 * This keeps A-V sync within ~one audio frame.
		 */

		/* ---- video: one frame ---- */
		if (vpkt.size > MAX_BUF_SIZE) {
			loge("video frame size %d too large", vpkt.size);
			goto out;
		}
		if (vpkt.size <= 0 ||
		    vpts >= (info->duration > 0 ? info->duration : LOOP_SEC) *
				    1000)
			break; /* video EOF or duration reached */

		vpkt.data = vbuf;
		aic_parser_read(vparser, &vpkt);

		out_pkt.type = MPP_MEDIA_TYPE_VIDEO;
		out_pkt.data = vpkt.data;
		out_pkt.size = vpkt.size;
		out_pkt.pts = out_pkt.dts = vpts;
		out_pkt.duration = vframe_dur;
		aic_muxer_write_packet(muxer, &out_pkt);
		vframe++;
		vpts += vframe_dur;

		/* ---- audio: write frames until apts >= vpts ---- */
		while (info->has_audio && !audio_eos && apts < vpts) {
			int rc = aic_parser_peek(aparser, &apkt);
			if (rc == PARSER_EOS || apkt.size <= 0) {
				audio_eos = 1;
				break;
			}

			apkt.data = abuf;
			aic_parser_read(aparser, &apkt);

			out_pkt.type = MPP_MEDIA_TYPE_AUDIO;
			out_pkt.data = apkt.data;
			out_pkt.size = apkt.size;
			out_pkt.pts = out_pkt.dts = apts;
			out_pkt.duration = audio_dur;
			out_pkt.stream_index = 1;
			aic_muxer_write_packet(muxer, &out_pkt);
			aframe++;
			apts += audio_dur;
		}

		/* peek next video frame */
		memset(&vpkt, 0, sizeof(vpkt));
		if (aic_parser_peek(vparser, &vpkt) == PARSER_EOS)
			break;
	}
	logi("mux done: v=%d frames, a=%d frames", vframe, aframe);
	ret = 0;

out:
	mpp_free(vbuf);
	mpp_free(abuf);
	if (vparser)
		aic_parser_destroy(vparser);
	if (aparser)
		aic_parser_destroy(aparser);
	return ret;
}

/* ================================================================
 *  main
 * ================================================================ */

int main(int argc, char *argv[])
{
	struct demo_config cfg = { 0 };
	struct aic_muxer *muxer;
	int ret;

	ret = parse_args(argc, argv, &cfg);
	if (ret > 0)
		return 0;
	if (ret < 0)
		return -1;

	if (parser_video_config(&cfg) < 0) {
		loge("cannot probe video config from %s", cfg.vin_path);
		return -1;
	}
	if (cfg.acodec_type != MPP_CODEC_AUDIO_ENCODER_UNKOWN)
		parser_audio_config(&cfg);

	cfg.info.duration = LOOP_SEC;

	muxer = create_muxer(&cfg);
	if (!muxer)
		return -1;

	if (cfg.codec_type == MPP_CODEC_VIDEO_ENCODER_H264)
		ret = do_h264_mux(muxer, &cfg.info, &cfg);
	else
		ret = do_mjpeg_mux(muxer, &cfg.info, &cfg);

	if (ret == 0)
		aic_muxer_write_trailer(muxer);
	else
		loge("mux failed");

	aic_muxer_destroy(muxer);
	return ret;
}
