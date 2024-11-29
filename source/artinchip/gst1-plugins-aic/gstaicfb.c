/*
 * Copyright (C) 2020-2023 ArtInChip Technology Co., Ltd.
 *
 * Authors:  <qi.xu@artinchip.com>
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <linux/fb.h>
#include <video/artinchip_fb.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <string.h>
#include <gst/gst.h>

#include "gstaicfb.h"

int gst_aicfb_open()
{
	int fd;
	fd = open("/dev/fb0", O_RDWR);
	if (fd < 0) {
		GST_ERROR_OBJECT(NULL, "open fb0 failed!");
		return -1;
	}

	return fd;
}

void gst_aicfb_close(int fd)
{
	struct aicfb_layer_data layer = {0};
	//* disable layer
	layer.enable = 0;
	if(ioctl(fd, AICFB_UPDATE_LAYER_CONFIG, &layer) < 0)
		GST_ERROR_OBJECT(NULL, "fb ioctl() AICFB_UPDATE_LAYER_CONFIG failed!");

	close(fd);
}

int gst_get_screeninfo(int fd, int *width, int *height)
{
	struct mpp_size s = {0};

	if (ioctl(fd, FBIOGET_FSCREENINFO, &s) == -1) {
		GST_ERROR_OBJECT(NULL, "read fixed information failed!");
		return -1;
	}

	*width = s.width;
	*height = s.height;

	return 0;
}

int gst_aicfb_render(int fd, struct mpp_buf* buf)
{
	int ret = 0;
	int i;
	int dmabuf_num = 0;
	struct aicfb_alpha_config alpha = {0};
	struct aicfb_layer_data layer = {0};
	struct dma_buf_info dmabuf_fd[3];

	alpha.layer_id = 1;
	alpha.enable = 1;
	alpha.mode = 1;
	alpha.value = 0;
	ret = ioctl(fd, AICFB_UPDATE_ALPHA_CONFIG, &alpha);
	if (ret < 0)
		GST_ERROR_OBJECT(NULL, "fb ioctl() AICFB_UPDATE_ALPHA_CONFIG failed!");

	layer.layer_id = AICFB_LAYER_TYPE_VIDEO;
	layer.enable = 1;
	layer.scale_size.width = buf->size.width;
	layer.scale_size.height= buf->size.height;
	layer.pos.x = 0;
	layer.pos.y = 0;
	memcpy(&layer.buf, buf, sizeof(struct mpp_buf));

	if (buf->format == MPP_FMT_ARGB_8888) {
		dmabuf_num = 1;
	} else if (buf->format == MPP_FMT_RGBA_8888) {
		dmabuf_num = 1;
	} else if (buf->format == MPP_FMT_RGB_888) {
		dmabuf_num = 1;
	} else if (buf->format == MPP_FMT_YUV420P) {
		dmabuf_num = 3;
	} else if (buf->format == MPP_FMT_YUV444P) {
		dmabuf_num = 3;
	} else if (buf->format == MPP_FMT_YUV422P) {
		dmabuf_num = 3;
	} else if (buf->format == MPP_FMT_YUV400) {
		dmabuf_num = 1;
	} else {
		GST_ERROR_OBJECT(NULL, "no support picture foramt %d, default argb8888", buf->format);
	}

	//* add dmabuf to de driver
	for(i=0; i<dmabuf_num; i++) {
		dmabuf_fd[i].fd = buf->fd[i];
		if (ioctl(fd, AICFB_ADD_DMABUF, &dmabuf_fd[i]) < 0)
			GST_ERROR_OBJECT(NULL, "fb ioctl() AICFB_UPDATE_LAYER_CONFIG failed!");
	}

	//* update layer config (it is async interface)
	if (ioctl(fd, AICFB_UPDATE_LAYER_CONFIG, &layer) < 0)
		GST_ERROR_OBJECT(NULL, "fb ioctl() AICFB_UPDATE_LAYER_CONFIG failed!");

	//* wait vsync (wait layer config)
	ioctl(fd, AICFB_WAIT_FOR_VSYNC, NULL);

	//* remove dmabuf to de driver
	for(i=0; i<dmabuf_num; i++) {
		if (ioctl(fd, AICFB_RM_DMABUF, &dmabuf_fd[i]) < 0)
			GST_ERROR_OBJECT(NULL, "fb ioctl() AICFB_UPDATE_LAYER_CONFIG failed!");
	}

	return 0;
}
