/*
 * Copyright (C) 2026 ArtInChip Technology Co. Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 *  author: <huahui.mai@artinchip.com>
 *  Desc: aic_video_render - DRM Backend Atomic implementation
 */

#define _GNU_SOURCE
#include <stdlib.h>
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <xf86drm.h>
#include <xf86drmMode.h>
#include <drm/drm_fourcc.h>
#include <drm/drm_mode.h>

#include "mpp_mem.h"
#include "mpp_log.h"
#include "mpp_list.h"
#include "aic_render.h"
#include "dma_allocator.h"
#include "mpp_ge.h"

#define DRM_DEVICE "/dev/dri/card0"
#define MAX_DRM_PLANES		4
#define DRM_PLANE_TYPE_OVERLAY	0
#define DRM_PLANE_TYPE_PRIMARY	1
#define DRM_PLANE_TYPE_CURSOR	2

struct drm_frame_info {
	s32 frame_id;
	s32 fd[MAX_DRM_PLANES];
	s32 fd_num;
	u32 fb_id;
	u32 handles[MAX_DRM_PLANES];
	u32 pitches[MAX_DRM_PLANES];
	u32 offsets[MAX_DRM_PLANES];
};

struct drm_frame_info_list {
	struct drm_frame_info info;
	struct mpp_list list;
};

struct aic_drm_video_render {
	struct aic_video_render base;
	s32 drm_fd;
	u32 connector_id;
	u32 encoder_id;
	u32 crtc_id;
	u32 plane_id;
	drmModeModeInfo mode;
	drmModeCrtcPtr saved_crtc;
	drmModeAtomicReqPtr atomic_req;
	struct mpp_list frame_list;
	struct mpp_rect disp_rect;
	struct mpp_buf last_buf;
	s32 enabled;
	s32 layer_id;

	u32 plane_fb_id_prop;
	u32 plane_crtc_id_prop;
	u32 plane_src_x_prop;
	u32 plane_src_y_prop;
	u32 plane_src_w_prop;
	u32 plane_src_h_prop;
	u32 plane_crtc_x_prop;
	u32 plane_crtc_y_prop;
	u32 plane_crtc_w_prop;
	u32 plane_crtc_h_prop;
	u32 crtc_active_prop;
	u32 crtc_mode_id_prop;
};

struct aic_drm_last_frame {
	struct mpp_frame frame;
	s32 enable;
	s32 dma_fd;
};

static struct aic_drm_last_frame g_last_frame = {0};

static u32 mpp_format_to_drm_format(enum mpp_pixel_format format)
{
	switch (format) {
	case MPP_FMT_ARGB_8888:
		return DRM_FORMAT_ARGB8888;
	case MPP_FMT_RGBA_8888:
		return DRM_FORMAT_RGBA8888;
	case MPP_FMT_RGB_888:
		return DRM_FORMAT_RGB888;
	case MPP_FMT_YUV420P:
		return DRM_FORMAT_YUV420;
	case MPP_FMT_NV12:
		return DRM_FORMAT_NV12;
	case MPP_FMT_NV21:
		return DRM_FORMAT_NV21;
	case MPP_FMT_YUV444P:
		return DRM_FORMAT_YUV444;
	case MPP_FMT_YUV422P:
		return DRM_FORMAT_YUV422;
	case MPP_FMT_YUV400:
		return DRM_FORMAT_R8;
	default:
		loge("Unsupported pixel format: %d", format);
		return DRM_FORMAT_ARGB8888;
	}
}

static s32 get_drm_plane_count(u32 drm_format)
{
	switch (drm_format) {
	case DRM_FORMAT_YUV420:
	case DRM_FORMAT_YUV422:
	case DRM_FORMAT_YUV444:
		return 3;
	case DRM_FORMAT_NV12:
	case DRM_FORMAT_NV21:
		return 2;
	case DRM_FORMAT_ARGB8888:
	case DRM_FORMAT_RGBA8888:
	case DRM_FORMAT_RGB888:
	case DRM_FORMAT_R8:
		return 1;
	default:
		return 1;
	}
}

static s32 get_component_num(enum mpp_pixel_format format)
{
	switch (format) {
	case MPP_FMT_ARGB_8888:
	case MPP_FMT_RGBA_8888:
	case MPP_FMT_RGB_888:
	case MPP_FMT_YUV400:
		return 1;
	case MPP_FMT_NV12:
	case MPP_FMT_NV21:
		return 2;
	case MPP_FMT_YUV420P:
	case MPP_FMT_YUV444P:
	case MPP_FMT_YUV422P:
		return 3;
	default:
		loge("no support picture format %d, default argb8888", format);
		return 0;
	}
}

static u32 get_plane_type_value(drmModePropertyRes *prop)
{
	for (int i = 0; i < prop->count_enums; i++) {
		if (strcmp(prop->enums[i].name, "Overlay") == 0)
			return prop->enums[i].value;
		if (strcmp(prop->enums[i].name, "Primary") == 0)
			return prop->enums[i].value;
	}
	return DRM_PLANE_TYPE_OVERLAY;
}

static s32 drm_find_resources(struct aic_drm_video_render *drm_render)
{
	drmModeRes *resources = NULL;
	drmModeConnector *connector = NULL;
	drmModeEncoder *encoder = NULL;
	drmModePlaneRes *plane_resources = NULL;
	drmModeObjectProperties *props = NULL;
	drmModePropertyRes *prop = NULL;
	s32 i, j;
	s32 ret = -1;

	resources = drmModeGetResources(drm_render->drm_fd);
	if (!resources) {
		loge("Cannot get DRM resources");
		goto cleanup;
	}

	/* Find first connected connector */
	for (i = 0; i < resources->count_connectors; i++) {
		connector = drmModeGetConnector(drm_render->drm_fd, resources->connectors[i]);
		if (connector && connector->connection == DRM_MODE_CONNECTED && connector->count_modes > 0) {
			drm_render->connector_id = connector->connector_id;
			drm_render->mode = connector->modes[0]; // Use first mode
			logi("Found connector %d with mode %dx%d@%d",
				connector->connector_id,
				drm_render->mode.hdisplay,
				drm_render->mode.vdisplay,
				drm_render->mode.vrefresh);
			break;
		}
		if (connector) {
			drmModeFreeConnector(connector);
			connector = NULL;
		}
	}

	if (i == resources->count_connectors) {
		loge("No connected connector found");
		goto cleanup;
	}

	/* Find encoder */
	encoder = drmModeGetEncoder(drm_render->drm_fd, connector->encoder_id);
	if (encoder) {
		drm_render->encoder_id = encoder->encoder_id;
		drm_render->crtc_id = encoder->crtc_id;
		drmModeFreeEncoder(encoder);
		encoder = NULL;
	} else {
		/* Try to find any encoder */
		for (i = 0; i < resources->count_encoders; i++) {
			encoder = drmModeGetEncoder(drm_render->drm_fd, resources->encoders[i]);
			if (encoder && encoder->possible_crtcs) {
				drm_render->encoder_id = encoder->encoder_id;
				drm_render->crtc_id = resources->crtcs[0]; // Use first CRTC
				drmModeFreeEncoder(encoder);
				encoder = NULL;
				break;
			}
			if (encoder) {
				drmModeFreeEncoder(encoder);
				encoder = NULL;
			}
		}
	}

	if (!drm_render->crtc_id) {
		loge("No CRTC found");
		goto cleanup;
	}

	/* Save current CRTC state */
	drm_render->saved_crtc = drmModeGetCrtc(drm_render->drm_fd, drm_render->crtc_id);

	plane_resources = drmModeGetPlaneResources(drm_render->drm_fd);
	if (!plane_resources) {
		loge("Cannot get plane resources");
		goto cleanup;
	}

	/* Find overlay plane (not used by any CRTC) */
	for (i = 0; i < plane_resources->count_planes; i++) {
		drmModePlane *plane = drmModeGetPlane(drm_render->drm_fd, plane_resources->planes[i]);
		if (!plane)
			continue;

		/* Check if plane is overlay type */
		props = drmModeObjectGetProperties(drm_render->drm_fd, plane->plane_id,
							DRM_MODE_OBJECT_PLANE);
		if (props) {
			for (j = 0; j < props->count_props; j++) {
				prop = drmModeGetProperty(drm_render->drm_fd, props->props[j]);
				if (prop && strcmp(prop->name, "type") == 0) {
					u32 type_value = get_plane_type_value(prop);
					if (type_value == DRM_PLANE_TYPE_OVERLAY && plane->crtc_id == 0) {
						drm_render->plane_id = plane->plane_id;
						drmModeFreeProperty(prop);
						drmModeFreeObjectProperties(props);
						drmModeFreePlane(plane);
						prop = NULL;
						props = NULL;
						goto found_plane;
					}
				}
				if (prop) {
					drmModeFreeProperty(prop);
					prop = NULL;
				}
			}
			drmModeFreeObjectProperties(props);
			props = NULL;
		}
		drmModeFreePlane(plane);
	}

found_plane:
	if (!drm_render->plane_id) {
		loge("No available overlay plane found");
		goto cleanup;
	}

	logi("Found plane %d for rendering", drm_render->plane_id);
	ret = 0;

cleanup:
	if (plane_resources)
		drmModeFreePlaneResources(plane_resources);
	if (connector)
		drmModeFreeConnector(connector);
	if (resources)
		drmModeFreeResources(resources);
	if (prop)
		drmModeFreeProperty(prop);
	if (props)
		drmModeFreeObjectProperties(props);

	return ret;
}

static s32 drm_get_properties(struct aic_drm_video_render *drm_render)
{
	drmModeObjectProperties *props = NULL;
	drmModePropertyRes *prop = NULL;
	s32 i;

	/* Get plane properties */
	props = drmModeObjectGetProperties(drm_render->drm_fd, drm_render->plane_id,
						DRM_MODE_OBJECT_PLANE);
	if (!props) {
		loge("Cannot get plane properties");
		return -1;
	}

	for (i = 0; i < props->count_props; i++) {
		prop = drmModeGetProperty(drm_render->drm_fd, props->props[i]);
		if (!prop)
			continue;

		if (strcmp(prop->name, "FB_ID") == 0)
			drm_render->plane_fb_id_prop = prop->prop_id;
		else if (strcmp(prop->name, "CRTC_ID") == 0)
			drm_render->plane_crtc_id_prop = prop->prop_id;
		else if (strcmp(prop->name, "SRC_X") == 0)
			drm_render->plane_src_x_prop = prop->prop_id;
		else if (strcmp(prop->name, "SRC_Y") == 0)
			drm_render->plane_src_y_prop = prop->prop_id;
		else if (strcmp(prop->name, "SRC_W") == 0)
			drm_render->plane_src_w_prop = prop->prop_id;
		else if (strcmp(prop->name, "SRC_H") == 0)
			drm_render->plane_src_h_prop = prop->prop_id;
		else if (strcmp(prop->name, "CRTC_X") == 0)
			drm_render->plane_crtc_x_prop = prop->prop_id;
		else if (strcmp(prop->name, "CRTC_Y") == 0)
			drm_render->plane_crtc_y_prop = prop->prop_id;
		else if (strcmp(prop->name, "CRTC_W") == 0)
			drm_render->plane_crtc_w_prop = prop->prop_id;
		else if (strcmp(prop->name, "CRTC_H") == 0)
			drm_render->plane_crtc_h_prop = prop->prop_id;

		drmModeFreeProperty(prop);
	}
	drmModeFreeObjectProperties(props);
	props = NULL;

	/* Get CRTC properties */
	props = drmModeObjectGetProperties(drm_render->drm_fd, drm_render->crtc_id,
						DRM_MODE_OBJECT_CRTC);
	if (!props) {
		loge("Cannot get CRTC properties");
		return -1;
	}

	for (i = 0; i < props->count_props; i++) {
		prop = drmModeGetProperty(drm_render->drm_fd, props->props[i]);
		if (!prop)
			continue;

		if (strcmp(prop->name, "ACTIVE") == 0)
			drm_render->crtc_active_prop = prop->prop_id;
		else if (strcmp(prop->name, "MODE_ID") == 0)
			drm_render->crtc_mode_id_prop = prop->prop_id;

		drmModeFreeProperty(prop);
	}
	drmModeFreeObjectProperties(props);

	return 0;
}

static s32 drm_video_render_init(struct aic_video_render *render, s32 layer, s32 dev_id)
{
	struct aic_drm_video_render *drm_render = (struct aic_drm_video_render *)render;
	s32 ret;

	drm_render->drm_fd = open(DRM_DEVICE, O_RDWR | O_CLOEXEC);
	if (drm_render->drm_fd < 0) {
		loge("Cannot open DRM device: %s", DRM_DEVICE);
		return -1;
	}

	ret = drmSetClientCap(drm_render->drm_fd, DRM_CLIENT_CAP_ATOMIC, 1);
	if (ret) {
		loge("no atomic modesetting support: %s", strerror(errno));
		close(drm_render->drm_fd);
		return -1;
	}

	drmSetClientCap(drm_render->drm_fd, DRM_CLIENT_CAP_UNIVERSAL_PLANES, 1);

	/* Use DRM master if not already */
	if (drmSetMaster(drm_render->drm_fd) < 0)
		logw("Cannot become DRM master (maybe already master)");

	drm_render->layer_id = layer;
	ret = drm_find_resources(drm_render);
	if (ret < 0) {
		close(drm_render->drm_fd);
		return -1;
	}

	ret = drm_get_properties(drm_render);
	if (ret < 0) {
		drmModeFreeCrtc(drm_render->saved_crtc);
		close(drm_render->drm_fd);
		return -1;
	}

	/* Initialize display rectangle to full screen */
	drm_render->disp_rect.x = 0;
	drm_render->disp_rect.y = 0;
	drm_render->disp_rect.width = drm_render->mode.hdisplay;
	drm_render->disp_rect.height = drm_render->mode.vdisplay;
	drm_render->enabled = 0;

	logi("DRM atomic render initialized: %dx%d@%d, plane: %d, crtc: %d",
		drm_render->mode.hdisplay,
		drm_render->mode.vdisplay,
		drm_render->mode.vrefresh,
		drm_render->plane_id,
		drm_render->crtc_id);

	return 0;
}

static s32 drm_video_render_destroy(struct aic_video_render *render)
{
	struct aic_drm_video_render *drm_render = (struct aic_drm_video_render *)render;
	struct drm_frame_info_list *frame_node, *temp_node;

	/* Disable plane */
	if (drm_render->enabled) {
		drm_render->atomic_req = drmModeAtomicAlloc();

		drmModeAtomicAddProperty(drm_render->atomic_req, drm_render->plane_id,
					drm_render->plane_crtc_id_prop, 0);
		drmModeAtomicAddProperty(drm_render->atomic_req, drm_render->plane_id,
					drm_render->plane_fb_id_prop, 0);
		drmModeAtomicCommit(drm_render->drm_fd, drm_render->atomic_req, 0, NULL);
	}

	/* Free all frame buffers */
	mpp_list_for_each_entry_safe(frame_node, temp_node, &drm_render->frame_list, list) {
		mpp_list_del(&frame_node->list);

		if (frame_node->info.fb_id)
			drmModeRmFB(drm_render->drm_fd, frame_node->info.fb_id);

		for (s32 i = 0; i < frame_node->info.fd_num; i++) {
			if (frame_node->info.handles[i]) {
				struct drm_gem_close gem_close = {
					.handle = frame_node->info.handles[i],
				};
				ioctl(drm_render->drm_fd, DRM_IOCTL_GEM_CLOSE, &gem_close);
			}
		}

		mpp_free(frame_node);
	}

	if (drm_render->atomic_req)
		drmModeAtomicFree(drm_render->atomic_req);

	if (drm_render->saved_crtc) {
		drmModeSetCrtc(drm_render->drm_fd,
				drm_render->saved_crtc->crtc_id,
				drm_render->saved_crtc->buffer_id,
				drm_render->saved_crtc->x,
				drm_render->saved_crtc->y,
				&drm_render->connector_id,
				1,
				&drm_render->saved_crtc->mode);
		drmModeFreeCrtc(drm_render->saved_crtc);
	}

	drmDropMaster(drm_render->drm_fd);

	if (drm_render->drm_fd > 0)
		close(drm_render->drm_fd);

	mpp_free(drm_render);

	logi("DRM atomic render destroyed");
	return 0;
}

static s32 drm_create_framebuffer(struct aic_drm_video_render *drm_render,
					struct mpp_frame *mpp_frame,
					struct drm_frame_info *drm_frame)
{
	u32 drm_format;
	s32 plane_count;
	s32 ret;
	u32 handles[MAX_DRM_PLANES] = {0};
	u32 pitches[MAX_DRM_PLANES] = {0};
	u32 offsets[MAX_DRM_PLANES] = {0};

	drm_format = mpp_format_to_drm_format(mpp_frame->buf.format);
	plane_count = get_drm_plane_count(drm_format);

	/* Import DMA buffers as GEM handles */
	for (s32 i = 0; i < plane_count; i++) {
		struct drm_prime_handle prime_handle = {
			.fd = mpp_frame->buf.fd[i],
			.flags = DRM_CLOEXEC | DRM_RDWR,
			.handle = 0,
		};

		ret = ioctl(drm_render->drm_fd, DRM_IOCTL_PRIME_FD_TO_HANDLE, &prime_handle);
		if (ret < 0) {
			loge("Failed to import DMA buffer as GEM handle (fd: %d): %m", mpp_frame->buf.fd[i]);
			/* Cleanup already imported handles */
			for (s32 j = 0; j < i; j++) {
				struct drm_gem_close gem_close = {
					.handle = handles[j],
				};
				ioctl(drm_render->drm_fd, DRM_IOCTL_GEM_CLOSE, &gem_close);
			}

			return -1;
		}

		drm_frame->handles[i] = prime_handle.handle;
		drm_frame->pitches[i] = mpp_frame->buf.stride[i];
		drm_frame->offsets[i] = 0; // default offset 0
		handles[i] = prime_handle.handle;
		pitches[i] = mpp_frame->buf.stride[i];
		offsets[i] = 0; // default offset 0
	}

	/* Create DRM framebuffer with multiple planes */
	ret = drmModeAddFB2(drm_render->drm_fd,
				mpp_frame->buf.size.width,
				mpp_frame->buf.size.height,
				drm_format,
				handles,
				pitches,
				offsets,
				&drm_frame->fb_id,
				0);

	if (ret < 0) {
		loge("Failed to create DRM framebuffer: %m");
		/* Cleanup GEM handles */
		for (s32 i = 0; i < plane_count; i++) {
			struct drm_gem_close gem_close = {
				.handle = handles[i],
			};
			ioctl(drm_render->drm_fd, DRM_IOCTL_GEM_CLOSE, &gem_close);
		}
		return -1;
	}

	drm_frame->fd_num = plane_count;

	logd("Created DRM framebuffer %d for frame %d (%dx%d, format: 0x%08x)",
		drm_frame->fb_id, mpp_frame->id,
		mpp_frame->buf.size.width, mpp_frame->buf.size.height,
		drm_format);

	return 0;
}

static s32 drm_video_render_rend(struct aic_video_render *render, struct mpp_frame *mpp_frame)
{
	struct aic_drm_video_render *drm_render = (struct aic_drm_video_render *)render;
	struct drm_frame_info_list *frame_node = NULL, *temp_node;
	int dmabuf_num = 0;
	s32 found = 0;
	s32 ret, i;

	if (!mpp_frame) {
		loge("mpp_frame is NULL");
		return -1;
	}

	dmabuf_num = get_component_num(mpp_frame->buf.format);

	/* Check if frame already exists in list */
	mpp_list_for_each_entry_safe(frame_node, temp_node, &drm_render->frame_list, list) {
		if (dmabuf_num == 1) {
			if (frame_node->info.fd[0] == mpp_frame->buf.fd[0]) {
				found = 1;
				break;
			}
		} else if (dmabuf_num == 2) {
			if (frame_node->info.fd[0] == mpp_frame->buf.fd[0]
				&& frame_node->info.fd[1] == mpp_frame->buf.fd[1]) {
				found = 1;
				break;
			}
		} else if (dmabuf_num == 3) {
			if (frame_node->info.fd[0] == mpp_frame->buf.fd[0]
				&& frame_node->info.fd[1] == mpp_frame->buf.fd[1]
				&& frame_node->info.fd[2] == mpp_frame->buf.fd[2]) {
				found = 1;
				break;
			}
		} else {
			loge("no support picture foramt %d", mpp_frame->buf.format);
			return -1;
		}
	}

	memcpy(&drm_render->last_buf, &mpp_frame->buf, sizeof(struct mpp_buf));

	if (!found) {
		/* Create new frame node */
		frame_node = mpp_alloc(sizeof(struct drm_frame_info_list));
		if (!frame_node) {
			loge("Failed to allocate frame node");
			return -1;
		}
		memset(frame_node, 0, sizeof(struct drm_frame_info_list));

		frame_node->info.frame_id = mpp_frame->id;
		mpp_list_init(&frame_node->list);

		for(i = 0; i < dmabuf_num; i++)
			frame_node->info.fd[i] = mpp_frame->buf.fd[i];

		/* Create DRM framebuffer */
		ret = drm_create_framebuffer(drm_render, mpp_frame, &frame_node->info);
		if (ret < 0) {
			mpp_free(frame_node);
			return -1;
		}

		mpp_list_add_tail(&frame_node->list, &drm_render->frame_list);
	}

	drm_render->atomic_req = drmModeAtomicAlloc();
	if (!drm_render->atomic_req) {
		loge("Failed to allocate atomic request");
		return -1;
	}

	drmModeAtomicAddProperty(drm_render->atomic_req, drm_render->plane_id,
				drm_render->plane_fb_id_prop, frame_node->info.fb_id);
	drmModeAtomicAddProperty(drm_render->atomic_req, drm_render->plane_id,
				drm_render->plane_crtc_id_prop, drm_render->crtc_id);

	/* Source rectangle (in 16.16 fixed point) */
	u32 src_x = 0;
	u32 src_y = 0;
	u32 src_w = mpp_frame->buf.size.width << 16;
	u32 src_h = mpp_frame->buf.size.height << 16;

	drmModeAtomicAddProperty(drm_render->atomic_req, drm_render->plane_id,
				drm_render->plane_src_x_prop, src_x);
	drmModeAtomicAddProperty(drm_render->atomic_req, drm_render->plane_id,
				drm_render->plane_src_y_prop, src_y);
	drmModeAtomicAddProperty(drm_render->atomic_req, drm_render->plane_id,
				drm_render->plane_src_w_prop, src_w);
	drmModeAtomicAddProperty(drm_render->atomic_req, drm_render->plane_id,
				drm_render->plane_src_h_prop, src_h);

	/* Destination rectangle */
	drmModeAtomicAddProperty(drm_render->atomic_req, drm_render->plane_id,
				drm_render->plane_crtc_x_prop, drm_render->disp_rect.x);
	drmModeAtomicAddProperty(drm_render->atomic_req, drm_render->plane_id,
				drm_render->plane_crtc_y_prop, drm_render->disp_rect.y);
	drmModeAtomicAddProperty(drm_render->atomic_req, drm_render->plane_id,
				drm_render->plane_crtc_w_prop, drm_render->disp_rect.width);
	drmModeAtomicAddProperty(drm_render->atomic_req, drm_render->plane_id,
				drm_render->plane_crtc_h_prop, drm_render->disp_rect.height);

	/* Ensure CRTC is active */
	drmModeAtomicAddProperty(drm_render->atomic_req, drm_render->crtc_id,
				drm_render->crtc_active_prop, 1);

	/* Commit atomic update */
	ret = drmModeAtomicCommit(drm_render->drm_fd, drm_render->atomic_req, 0, NULL);
	if (ret < 0) {
		loge("Failed to commit atomic update ret %d: %m", ret);
	} else {
		drm_render->enabled = 1;
		logv("Frame %d render successfully via DRM atomic (FB: %d)",
			mpp_frame->id, frame_node->info.fb_id);
	}

	drmModeAtomicFree(drm_render->atomic_req);
	drm_render->atomic_req = NULL;

	return ret;
}

static s32 drm_get_screen_size(struct aic_video_render *render, struct mpp_size *size)
{
	struct aic_drm_video_render *drm_render = (struct aic_drm_video_render *)render;

	if (!size) {
		loge("size parameter is NULL");
		return -1;
	}

	size->width = drm_render->mode.hdisplay;
	size->height = drm_render->mode.vdisplay;

	return 0;
}

static s32 drm_video_render_set_dis_rect(struct aic_video_render *render, struct mpp_rect *rect)
{
	struct aic_drm_video_render *drm_render = (struct aic_drm_video_render *)render;

	if (!rect) {
		loge("rect parameter is NULL");
		return -1;
	}

	drm_render->disp_rect.x = rect->x;
	drm_render->disp_rect.y = rect->y;
	drm_render->disp_rect.width = rect->width;
	drm_render->disp_rect.height = rect->height;

	logd("Set display rect: x=%d, y=%d, w=%d, h=%d",
		rect->x, rect->y, rect->width, rect->height);

	return 0;
}

static s32 drm_video_render_get_dis_rect(struct aic_video_render *render, struct mpp_rect *rect)
{
	struct aic_drm_video_render *drm_render = (struct aic_drm_video_render *)render;

	if (!rect) {
		loge("rect parameter is NULL");
		return -1;
	}

	rect->x = drm_render->disp_rect.x;
	rect->y = drm_render->disp_rect.y;
	rect->width = drm_render->disp_rect.width;
	rect->height = drm_render->disp_rect.height;

	return 0;
}

static s32 drm_video_render_set_on_off(struct aic_video_render *render, s32 enable)
{
	struct aic_drm_video_render *drm_render = (struct aic_drm_video_render *)render;

	drm_render->enabled = enable;

	logd("drm_video_render_set_on_off");
	return 0;
}

static s32 drm_video_render_get_on_off(struct aic_video_render *render, s32 *enable)
{
	struct aic_drm_video_render *drm_render = (struct aic_drm_video_render *)render;

	if (!enable) {
		loge("enable parameter is NULL");
		return -1;
	}

	*enable = drm_render->enabled;
	return 0;
}

static int drm_video_render_last_frame_alloc(struct mpp_frame *frame)
{
	int dma_fd = -1;
	if (frame == NULL) {
		loge("frame is null\n");
		return -1;
	}

	dma_fd = dmabuf_device_open();
	if (dma_fd < 0) {
		loge("dmabuf_device_open error dma_fd:%d \n", dma_fd);
		return -1;
	}

	if (mpp_buf_alloc(dma_fd, &frame->buf) < 0) {
		loge("mpp_buf_alloc frame buf error dma_fd.\n");
		dmabuf_device_close(dma_fd);
		return -1;
	}

	return dma_fd;
}

static s32 drm_video_render_last_frame_free(struct mpp_frame* frame, int dma_fd)
{
	if (frame == NULL || dma_fd <= 0) {
		loge("frame free error frame:%p, dma_fd:%d.\n", frame, dma_fd);
		return -1;
	}

	mpp_buf_free(&frame->buf);

	dmabuf_device_close(dma_fd);
	return 0;
}

static s32 drm_video_render_last_frame_copy(struct mpp_buf *src_buf, struct mpp_frame *dest_frame)
{
	s32 ret = 0;
	struct mpp_ge *ge = NULL;
	struct ge_bitblt blt;
	int i = 0;
	int comp_num = 0;

	ge = mpp_ge_open();
	if (!ge) {
		printf("open ge device error\n");
		return -1;
	}

	comp_num = get_component_num(src_buf->format);
	memset(&blt, 0, sizeof(struct ge_bitblt));
	memcpy(&blt.src_buf, src_buf, sizeof(struct mpp_buf));
	memcpy(&blt.dst_buf, &dest_frame->buf, sizeof(struct mpp_buf));

	for (i = 0; i < comp_num; i++) {
		mpp_ge_add_dmabuf(ge, src_buf->fd[i]);
		mpp_ge_add_dmabuf(ge, dest_frame->buf.fd[i]);
	}

	ret =  mpp_ge_bitblt(ge, &blt);
	if (ret < 0) {
		printf("ge bitblt fail\n");
		goto exit;
	}

	ret = mpp_ge_emit(ge);
	if (ret < 0) {
		printf("ge emit fail\n");
		goto exit;
	}

	ret = mpp_ge_sync(ge);
	if (ret < 0) {
		printf("ge sync fail\n");
	}

exit:
	for (i = 0; i < comp_num; i++) {
		mpp_ge_rm_dmabuf(ge, src_buf->fd[i]);
		mpp_ge_rm_dmabuf(ge, dest_frame->buf.fd[i]);
	}

	if (ge)
		mpp_ge_close(ge);

	return ret;
}

static s32 drm_video_render_rend_last_frame(struct aic_video_render *render, s32 enable)
{
	struct aic_drm_video_render *drm_render = (struct aic_drm_video_render *)render;
	struct mpp_frame cur_frame;
	s32 ret = 0;

	if (!drm_render) {
		loge("DRM atomic render is not initialized");
		return -1;
	}

	if (enable) {
		/* step1: malloc empty for last frame */
		memcpy(&cur_frame.buf, &drm_render->last_buf, sizeof(struct mpp_buf));

		/* the first frame need malloc buf for last frame */
		if (!g_last_frame.enable) {
			logi("%s:alloc last frame firsttime, enable:%d.\n", __func__, enable);
			memset(&g_last_frame.frame, 0, sizeof(struct mpp_frame));

			g_last_frame.dma_fd = drm_video_render_last_frame_alloc(&cur_frame);
			if (g_last_frame.dma_fd < 0) {
				loge("fb_video_render_last_frame_alloc failed.\n");
				return -1;
			}

			memcpy(&g_last_frame.frame, &cur_frame, sizeof(struct mpp_frame));
		} else {
			/* the frame resolution changed need realloc buf for last frame */
			if ((g_last_frame.frame.buf.size.height != cur_frame.buf.size.height) ||
				(g_last_frame.frame.buf.size.width != cur_frame.buf.size.width)) {
				logi("free last frame and alloc next frame.\n");
				drm_video_render_last_frame_free(&g_last_frame.frame, g_last_frame.dma_fd);

				g_last_frame.dma_fd = drm_video_render_last_frame_alloc(&cur_frame);
				if (g_last_frame.dma_fd < 0) {
					loge("fb_video_render_last_frame_alloc failed.\n");
					return -1;
				}
				memcpy(&g_last_frame.frame, &cur_frame, sizeof(struct mpp_frame));
			}
		}

		logi("fb_video_render_last_frame_copy.\n");

		/* step2: copy frame data to last frame */
		drm_video_render_last_frame_copy(&drm_render->last_buf, &g_last_frame.frame);

		/* step3: display last frame */
		drm_video_render_rend(render, &g_last_frame.frame);
	} else {
		if (g_last_frame.enable) {
			logi("%s:reclaim the final last frame, enable:%d.\n", __func__, enable);

			/* reclaim the end last frame */
			ret = drm_video_render_last_frame_free(&g_last_frame.frame, g_last_frame.dma_fd);
			if (ret) {
				loge("drm_video_render_last_frame_free error %d.", ret);
				return ret;
			}
			memset(&g_last_frame.frame, 0, sizeof(struct mpp_frame));
		}
	}

	g_last_frame.enable = enable;
	return ret;
}

s32 aic_video_render_create(struct aic_video_render **render)
{
	struct aic_drm_video_render *drm_render;

	drm_render = mpp_alloc(sizeof(struct aic_drm_video_render));
	if (!drm_render) {
		loge("Failed to allocate DRM atomic render structure");
		*render = NULL;
		return -1;
	}

	memset(drm_render, 0, sizeof(struct aic_drm_video_render));
	mpp_list_init(&drm_render->frame_list);

	drm_render->base.init = drm_video_render_init;
	drm_render->base.destroy = drm_video_render_destroy;
	drm_render->base.rend = drm_video_render_rend;
	drm_render->base.set_dis_rect = drm_video_render_set_dis_rect;
	drm_render->base.get_dis_rect = drm_video_render_get_dis_rect;
	drm_render->base.set_on_off = drm_video_render_set_on_off;
	drm_render->base.get_on_off = drm_video_render_get_on_off;
	drm_render->base.get_screen_size = drm_get_screen_size;
	drm_render->base.rend_last_frame = drm_video_render_rend_last_frame;

	*render = &drm_render->base;

	logi("DRM atomic video render created successfully");
	return 0;
}
