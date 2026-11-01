/*
 * Copyright (C) 2020-2026 ArtInChip Technology Co. Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 *  Author: <che.jiang@artinchip.com>
 *  Desc: Camera abstraction layer (DVP / MIPI)
 */

#ifndef __RECORDER_CAM_H__
#define __RECORDER_CAM_H__

#include <stdbool.h>
#include <linux/videodev2.h>
#include <linux/v4l2-subdev.h>

//DVP 3 bufs + Render 1 buf + VENC 2 bufs
#define CAM_BUF_NUM   6
#define CAM_PLANE_NUM 2

enum aic_recorder_vin_type {
	AIC_RECORDER_VIN_INVALID = 0,
	AIC_RECORDER_VIN_DVP,
	AIC_RECORDER_VIN_USB,
	AIC_RECORDER_VIN_MIPI,
};

struct video_plane {
	int fd;
	int buf;
	char *vaddr;
	unsigned int len;
	unsigned int handle;
};

struct video_buf_info {
	struct video_plane planes[CAM_PLANE_NUM];
};

struct recorder_cam_data {
	int w;
	int h;
	int frame_size;
	int fmt;
	bool sfield_mode;
	struct v4l2_subdev_format src_fmt;
	struct video_buf_info binfo[CAM_BUF_NUM];
	bool inited;
};

struct recorder_media_dev {
	int sensor_fd;
	int sensor_width;
	int sensor_height;
	int sensor_fr;
	int dvp_fd;
	int video_fd;

	int rotation;

	int fb_fd;
	int fb_xres;
	int fb_yres;
};

int  recorder_cam_init(struct recorder_cam_data *cam,
		       struct recorder_media_dev *mdev,
		       enum aic_recorder_vin_type vin_type);
void recorder_cam_deinit(struct recorder_cam_data *cam,
			 struct recorder_media_dev *mdev);
int  recorder_cam_start(struct recorder_media_dev *mdev);
int  recorder_cam_stop(struct recorder_media_dev *mdev);
int  recorder_cam_queue_buf(struct recorder_media_dev *mdev, int index);
int  recorder_cam_dequeue_buf(struct recorder_media_dev *mdev, int *index);
int  recorder_media_open(struct recorder_media_dev *mdev);
void recorder_media_close(struct recorder_media_dev *mdev);

#endif
