// SPDX-License-Identifier: Apache-2.0
/*
 * Copyright (C) 2025-2026 ArtInChip Technology Co., Ltd.
 * Authors:  Matteo <duanmt@artinchip.com>
 */
#ifndef _TEST_VIN_H_
#define _TEST_VIN_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <xf86drm.h>
#include <xf86drmMode.h>

#define DEV_NAME_LEN		32
#define VIN_DEV_NUM_MAX		4
#define VIN_PROP_NUM_MAX	10

#define VIN_BUF_NUM			3
#define VIN_PLANE_NUM		2

#define VIN_IS_DVP		BIT(0)
#define VIN_IS_CSI		BIT(1)
#define VIN_IS_DSI		BIT(2)

struct vin_dev_info {
	int  id;
    int  type;
    char video_name[DEV_NAME_LEN];
	char vin_ctrl_name[DEV_NAME_LEN];
};

struct video_plane {
	int fd;
	int buf;
	u32 len;
	u32 handle;
	char *vaddr;
};

struct video_buf_info {
	u32 len;
	u32 offset;
	u32 id;
	struct video_plane planes[VIN_PLANE_NUM];
};

struct aic_sensor_info {
	u32 fmt; // i.e. code
	u32 width;
	u32 height;
	u32 fr; // frame rate
};

struct aic_video_data {
	u32 w;
	u32 h;
	u32 fmt;  // output format
	int result;
	bool streaming;
	struct v4l2_subdev_format src_fmt;
	struct video_buf_info binfo[VIN_BUF_NUM + 1];
	struct vin_dev_info *vin_dev;
};

struct aic_fb_info {
	u32 fmt;
	u32 fb_width;
	u32 fb_height;
	u32 fb_size;
	u32 win_width[VIN_DEV_NUM_MAX];
	u32 win_height[VIN_DEV_NUM_MAX];
	u32 win_x[VIN_DEV_NUM_MAX];
	u32 win_y[VIN_DEV_NUM_MAX];

	u32 crtc_id;
	u32 conn_id;
	u32 plane_id[VIN_DEV_NUM_MAX];
	drmModeRes *res;
	drmModeModeInfo *mode;
	drmModeConnector *conn;
	drmModePlaneRes *plane_res;
	drmModeObjectProperties *prop[VIN_DEV_NUM_MAX];
};

struct aic_media_dev {
	/* about Sensor */
	int sensor_fd;
	struct aic_sensor_info sensor_info;

	/* about VIN controller(DVP/CSI/LVDS In/MIPI In) */
	int vin_fd;
	int video_fd[VIN_DEV_NUM_MAX];

	/* about GE */
	struct mpp_ge *ge_dev;
	int rotation;

	/* about DE */
	int fb_fd;
	struct aic_fb_info fb_info;
	pthread_mutex_t fb_lock;

	/* about output file */
	bool out_to_file;
	int ofile_fd[VIN_DEV_NUM_MAX];
};

struct vin_render_info {
	bool ready;
	bool queue;
	u32 buf_index;
	u32 plane_id;
    u32 prop_id[VIN_PROP_NUM_MAX];
	u32 prop_val[VIN_PROP_NUM_MAX];

	pthread_mutex_t mutex;
	pthread_cond_t  cond;

	u32 send_cnt;
	u32 recv_cnt;
	u32 drop_cnt;
	u32 delay_cnt; /* the number of frame with delayed sent */
};

extern bool g_verbose;
extern volatile bool g_running;
extern u32 g_vin_dev_num;
extern struct vin_dev_info g_vin_dev_info[VIN_DEV_NUM_MAX];
extern char g_src_subdev_name[DEV_NAME_LEN];
extern char g_dst_subdev_name[DEV_NAME_LEN];

extern struct aic_media_dev g_mdev;
extern struct aic_video_data g_vdata[VIN_DEV_NUM_MAX];
extern struct vin_render_info g_render_info[VIN_DEV_NUM_MAX];

int vin_queue_buf(int index, int dev_id);

int media_dev_parse(char *dev);

void *drm_render_thread(void *arg);
void drm_render_init(void);
void drm_render_deinit(void);

#ifdef __cplusplus
}
#endif

#endif	// end of _TEST_VIN_H_
