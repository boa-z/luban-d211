// SPDX-License-Identifier: Apache-2.0
/*
 * Copyright (C) 2025-2026 ArtInChip Technology Co., Ltd.
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Authors:  Matteo <duanmt@artinchip.com>
 */

#include <errno.h>
#include <poll.h>
#include <pthread.h>
#include <string.h>
#include <sys/time.h>

#include <linux/dma-buf.h>
#include <linux/dma-heap.h>
#include <linux/videodev2.h>
#include <linux/v4l2-subdev.h>
#include <linux/fb.h>

#include <drm/drm_fourcc.h>

#include <video/artinchip_fb.h>
#include <artinchip/sample_base.h>

#include "mediactl.h"
#include "v4l2subdev.h"

#include "test_vin.h"

#ifdef SUPPORT_ROTATION
#include "mpp_ge.h"
#endif

/* Global macro and variables */

#define VIN_DEBUG_NO_SIGNAL

#define DRM_PLANE_NUM	4
#define FB_DEV			"/dev/dri/card0"
#define MEDIA_DEV		"/dev/media0"

#define FOURCC_2_STR(f)		f & 0xFF, (f >> 8) & 0xFF, (f >> 16) & 0xFF, (f >> 24)

struct aic_vin_fmt {
	int  vin_type;
	char sensor_ofmt_name[8];	// output format of Sensor
	u32  sensor_ofmt;
	char vin_ofmt_name[8];		// output format of VIN
	u32  vin_ofmt;
	u32  drm_fmt;				// output format of DRM (Display)
};

struct aic_media_dev g_mdev = {0};
struct aic_video_data g_vdata[VIN_DEV_NUM_MAX] = {0};
struct vin_dev_info g_vin_dev_info[VIN_DEV_NUM_MAX] = {0};

bool g_verbose = false;
volatile bool g_running = false;
static volatile u32 g_frame_cnt = 0x7FFFFFFF;

u32 g_vin_dev_num = 0;
char g_src_subdev_name[DEV_NAME_LEN] = "";
char g_dst_subdev_name[DEV_NAME_LEN] = "";

struct aic_vin_fmt g_vin_formats[] = {
	/*           Sensor                ->               VIN                ->          Display */
	/* for DVP */
	{VIN_IS_DVP, "UYVY8",  MEDIA_BUS_FMT_UYVY8_2X8,     "NV16",   V4L2_PIX_FMT_NV16,   DRM_FORMAT_NV16},
	{VIN_IS_DVP, "UYVY8",  MEDIA_BUS_FMT_UYVY8_2X8,     "NV12",   V4L2_PIX_FMT_NV12,   DRM_FORMAT_NV12},
	{VIN_IS_DVP, "RGB565", MEDIA_BUS_FMT_RGB565_2X8_LE, "RGB565", V4L2_PIX_FMT_RGB565, DRM_FORMAT_RGB565},
	{VIN_IS_DVP, "RGB888", MEDIA_BUS_FMT_RGB888_1X24,   "RGB888", V4L2_PIX_FMT_RGB24,  DRM_FORMAT_BGR888},
	/* for CSI */
	{VIN_IS_CSI, "UYVY8",  MEDIA_BUS_FMT_UYVY8_1X16,    "NV16",   V4L2_PIX_FMT_NV16,   DRM_FORMAT_NV16},
	{VIN_IS_CSI, "UYVY8",  MEDIA_BUS_FMT_UYVY8_1X16,    "NV61",   V4L2_PIX_FMT_NV61,   DRM_FORMAT_NV61},
	{VIN_IS_CSI | VIN_IS_DSI, "RGB565", MEDIA_BUS_FMT_RGB565_1X16,   "RGB565", V4L2_PIX_FMT_RGB565, DRM_FORMAT_RGB565},
	{VIN_IS_CSI, "RGB888", MEDIA_BUS_FMT_BGR888_1X24,   "RGB888", V4L2_PIX_FMT_BGR24,  DRM_FORMAT_BGR888},
	/* for DSI Rx */
	{VIN_IS_DSI, "RGB888",  MEDIA_BUS_FMT_RBG888_1X24,        "RGB888",  V4L2_PIX_FMT_RGB24,  DRM_FORMAT_RGB888},
	{VIN_IS_DSI, "RGB666",  MEDIA_BUS_FMT_RGB666_1X24_CPADHI, "RGB666",  V4L2_PIX_FMT_BGR666, DRM_FORMAT_RGB888},
	{VIN_IS_DSI, "RGB666L", MEDIA_BUS_FMT_BGR666_1X24_CPADHI, "RGB666L", V4L2_PIX_FMT_BGR666, DRM_FORMAT_RGB888},
	/* shared format */
	{VIN_IS_CSI | VIN_IS_DVP, "RAW8",   MEDIA_BUS_FMT_SGBRG8_1X8,    "RAW8",   V4L2_PIX_FMT_GREY,   DRM_FORMAT_NV12},
};

static const char sopts[] = "s:f:c:w:h:r:a:d:n:o:pluv";
static const struct option lopts[] = {
	{"sensor_format", required_argument, NULL, 's'},
	{"format",		  required_argument, NULL, 'f'},
	{"capture",		  required_argument, NULL, 'c'},
	{"width",		  required_argument, NULL, 'w'},
	{"height",		  required_argument, NULL, 'h'},
	{"framerate",	  required_argument, NULL, 'r'},
	{"angle",		  required_argument, NULL, 'a'},
	{"device",		  required_argument, NULL, 'd'},
	{"dev_num",		  required_argument, NULL, 'n'},
	{"outfile",		  required_argument, NULL, 'o'},
	{"print",		  no_argument,		 NULL, 'p'},
	{"list",		  no_argument,		 NULL, 'l'},
	{"usage",		  no_argument,		 NULL, 'u'},
	{"verbose",		  required_argument, NULL, 'v'},
	{0, 0, 0, 0}
};

/* Functions */

void usage(char *program)
{
	printf("Usage: %s [options]: \n", program);
	printf("\t -s, --sensor_format\tformat of sensor output, UYUV8/RGB888 etc\n");
	printf("\t -f, --format\t\tformat of VIN output, NV16/NV12 etc\n");
	printf("\t -c, --count\t\tthe number of capture frame \n");
	printf("\t -w, --width\t\tthe width of sensor \n");
	printf("\t -h, --height\t\tthe height of sensor \n");
	printf("\t -r, --framerate\tthe framerate of sensor \n");
#ifdef SUPPORT_ROTATION
	printf("\t -a, --angle\t\tthe angle of rotation. Be ignored when output to a file\n");
#endif
	printf("\t -d, --device\t\tthe media device, default is /dev/media0 \n");
	printf("\t -n, --dev_num\t\tthe number of VIN device, default is 1\n");
	printf("\t -o, --outfile\t\tthe file name to save video data\n");
	printf("\t\t\t\tBy default, the video data will be displayed in panel.\n");
	printf("\t -p, --print \t\tonly print the device information \n");
	printf("\t -l, --list \t\tlist all the supported stream format \n");
	printf("\t -u, --usage \n");
	printf("\t -v, --verbose \n");
	printf("\n");
	printf("Example: %s -f nv16 -c 1000\n", program);
}

char *vin_dev_type2name(int type)
{
    static char name[32];

    memset(name, 0, sizeof(name));
    if (type & VIN_IS_DVP)
        strncat(name, "DVP", sizeof(name) - strlen(name) - 1);

    if (type & VIN_IS_CSI) {
        if (name[0] != '\0')
            strncat(name, " CSI", sizeof(name) - strlen(name) - 1);
        else
            strncat(name, "CSI", sizeof(name) - strlen(name) - 1);
    }

    if (type & VIN_IS_DSI) {
        if (name[0] != '\0')
            strncat(name, " DSI", sizeof(name) - strlen(name) - 1);
        else
            strncat(name, "DSI", sizeof(name) - strlen(name) - 1);
    }

    return name;
}

void show_all_formats(void)
{
	printf("No. Sensor format VIN device   VIN format Display format\n");
	printf("--- ------------- ------------ ---------- --------------\n");
	for (int i = 0; i < ARRAY_SIZE(g_vin_formats); i++) {
		struct aic_vin_fmt *fmt = &g_vin_formats[i];
		printf("%3d %-13s %-12s %-10s %c%c%c%c\n", i,
			fmt->sensor_ofmt_name,
			vin_dev_type2name(fmt->vin_type), fmt->vin_ofmt_name,
			FOURCC_2_STR(fmt->drm_fmt));
	}
}

/* Open a device file to be needed. */
int device_open(char *_fname, int _flag)
{
	s32 fd = -1;

	fd = open(_fname, _flag);
	if (fd < 0) {
		ERR("Failed to open %s errno: %d[%s]\n",
			_fname, errno, strerror(errno));
		return -1;
	}
	return fd;
}

void set_fb_multi_win_size(void)
{
	struct aic_sensor_info *s_info = &g_mdev.sensor_info;
	struct aic_fb_info *fb_info = &g_mdev.fb_info;
	u32 stitch_w, stitch_h, board_x, board_y, dst_w, dst_h;
	float scale_w, scale_h, scale;
	u32 x_num = 2, y_num = 1; /* 2x1 array windows  */

	if (g_vin_dev_num > 2)
		y_num = 2; /* 2x2 array windows */

	stitch_w = s_info->width * x_num;
	stitch_h = s_info->height * y_num;

	if ((stitch_w < fb_info->fb_width) && (stitch_h < fb_info->fb_height)) {
		/* Case 1. The stitch window is smaller than FB, centered display it */
		dst_w = s_info->width;
		dst_h = s_info->height;
	} else {
		/* Case 2. Scale proportionally the sensor image to display window */
		scale_w = (float)stitch_w / (float)fb_info->fb_width;
		scale_h = (float)stitch_h / (float)fb_info->fb_height;
		scale = (scale_w > scale_h) ? scale_w : scale_h;

		dst_w = ALIGN_DOWN((u32)((float)s_info->width / scale), 4);
		dst_h = ALIGN_DOWN((u32)((float)s_info->height / scale), 4);
	}

	board_x = (fb_info->fb_width - dst_w * x_num) / x_num / 2;
	board_y = (fb_info->fb_height - dst_h * y_num) / y_num / 2;
	for (int i = 0; i < g_vin_dev_num; i++) {
		u32 x_pos = i % x_num;
		u32 y_pos = i / x_num;
		fb_info->win_width[i]  = dst_w;
		fb_info->win_height[i] = dst_h;
		fb_info->win_x[i] = (fb_info->win_width[i] + board_x * 2) * x_pos + board_x;
		fb_info->win_y[i] = (fb_info->win_height[i] + board_y * 2) * y_pos + board_y;
	}
}

void set_fb_win_pos(void)
{
	struct aic_sensor_info *s_info = &g_mdev.sensor_info;
	struct aic_fb_info *fb_info = &g_mdev.fb_info;
	float scale_w, scale_h, scale;

	if (!s_info->width || !s_info->height ||
		!fb_info->fb_width || !fb_info->fb_height) {
		ERR("Invalid size\n");
		return;
	}

	if (g_vin_dev_num > 1)
		return set_fb_multi_win_size();

	if ((s_info->width < fb_info->fb_width) &&
		(s_info->height < fb_info->fb_height)) {
		/* Case 1. The display window is smaller than FB, centered display it */
		fb_info->win_width[0]  = s_info->width;
		fb_info->win_height[0] = s_info->height;
		fb_info->win_x[0] = (fb_info->fb_width - s_info->width) / 2;
		fb_info->win_y[0] = (fb_info->fb_height - s_info->height) / 2;
	} else {
		/* Case 2. Scale proportionally the sensor image to display window */
		scale_w = (float)s_info->width / (float)fb_info->fb_width;
		scale_h = (float)s_info->height / (float)fb_info->fb_height;
		scale = (scale_w > scale_h) ? scale_w : scale_h;

		fb_info->win_width[0]  = ALIGN_DOWN((u32)((float)s_info->width / scale), 4);
		fb_info->win_height[0] = ALIGN_DOWN((u32)((float)s_info->height / scale), 4);
		fb_info->win_x[0] = (fb_info->fb_width - fb_info->win_width[0]) / 2;
		fb_info->win_y[0] = (fb_info->fb_height - fb_info->win_height[0]) / 2;
	}
}

int fb_get_info(void)
{
	struct aic_fb_info *fb_info = &g_mdev.fb_info;
	int ret = 0;

	fb_info->res = drmModeGetResources(g_mdev.fb_fd);
	if (!fb_info->res) {
		ERR("Failed to get DRM resource\n");
		return -1;
	}

	ret = drmSetClientCap(g_mdev.fb_fd, DRM_CLIENT_CAP_ATOMIC, 1);
	if (ret) {
		ERR("no atomic modesetting support: %s\n", strerror(errno));
		ret = -1;
		goto free_res;
	}

	fb_info->crtc_id = fb_info->res->crtcs[0];
	fb_info->conn_id = fb_info->res->connectors[0];
	drmSetClientCap(g_mdev.fb_fd, DRM_CLIENT_CAP_UNIVERSAL_PLANES, 1);

	fb_info->plane_res = drmModeGetPlaneResources(g_mdev.fb_fd);
	if (!fb_info->plane_res) {
		ERR("Failed to get DRM plane resources\n");
		ret = -1;
		goto free_res;
	}

	if (fb_info->plane_res->count_planes < g_vin_dev_num) {
		ERR("Not enough planes. Expect %d, actual %d\n",
			g_vin_dev_num, fb_info->plane_res->count_planes);
		ret = -1;
		goto free_res;
	}

	fb_info->conn = drmModeGetConnector(g_mdev.fb_fd, fb_info->conn_id);
	fb_info->mode = &fb_info->conn->modes[0];
	fb_info->fb_width  = fb_info->conn->modes[0].hdisplay;
	fb_info->fb_height = fb_info->conn->modes[0].vdisplay;
	set_fb_win_pos();
	return 0;

free_res:
	drmModeFreeResources(fb_info->res);
	return ret;
}

void fb_free_info(void)
{
	struct aic_fb_info *fb_info = &g_mdev.fb_info;

	if (fb_info->plane_res)
		drmModeFreePlaneResources(fb_info->plane_res);

	if (fb_info->conn)
		drmModeFreeConnector(fb_info->conn);

	if (fb_info->res)
		drmModeFreeResources(fb_info->res);
}

u32 fb_get_prop_id(int fd, drmModeObjectProperties *prop, const char *name)
{
	drmModePropertyPtr property = NULL;
	u32 i, id = 0;

	for (i = 0; i < prop->count_props; i++) {
		property = drmModeGetProperty(fd, prop->props[i]);
		if (!strncmp(property->name, name, strlen(name))) {
			id = property->prop_id;
			drmModeFreeProperty(property);
			return id;
		}
		drmModeFreeProperty(property);
	}
	ERR("Failed to get property %s\n", name);
	return 0;
}

void fb_show_plane_info(void)
{
	struct aic_fb_info *fb_info = &g_mdev.fb_info;
	drmModeObjectProperties *prop = NULL;
	int i;

	printf("Available planes: %d\n", fb_info->plane_res->count_planes);
	printf("-------------------------------------------------------------\n");
	for (i = 0; i < fb_info->plane_res->count_planes; i++) {
		drmModePlane *drm_plane = drmModeGetPlane(g_mdev.fb_fd, fb_info->plane_res->planes[i]);
		if (drm_plane) {
			printf("  Plane %d: ID=%u, CRTC_ID=%u, FB_ID=%u\n",
				   i, drm_plane->plane_id, drm_plane->crtc_id, drm_plane->fb_id);
			drmModeFreePlane(drm_plane);
		}

		prop = drmModeObjectGetProperties(g_mdev.fb_fd, fb_info->plane_res->planes[i],
										  DRM_MODE_OBJECT_PLANE);
		if (!prop) {
			ERR("No properties. Error: %d[%s]\n", errno, strerror(errno));
			continue;
		}
		printf("\tAvailable properties %u:\n", prop->count_props);
		for (int j = 0; j < prop->count_props; j++) {
			drmModePropertyPtr property = drmModeGetProperty(g_mdev.fb_fd, prop->props[j]);
			if (property) {
				printf("\t  Property %d: %s (ID: %u)\n", j, property->name, property->prop_id);
				drmModeFreeProperty(property);
			}
		}
		drmModeFreeObjectProperties(prop);
	}
}

bool drm_need_uv_data(u32 drm_fmt)
{
	if (drm_fmt == DRM_FORMAT_BGR565 || drm_fmt == DRM_FORMAT_BGR888)
		return false;
	else
		return true;
}

u32 vin_fmt_to_pitch(u32 sensor_fmt, u32 vin_fmt, u32 width)
{
	if (vin_fmt == V4L2_PIX_FMT_RGB565)
		return width * 2;
	if (sensor_fmt == MEDIA_BUS_FMT_RGB888_3X8 || vin_fmt == V4L2_PIX_FMT_BGR24)
		return width * 3;
	if (sensor_fmt == MEDIA_BUS_FMT_RGB888_1X24 || vin_fmt == V4L2_PIX_FMT_BGR24)
		return width;

	return width;
}

int vidbuf_dmabuf_begin(struct aic_video_data *vdata, u32 num, int dev_id)
{
	struct aic_fb_info *fb_info = &g_mdev.fb_info;
	struct video_buf_info *binfo = NULL;
	u32 handles[DRM_PLANE_NUM] = {0};
	u32 pitches[DRM_PLANE_NUM] = {0};
	u32 offsets[DRM_PLANE_NUM] = {0};
	int i, ret = 0;

	if (g_mdev.out_to_file)
		return 0;

	if (g_verbose)
		fb_show_plane_info();

	fb_info->plane_id[dev_id] = fb_info->plane_res->planes[dev_id];
	pthread_mutex_lock(&g_mdev.fb_lock);
	fb_info->prop[dev_id] = drmModeObjectGetProperties(g_mdev.fb_fd, fb_info->plane_id[dev_id],
											DRM_MODE_OBJECT_PLANE);
	pthread_mutex_unlock(&g_mdev.fb_lock);
	if (!fb_info->prop[dev_id]) {
		ERR("Dev%d: No properties for plane %d. Error: %d[%s]\n",
			vdata->vin_dev->id, i, errno, strerror(errno));
		return -1;
	}

	binfo = vdata->binfo;
	for (i = 0; i < num; i++, binfo++) {
		struct video_plane *plane = binfo->planes;

		handles[0] = plane[0].handle;
		pitches[0] = vin_fmt_to_pitch(vdata->src_fmt.format.code, vdata->fmt, vdata->w);

		if (handles[0] == 0) {
			ERR("Buf handle is 0\n");
			return -1;
		}

		if (drm_need_uv_data(fb_info->fmt)) {
			handles[1] = plane[1].handle;
			pitches[1] = vdata->w;
		} else {
			printf("[DEBUG]        No UV plane needed\n");
		}

		pthread_mutex_lock(&g_mdev.fb_lock);
		ret = drmModeAddFB2(g_mdev.fb_fd, vdata->w, vdata->h, fb_info->fmt,
							handles, pitches, offsets, &binfo->id, 0);
		pthread_mutex_unlock(&g_mdev.fb_lock);
		if (ret) {
			ERR("Dev%d Buf%d: Failed to add FB! err %d[%s]\n",
				vdata->vin_dev->id, i, errno, strerror(errno));
			break;
		}
	}
	return ret;
}

void vidbuf_dmabuf_end(struct aic_video_data *vdata, u32 num, int dev_id)
{
	struct aic_fb_info *fb_info = &g_mdev.fb_info;
	struct video_buf_info *binfo = vdata->binfo;
	int i, ret = 0;

	if (g_mdev.out_to_file)
		return;

	pthread_mutex_lock(&g_mdev.fb_lock);
	for (i = 0; i < num; i++, binfo++) {
		ret = drmModeRmFB(g_mdev.fb_fd, binfo->id);
		if (ret < 0) {
			ERR("Failed to rm FB %d[%d]! err %d[%s]\n",
				i, binfo->id, errno, strerror(errno));
		}
	}
	pthread_mutex_unlock(&g_mdev.fb_lock);

	if (fb_info->prop[dev_id])
		drmModeFreeObjectProperties(fb_info->prop[dev_id]);
}

bool sensor_ofmt_is_available(u32 *ofmt, char *name)
{
	struct aic_vin_fmt *fmt = g_vin_formats;
	int i;

	for (i = 0; i < ARRAY_SIZE(g_vin_formats); i++, fmt++) {
		if (!(fmt->vin_type & g_vin_dev_info[0].type))
			continue;

		if (!strncasecmp(name, fmt->sensor_ofmt_name, strlen(name))) {
			*ofmt = fmt->sensor_ofmt;
			return true;
		}
	}
	return false;
}

bool vin_ofmt_is_available(u32 *ofmt, char *name)
{
	struct aic_vin_fmt *fmt = g_vin_formats;
	int i;

	for (i = 0; i < ARRAY_SIZE(g_vin_formats); i++, fmt++) {
		if (!(fmt->vin_type & g_vin_dev_info[0].type))
			continue;

		if (!strncasecmp(name, fmt->vin_ofmt_name, strlen(fmt->vin_ofmt_name))) {
			*ofmt = fmt->vin_ofmt;
			return true;
		 }
	}
	return false;
}

void vin_ofmt_expand(void)
{
	for (int i = 1; i < g_vin_dev_num; i++) {
		g_vdata[i].fmt = g_vdata[0].fmt;
	}
}

bool vin_fmt_is_match(struct v4l2_subdev_format *s_f)
{
	struct aic_sensor_info *s_info = &g_mdev.sensor_info;
	struct aic_fb_info *fb_info = &g_mdev.fb_info;
	struct aic_vin_fmt *fmt = g_vin_formats;
	u32 found_vin_ofmt = 0;
	bool matched = false;
	int i;

	if (s_info->fmt == 0)
		return true;

	for (i = 0; i < ARRAY_SIZE(g_vin_formats); i++, fmt++) {
		/* 1. Check the VIN device type */
		if (!(fmt->vin_type & g_vin_dev_info[0].type))
			continue;

		/* 2. Check the format of Sensor */
		if (fmt->sensor_ofmt != s_info->fmt)
			continue;

		/* 3. Check the format of VIN */
		found_vin_ofmt = fmt->vin_ofmt;
		if (g_vdata[0].fmt && g_vdata[0].fmt != found_vin_ofmt)
			continue;

		fb_info->fmt = fmt->drm_fmt;
		matched = true;
		break;
	}

	if (matched) {
		g_vdata[0].fmt = found_vin_ofmt;
		printf("The stream format:\n");
	} else {
		ERR("Failed to match the format\n");
		printf(" [Sensor] (%d - %#x)\n\t└─> [VIN] (%#x)\n",
			s_f->format.colorspace, s_info->fmt, g_vdata[0].fmt);
			return matched;
	}
	vin_ofmt_expand();

	printf(" [Sensor] %s (%d - %#x)\n\t└─> [VIN] %s (%#x)\n",
		   g_vin_formats[i].sensor_ofmt_name, s_f->format.colorspace, s_info->fmt,
		   g_vin_formats[i].vin_ofmt_name, g_vdata[0].fmt);

	if (g_mdev.out_to_file)
		printf("\n");
	else
		printf("\t\t└─> [Panel] %c%c%c%c (%#x)\n\n", FOURCC_2_STR(fb_info->fmt), fb_info->fmt);
	return matched;
}

int sensor_set_fmt(void)
{
	struct aic_sensor_info *s_info = &g_mdev.sensor_info;
	struct v4l2_subdev_frame_interval fr = {0};
	struct v4l2_subdev_format f = {0};
	int ret = 0;

	/* Get the current format, first */

	f.pad = 0;
	f.which = V4L2_SUBDEV_FORMAT_ACTIVE;
	if (ioctl(g_mdev.sensor_fd, VIDIOC_SUBDEV_G_FMT, &f) < 0) {
		ERR("Failed to get sensor format! err %d[%s]\n", errno, strerror(errno));
		return -1;
	}

	if (!s_info->fmt)
		s_info->fmt = f.format.code;

	/* Set the format and resolution */

	if (f.format.width != s_info->width || f.format.height != s_info->height ||
		f.format.code != s_info->fmt) {
		if (g_verbose)
			printf("Set sensor format: %dx%d [%#x] -> %dx%d [%#x]\n",
				f.format.width, f.format.height, f.format.code,
				s_info->width, s_info->height, s_info->fmt);
		f.format.width = s_info->width;
		f.format.height = s_info->height;
		f.format.code = s_info->fmt;
		if (ioctl(g_mdev.sensor_fd, VIDIOC_SUBDEV_S_FMT, &f) < 0) {
			ERR("Failed to set sensor format! %d[%s]\n", errno, strerror(errno));
			return -1;
		}
	}

	/* Confirm the current resolution */
	if (ioctl(g_mdev.sensor_fd, VIDIOC_SUBDEV_G_FMT, &f) < 0) {
		ERR("Failed to confirm sensor format! err %d[%s]\n", errno, strerror(errno));
		return -1;
	}

	if (!vin_fmt_is_match(&f))
		return -1;

	/* Set framerate */

	ret = ioctl(g_mdev.sensor_fd, VIDIOC_SUBDEV_G_FRAME_INTERVAL, &fr);
	if ((ret == 0) && (fr.interval.denominator != s_info->fr)) {
		printf("Set sensor framerate: %d -> %d\n", fr.interval.denominator, s_info->fr);
		fr.interval.denominator = s_info->fr;
		ret = ioctl(g_mdev.sensor_fd, VIDIOC_SUBDEV_S_FRAME_INTERVAL, &fr);
		if (ret < 0) {
			ERR("Failed to set FPS of sensor! err %d[%s]\n", errno, strerror(errno));
			return -1;
		}

		/* Confirm the current framerate */
		ioctl(g_mdev.sensor_fd, VIDIOC_SUBDEV_G_FRAME_INTERVAL, &fr);
	}

	for (int i = 0; i < g_vin_dev_num; i++) {
		g_vdata[i].src_fmt = f;
		g_vdata[i].w = f.format.width;
		g_vdata[i].h = f.format.height;
		g_vdata[i].vin_dev = &g_vin_dev_info[i];
	}

	if (g_verbose)
		printf("Sensor format: w %d h %d, code %#x, colorspace %#x, fr %d\n",
			   g_vdata[0].w, g_vdata[0].h, f.format.code,
			   f.format.colorspace, fr.interval.denominator);
	return 0;
}

int vin_subdev_set_fmt(void)
{
	struct v4l2_subdev_format f = g_vdata[0].src_fmt;

	f.pad = 0;
	f.which = V4L2_SUBDEV_FORMAT_ACTIVE;
	if (ioctl(g_mdev.vin_fd, VIDIOC_SUBDEV_S_FMT, &f) < 0) {
		ERR("Failed to set VIN in-format! err %d[%s]\n", errno, strerror(errno));
		return -1;
	}

	return 0;
}

int vin_cfg(int width, int height, int format)
{
	struct v4l2_format f = {0};

	f.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
	f.fmt.pix_mp.num_planes = VIN_PLANE_NUM;

	for (int i = 0; i < g_vin_dev_num; i++) {
		f.fmt.pix_mp.width = g_vdata[i].src_fmt.format.width;
		f.fmt.pix_mp.height = g_vdata[i].src_fmt.format.height;
		f.fmt.pix_mp.pixelformat = g_vdata[i].fmt;

		if (ioctl(g_mdev.video_fd[i], VIDIOC_S_FMT, &f) < 0) {
			ERR("Failed to set VIN %d out-format! err %d[%s]\n", i, errno, strerror(errno));
			return -1;
		}
	}

	return 0;
}

int vin_expbuf(int index, struct video_buf_info *binfo, int dev_id)
{
	struct v4l2_exportbuffer expbuf = {0};
	int i;

	for (i = 0; i < VIN_PLANE_NUM; i++) {
		memset(&expbuf, 0, sizeof(struct v4l2_exportbuffer));
		expbuf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
		expbuf.index = index;
		expbuf.plane = i;
		if (ioctl(g_mdev.video_fd[dev_id], VIDIOC_EXPBUF, &expbuf) < 0) {
			ERR("%d/%d: Failed to export buf! err %d[%s]\n",
			    i, index, errno, strerror(errno));
			return -1;
		}
		binfo->planes[i].fd = expbuf.fd;
		if (g_verbose)
			DBG("%d-%d-%d Export buf fd %d\n", dev_id, index, i, expbuf.fd);
	}

	return 0;
}

static void vin_fill_buf(struct video_plane *planes, u32 plane_num)
{
	struct aic_sensor_info *s_info = &g_mdev.sensor_info;
	struct dma_buf_sync flag = {DMA_BUF_SYNC_WRITE | DMA_BUF_SYNC_END};
	u32 height = s_info->height;
	u32 width = s_info->width;
	char *y = planes[0].vaddr;
	u32 block_height = 100;
	u32 r, c;

	if (!width || !height || !plane_num || !y)
		return;

	for (r = 0; r < height; r++)
		for (c = 0; c < width; c++)
			y[r * width + c] = ((r / block_height + c / 100) & 1) ? 0x1C : 0xA1;

	if (ioctl(planes[0].fd, DMA_BUF_IOCTL_SYNC, &flag) < 0)
		ERR("plane 0: DMA sync failed! err %d[%s]\n", errno, strerror(errno));

	if (planes[1].vaddr && planes[1].len) {
		memset(planes[1].vaddr, 0x80, planes[1].len);
		if (ioctl(planes[1].fd, DMA_BUF_IOCTL_SYNC, &flag) < 0)
			ERR("plane 1: DMA sync failed! err %d[%s]\n", errno, strerror(errno));
	}
}

int vin_mmap(int index, u32 plane_num, struct v4l2_plane *vplane,
			 struct video_buf_info *binfo, int dev_id)
{
	struct video_plane *plane = binfo->planes;
	int i, sum = 0;

	for (i = 0; i < plane_num; i++, plane++) {
		if (!plane->fd)
			continue;

		if (i > 0 && !drm_need_uv_data(g_mdev.fb_info.fmt))
			continue;

		plane->vaddr = mmap(NULL, vplane[i].length, PROT_READ | PROT_WRITE,
							MAP_SHARED, g_mdev.video_fd[dev_id],
							vplane[i].m.mem_offset);
		if (plane->vaddr == MAP_FAILED) {
			ERR("%s buf %d-%d: Failed to mmap %d for file %d! %d[%s] \n",
				g_vin_dev_info[dev_id].vin_ctrl_name,
				index, i, vplane[i].length, plane->fd, errno, strerror(errno));
			return -1;
		}

		plane->len = vplane[i].length;
		sum += plane->len;
	}

	vin_fill_buf(binfo->planes, plane_num);
	return sum;
}

int vin_map_handle(int index, u32 plane_num, struct v4l2_plane *vplane,
				   struct video_buf_info *binfo, int dev_id)
{
	struct video_plane *plane = binfo->planes;
	int i, sum = 0, ret = 0;

	for (i = 0; i < VIN_PLANE_NUM; i++) {
		plane[i].handle = 0;
	}

	printf("Dev%d: %s map handle %d: ",
		   dev_id, g_vin_dev_info[dev_id].vin_ctrl_name, index);
	for (i = 0; i < plane_num; i++, plane++) {
		if (!plane->fd)
			continue;

		if (i > 0 && !drm_need_uv_data(g_mdev.fb_info.fmt))
			continue;

		ret = drmPrimeFDToHandle(g_mdev.fb_fd, plane->fd, &plane->handle);
		if (ret) {
			ERR("Failed to get DRM PrimeFD handle\n");
			return ret;
		}
		sum += vplane[i].length;
		printf("%8d[%d] ", vplane[i].length, plane->handle);
	}
	printf("\n");

	return sum;
}

int vin_request_buf(int num, struct video_buf_info *binfo, int dev_id)
{
	struct v4l2_buffer buf = {0};
	struct v4l2_requestbuffers req = {0};
	struct v4l2_plane planes[VIN_PLANE_NUM];
	int i, sum = 0, ret = 0;

	req.count  = num;
	req.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
	req.memory = V4L2_MEMORY_MMAP; // Only MMAP will do alloc memory
	if (ioctl(g_mdev.video_fd[dev_id], VIDIOC_REQBUFS, &req) < 0) {
		ERR("Failed to request buf! err %d[%s]\n", errno, strerror(errno));
		return -1;
	}

	for (i = 0; i < num; i++) {
		if (vin_expbuf(i, &binfo[i], dev_id) < 0)
			return -1;
		memset(&buf, 0, sizeof(struct v4l2_buffer));

		memset(planes, 0, sizeof(struct v4l2_plane) * VIN_PLANE_NUM);
		buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
		buf.index = i;
		buf.length = VIN_PLANE_NUM;
		buf.memory = V4L2_MEMORY_DMABUF;
		buf.m.planes = planes;
		if (ioctl(g_mdev.video_fd[dev_id], VIDIOC_QUERYBUF, &buf) < 0) {
			ERR("Failed to query buf! err %d[%s]\n", errno, strerror(errno));
			return -1;
		}

		ret = vin_mmap(i, VIN_PLANE_NUM, planes, &binfo[i], dev_id);
		if (ret < 0)
			return -1;

		ret = vin_map_handle(i, VIN_PLANE_NUM, planes, &binfo[i], dev_id);
		if (ret < 0)
			return -1;
		sum += ret;
	}
	if (sum)
		printf("%36sTotal: %.2f MB\n", "", (float)sum/1024/1024);

	return 0;
}

void vin_release_buf(int num)
{
	int i, j, dev;
	struct video_plane *plane = NULL;

	for (dev = 0; dev < g_vin_dev_num; dev++) {
		for (i = 0; i < num; i++) {
			for (j = 0; j < VIN_PLANE_NUM; j++) {
				plane = &g_vdata[dev].binfo[i].planes[j];
				if (plane->vaddr) {
					munmap(plane->vaddr, plane->len);
					plane->vaddr = NULL;
				}
				drmCloseBufferHandle(g_mdev.fb_fd, plane->handle);
			}
		}
	}
}

int vin_queue_buf(int index, int dev_id)
{
	struct v4l2_plane planes[VIN_PLANE_NUM] = {0};
	struct v4l2_buffer buf = {0};

	if (g_verbose)
		printf("Q Dev%d Buf%d\n", dev_id, index);

	buf.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
	buf.memory = V4L2_MEMORY_MMAP;
	buf.index  = index;
	buf.length = VIN_PLANE_NUM;
	buf.m.planes = planes;
	if (ioctl(g_mdev.video_fd[dev_id], VIDIOC_QBUF, &buf) < 0) {
		ERR("Q Dev%d Buf%d failed! Maybe buf state is invalid. err %d[%s]\n",
			dev_id, index, errno, strerror(errno));
		return -1;
	}

	return 0;
}

int vin_dequeue_buf(int *index, int dev_id)
{
	struct v4l2_buffer buf = {0};
	struct v4l2_plane planes[VIN_PLANE_NUM] = {0};

	buf.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
	buf.memory = V4L2_MEMORY_MMAP;
	buf.length = VIN_PLANE_NUM;
	buf.m.planes = planes;
	if (ioctl(g_mdev.video_fd[dev_id], VIDIOC_DQBUF, &buf) < 0) {
		ERR("DQ Dev%d failed! Maybe cannot receive data from Camera. err %d[%s]\n",
			dev_id, errno, strerror(errno));
		return -1;
	}

	*index = buf.index;
	return 0;
}

int vin_start(int dev_id)
{
	enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;

	if (ioctl(g_mdev.video_fd[dev_id], VIDIOC_STREAMON, &type) < 0) {
		ERR("Failed to start streaming %d! err %d[%s]\n", dev_id, errno, strerror(errno));
		return -1;
	}

	g_vdata[dev_id].streaming = true;
	return 0;
}

int vin_stop(int dev_id)
{
	enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;

	if (ioctl(g_mdev.video_fd[dev_id], VIDIOC_STREAMOFF, &type) < 0) {
		ERR("Failed to stop streaming %d! err %d[%s]\n", dev_id, errno, strerror(errno));
		return -1;
	}

	g_vdata[dev_id].streaming = false;
	return 0;
}

#ifdef SUPPORT_ROTATION
int do_rotate(struct aic_video_data *vdata, int index)
{
	struct ge_bitblt blt = {0};
	struct mpp_buf  *src = &blt.src_buf;
	struct mpp_buf  *dst = &blt.dst_buf;
	int ret = 0;

	if (g_vdata.fmt == V4L2_PIX_FMT_NV16) {
		src->format = MPP_FMT_NV16;
		dst->format = MPP_FMT_NV16;
	} else {
		src->format = MPP_FMT_NV12;
		dst->format = MPP_FMT_NV12;
	}

	src->buf_type = MPP_DMA_BUF_FD;
	src->fd[0] = vdata->binfo[index].planes[0].fd;
	src->fd[1] = vdata->binfo[index].planes[1].fd;
	src->stride[0] = vdata->w;
	src->stride[1] = vdata->w;
	src->size.width = vdata->w;
	src->size.height = vdata->h;

	dst->buf_type = MPP_DMA_BUF_FD;
	dst->fd[0] = vdata->binfo[VIN_BUF_NUM].planes[0].fd;
	dst->fd[1] = vdata->binfo[VIN_BUF_NUM].planes[1].fd;
	if (g_mdev.rotation == MPP_ROTATION_180) {
		dst->stride[0] = vdata->w;
		dst->stride[1] = vdata->w;
		dst->size.width = vdata->w;
		dst->size.height = vdata->h;
	} else {
		dst->stride[0] = vdata->h;
		dst->stride[1] = vdata->h;
		dst->size.width = vdata->h;
		dst->size.height = vdata->w;
	}
	blt.ctrl.flags = g_mdev.rotation;
#if 0
	printf("GE: %d(%d) * %d -> %d * %d, canvas %d(%d) * %d\n",
           src->size.width, src->stride[0],
           src->size.height,
           dst->crop.width, dst->crop.height,
           dst->size.width, dst->stride[0],
           dst->size.height);
#endif
	ret =  mpp_ge_bitblt(g_mdev.ge_dev, &blt);
	if (ret < 0) {
		ERR("GE bitblt failed\n");
		return -1;
	}

	ret = mpp_ge_emit(g_mdev.ge_dev);
	if (ret < 0) {
		ERR("GE emit failed\n");
		return -1;
	}

	ret = mpp_ge_sync(g_mdev.ge_dev);
	if (ret < 0) {
		ERR("GE sync failed\n");
		return -1;
	}
	return 0;
}
#endif

#define VIN_SCALE_OFFSET	10

int video_data_display(struct aic_video_data *vdata, int index, int dev_id, bool queue)
{
	struct vin_render_info *render_info = &g_render_info[dev_id];
	struct video_buf_info *binfo = &vdata->binfo[index];
	struct aic_fb_info *fb_info = &g_mdev.fb_info;
	static u32 plane_ids[VIN_DEV_NUM_MAX][VIN_PROP_NUM_MAX] = {0};
	int i, ret = 0;
	struct {
		char *name;
		u32 value;
	} plane_prop[VIN_PROP_NUM_MAX] = {
		{"FB_ID",   0},
		{"CRTC_ID", fb_info->crtc_id},
		{"CRTC_X",  fb_info->win_x[dev_id]},
		{"CRTC_Y",  fb_info->win_y[dev_id]},
		{"CRTC_W",  fb_info->win_width[dev_id]},
		{"CRTC_H",  fb_info->win_height[dev_id]},
		{"SRC_X",   0},
		{"SRC_Y",   0},
		{"SRC_W",   vdata->w << 16},
		{"SRC_H",   vdata->h << 16},
	};

	plane_prop[0].value = binfo->id;
	for (i = 0; i < ARRAY_SIZE(plane_prop); i++) {
		if (g_verbose && i == 0)
			printf("\tSet %8s: %d\n", plane_prop[i].name, plane_prop[i].value);

		if (plane_ids[dev_id][i] == 0) {
			pthread_mutex_lock(&g_mdev.fb_lock);
			plane_ids[dev_id][i] = fb_get_prop_id(g_mdev.fb_fd, fb_info->prop[dev_id], plane_prop[i].name);
			pthread_mutex_unlock(&g_mdev.fb_lock);
			if (!plane_ids[dev_id][i])
				return -1;
		}
		render_info->prop_id[i] = plane_ids[dev_id][i];
	}

	pthread_mutex_lock(&render_info->mutex);
	if (render_info->ready) {
		struct timespec ts = {0};
		clock_gettime(CLOCK_REALTIME, &ts);
		ts.tv_nsec += 10000000; // 10 ms
		if (ts.tv_nsec >= 1000000000) {
			ts.tv_nsec -= 1000000000;
			ts.tv_sec++;
		}

#if 1
		ret = pthread_cond_timedwait(&render_info->cond, &render_info->mutex, &ts);
		if (ret != 0 || render_info->ready) {
#else
		pthread_mutex_unlock(&render_info->mutex);
		usleep(5000);
		pthread_mutex_lock(&render_info->mutex);
		if (render_info->ready) {
#endif
			ERR("Dev%d: Maybe wait render timeout. return %d\n", dev_id, ret);
			/* Force to drop the previous frame */
			if (queue)
				vin_queue_buf(render_info->buf_index, dev_id);
			render_info->drop_cnt++;
		} else {
			render_info->delay_cnt++;
		}
	}

	for (i = 0; i < VIN_PROP_NUM_MAX; i++) {
		render_info->prop_val[i] = plane_prop[i].value;
	}
	render_info->ready = true;
	render_info->buf_index = index;
	render_info->plane_id = fb_info->plane_id[dev_id];
	render_info->send_cnt++;
	render_info->queue = queue;
	pthread_mutex_unlock(&render_info->mutex);

	return 0;
}

int video_data_save(struct aic_video_data *vdata, int index, int dev_id)
{
	struct dma_buf_sync flag = {DMA_BUF_SYNC_READ | DMA_BUF_SYNC_START};
	struct video_plane *plane = vdata->binfo[index].planes;
	int ret = 0, i;

	for (i = 0; i < 2; i++, plane++) {
		if (plane->len < 64)
			continue;

		if (ioctl(plane->fd, DMA_BUF_IOCTL_SYNC, &flag) < 0)
			ERR("%d-%d: DMA sync failed! err %d[%s]\n", index, i, errno, strerror(errno));

		ret = write(g_mdev.ofile_fd[dev_id], plane->vaddr, plane->len);
		if (ret < 0) {
			ERR("%d-%d: Failed to write %d data. err %d[%s]\n",
				index, i, plane->len, errno, strerror(errno));
			return -1;
		}
	}
	return 0;
}

int vin_capture(u32 cnt, int dev_id)
{
	struct timeval start, end;
	char fps_prefix[32] = "";
	int i, index = 0;

	gettimeofday(&start, NULL);
	for (i = 0; i < cnt; i++) {
		if (!g_running)
			break;

		if (vin_dequeue_buf(&index, dev_id) < 0)
			return -1;

		if (g_verbose)
			DBG("DQ Dev%d return the buf %d\n", dev_id, index);

#ifdef SUPPORT_ROTATION
		if (g_mdev.rotation) {
			if (do_rotate(&g_vdata, index) < 0)
				return -1;

			index = VIN_BUF_NUM;
		}
#endif
		if (g_mdev.out_to_file) {
			if (video_data_save(&g_vdata[dev_id], index, dev_id) < 0)
				return -1;

			vin_queue_buf(index, dev_id);
		 } else {
			if (video_data_display(&g_vdata[dev_id], index, dev_id, true) < 0)
				return -1;
		}

		if (i && (i % 1000 == 0)) {
			gettimeofday(&end, NULL);
			snprintf(fps_prefix, sizeof(fps_prefix), "[%s][%d]",
					 g_vin_dev_info[dev_id].vin_ctrl_name, i);
			show_fps(fps_prefix, &start, &end, 1000);
			gettimeofday(&start, NULL);
		}
	}

	i = (i - 1) % 1000;
	if (i) {
		gettimeofday(&end, NULL);
		snprintf(fps_prefix, sizeof(fps_prefix), "[%s][%d]",
				 g_vin_dev_info[dev_id].vin_ctrl_name, i + 1);
		show_fps(fps_prefix, &start, &end, i + 1);
	}

	return 0;
}

int media_dev_open(char *ofilename)
{
	char fullname[DEV_NAME_LEN] = "";
	int i;

	g_mdev.sensor_fd = device_open(g_src_subdev_name, O_RDWR);
	if (g_mdev.sensor_fd < 0)
		return -1;

	for (i = 0; i < g_vin_dev_num; i++) {
		g_mdev.video_fd[i] = device_open(g_vin_dev_info[i].video_name, O_RDWR);
		if (g_mdev.video_fd[i] < 0)
			return -1;
	}

	g_mdev.vin_fd = device_open(g_dst_subdev_name, O_RDWR);
	if (g_mdev.vin_fd < 0)
		return -1;

	if (g_mdev.out_to_file && ofilename && ofilename[0])  {
		for (i = 0; i < g_vin_dev_num; i++) {
			snprintf(fullname, DEV_NAME_LEN, "%s%d", ofilename, i);
			g_mdev.ofile_fd[i] = device_open(fullname, O_CREAT | O_WRONLY | O_TRUNC);
			if (g_mdev.ofile_fd[i] < 0)
				return -1;

			printf("Output file of video %d: %s\n\n", i, ofilename);
		}
	} else {
		g_mdev.fb_fd = device_open(FB_DEV, O_RDWR);
		if (g_mdev.fb_fd < 0)
			return -1;
	}

#ifdef SUPPORT_ROTATION
	if (g_mdev.rotation) {
		g_mdev.ge_dev = mpp_ge_open();
		if (!g_mdev.ge_dev) {
			ERR("Failed to open GE\n");
			return -1;
		}
	}
#endif
	return 0;
}

void media_dev_close(void)
{
	int i;

	if (g_mdev.sensor_fd > 0)
		close(g_mdev.sensor_fd);
	if (g_mdev.vin_fd > 0)
		close(g_mdev.vin_fd);
	if (g_mdev.fb_fd > 0) {
		fb_free_info();
		close(g_mdev.fb_fd);
		g_mdev.fb_fd = 0;
	}
#ifdef SUPPORT_ROTATION
	if (g_mdev.ge_dev)
		mpp_ge_close(g_mdev.ge_dev);
#endif
	for (i = 0; i < g_vin_dev_num; i++) {
		if (g_mdev.video_fd[i] > 0)
			close(g_mdev.video_fd[i]);

		if (g_mdev.ofile_fd[i] > 0)
			close(g_mdev.ofile_fd[i]);
	}
}

void *media_thread(void *arg)
{
	struct aic_video_data *vdata = arg;
	int dev_id = vdata->vin_dev->id;
	int ret = 0, i;

	vdata->result = -1;
	if (vin_cfg(vdata->w, vdata->h, vdata->fmt) < 0)
		goto end;
	if (g_mdev.rotation) {
		printf("Rotate %d by GE\n", g_mdev.rotation * 90);
		/* Use the last buf connect GE and Video layer */
		if (vin_request_buf(VIN_BUF_NUM + 1, vdata->binfo, dev_id) < 0)
			goto end;
		if (vidbuf_dmabuf_begin(vdata, VIN_BUF_NUM + 1, dev_id))
			goto end;
	} else {
		if (vin_request_buf(VIN_BUF_NUM, vdata->binfo, dev_id) < 0)
			goto end;

		if (vidbuf_dmabuf_begin(vdata, VIN_BUF_NUM, dev_id))
			goto end;
	}

	for (i = 0; i < VIN_BUF_NUM; i++)
		if (vin_queue_buf(i, dev_id) < 0)
			goto end;

	if (vin_start(dev_id) < 0)
		goto end;

#ifdef VIN_DEBUG_NO_SIGNAL
	video_data_display(&g_vdata[dev_id], VIN_BUF_NUM - 1, dev_id, false);
#endif

	ret |= vin_capture(g_frame_cnt, dev_id);

	if (g_verbose)
		DBG("Stop stream %d\n", dev_id);

	ret |= vin_stop(dev_id);
	if (g_mdev.rotation) {
		vidbuf_dmabuf_end(vdata, VIN_BUF_NUM + 1, dev_id);
		vin_release_buf(VIN_BUF_NUM + 1);
	} else {
		vidbuf_dmabuf_end(vdata, VIN_BUF_NUM, dev_id);
		vin_release_buf(VIN_BUF_NUM);
	}

	vdata->result = ret;
end:
	return &vdata->result;
}

int media_process(void)
{
	struct aic_sensor_info *s_info = &g_mdev.sensor_info;
	struct aic_fb_info *fb_info = &g_mdev.fb_info;
	pthread_t th_id[VIN_DEV_NUM_MAX] = {0};
	pthread_t th_id_render = 0;
	char dev_num_str[8] = "";
	int ret = 0, i;

	if (sensor_set_fmt() < 0)
		return -1;
	if (vin_subdev_set_fmt() < 0)
		return -1;

	if (g_vin_dev_num > 1)
		snprintf(dev_num_str, 8, "x %d", g_vin_dev_num);

	printf("The stream size:\n");
	printf(" [Sensor] %d x %d %s\n\t└─> [VIN] %d x %d %s\n",
		   s_info->width, s_info->height, dev_num_str,
		   g_vdata[0].w, g_vdata[0].h, dev_num_str);
	if (!g_mdev.out_to_file) {
		if (fb_get_info())
			return -1;

		printf("\t\t└─> [Panel] %d x %d\n",	fb_info->fb_width, fb_info->fb_height);
		for (i = 0; i < g_vin_dev_num; i++) {
			printf("\t\t\t└─> [Win %d] [%d, %d] %d x %d\n", i,
				fb_info->win_x[i], fb_info->win_y[i],
				fb_info->win_width[i], fb_info->win_height[i]);
		}
		pthread_mutex_init(&g_mdev.fb_lock, NULL);
		drm_render_init();
	}
	printf("\n");

	for (i = 0; i < g_vin_dev_num; i++) {
		ret = pthread_create(&th_id[i], NULL, media_thread, &g_vdata[i]);
		if (ret) {
			ERR("Failed to create thread %d\n", i);
			goto out;
		}
	}

	g_running = true;
	if (!g_mdev.out_to_file) {
		ret = pthread_create(&th_id_render, NULL, drm_render_thread, NULL);
		if (ret) {
			ERR("Failed to create render thread\n");
			g_running = false;
		}
	}

out:
	/* Wait for all threads to finish */
	for (i = 0; i < g_vin_dev_num; i++) {
		if (th_id[i]) {
			if (g_verbose)
				DBG("Wait for thread %d ...\n", i);

			pthread_join(th_id[i], NULL);
			ret |= g_vdata[i].result;
		}
	}
	g_running = false;

	if (!g_mdev.out_to_file) {
		pthread_join(th_id_render, NULL);
		drm_render_deinit();
	}

	pthread_mutex_destroy(&g_mdev.fb_lock);
	return ret;
}

int parse_vin_dev_type(void)
{
	struct vin_dev_info *info = g_vin_dev_info;

	for (int i = 0; i < g_vin_dev_num; i++, info++) {
		if (strstr(info->vin_ctrl_name, "csi")) {
			info->type = VIN_IS_CSI;
		} else if (strstr(info->vin_ctrl_name, "dvp")) {
			info->type = VIN_IS_DVP;
		} else if (strstr(info->vin_ctrl_name, "dsi")) {
			info->type = VIN_IS_DSI;
		} else {
			ERR("Unsupported VIN device %d: %s\n", i, info->vin_ctrl_name);
			return -1;
		}
	}

	return 0;
}

int main(int argc, char **argv)
{
	char s_fmt[DEV_NAME_LEN] = "", vin_fmt[DEV_NAME_LEN] = "";
	struct aic_sensor_info *s_info = &g_mdev.sensor_info;
	char mdevice[DEV_NAME_LEN] = MEDIA_DEV;
	char ofilename[DEV_NAME_LEN] = "";
	bool only_show_topology = false;
	int c, ret = 0, dev_num = 0;

	if (argc < 2) {
		usage(argv[0]);
		return -1;
	}

	s_info->width = 640;
	s_info->height = 480;
	s_info->fr = 30;
	while ((c = getopt_long(argc, argv, sopts, lopts, NULL)) != -1) {
		switch (c) {
		case 's':
			strncpy(s_fmt, optarg, DEV_NAME_LEN - 1);
			break;
		case 'f':
			strncpy(vin_fmt, optarg, DEV_NAME_LEN - 1);
			break;
		case 'c':
			g_frame_cnt = str2int(optarg);
			if (g_frame_cnt < 1) {
				ERR("Invalid frame count: %s\n", optarg);
				return -1;
			}
			break;
		case 'w':
			s_info->width = str2int(optarg);
			if (!s_info->width) {
				ERR("Invalid width: %s\n", optarg);
				return -1;
			}
			break;
		case 'h':
			s_info->height = str2int(optarg);
			if (!s_info->height) {
				ERR("Invalid height: %s\n", optarg);
				return -1;
			}
			break;
		case 'r':
			s_info->fr = str2int(optarg);
			if (!s_info->fr) {
				ERR("Invalid frame rate: %s\n", optarg);
				return -1;
			}
			break;
#ifdef SUPPORT_ROTATION
		case 'a':
			g_mdev.rotation = (str2int(optarg) % 360) / 90;
			break;
#endif
		case 'd':
			strncpy(mdevice, optarg, DEV_NAME_LEN - 2);
			break;
		case 'n':
			dev_num = str2int(optarg);
			if (dev_num < 1 || dev_num > VIN_DEV_NUM_MAX) {
				ERR("Invalid device number: %s\n", optarg);
				return -1;
			}
			break;
		case 'o':
			strncpy(ofilename, optarg, DEV_NAME_LEN - 1);
			g_mdev.out_to_file = true;
			g_mdev.rotation = 0;
			break;
		case 'p':
			g_verbose = true;
			only_show_topology = true;
			break;
		case 'l':
			show_all_formats();
			return 0;
		case 'u':
			usage(argv[0]);
			return 0;
		case 'v':
			g_verbose = true;
			break;
		default:
			ERR("Invalid argument: %s\n", optarg);
			usage(argv[0]);
			return -1;
		}
	}

	if (media_dev_parse(mdevice))
		return -1;

	if (parse_vin_dev_type())
		return -1;

	if (s_fmt[0] && !sensor_ofmt_is_available(&s_info->fmt, s_fmt)) {
		ERR("Invalid sensor output format: %s\n", s_fmt);
		return -1;
	}
	if (vin_fmt[0] && !vin_ofmt_is_available(&g_vdata[0].fmt, vin_fmt)) {
		ERR("Invalid VIN output format: %s\n", vin_fmt);
		return -1;
	}

	if (!g_vin_dev_num) {
		ERR("No video device found\n");
		return -1;
	}

	printf("-------------------------------------------------------------\n");
	printf("           Media device: %s\n", mdevice);
	printf("          Camera subdev: %s\n", g_src_subdev_name);
	printf("  VIN controller subdev: %s\n", g_dst_subdev_name);
	for (int i = 0; i < g_vin_dev_num; i++) {
		printf("           VIN device %d: %s\n", i, g_vin_dev_info[i].video_name);
		printf("       VIN controller %d: %s\n", i, g_vin_dev_info[i].vin_ctrl_name);
	}
	printf("\n");

	if (dev_num) {
		if (dev_num > g_vin_dev_num) {
			ERR("There are only %d VIN device, expect %d\n", g_vin_dev_num, dev_num);
		} else {
			g_vin_dev_num = dev_num;
		}
	}

	if (only_show_topology)
		return 0;

	printf("Will capture %d frames from %d VIN device(s) ...\n\n",
		   g_frame_cnt, g_vin_dev_num);
	ret = media_dev_open(ofilename);
	if (!ret)
		ret = media_process();

	media_dev_close();
	return ret;
}
