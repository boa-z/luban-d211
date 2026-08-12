/*
 * Copyright (C) 2020-2026 ArtInChip Technology Co. Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 *  author: <qi.xu@artinchip.com>
 *   Desc: jpeg/png decode demo
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <malloc.h>
#include <pthread.h>
#include <errno.h>

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
#include <video/artinchip_ge.h>
#include <linux/dma-buf.h>
#include <linux/dma-heap.h>

#ifdef RENDER_DRM_BACKEND
#include <xf86drm.h>
#include <xf86drmMode.h>
#include <drm_fourcc.h>
#endif

#include "mpp_ge.h"
#include "mpp_decoder.h"
#include "mpp_log.h"
static void print_help(char* program)
{
	printf("Compile time: %s %s\n", __DATE__, __TIME__);
	printf("Usage: %s [options]\n", program);
	printf("\t -i, --input: \t\t input stream file name\n");
	printf("\t -r, --rotate: \t\t enable clockwise rotate(0/90/180/270)\n");
	printf("\t -s, --scale: \t\t enable scale(1- 1/2 scale; 2- 1/4 scale; 3- 1/8 scale)\n");
	printf("\t -l, --flip: \t\t enable flip(1-horizontal flip; 2-vertical flip; 3-ver & hor flip)\n");
	printf("\t -h, --help: \t\t print help info\n");
}

static int get_file_size(FILE* fp)
{
	int len = 0;
	fseek(fp, 0, SEEK_END);
	len = ftell(fp);
	fseek(fp, 0, SEEK_SET);
	return len;
}

#ifdef RENDER_DRM_BACKEND
static int get_format_planes(enum mpp_pixel_format format)
{
	switch (format) {
		case MPP_FMT_ARGB_8888:
		case MPP_FMT_RGBA_8888:
		case MPP_FMT_RGB_888:
		case MPP_FMT_YUV400:
			return 1;
		case MPP_FMT_YUV420P:
		case MPP_FMT_YUV444P:
		case MPP_FMT_YUV422P:
			return 3;
		default:
			loge("no support picture format %d, default argb8888", format);
			return 1;
	}
}

static uint32_t conver_format(uint32_t format, bool mpp_to_drm)
{
	static const struct {
		uint32_t drm_fmt;
		enum mpp_pixel_format mpp_fmt;
	} format_info[] = {
		{ .drm_fmt = DRM_FORMAT_ARGB8888, .mpp_fmt = MPP_FMT_ARGB_8888, },
		{ .drm_fmt = DRM_FORMAT_ABGR8888, .mpp_fmt = MPP_FMT_ABGR_8888, },
		{ .drm_fmt = DRM_FORMAT_RGBA8888, .mpp_fmt = MPP_FMT_RGBA_8888, },
		{ .drm_fmt = DRM_FORMAT_BGRA8888, .mpp_fmt = MPP_FMT_BGRA_8888, },
		{ .drm_fmt = DRM_FORMAT_XRGB8888, .mpp_fmt = MPP_FMT_XRGB_8888, },
		{ .drm_fmt = DRM_FORMAT_XBGR8888, .mpp_fmt = MPP_FMT_XBGR_8888, },
		{ .drm_fmt = DRM_FORMAT_RGBX8888, .mpp_fmt = MPP_FMT_RGBX_8888, },
		{ .drm_fmt = DRM_FORMAT_BGRX8888, .mpp_fmt = MPP_FMT_BGRX_8888, },
		{ .drm_fmt = DRM_FORMAT_RGB888, .mpp_fmt = MPP_FMT_RGB_888, },
		{ .drm_fmt = DRM_FORMAT_BGR888, .mpp_fmt = MPP_FMT_BGR_888, },
		{ .drm_fmt = DRM_FORMAT_RGB565, .mpp_fmt = MPP_FMT_RGB_565, },
		{ .drm_fmt = DRM_FORMAT_BGR565, .mpp_fmt = MPP_FMT_BGR_565, },
		{ .drm_fmt = DRM_FORMAT_ARGB4444, .mpp_fmt = MPP_FMT_ARGB_4444, },
		{ .drm_fmt = DRM_FORMAT_ABGR4444, .mpp_fmt = MPP_FMT_ABGR_4444, },
		{ .drm_fmt = DRM_FORMAT_RGBA4444, .mpp_fmt = MPP_FMT_RGBA_4444, },
		{ .drm_fmt = DRM_FORMAT_BGRA4444, .mpp_fmt = MPP_FMT_BGRA_4444, },
		{ .drm_fmt = DRM_FORMAT_YUV422, .mpp_fmt = MPP_FMT_YUV422P, },
		{ .drm_fmt = DRM_FORMAT_YUV420, .mpp_fmt = MPP_FMT_YUV420P, },
		{ .drm_fmt = DRM_FORMAT_YUV444, .mpp_fmt = MPP_FMT_YUV444P, },
		{ .drm_fmt = DRM_FORMAT_NV12, .mpp_fmt = MPP_FMT_NV12, },
		{ .drm_fmt = DRM_FORMAT_NV21, .mpp_fmt = MPP_FMT_NV21, },
		{ .drm_fmt = DRM_FORMAT_NV16, .mpp_fmt = MPP_FMT_NV16, },
		{ .drm_fmt = DRM_FORMAT_NV61, .mpp_fmt = MPP_FMT_NV61, },
		{ .drm_fmt = DRM_FORMAT_YUYV, .mpp_fmt = MPP_FMT_YUYV, },
		{ .drm_fmt = DRM_FORMAT_YVYU, .mpp_fmt = MPP_FMT_YVYU, },
		{ .drm_fmt = DRM_FORMAT_UYVY, .mpp_fmt = MPP_FMT_UYVY, },
		{ .drm_fmt = DRM_FORMAT_VYUY, .mpp_fmt = MPP_FMT_VYUY, },
	};

	int i;

	if (mpp_to_drm) {
		for (i = 0; i < sizeof(format_info) / sizeof(format_info[0]); i++) {
			if (format == format_info[i].mpp_fmt)
				return format_info[i].drm_fmt;
		}

	} else {
		for (i = 0; i < sizeof(format_info) / sizeof(format_info[0]); i++) {
			if (format == format_info[i].drm_fmt)
				return format_info[i].mpp_fmt;
		}
	}

	loge("invalid drm format, use default ARGB8888 format\n");

	return 0;
}

static int ge_bitblt_frame(struct mpp_frame *frame)
{
	enum mpp_pixel_format mpp_format;
	drmModeFB2 *drmfb2 = NULL;
	drmModeCrtc *crtc = NULL;
	int fd, ret = 0;
	uint32_t crtc_id = 0;
	uint32_t buffer_id = 0;
	int dst_fd = -1;

	struct ge_bitblt blt = {0};

	fd = drmOpen("artinchip", NULL);
	if (fd < 0) {
		loge("failed to open drm\n");
		ret = -1;
		goto cleanup;
	}

	drmModeRes *resources = drmModeGetResources(fd);
	if (!resources) {
		loge("failed to Get Resources");
		drmClose(fd);
		goto cleanup;
	}

	if (resources->count_crtcs <= 0) {
		loge("no CRTCs available");
		ret = -1;
		goto cleanup_resources;
	}

	crtc_id = resources->crtcs[0];

	crtc = drmModeGetCrtc(fd, crtc_id);
	if (!crtc) {
		loge("Failed to get CRTC %u: %s", crtc_id, strerror(errno));
		ret = -1;
		goto cleanup_resources;
	}

	buffer_id = crtc->buffer_id;

	drmfb2 = drmModeGetFB2(fd, buffer_id);
	if (!drmfb2) {
		loge("Failed to get drmfb2 buffer id: %d, %s",
					buffer_id, strerror(errno));
		ret = -1;
		goto cleanup_crtc;
	}

	ret = drmPrimeHandleToFD(fd, drmfb2->handles[0], 0, &dst_fd);
	if (ret) {
		loge("Failed to get drmPrimeHandleToFD: %d, %s",
					drmfb2->handles[0], strerror(errno));
		ret = -1;
		goto cleanup_crtc;
	}

	memcpy(&blt.src_buf, &frame->buf, sizeof(struct mpp_frame));

	struct mpp_ge *ge = mpp_ge_open();
	if (!ge) {
		loge("ge open fail");
		return -1;
		goto cleanup_crtc;
	}

	mpp_format = conver_format(drmfb2->pixel_format, false);

	/* destination buffer */
	blt.dst_buf.buf_type = MPP_DMA_BUF_FD;
	blt.dst_buf.fd[0] = dst_fd;
	blt.dst_buf.stride[0] = drmfb2->pitches[0];
	blt.dst_buf.size.width = drmfb2->width;
	blt.dst_buf.size.height = drmfb2->height;
	blt.dst_buf.format = mpp_format;

	blt.dst_buf.crop_en = 1;
	blt.dst_buf.crop.x = 0;
	blt.dst_buf.crop.y = 0;
	blt.dst_buf.crop.width = frame->buf.size.width;
	blt.dst_buf.crop.height = frame->buf.size.height;

	blt.ctrl.alpha_en = 1;

	ret = mpp_ge_bitblt(ge, &blt);
	if (ret < 0) {
		loge("ge bitblt fail");
		goto cleanup_ge;
	}

	ret = mpp_ge_emit(ge);
	if (ret < 0) {
		loge("ge emit fail");
		goto cleanup_ge;
	}

	ret = mpp_ge_sync(ge);
	if (ret < 0) {
		loge("ge sync fail");
	}

cleanup_ge:
	if (ge)
		mpp_ge_close(ge);

cleanup_crtc:
	if (crtc)
		drmModeFreeCrtc(crtc);

cleanup_resources:
	if (resources)
		drmModeFreeResources(resources);

cleanup:
	if (dst_fd >= 0)
		close(dst_fd);
	if (drmfb2)
		drmModeFreeFB2(drmfb2);
	if (fd >= 0)
		drmClose(fd);

	return ret;
}

static uint32_t get_property_id(int fd, drmModeObjectProperties *props,
				const char *name)
{
	drmModePropertyPtr property;
	uint32_t i, id = 0;

	for (i = 0; i < props->count_props; i++) {
		property = drmModeGetProperty(fd, props->props[i]);
		if (!strcmp(property->name, name))
			id = property->prop_id;
		drmModeFreeProperty(property);

		if (id)
			break;
	}

	return id;
}

static uint32_t drm_export_buffer_id(int fd, struct mpp_frame *frame)
{
	struct mpp_buf *picture_buf = &frame->buf;
	uint32_t handles[4] = {0};
	uint32_t pitches[4] = {0};
	uint32_t offsets[4] = {0};
	uint32_t buf_id;
	int dmabuf_num = 0;
	int ret;

	dmabuf_num = get_format_planes(picture_buf->format);

	/* Convert DMA-BUF file descriptors to DRM handles */
	for (int i = 0; i < dmabuf_num; i++) {
		struct drm_prime_handle prime_handle = {0};
		prime_handle.fd = picture_buf->fd[i];
		prime_handle.flags = 0;

		ret = drmIoctl(fd, DRM_IOCTL_PRIME_FD_TO_HANDLE, &prime_handle);
		if (ret) {
			loge("Failed to get DRM handle from DMA-BUF fd[%d]: %s\n",
				i, strerror(errno));
			return -1;
		}

		handles[i] = prime_handle.handle;
		pitches[i] = picture_buf->stride[i];
	}

	uint32_t width = picture_buf->size.width;
	uint32_t height = picture_buf->size.height;
	uint32_t format = conver_format(picture_buf->format, true);

	ret = drmModeAddFB2(fd, width, height, format,
			    handles, pitches, offsets, &buf_id, 0);
	if (ret < 0) {
		loge("Failed to add DRM FB2: %s", strerror(errno));
		return -1;
	}

	return buf_id;
}

static int drm_backend_render_direct(struct mpp_frame *frame)
{
	uint32_t crtc_id, plane_id, buffer_id;
	uint32_t prop_crtc_id, prop_fb_id;
	uint32_t prop_crtc_x, prop_crtc_y;
	uint32_t prop_crtc_w, prop_crtc_h;
	uint32_t prop_src_x, prop_src_y;
	uint32_t prop_src_w, prop_src_h;
	drmModeObjectProperties *props;
	drmModePlaneRes *plane_res;
	drmModeAtomicReq *req;
	drmModeRes *res;
	int fd, ret = 0;

	fd = drmOpen("artinchip", NULL);
	if (fd < 0) {
		loge("failed to open drm");
		return -1;
	}

	res = drmModeGetResources(fd);
	if (res == 0) {
		loge("Failed to get resources from card");
		ret = -1;
		goto close_fd;
	}

	/* Use the first available CRTC by default */
	crtc_id = res->crtcs[0];

	ret = drmSetClientCap(fd, DRM_CLIENT_CAP_ATOMIC, 1);
	if (ret) {
		loge("no atomic modesetting support: %s", strerror(errno));
		ret = -1;
		goto free_resources;
	}

	drmSetClientCap(fd, DRM_CLIENT_CAP_UNIVERSAL_PLANES, 1);

	plane_res = drmModeGetPlaneResources(fd);
	if (!plane_res) {
		loge("drmModeGetPlaneResources failed: %s",
			strerror(errno));
		ret = -1;
		goto free_resources;
	}

	/* Use the first available plane by default */
	plane_id = plane_res->planes[0];

	props = drmModeObjectGetProperties(fd, plane_id, DRM_MODE_OBJECT_PLANE);
	if (!props) {
		loge("No properties: %s.", strerror(errno));
		ret = -1;
		goto free_plane_res;
	}

	buffer_id = drm_export_buffer_id(fd, frame);
	if (buffer_id < 0) {
		loge("drm export buffer_id failed %s", strerror(errno));
		ret = -1;
		goto free_plane_res;
	}

	prop_crtc_id = get_property_id(fd, props, "CRTC_ID");
	prop_fb_id = get_property_id(fd, props, "FB_ID");
	prop_crtc_x = get_property_id(fd, props, "CRTC_X");
	prop_crtc_y = get_property_id(fd, props, "CRTC_Y");
	prop_crtc_w = get_property_id(fd, props, "CRTC_W");
	prop_crtc_h = get_property_id(fd, props, "CRTC_H");
	prop_src_x = get_property_id(fd, props, "SRC_X");
	prop_src_y = get_property_id(fd, props, "SRC_Y");
	prop_src_w = get_property_id(fd, props, "SRC_W");
	prop_src_h = get_property_id(fd, props, "SRC_H");
	drmModeFreeObjectProperties(props);

	req = drmModeAtomicAlloc();
	if (!req) {
		loge("Failed to allocate atomic request");
		ret = -1;
		goto free_plane_res;
	}

	ret = 0;
	ret |= drmModeAtomicAddProperty(req, plane_id, prop_crtc_id, crtc_id);
	ret |= drmModeAtomicAddProperty(req, plane_id, prop_fb_id, buffer_id);
	ret |= drmModeAtomicAddProperty(req, plane_id, prop_crtc_x, 0);
	ret |= drmModeAtomicAddProperty(req, plane_id, prop_crtc_y, 0);
	ret |= drmModeAtomicAddProperty(req, plane_id, prop_crtc_w, frame->buf.size.width);
	ret |= drmModeAtomicAddProperty(req, plane_id, prop_crtc_h, frame->buf.size.height);
	ret |= drmModeAtomicAddProperty(req, plane_id, prop_src_x, 0);
	ret |= drmModeAtomicAddProperty(req, plane_id, prop_src_y, 0);
	ret |= drmModeAtomicAddProperty(req, plane_id, prop_src_w, frame->buf.size.width << 16);
	ret |= drmModeAtomicAddProperty(req, plane_id, prop_src_h, frame->buf.size.height << 16);
	if (ret < 0) {
		loge("failed to set atomic property");
		ret = -1;
		goto cleanup_atomic;
	}

	ret = drmModeAtomicCommit(fd, req, 0, NULL);
	if (ret)
		loge("Atomic Commit failed");

cleanup_atomic:
	if (req)
		drmModeAtomicFree(req);

	getchar();

free_plane_res:
	if (plane_res)
		drmModeFreePlaneResources(plane_res);
free_resources:
	if (res)
		drmModeFreeResources(res);
close_fd:
	close(fd);
	return 0;
}

static int drm_backend_render_frame(struct mpp_frame *frame)
{
	int ret = 0;

	if (frame->buf.format == MPP_FMT_ARGB_8888 || frame->buf.format == MPP_FMT_ABGR_8888 ||
	    frame->buf.format == MPP_FMT_RGBA_8888 || frame->buf.format == MPP_FMT_BGRA_8888 ) {
		// 1. if the pixels have alpha channel, we need enable pixel alpha blending in GE.
		//     the data flow: VE -> GE -> DE.
		logi("alpha channel, we need pixel alpha blending");
		ret = ge_bitblt_frame(frame);
	} else {
		// the data flow: VE -> DE.
		ret = drm_backend_render_direct(frame);
		logi("no alpha channel, drm render direct");
	}

	return ret;
}
#else
static int g_screen_w = 0;
static int g_screen_h = 0;
static int g_fb_len = 0;
static int g_fb_stride = 0;
static unsigned int g_fb_format = 0;
static unsigned int g_fb_phy = 0;
unsigned char *g_fb_buf = NULL;
static int set_fb_layer_alpha(int fb0_fd, int val)
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

static void video_layer_set(int fb0_fd, struct mpp_buf *picture_buf)
{
	struct aicfb_layer_data layer = {0};
	int dmabuf_num = 0;
	struct dma_buf_info dmabuf_fd[3];
	int i;

	if (fb0_fd < 0)
		return;

	layer.layer_id = 0;
	layer.enable = 1;
	layer.scale_size.width = MPP_MIN(1024, picture_buf->size.width);
	layer.scale_size.height= MPP_MIN(600, picture_buf->size.height);

	logi("stride: %d, crop_en: %d", picture_buf->stride[0], picture_buf->crop_en);
	logi("%d: %d, %d: %d", picture_buf->crop.x, picture_buf->crop.y, picture_buf->crop.width, picture_buf->crop.height);

	layer.pos.x = 0;
	layer.pos.y = 0;
	layer.buf = *picture_buf;

	if (picture_buf->format == MPP_FMT_ARGB_8888) {
		dmabuf_num = 1;
	} else if (picture_buf->format == MPP_FMT_RGBA_8888) {
		dmabuf_num = 1;
	} else if (picture_buf->format == MPP_FMT_RGB_888) {
		dmabuf_num = 1;
	} else if (picture_buf->format == MPP_FMT_YUV420P) {
		dmabuf_num = 3;
	} else if (picture_buf->format == MPP_FMT_YUV444P) {
		dmabuf_num = 3;
	} else if (picture_buf->format == MPP_FMT_YUV422P) {
		dmabuf_num = 3;
	} else if (picture_buf->format == MPP_FMT_YUV400) {
		dmabuf_num = 1;
	} else {
		loge("no support picture foramt %d, default argb8888", picture_buf->format);
	}

	// add dmabuf to de driver
	for(i=0; i<dmabuf_num; i++) {
		dmabuf_fd[i].fd = picture_buf->fd[i];
		if (ioctl(fb0_fd, AICFB_ADD_DMABUF, &dmabuf_fd[i]) < 0)
			loge("fb ioctl() AICFB_UPDATE_LAYER_CONFIG failed!");
	}

	// update layer config (it is async interface)
	if (ioctl(fb0_fd, AICFB_UPDATE_LAYER_CONFIG, &layer) < 0)
		loge("fb ioctl() AICFB_UPDATE_LAYER_CONFIG failed!");

	// wait vsync (wait layer config)
	ioctl(fb0_fd, AICFB_WAIT_FOR_VSYNC, NULL);

	// display this picture 2 seconds
	usleep(2000000);

	// disable layer
	layer.enable = 0;
	if(ioctl(fb0_fd, AICFB_UPDATE_LAYER_CONFIG, &layer) < 0)
		loge("fb ioctl() AICFB_UPDATE_LAYER_CONFIG failed!");

	// remove dmabuf to de driver
	for(i=0; i<dmabuf_num; i++) {
		if (ioctl(fb0_fd, AICFB_RM_DMABUF, &dmabuf_fd[i]) < 0)
			loge("fb ioctl() AICFB_UPDATE_LAYER_CONFIG failed!");
	}
}

static int fb_open(void)
{
	struct fb_fix_screeninfo fix;
	struct fb_var_screeninfo var;
	struct aicfb_layer_data layer;

	int fb_fd;
	fb_fd = open("/dev/fb0", O_RDWR);
	if (fb_fd == -1) {
		loge("open fb fail");
		return -1;
	}

	if (ioctl(fb_fd, FBIOGET_FSCREENINFO, &fix) < 0) {
		loge("ioctl FBIOGET_FSCREENINFO");
		close(fb_fd);
		return -1;
	}

	if (ioctl(fb_fd, FBIOGET_VSCREENINFO, &var) < 0) {
		loge("ioctl FBIOGET_VSCREENINFO");
		close(fb_fd);
		return -1;
	}

	if(ioctl(fb_fd, AICFB_GET_FB_LAYER_CONFIG, &layer) < 0) {
		loge("ioctl FBIOGET_VSCREENINFO");
		close(fb_fd);
		return -1;
	}

	g_screen_w = var.xres;
	g_screen_h = var.yres;
	g_fb_len = fix.smem_len;
	g_fb_phy = fix.smem_start;
	g_fb_stride = fix.line_length;
	g_fb_format = layer.buf.format;

	logi("screen_w = %d, screen_h = %d, stride = %d, format = %d\n",
			var.xres, var.yres, g_fb_stride, g_fb_format);
	return fb_fd;
}

static int fbdev_backend_render_frame(struct mpp_frame *frame)
{
	int ret = 0;

	int fb_fd = fb_open();

	if (frame->buf.format == MPP_FMT_ARGB_8888 || frame->buf.format == MPP_FMT_ABGR_8888 ||
	    frame->buf.format == MPP_FMT_RGBA_8888 || frame->buf.format == MPP_FMT_BGRA_8888 ||
	    frame->buf.format == MPP_FMT_YUV444P) {
		// 1. if the pixels have alpha channel, we need enable pixel alpha blending in GE.
		//     the data flow: VE -> GE -> DE.
		logi("alpha channel, we need pixel alpha blending");
		struct ge_bitblt blt = {0};
		memset(&blt, 0, sizeof(struct ge_bitblt));
		/* source buffer */
		memcpy(&blt.src_buf, &frame->buf, sizeof(struct mpp_frame));

		struct mpp_ge *ge = mpp_ge_open();
		if (!ge) {
			loge("ge open fail");
			return -1;
		}

		/* dstination buffer */
		blt.dst_buf.buf_type = MPP_PHY_ADDR;
		blt.dst_buf.phy_addr[0] = g_fb_phy;
		blt.dst_buf.stride[0] = g_fb_stride;
		blt.dst_buf.size.width = g_screen_w;
		blt.dst_buf.size.height = g_screen_h;
		blt.dst_buf.format = g_fb_format;

		blt.dst_buf.crop_en = 1;
		blt.dst_buf.crop.x = 0;
		blt.dst_buf.crop.y = 0;
		blt.dst_buf.crop.width = frame->buf.size.width;
		blt.dst_buf.crop.height = frame->buf.size.height;

		if (frame->buf.format == MPP_FMT_YUV444P)
			blt.ctrl.alpha_en = 0;
		else
			blt.ctrl.alpha_en = 1;

		ret =  mpp_ge_bitblt(ge, &blt);
		if (ret < 0) {
			loge("ge bitblt fail");
		}

		ret = mpp_ge_emit(ge);
		if (ret < 0) {
			loge("ge emit fail");
		}

		ret = mpp_ge_sync(ge);
		if (ret < 0) {
			loge("ge sync fail");
		}

		if (ge)
			mpp_ge_close(ge);
	} else {
		// 2. data flow: VE -> DE
		set_fb_layer_alpha(fb_fd, 0);
		video_layer_set(fb_fd, &frame->buf);
	}

	close(fb_fd);

	return 0;
}
#endif

static int render_frame(struct mpp_frame *frame)
{
#ifdef RENDER_DRM_BACKEND
	return drm_backend_render_frame(frame);
#else
	return fbdev_backend_render_frame(frame);
#endif
}

static int parse_rotation(char *str)
{
    if (!strcmp(optarg, "90"))
        return MPP_ROTATION_90;

    if (!strcmp(optarg, "180"))
        return MPP_ROTATION_180;

    if (!strcmp(optarg, "270"))
        return MPP_ROTATION_270;

    return MPP_ROTATION_0;
}

static int parse_flip(char *str)
{
    if (!strcmp(optarg, "1"))
        return MPP_FLIP_H;

    if (!strcmp(optarg, "2"))
        return MPP_FLIP_V;

    if (!strcmp(optarg, "3"))
        return MPP_FLIP_H | MPP_FLIP_V;

    return 0;
}

int main(int argc, char **argv)
{
	int ret = 0;
	int opt;
	int file_len;
	FILE* fp = NULL;
	char* ptr = NULL;
	int type = MPP_CODEC_VIDEO_DECODER_MJPEG;
	int ver_scale = 0;
	int hor_scale = 0;
	int rot_flip_flag = 0;

	while (1) {
		opt = getopt(argc, argv, "i:s:r:l:h");
		if (opt == -1) {
			break;
		}
		switch (opt) {
		case 'i':
			logd("file path: %s", optarg);
			if (optarg) {
				ptr = strrchr(optarg, '.');
				if (ptr == NULL) {
					loge("unsupport this file: %s", optarg);
					return -1;
				}

				if (!strcmp(ptr, ".jpg")) {
					type = MPP_CODEC_VIDEO_DECODER_MJPEG;
				} else if (!strcmp(ptr, ".png")) {
					type = MPP_CODEC_VIDEO_DECODER_PNG;
				} else if (!strcmp(ptr, ".aicp")) {
					type = MPP_CODEC_VIDEO_DECODER_AICP;
				}
				logd("decode type: 0x%02X", type);
			}
			fp = fopen(optarg, "rb");

			continue;
		case 's':
			ver_scale = atoi(optarg);
			hor_scale = ver_scale = ver_scale > 3 ? 3: ver_scale;
			loge("scale: %d", ver_scale);
			continue;
		case 'r':
			rot_flip_flag |= parse_rotation(optarg);
			continue;
		case 'l':
			rot_flip_flag |= parse_flip(optarg);
			continue;
		case 'h':
		default:
			print_help(argv[0]);
			return -1;
		}
	}

	if(fp == NULL) {
		loge("please input the right file path");
		return -1;
	}

	file_len = get_file_size(fp);

	// 1. create mpp_decoder
	struct mpp_decoder* dec = mpp_decoder_create(type);
	if(dec == NULL) {
		loge("create decoder failed");
		goto out;
	}

	struct decode_config config;
	config.bitstream_buffer_size = (file_len + 1023 + 32) & (~1023);
	config.extra_frame_num = 0;
	config.packet_count = 1;

	// JPEG not supprt YUV2RGB
	if(type == MPP_CODEC_VIDEO_DECODER_MJPEG)
		config.pix_fmt = MPP_FMT_YUV420P;
	else if(type == MPP_CODEC_VIDEO_DECODER_PNG)
		config.pix_fmt = MPP_FMT_ARGB_8888;

	if(ver_scale || hor_scale) {
		struct mpp_scale_ratio scale;
		scale.hor_scale = hor_scale;
		scale.ver_scale = ver_scale;
		mpp_decoder_control(dec, MPP_DEC_INIT_CMD_SET_SCALE, &scale);
	}

	if(rot_flip_flag) {
		logw("rot_flip_flag: %d", rot_flip_flag);
		mpp_decoder_control(dec, MPP_DEC_INIT_CMD_SET_ROT_FLIP_FLAG, &rot_flip_flag);
	}

	// 2. init mpp_decoder
	mpp_decoder_init(dec, &config);

	// 3. get an empty packet from mpp_decoder
	struct mpp_packet packet;
	memset(&packet, 0, sizeof(struct mpp_packet));
	mpp_decoder_get_packet(dec, &packet, file_len);

	// 4. copy data to packet
	fread(packet.data, 1, file_len, fp);
	packet.size = file_len;
	packet.flag = PACKET_FLAG_EOS;

	// 5. put the packet to mpp_decoder
	mpp_decoder_put_packet(dec, &packet);

	// 6. decode
	ret = mpp_decoder_decode(dec);
	if(ret < 0) {
		loge("decode error");
		goto out;
	}

	// 7. get a decoded frame
	struct mpp_frame frame;
	memset(&frame, 0, sizeof(struct mpp_frame));
	mpp_decoder_get_frame(dec, &frame);
	// 8. render this frame
	render_frame(&frame);

	// 9. return this frame
	mpp_decoder_put_frame(dec, &frame);

	// 10. destroy mpp_decoder
	mpp_decoder_destory(dec);

out:
	if(fp)
		fclose(fp);
	return ret;
}
