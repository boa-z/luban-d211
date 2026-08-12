/*
 * Copyright (C) 2020-2026 ArtInChip Technology Co. Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 *  author: <che.jiang@artinchip.com>
 *  Desc:  recorder demo
 */

#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <getopt.h>
#include <stdbool.h>
#include <errno.h>
#include <linux/fb.h>
#include <video/artinchip_fb.h>
#include <sys/ioctl.h>
//#define LOG_DEBUG
#include "aic_recorder.h"
#include "aic_storage.h"
#include "mpp_log.h"
#include "mpp_time.h"
#include "test/recorder_cam.h"

#define RECORDER_RECORD_TIME 0x7FFFFFFF
#define RECORDER_OUTPUT_PATH "/mnt/sdcard/"
#define BUFFER_LEN 32
#define FB_DEV "/dev/fb0"


struct recorder_render_data {
	struct aicfb_alpha_config ui_alpha;
	int fd_dev;
	int fb_xres;
	int fb_yres;
	bool render_init;
};

struct recorder_context {
	struct aic_recorder *recorder;
	enum aic_recorder_vin_type vin_source_type;
	struct recorder_cam_data cam_data;
	struct recorder_media_dev media_dev;
	struct recorder_render_data render_data;
	struct aic_recorder_config config;

	char output_path[256];
	unsigned int record_time;
	bool render_flag;
	bool record_flag;
	bool recorder_stop;
	int recoder_pending;
	int render_idx;  /* index of last rendered frame, -1 = none */
	pthread_mutex_t lock;
	struct aic_recorder_frame frame[CAM_BUF_NUM];
};

static struct recorder_context *g_recorder_ctx = NULL;

static void print_help(const char *prog)
{
	printf("name: %s\n", prog);
	printf("Usage: recoder_demo [options]:\n"
	       "\t-i                             video input source\n"
	       "\t-t                             recoder time(s)\n"
	       "\t-o                             set output path\n"
	       "\t-d                             display to screen\n"
	       "\t-q                             set pic quality\n"
	       "\t-r                             record data\n"
	       "\t-h                             help\n\n"
	       "Example1: recoder_demo -i dvp -t 60 -o /mnt/sdcard/\n");
}

static s32 event_handle(void *app_data, s32 event, s32 data1, s32 data2)
{
	struct recorder_context *ctx = (struct recorder_context *)app_data;

	switch (event) {
	case AIC_RECORDER_EVENT_COMPLETE:
		ctx->recorder_stop = true;
		break;
	case AIC_RECORDER_EVENT_NO_SPACE:
		break;
	default:
		break;
	}
	return 0;
}

static s32 giveback_buf(void *app_data, s32 event, void *buffer)
{
	struct recorder_context *ctx;
	struct mpp_frame *frame;
	s32 ret = 0;
	if (!app_data || !buffer || !g_recorder_ctx) {
		loge("app_data %p or buffer %p is null", app_data, buffer);
		return -1;
	}

	ctx = g_recorder_ctx;
	switch (event) {
	case AIC_RECORDER_EVENT_RELEASE_VIDEO_FRAME:
		pthread_mutex_lock(&ctx->lock);
		ctx->recoder_pending--;
		pthread_mutex_unlock(&ctx->lock);
		frame = &((struct aic_recorder_frame *)buffer)->mpp_frame;
		ret = recorder_cam_queue_buf(&ctx->media_dev, frame->id);
		logd("recorder_cam_queue_buf id:%d", frame->id);
		break;

	default:
		break;
	}

	return ret;
}

static int parse_options(struct recorder_context *ctx, int cnt, char **options)
{
	int argc = cnt;
	char **argv = options;
	int opt;

	if (!ctx || argc == 0 || !argv) {
		loge("para error !!!");
		return -1;
	}
	optind = 0;
	while (1) {
		opt = getopt(argc, argv, "i:o:r:t:q:b:dh");
		if (opt == -1)
			break;
		switch (opt) {
		case 'i':
			if (strncmp(optarg, "dvp", 3) == 0)
				ctx->vin_source_type = AIC_RECORDER_VIN_DVP;
			break;
		case 'o':
			snprintf(ctx->output_path, sizeof(ctx->output_path), "%s", optarg);
			break;
		case 't':
			ctx->record_time = atoi(optarg);
			break;
		case 'r':
			ctx->record_flag = atoi(optarg);
			break;
		case 'q':
			ctx->config.qfactor = atoi(optarg);
			break;
		case 'd':
			ctx->render_flag = true;
			break;
		case 'h':
		default:
			print_help(argv[0]);
			return -1;
		}
	}
	return 0;
}

static int recorder_render_init(struct recorder_render_data *rd)
{
	struct fb_var_screeninfo var;
	struct aicfb_alpha_config alpha = { 0 };

	if (!rd)
		return -1;

	rd->fd_dev = open(FB_DEV, O_RDWR);
	if (rd->fd_dev < 0) {
		loge("open /dev/fb0 fail");
		return -1;
	}
	if (ioctl(rd->fd_dev, FBIOGET_VSCREENINFO, &var) < 0) {
		loge("FBIOGET_VSCREENINFO fail");
		close(rd->fd_dev);
		return -1;
	}
	rd->fb_xres = var.xres;
	rd->fb_yres = var.yres;
	printf("Framebuf size: %d x %d\n", rd->fb_xres, rd->fb_yres);

	rd->ui_alpha.layer_id = AICFB_LAYER_TYPE_UI;
	ioctl(rd->fd_dev, AICFB_GET_ALPHA_CONFIG, &rd->ui_alpha);
	alpha.layer_id = AICFB_LAYER_TYPE_UI;
	alpha.enable = 1;
	alpha.mode = 1;
	alpha.value = 0;
	ioctl(rd->fd_dev, AICFB_UPDATE_ALPHA_CONFIG, &alpha);
	rd->render_init = true;
	return 0;
}

static void recorder_render_deinit(struct recorder_render_data *rd)
{
	struct aicfb_layer_data layer = { 0 };

	if (!rd || !rd->render_init)
		return;
	layer.layer_id = AICFB_LAYER_TYPE_VIDEO;
	ioctl(rd->fd_dev, AICFB_UPDATE_LAYER_CONFIG, &layer);
	ioctl(rd->fd_dev, AICFB_UPDATE_ALPHA_CONFIG, &rd->ui_alpha);
	if (rd->fd_dev > 0)
		close(rd->fd_dev);
	rd->render_init = false;
}

static int recorder_render_frame(struct recorder_render_data *rd,
				 struct mpp_frame *frame)
{
	struct aicfb_layer_data layer = { 0 };

	if (!rd || !rd->render_init || !frame)
		return -1;
	layer.layer_id = AICFB_LAYER_TYPE_VIDEO;
	layer.enable = 1;
	layer.buf = frame->buf;
	layer.pos.x = 0;
	layer.pos.y = 0;
	layer.scale_size.width = rd->fb_xres;
	layer.scale_size.height = rd->fb_yres;
	if (ioctl(rd->fd_dev, AICFB_UPDATE_LAYER_CONFIG, &layer) < 0) {
		loge("AICFB_UPDATE_LAYER_CONFIG failed: %s", strerror(errno));
		return -1;
	}
	ioctl(rd->fd_dev, AICFB_WAIT_FOR_VSYNC, NULL);
	return 0;
}


static int recorder_record_init(struct recorder_context *ctx)
{
	if (!ctx)
		return -1;

	ctx->recorder = aic_recorder_create();
	if (!ctx->recorder) {
		loge("aic_recorder_create error");
		return -1;
	}
	if (aic_recorder_set_event_callback(ctx->recorder, ctx, event_handle)) {
		loge("aic_recorder_set_event_callback error");
		return -1;
	}
	if (aic_recorder_set_output_path(ctx->recorder, ctx->output_path)) {
		loge("aic_recorder_set_output_path error");
		return -1;
	}
	if (aic_recorder_set_buf_callback(ctx->recorder, giveback_buf)) {
		loge("aic_recorder_set_giveback_buf_callback error");
		return -1;
	}
	if (aic_recorder_init(ctx->recorder, &ctx->config)) {
		loge("aic_recorder_init error");
		return -1;
	}
	if (aic_recorder_start(ctx->recorder)) {
		loge("aic_recorder_start error");
		return -1;
	}
	return 0;
}


static void do_recorder(struct recorder_context *ctx)
{
	struct recorder_cam_data *cam = &ctx->cam_data;
	struct recorder_media_dev *mdev = &ctx->media_dev;
	struct aic_recorder_frame *last_frame;
	struct aic_recorder_frame *cur_frame;
	struct mpp_frame *frame;
	int index, i, ret;

	if (!ctx)
		return;

	if (recorder_cam_dequeue_buf(mdev, &index) != 0) {
		logw("dequeue buf error, retry...");
		usleep(5000);
		return;
	}

	cur_frame = &ctx->frame[index];
	frame = &cur_frame->mpp_frame;
	frame->id = index;
	frame->pts = mpp_get_time_us();
	frame->buf.buf_type = MPP_DMA_BUF_FD;
	frame->buf.size.width = cam->w;
	frame->buf.size.height = cam->h;
	frame->buf.stride[0] = cam->w;
	frame->buf.stride[1] = cam->w;
	frame->buf.stride[2] = 0;
	frame->buf.format = (cam->fmt == V4L2_PIX_FMT_NV16) ? MPP_FMT_NV16 : MPP_FMT_NV12;
	for (i = 0; i < CAM_PLANE_NUM; i++) {
		frame->buf.fd[i] = cam->binfo[index].planes[i].fd;
		cur_frame->vaddr[i] = cam->binfo[index].planes[i].vaddr;
	}

	/* render current frame (fresh from DVP, zero OSD) */
	if (ctx->render_flag) {
		recorder_render_frame(&ctx->render_data, frame);
	}

	if (!ctx->record_flag) {
		recorder_cam_queue_buf(mdev, index);
		return;
	}

	/* send PREVIOUS frame to recorder, avoid osd overlay polluted the render frame*/
	if (ctx->render_idx >= 0) {
		last_frame = &ctx->frame[ctx->render_idx];
		ret = aic_recorder_send_frame(ctx->recorder, last_frame);
		if (ret != 0) {
			logw("send frame %d failed, ret:%d.", last_frame->mpp_frame.id, ret);
			recorder_cam_queue_buf(mdev, last_frame->mpp_frame.id);
			ctx->render_idx = index;
			return;
		}
		pthread_mutex_lock(&ctx->lock);
		ctx->recoder_pending++;
		pthread_mutex_unlock(&ctx->lock);
	}
	ctx->render_idx = index;
}

static void *recorder_thread(void *arg)
{
	struct recorder_context *ctx = (struct recorder_context *)arg;
	struct timespec start_time = { 0 }, cur_time = { 0 };

	clock_gettime(CLOCK_REALTIME, &start_time);

	while (!ctx->recorder_stop) {
		clock_gettime(CLOCK_REALTIME, &cur_time);
		if (ctx->record_time <= (cur_time.tv_sec - start_time.tv_sec)) {
			ctx->recorder_stop = true;
			printf("recorder end time, stop.\n");
			break;
		}
		do_recorder(ctx);
	}
	printf("recorder_thread exit\n");
	return NULL;
}


static void recorder_default_config(struct recorder_context *ctx)
{
	ctx->render_flag = false;
	ctx->record_flag = true;
	ctx->record_time = RECORDER_RECORD_TIME;
	snprintf(ctx->output_path, sizeof(ctx->output_path), "%s", RECORDER_OUTPUT_PATH);

	ctx->config.file_duration = 60;
	ctx->config.file_num = 0;
	ctx->config.file_muxer_type = AIC_MUXER_TYPE_MP4;
	ctx->config.qfactor = 80;

	ctx->config.has_video = 1;
	ctx->config.video_config.codec_type = MPP_CODEC_VIDEO_ENCODER_MJPEG;
	ctx->config.video_config.in_width = 1280;
	ctx->config.video_config.in_height = 720;
	ctx->config.video_config.out_width = 1280;
	ctx->config.video_config.out_height = 720;
	ctx->config.video_config.out_frame_rate = 30;
	ctx->config.video_config.out_bit_rate = 1024 * 1024;
	ctx->config.video_config.in_pix_fomat = MPP_FMT_NV12;

	ctx->config.has_audio = 0;
}

static int recorder_osd_set(struct aic_recorder *recorder)
{
	char *text = "51'30'21.4:N, 0'07'39.5:E";
	struct aic_recorder_osd osd = { .id = 0, .enable = true,
					.x = 550, .y = 300, .text = text };
	aic_recorder_control(recorder, AIC_RECORDER_CMD_SET_OSD_TEXT, &osd);
	return 0;
}

static void set_debug_info(struct aic_recorder *recorder)
{
	static int debug_en = 0;
	debug_en ^= 0x01;

	if (recorder)
		aic_recorder_control(recorder, AIC_RECORDER_CMD_SET_DEBUG_INFO, &debug_en);
}

static void set_record(struct aic_recorder *recorder)
{
	char *user_data = "GPS: 51'30'21.4:N, 0'07'39.5:E";
	int lock = 1;

	if (!recorder)
		return;
	aic_recorder_control(recorder, AIC_RECORDER_CMD_SET_LOCK_RECORD, &lock);
	aic_recorder_control(recorder, AIC_RECORDER_CMD_SET_USER_RECORD_DATA, user_data);
}

static void print_records(struct aic_recorder_record *recs, int count)
{
	for (int i = 0; i < count; i++) {
		printf("  %s  size=%lld  dur=%ds  %s\n",
		       recs[i].file_path, (long long)recs[i].file_size,
		       recs[i].duration, recs[i].created_at);
	}
}

static void search_record(struct aic_recorder *recorder)
{
	struct aic_recorder_record recs[10];
	struct aic_recorder_query q = { 0 };
	char date[32] = { 0 };
	struct tm local_tm;
	int64_t size_mb;
	int mounted;
	int count;

	if (!recorder)
		return;

	mounted = aic_storage_is_mounted(RECORDER_OUTPUT_PATH);
	printf("CARD mounted: %d\n", mounted);
	size_mb = aic_storage_get_total_mb(RECORDER_OUTPUT_PATH);
	printf("CARD capacity: %ld MB\n", size_mb);
	size_mb = aic_storage_get_free_mb(RECORDER_OUTPUT_PATH);
	printf("CARD Free:     %ld MB\n", size_mb);
	size_mb = aic_storage_get_used_mb(RECORDER_OUTPUT_PATH);
	printf("CARD Used:     %ld MB\n", size_mb);

	aic_recorder_control(recorder, AIC_RECORDER_CMD_GET_RECORD_COUNT, &count);
	printf("Record: count %d\n", count);

	q.max = 10;
	q.records = recs;
	q.count = 10;
	aic_recorder_control(recorder, AIC_RECORDER_CMD_GET_RECORD_LIST, &q);
	printf("Record: latest count %d\n", q.count);
	print_records(recs, q.count);

	mpp_get_local_tm(&local_tm);
	snprintf(date, sizeof(date), "%04d-%02d-%02d",
		 1900 + local_tm.tm_year, 1 + local_tm.tm_mon, local_tm.tm_mday);
	q.max = 10;
	q.records = recs;
	q.date = date;
	aic_recorder_control(recorder, AIC_RECORDER_CMD_GET_RECORD_COUNT_BY_DATE, &q);
	printf("Record: today (%s) count %d\n", date, q.count);
	aic_recorder_control(recorder, AIC_RECORDER_CMD_GET_RECORD_BY_DATE, &q);
	print_records(recs, q.count);

	aic_recorder_control(recorder, AIC_RECORDER_CMD_GET_LOCKED_COUNT, &count);
	printf("Locked: count %d\n", count);
	q.max = count < 10 ? count : 10;
	q.records = recs;
	q.count = 10;
	aic_recorder_control(recorder, AIC_RECORDER_CMD_GET_LOCKED_LIST, &q);
	print_records(recs, q.count);
}

static void print_pictures(struct aic_recorder_picture *pics, int count)
{
	for (int i = 0; i < count; i++) {
		printf("  %s  size=%lld  %s\n",
		       pics[i].file_path, (long long)pics[i].file_size,
		       pics[i].created_at);
	}
}

static void search_picture(struct aic_recorder *recorder)
{
	struct aic_recorder_picture pics[10];
	struct aic_recorder_query q;
	struct tm local_tm;
	char date[16];
	int count;

	if (!recorder)
		return;

	aic_recorder_control(recorder, AIC_RECORDER_CMD_GET_PICTURE_COUNT, &count);
	printf("Picture: count %d\n", count);

	q.max = 10;
	q.pictures = pics;
	aic_recorder_control(recorder, AIC_RECORDER_CMD_GET_PICTURE_LIST, &q);
	printf("Picture: latest count %d\n", q.count);
	print_pictures(pics, q.count);

	mpp_get_local_tm(&local_tm);
	snprintf(date, sizeof(date), "%04d-%02d-%02d",
		 1900 + local_tm.tm_year, 1 + local_tm.tm_mon, local_tm.tm_mday);

	q.max = 10;
	q.pictures = pics;
	q.date = date;
	aic_recorder_control(recorder, AIC_RECORDER_CMD_GET_PICTURE_COUNT_BY_DATE, &q);
	printf("Picture: today (%s) count %d\n", date, q.count);
	aic_recorder_control(recorder, AIC_RECORDER_CMD_GET_PICTURE_BY_DATE, &q);
	print_pictures(pics, q.count);
}

static void switch_duration(struct aic_recorder *recorder)
{
	int max_duaration = 0;
	if (!recorder)
		return;

	max_duaration = (random() % 5 + 1) * 60;
	printf("set max duration %d.\n", max_duaration);
	aic_recorder_control(recorder, AIC_RECORDER_CMD_SET_RECORD_DURATION, &max_duaration);
}

static int process_command(struct recorder_context *ctx, char cmd)
{
	if (!ctx)
		return -1;

	switch (cmd) {
	case 's':
		aic_recorder_snapshot(ctx->recorder);
		break;
	case 'o':
		recorder_osd_set(ctx->recorder);
		break;
	case 'i':
		set_debug_info(ctx->recorder);
		break;
	case 'r':
		search_record(ctx->recorder);
		break;
	case 'p':
		search_picture(ctx->recorder);
		break;
	case 'l':
		set_record(ctx->recorder);
		break;
	case 'd':
		switch_duration(ctx->recorder);
		break;
	default:
		break;
	}
	return 0;
}

int main(int argc, char *argv[])
{
	struct recorder_context *ctx = NULL;
	bool thread_created = false;
	char buffer[BUFFER_LEN];
	pthread_t thread_id;
	int flag = 0;
	int ret = 0;

	ctx = malloc(sizeof(struct recorder_context));
	if (!ctx) {
		loge("malloc error");
		return -1;
	}
	memset(ctx, 0x00, sizeof(struct recorder_context));
	ctx->render_idx = -1;
	recorder_default_config(ctx);
	pthread_mutex_init(&ctx->lock, NULL);
	if (parse_options(ctx, argc, argv))
		goto _exit;
	g_recorder_ctx = ctx;

	/* init camera */
	if (recorder_cam_init(&ctx->cam_data, &ctx->media_dev,
			      ctx->vin_source_type)) {
		loge("cam init failed");
		goto _exit;
	}

	/* update video config from actual cam data */
	ctx->config.video_config.in_width = ctx->cam_data.w;
	ctx->config.video_config.in_height = ctx->cam_data.h;
	ctx->config.video_config.out_width = ctx->cam_data.w;
	ctx->config.video_config.out_height = ctx->cam_data.h;
	ctx->config.video_config.out_frame_rate = ctx->media_dev.sensor_fr;

	/* init recorder */
	if (ctx->record_flag) {
		if (recorder_record_init(ctx))
			goto _exit;
	}

	/* init render */
	if (ctx->render_flag) {
		if (recorder_render_init(&ctx->render_data))
			goto _exit;
	}

	/* start capture thread */
	ret = pthread_create(&thread_id, NULL, recorder_thread, ctx);
	if (ret) {
		loge("create recorder_thread failed");
		goto _exit;
	}
	thread_created = true;

	flag = fcntl(STDIN_FILENO, F_GETFL);
	flag |= O_NONBLOCK;
	fcntl(STDIN_FILENO, F_SETFL, flag);

	while (!ctx->recorder_stop) {
		if (read(STDIN_FILENO, buffer, BUFFER_LEN) > 0) {
			if (buffer[0] == 'e') {
				logd("app want to exit");
				ctx->recorder_stop = true;
				break;
			}
			process_command(ctx, buffer[0]);
		} else {
			usleep(50000);
		}
	}

_exit:
	if (thread_created)
		pthread_join(thread_id, NULL);
	if (ctx) {
		if (ctx->recorder) {
			aic_recorder_stop(ctx->recorder);
			aic_recorder_destroy(ctx->recorder);
			ctx->recorder = NULL;
		}
		recorder_cam_deinit(&ctx->cam_data, &ctx->media_dev);
		if (ctx->render_flag)
			recorder_render_deinit(&ctx->render_data);
		pthread_mutex_destroy(&ctx->lock);
		free(ctx);
	}
	return ret;
}
