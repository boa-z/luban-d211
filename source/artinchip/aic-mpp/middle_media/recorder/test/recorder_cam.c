/*
 * Copyright (C) 2020-2026 ArtInChip Technology Co. Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 *  Author: <che.jiang@artinchip.com>
 *  Desc: Record camera abstraction layer
 */

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <linux/dma-buf.h>
#include <linux/dma-heap.h>

#include <linux/fb.h>
#include <video/artinchip_fb.h>
#include "mpp_log.h"
#include "recorder_cam.h"

/* platform device paths */
#define SENSOR_DEV      "/dev/v4l-subdev0"
#define VIDEO_DEV       "/dev/video0"
#define DVP_SUBDEV_DEV  "/dev/v4l-subdev1"
#define FB_DEV          "/dev/fb0"


int recorder_media_open(struct recorder_media_dev *mdev)
{
	if (!mdev)
		return -1;

	mdev->sensor_fd = -1;
	mdev->video_fd = -1;
	mdev->dvp_fd = -1;
	mdev->fb_fd = -1;

	mdev->sensor_fd = open(SENSOR_DEV, O_RDWR);
	if (mdev->sensor_fd < 0) {
		loge("open %s failed", SENSOR_DEV);
		return -1;
	}
	mdev->video_fd = open(VIDEO_DEV, O_RDWR);
	if (mdev->video_fd < 0) {
		loge("open %s failed", VIDEO_DEV);
		close(mdev->sensor_fd);
		return -1;
	}
	mdev->dvp_fd = open(DVP_SUBDEV_DEV, O_RDWR);
	if (mdev->dvp_fd < 0) {
		loge("open %s failed", DVP_SUBDEV_DEV);
		close(mdev->video_fd);
		close(mdev->sensor_fd);
		return -1;
	}
	mdev->fb_fd = open(FB_DEV, O_RDWR);
	if (mdev->fb_fd < 0) {
		loge("open %s failed", FB_DEV);
		close(mdev->dvp_fd);
		close(mdev->video_fd);
		close(mdev->sensor_fd);
		return -1;
	}
	return 0;
}

void recorder_media_close(struct recorder_media_dev *mdev)
{
	if (!mdev)
		return;
	if (mdev->sensor_fd > 0) { close(mdev->sensor_fd); mdev->sensor_fd = -1; }
	if (mdev->video_fd > 0)  { close(mdev->video_fd);  mdev->video_fd = -1; }
	if (mdev->dvp_fd > 0)    { close(mdev->dvp_fd);    mdev->dvp_fd = -1; }
	if (mdev->fb_fd > 0)     { close(mdev->fb_fd);     mdev->fb_fd = -1; }
}

static int sensor_set_fmt(struct recorder_media_dev *mdev,
			  struct recorder_cam_data *cam)
{
	struct v4l2_subdev_frame_interval fr = { 0 };
	struct v4l2_subdev_format f = { 0 };
	int ret;

	f.pad = 0;
	f.which = V4L2_SUBDEV_FORMAT_ACTIVE;
	ret = ioctl(mdev->sensor_fd, VIDIOC_SUBDEV_G_FMT, &f);
	if (ret < 0) {
		loge("sensor G_FMT failed");
		return -1;
	}

	if (f.format.width != mdev->sensor_width ||
	    f.format.height != mdev->sensor_height) {
		printf("Set sensor %dx%d -> %dx%d\n",
		       f.format.width, f.format.height,
		       mdev->sensor_width, mdev->sensor_height);
		f.format.width = mdev->sensor_width;
		f.format.height = mdev->sensor_height;
		ret = ioctl(mdev->sensor_fd, VIDIOC_SUBDEV_S_FMT, &f);
		if (ret < 0) {
			loge("sensor S_FMT failed");
			return -1;
		}
	}
	ioctl(mdev->sensor_fd, VIDIOC_SUBDEV_G_FMT, &f);

	ret = ioctl(mdev->sensor_fd, VIDIOC_SUBDEV_G_FRAME_INTERVAL, &fr);
	if (ret == 0 && fr.interval.denominator != mdev->sensor_fr) {
		printf("Set sensor framerate %d -> %d\n",
		       fr.interval.denominator, mdev->sensor_fr);
		fr.interval.denominator = mdev->sensor_fr;
		ret = ioctl(mdev->sensor_fd, VIDIOC_SUBDEV_S_FRAME_INTERVAL, &fr);
		if (ret < 0) {
			loge("sensor S_FRAME_INTERVAL failed");
			return -1;
		}
		ioctl(mdev->sensor_fd, VIDIOC_SUBDEV_G_FRAME_INTERVAL, &fr);
	}

	cam->src_fmt = f;
	cam->w = f.format.width;
	cam->h = f.format.height;
	printf("Sensor: %dx%d code %#x fr %d\n",
	       f.format.width, f.format.height, f.format.code,
	       fr.interval.denominator);
	return 0;
}

static int dvp_subdev_set_fmt(struct recorder_media_dev *mdev,
			      struct recorder_cam_data *cam)
{
	struct v4l2_subdev_format f = cam->src_fmt;

	f.pad = 0;
	f.which = V4L2_SUBDEV_FORMAT_ACTIVE;
	if (ioctl(mdev->dvp_fd, VIDIOC_SUBDEV_S_FMT, &f) < 0) {
		loge("dvp subdev S_FMT failed");
		return -1;
	}
	return 0;
}

static int dvp_cfg(struct recorder_media_dev *mdev, struct recorder_cam_data *cam)
{
	struct v4l2_format f = { 0 };

	f.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
	f.fmt.pix_mp.width  = cam->w;
	f.fmt.pix_mp.height = cam->h;
	f.fmt.pix_mp.pixelformat = cam->fmt;
	f.fmt.pix_mp.num_planes = CAM_PLANE_NUM;
	if (ioctl(mdev->video_fd, VIDIOC_S_FMT, &f) < 0) {
		loge("VIDIOC_S_FMT failed");
		return -1;
	}
	return 0;
}

int recorder_cam_start(struct recorder_media_dev *mdev)
{
	enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;

	if (ioctl(mdev->video_fd, VIDIOC_STREAMON, &type) < 0) {
		loge("VIDIOC_STREAMON failed");
		return -1;
	}
	return 0;
}

int recorder_cam_stop(struct recorder_media_dev *mdev)
{
	enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;

	if (ioctl(mdev->video_fd, VIDIOC_STREAMOFF, &type) < 0) {
		loge("VIDIOC_STREAMOFF failed");
		return -1;
	}
	return 0;
}

int recorder_cam_queue_buf(struct recorder_media_dev *mdev, int index)
{
	struct v4l2_plane planes[CAM_PLANE_NUM] = { 0 };
	struct v4l2_buffer buf = { 0 };

	buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
	buf.memory = V4L2_MEMORY_MMAP;
	buf.index = index;
	buf.length = CAM_PLANE_NUM;
	buf.m.planes = planes;
	if (ioctl(mdev->video_fd, VIDIOC_QBUF, &buf) < 0) {
		loge("VIDIOC_QBUF failed");
		return -1;
	}
	return 0;
}

int recorder_cam_dequeue_buf(struct recorder_media_dev *mdev, int *index)
{
	struct v4l2_plane planes[CAM_PLANE_NUM] = { 0 };
	struct v4l2_buffer buf = { 0 };

	buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
	buf.memory = V4L2_MEMORY_MMAP;
	buf.length = CAM_PLANE_NUM;
	buf.m.planes = planes;
	if (ioctl(mdev->video_fd, VIDIOC_DQBUF, &buf) < 0) {
		logw("VIDIOC_DQBUF failed");
		return -1;
	}
	*index = buf.index;
	return 0;
}

static int cam_expbuf(struct recorder_media_dev *mdev,
		      struct recorder_cam_data *cam, int index)
{
	struct video_buf_info *binfo = &cam->binfo[index];

	for (int i = 0; i < CAM_PLANE_NUM; i++) {
		struct v4l2_exportbuffer expbuf = { 0 };
		expbuf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
		expbuf.index = index;
		expbuf.plane = i;
		if (ioctl(mdev->video_fd, VIDIOC_EXPBUF, &expbuf) < 0) {
			loge("VIDIOC_EXPBUF %d/%d failed", i, index);
			return -1;
		}
		binfo->planes[i].fd = expbuf.fd;
	}
	return 0;
}

static int vin_mmap(int index, int plane_num, struct v4l2_plane *vplane,
			 struct video_buf_info *binfo, int dev_id)
{
	struct video_plane *plane = binfo->planes;
	int i, sum = 0;

	for (i = 0; i < plane_num; i++, plane++) {
		plane->vaddr = NULL;
		plane->len = 0;
		if (!plane->fd)
			continue;

		plane->vaddr = mmap(NULL, vplane[i].length, PROT_READ | PROT_WRITE,
							MAP_SHARED, dev_id, vplane[i].m.mem_offset);
		if (plane->vaddr == MAP_FAILED) {
			loge("%s buf %d-%d: Failed to mmap %d for file %d! %d[%s] \n",
				"video",
				index, i, vplane[i].length, plane->fd, errno, strerror(errno));
			return -1;
		}
		plane->len = vplane[i].length;
		sum += plane->len;
	}

	return sum;
}

static int cam_request_buf(struct recorder_media_dev *mdev,
			   struct recorder_cam_data *cam, int num)
{
	struct v4l2_plane planes[CAM_PLANE_NUM] = { 0 };
	struct v4l2_requestbuffers req = { 0 };
	struct v4l2_buffer buf = { 0 };
	int ret;

	req.count = num;
	req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
	req.memory = V4L2_MEMORY_MMAP;
	if (ioctl(mdev->video_fd, VIDIOC_REQBUFS, &req) < 0) {
		loge("VIDIOC_REQBUFS failed");
		return -1;
	}

	for (int i = 0; i < num; i++) {
		if (cam_expbuf(mdev, cam, i) < 0)
			return -1;
		memset(&buf, 0, sizeof(struct v4l2_buffer));
		memset(planes, 0, sizeof(struct v4l2_plane) * CAM_PLANE_NUM);
		buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
		buf.index = i;
		buf.length = CAM_PLANE_NUM;
		buf.memory = V4L2_MEMORY_DMABUF;
		buf.m.planes = planes;
		if (ioctl(mdev->video_fd, VIDIOC_QUERYBUF, &buf) < 0) {
			loge("VIDIOC_QUERYBUF %d failed", i);
			return -1;
		}

		ret = vin_mmap(i, CAM_PLANE_NUM, planes, &cam->binfo[i], mdev->video_fd);
		if (ret < 0)
			return -1;

		printf("DVP Buf(%d): fd: %d, %d, vaddr: %p, %p.\n", i,
			cam->binfo[i].planes[0].fd, cam->binfo[i].planes[1].fd,
			cam->binfo[i].planes[0].vaddr, cam->binfo[i].planes[1].vaddr);
	}
	return 0;
}

static void cam_release_buf(struct recorder_cam_data *cam, int num)
{
	for (int i = 0; i < num; i++) {
		struct video_buf_info *binfo = &cam->binfo[i];
		for (int j = 0; j < CAM_PLANE_NUM; j++) {
			if (binfo->planes[j].vaddr && binfo->planes[j].len > 0) {
				munmap(binfo->planes[j].vaddr, binfo->planes[j].len);
				binfo->planes[j].vaddr = NULL;
				binfo->planes[j].len = 0;
			}
		}
	}
}

static void recorder_cam_dmabuf_begin(struct recorder_cam_data *cam,
			       struct recorder_media_dev *mdev, unsigned int num)
{
	for (int i = 0; i < num; i++) {
		struct video_plane *plane = cam->binfo[i].planes;
		for (int j = 0; j < CAM_PLANE_NUM; j++, plane++) {
			struct dma_buf_info fds = { .fd = plane->fd };
			if (ioctl(mdev->fb_fd, AICFB_ADD_DMABUF, &fds) < 0)
				loge("ADD DMABUF %d failed", plane->fd);
		}
	}
}

static void recorder_cam_dmabuf_end(struct recorder_cam_data *cam,
			     struct recorder_media_dev *mdev, unsigned int num)
{
	for (int i = 0; i < num; i++) {
		struct video_plane *plane = cam->binfo[i].planes;
		for (int j = 0; j < CAM_PLANE_NUM; j++, plane++) {
			struct dma_buf_info fds = { .fd = plane->fd };
			if (ioctl(mdev->fb_fd, AICFB_RM_DMABUF, &fds) < 0)
				loge("RM DMABUF %d failed", plane->fd);
		}
	}
}

int recorder_cam_init(struct recorder_cam_data *cam,
		      struct recorder_media_dev *mdev,
		      enum aic_recorder_vin_type vin_type)
{
	int i;

	if (!cam || !mdev)
		return -1;

	switch (vin_type) {
	case AIC_RECORDER_VIN_DVP:
		break;
	case AIC_RECORDER_VIN_MIPI:
		/* TODO: implement MIPI init */
		loge("MIPI not implemented yet");
		return -1;
	default:
		loge("unsupported vin type %d", vin_type);
		return -1;
	}

	memset(cam, 0, sizeof(struct recorder_cam_data));
	mdev->sensor_width = 1280;
	mdev->sensor_height = 720;
	mdev->sensor_fr = 30;
	cam->fmt = V4L2_PIX_FMT_NV12;

	if (recorder_media_open(mdev) != 0) {
		loge("media open failed");
		return -1;
	}

	if (sensor_set_fmt(mdev, cam) < 0)
		goto err;
	if (dvp_subdev_set_fmt(mdev, cam) < 0)
		goto err;

	if (cam->fmt == V4L2_PIX_FMT_NV16)
		cam->frame_size = cam->w * cam->h * 2;
	else
		cam->frame_size = (cam->w * cam->h * 3) >> 1;

	if (dvp_cfg(mdev, cam) < 0)
		goto err;
	if (cam_request_buf(mdev, cam, CAM_BUF_NUM) < 0)
		goto err_release;

	recorder_cam_dmabuf_begin(cam, mdev, CAM_BUF_NUM);
	for (i = 0; i < CAM_BUF_NUM; i++) {
		if (recorder_cam_queue_buf(mdev, i) < 0)
			goto err_dmabuf;
	}

	if (recorder_cam_start(mdev) < 0)
		goto err_dmabuf;

	cam->inited = true;
	return 0;

err_dmabuf:
	recorder_cam_dmabuf_end(cam, mdev, CAM_BUF_NUM);
err_release:
	cam_release_buf(cam, CAM_BUF_NUM);
err:
	recorder_media_close(mdev);
	return -1;
}

void recorder_cam_deinit(struct recorder_cam_data *cam,
			 struct recorder_media_dev *mdev)
{
	if (!cam || !cam->inited)
		return;

	recorder_cam_stop(mdev);
	recorder_cam_dmabuf_end(cam, mdev, CAM_BUF_NUM);
	cam_release_buf(cam, CAM_BUF_NUM);
	recorder_media_close(mdev);
	cam->inited = false;
}
