/*
 * Copyright (C) 2020-2025 ArtInChip Technology Co. Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 *  author: <jun.ma@artinchip.com>
 *  Desc: aic_video_render
 */
#include <stdlib.h>
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/types.h>
#include <linux/fb.h>
#include <video/artinchip_fb.h>
#include <sys/ioctl.h>

#include "mpp_mem.h"
#include "mpp_log.h"
#include "mpp_list.h"
#include "aic_render.h"
#include "dma_allocator.h"
#include "mpp_ge.h"

#define DEV_FB "/dev/fb0"

struct frame_dma_buf_info {
	s32 frame_id;
	s32 fd[3];		// dma-buf fd
	s32 fd_num;		// number of dma-buf
	s32 used;		// ifthe dma-buf of this frame add to de drive
};

struct frame_dma_buf_info_list {
	struct frame_dma_buf_info dma_buf_info;
	struct mpp_list list;
};

struct aic_fb_video_render {
	struct aic_video_render base;
	struct aicfb_layer_data layer;
	s32 fd;
	struct mpp_list dma_list;
};

struct aic_fb_video_render_last_frame {
	struct aicfb_layer_data layer;
	struct mpp_frame frame;
	s32 enable;
	s32 dma_fd;
};

static struct aic_fb_video_render_last_frame g_last_frame = {0};

static s32 fb_video_render_init(struct aic_video_render *render,s32 layer,s32 dev_id)
{
	struct aic_fb_video_render *fb_render = (struct aic_fb_video_render*)render;
	fb_render->fd = open(DEV_FB, O_RDWR);
	if (fb_render->fd < 0) {
		loge("open /dev/fb0 failed!");
		return -1;
	}
	fb_render->layer.layer_id = layer;
	ioctl(fb_render->fd, AICFB_GET_LAYER_CONFIG, &fb_render->layer);

	if (!g_last_frame.enable) {
		fb_render->layer.enable = 0;
		ioctl(fb_render->fd, AICFB_UPDATE_LAYER_CONFIG, &fb_render->layer);
	}

	return 0;
}

static s32 fb_video_render_destroy(struct aic_video_render *render)
{
	struct aic_fb_video_render *fb_render = (struct aic_fb_video_render*)render;
	struct dma_buf_info dmabuf_fd[3];
	int i = 0;

	if (!g_last_frame.enable) {
		fb_render->layer.enable = 0;
		if (ioctl(fb_render->fd, AICFB_UPDATE_LAYER_CONFIG, &fb_render->layer) < 0)
			loge("fb ioctl() AICFB_UPDATE_LAYER_CONFIG failed!");
	}

	if (!mpp_list_empty(&fb_render->dma_list)) {
		struct frame_dma_buf_info_list *dma_buf_node = NULL,*dma_buf_node1 = NULL;
		mpp_list_for_each_entry_safe(dma_buf_node,dma_buf_node1, &fb_render->dma_list, list) {
			mpp_list_del(&dma_buf_node->list);
			if (dma_buf_node->dma_buf_info.used == 1) {
				for(i = 0;i < dma_buf_node->dma_buf_info.fd_num;i++) {
					dmabuf_fd[i].fd =dma_buf_node->dma_buf_info.fd[i];
					ioctl(fb_render->fd, AICFB_RM_DMABUF, &dmabuf_fd[i]);
					logd("AICFB_RM_DMABUF frame_id:%d\n",dmabuf_fd[i].fd);
				}
			}
			mpp_free(dma_buf_node);
		}
	}

	if (fb_render->fd > 0) {
		close(fb_render->fd);
	}

	mpp_free(fb_render);
	return 0;
}

static s32 get_component_num(enum mpp_pixel_format format)
{
	int component_num = 0;
	if (format == MPP_FMT_ARGB_8888) {
		component_num = 1;
	} else if (format == MPP_FMT_RGBA_8888) {
		component_num = 1;
	} else if (format == MPP_FMT_RGB_888) {
		component_num = 1;
	} else if (format == MPP_FMT_YUV420P) {
		component_num = 3;
	} else if (format == MPP_FMT_NV12 || format == MPP_FMT_NV21) {
		component_num = 2;
	} else if (format == MPP_FMT_YUV444P) {
		component_num = 3;
	} else if (format == MPP_FMT_YUV422P) {
		 component_num = 3;
	} else if (format == MPP_FMT_YUV400) {
		component_num = 1;
	} else {
		loge("no support picture foramt %d, default argb8888", format);
	}
	return component_num;
}

static s32 fb_video_render_rend(struct aic_video_render *render,struct mpp_frame *frame_info)
{
	struct aic_fb_video_render *fb_render = (struct aic_fb_video_render*)render;
	s32 i;
	int dmabuf_num = 0;
	s32 find = 0;
	struct dma_buf_info dmabuf_fd[3];
	struct frame_dma_buf_info_list * dma_buf_node = NULL,*dma_buf_node1 = NULL;

	if (frame_info == NULL) {
		loge("frame_info=NULL\n");
		return -1;
	}
	dmabuf_num = get_component_num(frame_info->buf.format);
	if (!mpp_list_empty(&fb_render->dma_list)) {
		mpp_list_for_each_entry_safe(dma_buf_node, dma_buf_node1, &fb_render->dma_list, list) {
			if (dmabuf_num == 1) {
				if (dma_buf_node->dma_buf_info.fd[0] == frame_info->buf.fd[0]) {
					find = 1;
					break;
				}
			} else if (dmabuf_num == 2) {
				if (dma_buf_node->dma_buf_info.fd[0] == frame_info->buf.fd[0]
					&& dma_buf_node->dma_buf_info.fd[1] == frame_info->buf.fd[1]) {
					find = 1;
					break;
				}
			} else if (dmabuf_num == 3) {
				if (dma_buf_node->dma_buf_info.fd[0] == frame_info->buf.fd[0]
					&& dma_buf_node->dma_buf_info.fd[1] == frame_info->buf.fd[1]
					&& dma_buf_node->dma_buf_info.fd[2] == frame_info->buf.fd[2]) {
					find = 1;
					break;
				}
			} else {
				loge("no support picture foramt %d", frame_info->buf.format);
				return -1;
			}
		}
	}

	if (find == 0) {
		dma_buf_node = (struct frame_dma_buf_info_list *)mpp_alloc(sizeof(struct frame_dma_buf_info_list));
		memset(dma_buf_node,0x00,sizeof(struct frame_dma_buf_info_list));
		dma_buf_node->dma_buf_info.frame_id = frame_info->id;
		printf("[%s:%d]%d,%u\n",__FUNCTION__,__LINE__,dma_buf_node->dma_buf_info.frame_id,frame_info->id);
		dma_buf_node->dma_buf_info.used = 0;
		for(i=0; i<dmabuf_num; i++) {
			dma_buf_node->dma_buf_info.fd[i] = frame_info->buf.fd[i];
			dmabuf_fd[i].fd = frame_info->buf.fd[i];
			if (ioctl(fb_render->fd, AICFB_ADD_DMABUF, &dmabuf_fd[i]) < 0) {
				loge("AICFB_ADD_DMABUF fd:%d failed!",dmabuf_fd[i].fd);
			} else {
				logw("AICFB_ADD_DMABUF fd:%d ok!",dmabuf_fd[i].fd);
			}
		}
		dma_buf_node->dma_buf_info.fd_num = dmabuf_num;
		dma_buf_node->dma_buf_info.used = 1;
		mpp_list_add_tail(&dma_buf_node->list, &fb_render->dma_list);
	}

	fb_render->layer.enable = 1;
	memcpy(&fb_render->layer.buf,&frame_info->buf,sizeof(struct mpp_buf));

	if (ioctl(fb_render->fd, AICFB_UPDATE_LAYER_CONFIG, &fb_render->layer) < 0)
		loge("fb ioctl() AICFB_UPDATE_LAYER_CONFIG failed!");

	// wait vsync (wait layer config)
	ioctl(fb_render->fd, AICFB_WAIT_FOR_VSYNC, NULL);

	return 0;
}

static s32 get_screen_size(struct aic_video_render *render,struct mpp_size *size)
{
	s32 ret = 0;
	struct aic_fb_video_render *fb_render = (struct aic_fb_video_render*)render;
	if (size == NULL) {
		loge("bad params!!!!\n");
		return -1;
	}
	if (ioctl(fb_render->fd, AICFB_GET_SCREEN_SIZE, size) < 0) {
		loge("fb ioctl() AICFB_GET_SCREEN_SIZE failed!");
		return -1;
	}
	return ret;
}

static s32 fb_video_render_set_dis_rect(struct aic_video_render *render,struct mpp_rect *rect)
{
	struct aic_fb_video_render *fb_render = (struct aic_fb_video_render*)render;
	if (rect == NULL) {
		loge("param error rect==NULL\n");
		return -1;
	}
	fb_render->layer.pos.x = rect->x;
	fb_render->layer.pos.y = rect->y;
	fb_render->layer.scale_size.width = rect->width;
	fb_render->layer.scale_size.height = rect->height;
	return 0;
}

static s32 fb_video_render_get_dis_rect(struct aic_video_render *render,struct mpp_rect *rect)
{
	struct aic_fb_video_render *fb_render = (struct aic_fb_video_render*)render;
	if (rect == NULL) {
		loge("param error rect==NULL\n");
		return -1;
	}
	rect->x = fb_render->layer.pos.x;
	rect->y = fb_render->layer.pos.y;
	rect->width = fb_render->layer.scale_size.width;
	rect->height = fb_render->layer.scale_size.height;
	return 0;
}

static s32 fb_video_render_set_on_off(struct aic_video_render *render,s32 enable)
{
	struct aic_fb_video_render *fb_render = (struct aic_fb_video_render*)render;
	fb_render->layer.enable = enable;
	return 0;
}

static s32 fb_video_render_get_on_off(struct aic_video_render *render,s32 *enable)
{
	struct aic_fb_video_render *fb_render = (struct aic_fb_video_render*)render;
	if (enable == NULL) {
		loge("param error rect==NULL\n");
		return -1;
	}
	*enable = fb_render->layer.enable;
	return 0;
}

static int fb_video_render_last_frame_alloc(struct mpp_frame *frame)
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



static s32 fb_video_render_last_frame_free(struct mpp_frame* frame, int dma_fd)
{
	if (frame == NULL || dma_fd <= 0) {
		loge("frame free error frame:%p, dma_fd:%d.\n", frame, dma_fd);
		return -1;
	}

	mpp_buf_free(&frame->buf);

	dmabuf_device_close(dma_fd);
	return 0;
}


static s32 fb_video_render_last_frame_copy(struct mpp_buf *src_buf, struct mpp_frame *dest_frame)
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

static s32 fb_video_render_rend_last_frame(struct aic_video_render *render, s32 enable)
{
	s32 ret = 0;
	struct mpp_frame cur_frame;
	struct aic_fb_video_render *fb_render = (struct aic_fb_video_render *)render;

	if (fb_render == NULL) {
		loge("fb_render is not initialization!");
		return -1;
	}

	if (enable) {
		/*step1: malloc empty for last frame*/
		memcpy(&cur_frame.buf, &fb_render->layer.buf, sizeof(struct mpp_buf));

		/*the first frame need malloc buf for last frame*/
		if (!g_last_frame.enable) {
			logi("%s:alloc last frame firsttime, enable:%d.\n", __func__, enable);
			memset(&g_last_frame.frame, 0, sizeof(struct mpp_frame));
			g_last_frame.dma_fd = fb_video_render_last_frame_alloc(&cur_frame);
			if (g_last_frame.dma_fd < 0) {
				loge("fb_video_render_last_frame_alloc failed.\n");
				return -1;
			}
			memcpy(&g_last_frame.frame, &cur_frame, sizeof(struct mpp_frame));
		} else {
			/*the frame resolution changed need realloc buf for last frame*/
			if ((g_last_frame.frame.buf.size.height != cur_frame.buf.size.height) ||
				(g_last_frame.frame.buf.size.width != cur_frame.buf.size.width)) {
				logi("free last frame and alloc next frame.\n");
				fb_video_render_last_frame_free(&g_last_frame.frame, g_last_frame.dma_fd);

				g_last_frame.dma_fd = fb_video_render_last_frame_alloc(&cur_frame);
				if (g_last_frame.dma_fd < 0) {
					loge("fb_video_render_last_frame_alloc failed.\n");
					return -1;
				}
				memcpy(&g_last_frame.frame, &cur_frame, sizeof(struct mpp_frame));
			}
		}
		logi("fb_video_render_last_frame_copy.\n");
		/*step2: copy frame data to last frame*/
		fb_video_render_last_frame_copy(&fb_render->layer.buf, &g_last_frame.frame);

		/*step3: display last frame*/
		fb_video_render_rend(render, &g_last_frame.frame);
	} else {
		if (g_last_frame.enable) {
			logi("%s:reclaim the final last frame, enable:%d.\n", __func__, enable);
			/*reclaim the end last frame*/
			ret = fb_video_render_last_frame_free(&g_last_frame.frame, g_last_frame.dma_fd);
			if (ret) {
				loge("fb_video_render_last_frame_free error %d.", ret);
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
	struct aic_fb_video_render * fb_render;
	fb_render = mpp_alloc(sizeof(struct aic_fb_video_render));
	if (fb_render == NULL) {
		loge("mpp_alloc fb_render fail!!!\n");
		*render = NULL;
		return -1;
	}

	memset(fb_render,0x00,sizeof(struct aic_fb_video_render));
	mpp_list_init(&fb_render->dma_list);
	fb_render->base.init = fb_video_render_init;
	fb_render->base.destroy = fb_video_render_destroy;
	fb_render->base.rend = fb_video_render_rend;
	fb_render->base.set_dis_rect = fb_video_render_set_dis_rect;
	fb_render->base.get_dis_rect = fb_video_render_get_dis_rect;
	fb_render->base.set_on_off = fb_video_render_set_on_off;
	fb_render->base.get_on_off = fb_video_render_get_on_off;
	fb_render->base.get_screen_size = get_screen_size;
	fb_render->base.rend_last_frame = fb_video_render_rend_last_frame;

	*render = &fb_render->base;
	return 0;
}

