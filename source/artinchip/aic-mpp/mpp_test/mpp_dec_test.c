/*
 * Copyright (C) 2020-2026 ArtInChip Technology Co. Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 *  author: <qi.xu@artinchip.com>
 *  Desc: video decode demo
 */

#define LOG_TAG "dec_test"

#include <stdio.h>
#include <unistd.h>
#include <malloc.h>
#include <pthread.h>
#include <errno.h>
#include <dirent.h>

#include <linux/types.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <semaphore.h>
#include <sys/ioctl.h>
#include <linux/fb.h>

#include <video/artinchip_fb.h>
#include <linux/dma-buf.h>
#include <linux/dma-heap.h>

#include "dma_allocator.h"
#include "bit_stream_parser.h"
#include "mpp_decoder.h"
#include "mpp_encoder.h"
#include "mpp_log.h"
#ifdef MIDDLEWARE
#include "aic_render.h"
#endif

#define FRAME_BUF_NUM		(18)
#define MAX_TEST_FILE           (256)
#define SCREEN_WIDTH            1024
#define SCREEN_HEIGHT           600
#define FRAME_COUNT		30

const char *dev_fb0 = "/dev/fb0";

struct frame_info {
	int fd[3];		// dma-buf fd
	int fd_num;		// number of dma-buf
	int used;		// if the dma-buf of this frame add to de drive
};

struct dec_ctx {
	struct mpp_decoder  *decoder;
	struct frame_info frame_info[FRAME_BUF_NUM];	//

	volatile int stream_eos;
	volatile int render_eos;
	volatile int dec_err;
	int cmp_data_err;

	char file_input[MAX_TEST_FILE][1024];	// test file name
	int file_num;				// test file number

	int output_format;
	int cmp_en;
	int display_en;
	int save_data;
	int input_by_frame;			// 0: by slice (default), 1: by frame
	int drop_b_frame;			// 1: drop non-reference B-frames
	FILE* fp_yuv;				// compare yuv
	FILE* fp_save;				// hw decode save yuv
	FILE* fp_result;			// test result (pass/fail)
};

static void print_help(const char* prog)
{
	printf("name: %s\n", prog);
	printf("Compile time: %s\n", __TIME__);
	printf("Usage: mpp_test [options]:\n"
		"\t-i                             input stream file name\n"
		"\t-t                             directory of test files\n"
		"\t-d                             enable display error picture\n"
		"\t-c                             enable compare output data\n"
		"\t-f                             output pixel format\n"
		"\t-l                             loop time\n"
		"\t-s                             save output data\n"
		"\t-m                             input mode: 0-slice(default), 1-frame\n"
		"\t-b                             drop non-reference B-frames\n"
		"\t-h                             help\n\n"
		"Example1(test single file): mpp_test -i test.264\n"
		"Example2(test some files) : mpp_test -t /usr/data/\n");
}

static long long get_now_us(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (long long)tv.tv_sec * 1000000ll + tv.tv_usec;
}

int set_fb_layer_alpha(int fb0_fd, int val)
{
	int ret = 0;
	struct aicfb_alpha_config alpha = {0};

	if (fb0_fd < 0)
		return -1;

	alpha.layer_id = 1;
	alpha.enable = 1;
	alpha.mode = 1;
	alpha.value = val;
	ret = ioctl(fb0_fd, AICFB_UPDATE_ALPHA_CONFIG, &alpha);
	if (ret < 0)
		loge("fb ioctl() AICFB_UPDATE_ALPHA_CONFIG failed!");

	return ret;
}

#define MIN(a, b) ((a) < (b) ? (a) : (b))
void video_layer_set(int fb0_fd, struct mpp_buf *picture_buf, struct frame_info* frame)
{
	struct aicfb_layer_data layer = {0};
	int dmabuf_num = 0;
	struct dma_buf_info dmabuf_fd[3];
	int i;

	if (fb0_fd < 0)
		return;

	layer.layer_id = AICFB_LAYER_TYPE_VIDEO;
	layer.enable = 1;
	if (picture_buf->format == MPP_FMT_YUV420P || picture_buf->format == MPP_FMT_NV12
	  || picture_buf->format == MPP_FMT_NV21) {
		  // rgb format not support scale
		layer.scale_size.width = SCREEN_WIDTH;
		layer.scale_size.height= SCREEN_HEIGHT;
	}

	layer.pos.x = 0;
	layer.pos.y = 0;
	memcpy(&layer.buf, picture_buf, sizeof(struct mpp_buf));

	if (picture_buf->format == MPP_FMT_ARGB_8888) {
		dmabuf_num = 1;
	} else if (picture_buf->format == MPP_FMT_RGBA_8888) {
		dmabuf_num = 1;
	} else if (picture_buf->format == MPP_FMT_RGB_888) {
		dmabuf_num = 1;
	} else if (picture_buf->format == MPP_FMT_YUV420P) {
		dmabuf_num = 3;
	} else if (picture_buf->format == MPP_FMT_NV12 || picture_buf->format == MPP_FMT_NV21) {
		dmabuf_num = 2;
	} else if (picture_buf->format == MPP_FMT_YUV444P) {
		dmabuf_num = 3;
	} else if (picture_buf->format == MPP_FMT_YUV422P) {
		dmabuf_num = 3;
	} else if (picture_buf->format == MPP_FMT_YUV400) {
		dmabuf_num = 1;
	} else {
		loge("no support picture foramt %d, default argb8888", picture_buf->format);
	}

	//* add dmabuf to de driver
	if(!frame->used) {
		for(i=0; i<dmabuf_num; i++) {
			frame->fd[i] = picture_buf->fd[i];
			dmabuf_fd[i].fd = picture_buf->fd[i];
			if (ioctl(fb0_fd, AICFB_ADD_DMABUF, &dmabuf_fd[i]) < 0)
				loge("fb ioctl() AICFB_UPDATE_LAYER_CONFIG failed!");
		}
		frame->used = 1;
		frame->fd_num = dmabuf_num;
	} else {

	}

	logi("width: %d, height %d, stride: %d, %d, crop_en: %d, crop_w: %d, crop_h: %d",
		layer.buf.size.width, layer.buf.size.height,
		layer.buf.stride[0], layer.buf.stride[1], layer.buf.crop_en,
		layer.buf.crop.width, layer.buf.crop.height);
	//* display
	if (ioctl(fb0_fd, AICFB_UPDATE_LAYER_CONFIG, &layer) < 0)
		loge("fb ioctl() AICFB_UPDATE_LAYER_CONFIG failed!");

	//* wait vsync (wait layer config)
	ioctl(fb0_fd, AICFB_WAIT_FOR_VSYNC, NULL);
}

static int send_data(struct dec_ctx *data, unsigned char* buf, int buf_size)
{
	int ret = 0;
	struct mpp_packet packet;
	memset(&packet, 0, sizeof(struct mpp_packet));

	// get an empty packet
	do {
		if(data->dec_err) {
			loge("decode error, break now");
			return -1;
		}

		ret = mpp_decoder_get_packet(data->decoder, &packet, buf_size);
		//logd("mpp_dec_get_packet ret: %x", ret);
		if (ret == 0) {
			break;
		}
		usleep(1000);
	} while (1);

	memcpy(packet.data, buf, buf_size);
	packet.size = buf_size;

	if (data->stream_eos)
		packet.flag |= PACKET_FLAG_EOS;

	ret = mpp_decoder_put_packet(data->decoder, &packet);
	logd("mpp_dec_put_packet ret %d", ret);

	return ret;
}

static int cmp_data(struct dec_ctx *data, FILE* fp, struct mpp_buf* video, FILE* fp_save)
{
	int i, j;
	int ret = 0;
	int data_size[3] = {0, 0, 0};
	unsigned char* hw_data[3] = {0};
	int comp = 3;

	if(video->format == MPP_FMT_YUV420P) {
		comp = 3;
		data_size[0] = video->size.height * video->stride[0];
		data_size[1] = data_size[2] = data_size[0]/4;
	} else if(video->format == MPP_FMT_NV12 || video->format == MPP_FMT_NV21) {
		comp = 2;
		data_size[0] = video->size.height * video->stride[0];
		data_size[1] =  data_size[0]/2;
	} else if(video->format == MPP_FMT_YUV444P) {
		comp = 3;
		data_size[0] = video->size.height * video->stride[0];
		data_size[1] = data_size[2] = data_size[0];
	} else if(video->format == MPP_FMT_YUV422P) {
		comp = 3;
		data_size[0] = video->size.height * video->stride[0];
		data_size[1] = data_size[2] = data_size[0]/2;
	} else if(video->format == MPP_FMT_RGBA_8888 || video->format == MPP_FMT_BGRA_8888
		|| video->format == MPP_FMT_ARGB_8888 || video->format == MPP_FMT_ABGR_8888) {
		comp = 1;
		data_size[0] = video->size.height * video->stride[0];
	} else if(video->format == MPP_FMT_RGB_888 || video->format == MPP_FMT_BGR_888) {
		comp = 1;
		data_size[0] = video->size.height * video->stride[0];
	} else if(video->format == MPP_FMT_RGB_565 || video->format == MPP_FMT_BGR_565) {
		comp = 1;
		data_size[0] = video->size.height * video->stride[0];
	}

	logd("data_size: %d %d %d, height: %d, stride: %d, format: %d",
		data_size[0], data_size[1], data_size[2],
		video->size.height, video->stride[0], video->format);

	// mmap dmabuf to virtual space and save the frame yuv data
	for(i=0; i<comp; i++) {
		hw_data[i] = mmap(NULL, data_size[i], PROT_READ, MAP_SHARED, video->fd[i], 0);
		if (hw_data[i] == MAP_FAILED) {
			loge("dmabuf alloc mmap failed!");
			ret = -1;
			goto out;
		}
		if(fp_save)
			fwrite(hw_data[i], 1, data_size[i], fp_save);
	}

	unsigned char* buf[3] = {0};
	if(fp && data->cmp_en) {
		// read data from compare file
		for(i=0; i<comp; i++) {
			buf[i] = (unsigned char*)malloc(data_size[i]);
			fread(buf[i], 1, data_size[i], fp);
		}

		// compare hw decode data
		for(i=0; i<comp; i++) {
			for(j=0; j<data_size[i]; j++) {
				if(abs(buf[i][j] - hw_data[i][j]) < 2)
					continue;

				ret = -1;
				loge("comp(%d) error, pos: %d, sw: %x, hw: %x",
					i, j,  buf[i][j], hw_data[i][j]);
				goto out;
			}
		}
		logi("compare data success");
	} else {
		logw("not compare data");
		ret = -1;
	}

out:
	// unmap dmabuf
	for(i=0; i<comp; i++) {
		if (hw_data[i] > 0)
			munmap(hw_data[i], data_size[i]);
	}

	if(buf[0]) free(buf[0]);
	if(buf[1]) free(buf[1]);
	if(buf[2]) free(buf[2]);

	return ret;
}

static void swap(int *a, int *b)
{
	int tmp = *a;
	*a = *b;
	*b = tmp;
}

void* render_thread(void *p)
{
	struct dec_ctx *data = (struct dec_ctx*)p;
#ifdef MIDDLEWARE
	struct aic_video_render *render = NULL;
#endif
	struct mpp_frame frame[2];
	int cur_frame_id = 0, last_frame_id = 1;
	int frame_num = 0, ret;
	long long time = 0, duration_time = 0;
	int disp_frame_cnt = 0, total_disp_frame_cnt = 0;
	long long total_duration_time = 0;

#ifdef MIDDLEWARE
	if (data->display_en) {
		aic_video_render_create(&render);
		if (render)
			aic_video_render_init(render, AICFB_LAYER_TYPE_VIDEO, 0);
	}
#endif

	time = get_now_us();
	while (!data->render_eos) {
		memset(&frame[cur_frame_id], 0, sizeof(struct mpp_frame));

		if (data->dec_err) {
			data->render_eos = 1;
			break;
		}

		ret = mpp_decoder_get_frame(data->decoder, &frame[cur_frame_id]);
		if (ret == DEC_NO_RENDER_FRAME || ret == DEC_ERR_FM_NOT_CREATE
		    || ret == DEC_NO_EMPTY_FRAME) {
			usleep(10000);
			continue;
		} else if (ret) {
			logw("mpp_dec_get_frame error, ret: %x", ret);
			data->dec_err = 1;
			break;
		}

		time = get_now_us() - time;
		duration_time += time;
		disp_frame_cnt += 1;
		data->render_eos = frame[cur_frame_id].flags & FRAME_FLAG_EOS;
		logi("decode_get_frame successful: frame id %d, number %d, flag: %d",
			frame[cur_frame_id].id, frame_num, frame[cur_frame_id].flags);

		if (frame[cur_frame_id].flags & FRAME_FLAG_ERROR)
			loge("frame error");

		// compare / save data
		if ((data->cmp_en || data->fp_save) &&
		    cmp_data(data, data->fp_yuv, &frame[cur_frame_id].buf, data->fp_save))
			data->cmp_data_err = 1;

#ifdef MIDDLEWARE
		if (render)
			aic_video_render_rend(render, &frame[cur_frame_id]);
#endif

		if (frame_num)
			mpp_decoder_put_frame(data->decoder, &frame[last_frame_id]);

		swap(&cur_frame_id, &last_frame_id);

		if (disp_frame_cnt > FRAME_COUNT) {
			float fps, avg_fps;
			total_disp_frame_cnt += disp_frame_cnt;
			total_duration_time += duration_time;
			fps = (float)(duration_time / 1000.0f);
			fps = (disp_frame_cnt * 1000) / fps;
			avg_fps = (float)(total_duration_time / 1000.0f);
			avg_fps = (total_disp_frame_cnt * 1000) / avg_fps;
			logi("decode speed info: fps: %.2f, avg_fps: %.2f", fps, avg_fps);
			duration_time = 0;
			disp_frame_cnt = 0;
		}
		time = get_now_us();
		frame_num++;
		usleep(30000);
	}

	mpp_decoder_put_frame(data->decoder, &frame[last_frame_id]);

#ifdef MIDDLEWARE
	if (render) {
		aic_video_render_destroy(render);
		render = NULL;
	}
#endif

	return NULL;
}

void* decode_thread(void *p)
{
	struct dec_ctx *data = (struct dec_ctx*)p;
	int ret = 0;

	int dec_num = 0;
	//while(!dec_num) {
	while(!data->render_eos) {
		ret = mpp_decoder_decode(data->decoder);
		if(ret == DEC_NO_READY_PACKET || ret == DEC_NO_EMPTY_FRAME) {
			logi("decode ret: %d", ret);
			usleep(1000);
			continue;
		} else if( ret ) {
			logw("decode ret: %x", ret);
			//data->dec_err = 1;
			//break;
		}

		dec_num ++;
		usleep(1000);
	}

	return NULL;
}

static int detect_dec_type(const char *filename, struct dec_ctx *data,
			     char *yuv_file_name, size_t name_size)
{
	int dec_type = 0;
	char *ptr = strrchr(filename, '.');
	if (ptr) {
		if (!strncmp(ptr, ".h264", 5) || !strncmp(ptr, ".264", 4))
			dec_type = MPP_CODEC_VIDEO_DECODER_H264;
		else if (!strncmp(ptr, ".jpg", 4))
			dec_type = MPP_CODEC_VIDEO_DECODER_MJPEG;
		else if (!strncmp(ptr, ".png", 4))
			dec_type = MPP_CODEC_VIDEO_DECODER_PNG;
	}
	logi("file type: 0x%02X", dec_type);

	if (ptr) {
		strncpy(yuv_file_name, filename, name_size - 1);
		yuv_file_name[name_size - 1] = '\0';
		ptr = strrchr(yuv_file_name, '.');
		if (ptr) {
			ptr[1] = 'y';
			ptr[2] = 'u';
			ptr[3] = 'v';
			ptr[4] = '\0';
		}
		logi("yuv file name: %s", yuv_file_name);
		data->fp_yuv = fopen(yuv_file_name, "rb");
		if (data->fp_yuv == NULL)
			logi("dec_data.fp_yuv open failed, erron(%d)", errno);
	}
	return dec_type;
}

static int send_h264_data(struct dec_ctx *data, int file_fd)
{
	struct bit_stream_parser *parser = bs_create(file_fd);
	if (parser == NULL) {
		loge("bs_create failed");
		return -1;
	}

	struct mpp_packet packet;
	memset(&packet, 0, sizeof(struct mpp_packet));

	while ((packet.flag & PACKET_FLAG_EOS) == 0) {
		int ret;

		memset(&packet, 0, sizeof(struct mpp_packet));
		if (data->input_by_frame)
			bs_prefetch_frame(parser, &packet);
		else
			bs_prefetch(parser, &packet);
		logi("bs_prefetch, size: %d", packet.size);

		do {
			if (data->dec_err) {
				loge("decode error, break now");
				bs_close(parser);
				return -1;
			}
			ret = mpp_decoder_get_packet(data->decoder, &packet, packet.size);
			if (ret == 0)
				break;
			usleep(1000);
		} while (1);

		bs_read(parser, &packet);
		mpp_decoder_put_packet(data->decoder, &packet);
	}

	bs_close(parser);
	return 0;
}

static int send_raw_data(struct dec_ctx *data, int file_fd, size_t buf_size)
{
	unsigned char *buf = malloc(buf_size);
	if (!buf) {
		loge("malloc buf failed");
		return -1;
	}

	if (read(file_fd, buf, buf_size) <= 0) {
		loge("read data error");
		free(buf);
		return -1;
	}

	data->stream_eos = 1;
	send_data(data, buf, buf_size);
	free(buf);
	return 0;
}

int dec_decode(struct dec_ctx *data, char *filename)
{
	int ret, file_fd, dec_type;
	size_t buf_size;
	pthread_t render_thread_id, decode_thread_id;
	char yuv_file_name[1024];

	logd("dec_test start");

	dec_type = detect_dec_type(filename, data, yuv_file_name, sizeof(yuv_file_name));

	file_fd = open(filename, O_RDONLY);
	if (file_fd < 0) {
		loge("failed to open input file %s", filename);
		ret = -1;
		goto out;
	}
	buf_size = lseek(file_fd, 0, SEEK_END);
	lseek(file_fd, 0, SEEK_SET);

	data->decoder = mpp_decoder_create(dec_type);
	if (!data->decoder) {
		loge("mpp_dec_create failed");
		ret = -1;
		goto out;
	}

	struct decode_config config = {0};
	if (dec_type == MPP_CODEC_VIDEO_DECODER_PNG || dec_type == MPP_CODEC_VIDEO_DECODER_MJPEG)
		config.bitstream_buffer_size = (buf_size + 1023) & (~1023);
	else
		config.bitstream_buffer_size = 1024 * 1024;
	config.extra_frame_num = 1;
	config.packet_count = 10;
	config.pix_fmt = data->output_format;
	if (dec_type == MPP_CODEC_VIDEO_DECODER_PNG)
		config.pix_fmt = MPP_FMT_ARGB_8888;
	ret = mpp_decoder_init(data->decoder, &config);
	if (ret) {
		logd("%p mpp_dec_init type %d failed", data->decoder, dec_type);
		goto out;
	}

	if (data->drop_b_frame && dec_type == MPP_CODEC_VIDEO_DECODER_H264) {
		int enable = 1;
		mpp_decoder_control(data->decoder, MPP_DEC_SET_DROP_B_FRAME, &enable);
		logi("enable B-frame drop");
	}

	pthread_create(&decode_thread_id, NULL, decode_thread, data);
	pthread_create(&render_thread_id, NULL, render_thread, data);

	if (dec_type == MPP_CODEC_VIDEO_DECODER_H264)
		ret = send_h264_data(data, file_fd);
	else
		ret = send_raw_data(data, file_fd, buf_size);

	if (ret < 0) {
		data->render_eos = 1;
		data->stream_eos = 1;
	}

	pthread_join(decode_thread_id, NULL);
	pthread_join(render_thread_id, NULL);

	if (data->cmp_data_err)
		ret = -1;

out:
	if (data->fp_result) {
		if (data->fp_yuv == NULL)
			fprintf(data->fp_result, "%s: not compare data\n", filename);
		else if (ret < 0)
			fprintf(data->fp_result, "%s: fail\n", filename);
		else
			fprintf(data->fp_result, "%s: pass\n", filename);
		fflush(data->fp_result);
	}

	if (data->decoder) {
		mpp_decoder_destory(data->decoder);
		data->decoder = NULL;
	}

	if (data->fp_yuv) {
		fclose(data->fp_yuv);
		data->fp_yuv = NULL;
	}

	if (file_fd >= 0)
		close(file_fd);

	return ret;
}

static int read_dir(char* path, struct dec_ctx* dec_data)
{
	char* ptr = NULL;
	struct dirent* dir_file;
	DIR* dir = opendir(path);
	if(dir == NULL) {
		loge("read dir failed");
		return -1;
	}

	while((dir_file = readdir(dir))) {
		if(strncmp(dir_file->d_name, ".", 1) == 0 || strncmp(dir_file->d_name, "..", 2) == 0)
			continue;

		ptr = strrchr(dir_file->d_name, '.');
		if(ptr == NULL)
			continue;

		if (strncmp(ptr, ".h264", 5) && strncmp(ptr, ".264", 4) && strncmp(ptr, ".png", 4) && strncmp(ptr, ".jpg", 4))
			continue;

		logi("name: %s", dir_file->d_name);
		snprintf(dec_data->file_input[dec_data->file_num], 1024, "%s%s", path, dir_file->d_name);
		logi("i: %d, filename: %s", dec_data->file_num, dec_data->file_input[dec_data->file_num]);
		dec_data->file_num ++;

		if(dec_data->file_num >= MAX_TEST_FILE)
			break;
	}

	return 0;
}

int main(int argc, char **argv)
{
	int ret = 0;
	int i, j;
	int opt;
	int loop_time = 1;

	static struct dec_ctx dec_data;
	memset(&dec_data, 0, sizeof(struct dec_ctx));
	dec_data.output_format = MPP_FMT_YUV420P;

	while (1) {
		opt = getopt(argc, argv, "i:t:f:l:m:bdhsc");
		if (opt == -1) {
			break;
		}
		switch (opt) {
		case 'i':
			strncpy(dec_data.file_input[0], optarg, 1023);
			dec_data.file_input[0][1023] = '\0';
			dec_data.file_num = 1;
			logd("file path: %s", dec_data.file_input[0]);

			break;
		case 'f':
			if (!strncmp(optarg, "nv12", 4)) {
				logi("output format nv12");
				dec_data.output_format = MPP_FMT_NV12;
			} else if(!strncmp(optarg, "nv21", 4)) {
				logi("output format nv21");
				dec_data.output_format = MPP_FMT_NV21;
			} else if(!strncmp(optarg, "yuv420", 6)) {
				logi("output format yuv420");
				dec_data.output_format = MPP_FMT_YUV420P;
			}
			break;
		case 'l':
			loop_time = atoi(optarg);
			break;
		case 'm':
			dec_data.input_by_frame = atoi(optarg);
			logi("input mode: %s", dec_data.input_by_frame ? "frame" : "slice");
			break;
		case 't':
			read_dir(optarg, &dec_data);
			break;
		case 'd':
			dec_data.display_en = 1;
			break;
		case 'c':
			dec_data.cmp_en = 1;
			break;
		case 'b':
			dec_data.drop_b_frame = 1;
			logi("enable drop non-reference B-frames");
			break;
		case 's':
			dec_data.save_data = 1;
			break;
		case 'h':
			print_help(argv[0]);
		default:
			goto out;
		}
	}

	if(dec_data.file_num == 0) {
		print_help(argv[0]);
		ret = -1;
		goto out;
	}

	dec_data.fp_result = fopen("result.txt", "wb");
	if(dec_data.fp_result == NULL) {
		logw("file result open failed");
	}

	if(dec_data.save_data) {
		dec_data.fp_save = fopen("save.bin", "wb");
		if(dec_data.fp_save == NULL)
			loge("dec_data.fp_save open failed, erron(%d)", errno);
	}

	for(j=0; j<loop_time; j++) {
		logi("loop: %d", j);
		for(i=0; i<dec_data.file_num; i++) {
			dec_data.render_eos = 0;
			dec_data.stream_eos = 0;
			dec_data.cmp_data_err = 0;
			dec_data.dec_err = 0;

			memset(dec_data.frame_info, 0, sizeof(struct frame_info)*FRAME_BUF_NUM);
			ret = dec_decode(&dec_data, dec_data.file_input[i]);
			if (0 == ret)
				logi("test successful!");
			else
				logw("test failed! ret %d", ret);
		}
	}

out:
	if(dec_data.fp_save)
		fclose(dec_data.fp_save);
	if(dec_data.fp_result)
		fclose(dec_data.fp_result);

	return ret;
}
