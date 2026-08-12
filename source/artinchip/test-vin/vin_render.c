// SPDX-License-Identifier: Apache-2.0
/*
 * Copyright (C) 2025-2026 ArtInChip Technology Co., Ltd.
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Authors:  Matteo <duanmt@artinchip.com>
 */

#include <linux/dma-buf.h>
#include <linux/dma-heap.h>
#include <linux/videodev2.h>
#include <linux/v4l2-subdev.h>
#include <linux/fb.h>
#include <sys/time.h>
#include <pthread.h>

#include <drm/drm_fourcc.h>

#include <video/artinchip_fb.h>
#include <artinchip/sample_base.h>

#include "mediactl.h"
#include "v4l2subdev.h"

#include "test_vin.h"

struct vin_render_info g_render_info[VIN_DEV_NUM_MAX] = {0};

u32 mode_get_fps(drmModeModeInfo *mode)
{
	u32 num, den;

	num = mode->clock;
	den = mode->htotal * mode->vtotal;

	if (mode->flags & DRM_MODE_FLAG_INTERLACE)
		num *= 2;
	if (mode->flags & DRM_MODE_FLAG_DBLSCAN)
		den *= 2;
	if (mode->vscan > 1)
		den *= mode->vscan;

	return num * 1000.00 / den;
}

int fb_set_prop_id(drmModeAtomicReq *req, u32 plane_id, u32 prop_id, u32 value, int dev_id)
{
	if (drmModeAtomicAddProperty(req, plane_id, prop_id, value) < 0) {
		ERR("Dev%d: Failed to add property %d: %d, return %d[%s]\n",
			dev_id, prop_id, value, errno, strerror(errno));
		return -1;
	}

	return 0;
}

u32 collect_render_req(drmModeAtomicReq *req, int dev_id)
{
	struct vin_render_info *info = &g_render_info[dev_id];
	u32 prop_val[VIN_PROP_NUM_MAX] = {0};
	u32 prop_id[VIN_PROP_NUM_MAX] = {0};
	u32 i, valid = 0, plane_id = 0, buf_index = 0;

	pthread_mutex_lock(&info->mutex);
	if (info->ready && info->plane_id) {
		memcpy(prop_val, info->prop_val, sizeof(u32) * VIN_PROP_NUM_MAX);
		memcpy(prop_id, info->prop_id, sizeof(u32) * VIN_PROP_NUM_MAX);
		buf_index = info->buf_index;
		plane_id = info->plane_id;

		valid = 1;
		info->ready = false;
		info->recv_cnt++;
	}
	pthread_mutex_unlock(&info->mutex);
	if (valid) {
		pthread_cond_signal(&info->cond);
		if (info->queue)
			vin_queue_buf(buf_index, dev_id);

		for (i = 0; i < VIN_PROP_NUM_MAX; i++) {
			if (!prop_id[i])
				continue;

			fb_set_prop_id(req, plane_id, prop_id[i], prop_val[i], dev_id);
		}
	}

	if (g_vdata[dev_id].streaming)
		return valid;
	else
		return 0;
}

void show_render_stats(void)
{
	struct vin_render_info *info = g_render_info;

	for (int i = 0; i < g_vin_dev_num; i++, info++) {
		printf("Dev%d render: %d/%d, drop %d, delay %d\n", i,
				info->recv_cnt, info->send_cnt,
				info->drop_cnt, info->delay_cnt);
	}
}

void *drm_render_thread(void *arg)
{
	struct aic_fb_info *fb_info = &g_mdev.fb_info;
	drmModeAtomicReq *req = NULL;
	int i, ret = 0, req_cnt = 0;
	u32 cnt = 0, timeout_ms = 0;

	ret = mode_get_fps(fb_info->mode);
	printf("Current DRM FPS: %d\n\n", ret);
	if (ret)
		timeout_ms = 1000 / ret / 2;
	else
		timeout_ms = 10;

	while (g_running) {
		if (!req) {
			req = drmModeAtomicAlloc();
			if (!req) {
				ERR("Failed to alloc atomic request\n");
				goto err;
			}
		}

		req_cnt = 0;
		for (i = 0; i < g_vin_dev_num; i++)
			req_cnt += collect_render_req(req, i);

		if (g_verbose && req_cnt)
			DBG("Will commit %d planes\n", req_cnt);

		if (req_cnt && g_mdev.fb_fd) {
			ret = drmModeAtomicCommit(g_mdev.fb_fd, req, 0, NULL);

			if (ret) {
				ERR("Atomic commit %d plane failed. Err %d[%s]\n",
					req_cnt, errno, strerror(errno));
			}
			drmModeAtomicFree(req);
			req = NULL;
			cnt++;
		}

		usleep(timeout_ms * 2000);
	}
	printf("DRM render total %d frames\n\n", cnt);

err:
	if (req)
		drmModeAtomicFree(req);

	show_render_stats();
	g_running = false;
	return NULL;
}

void drm_render_init(void)
{
	if (g_running)
		return;

	for (int i = 0; i < VIN_DEV_NUM_MAX; i++) {
		pthread_mutex_init(&g_render_info[i].mutex, NULL);
		pthread_cond_init(&g_render_info[i].cond, NULL);
	}
}

void drm_render_deinit(void)
{
	if (g_running)
		return;

	for (int i = 0; i < VIN_DEV_NUM_MAX; i++) {
		pthread_mutex_destroy(&g_render_info[i].mutex);
		pthread_cond_destroy(&g_render_info[i].cond);
	}
}
