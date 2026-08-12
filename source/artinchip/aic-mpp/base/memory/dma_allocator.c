/*
 * Copyright (C) 2020-2026 Artinchip Technology Co. Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 *  author: qi.xu@artinchip.com
 *  Desc: dma-buf allocator
 */

#define LOG_TAG "dma_allocator"

#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <linux/dma-buf.h>
#include <linux/dma-heap.h>

#include "dma_allocator.h"
#include "mpp_log.h"

#define DMABUF_DEV  "/dev/dma_heap/mpp"
#define ALIGN_8B(x) (((x) + (7)) & ~(7))
#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]))

struct mpp_fmt_info{
	unsigned int format;
	unsigned int plane_num;
	unsigned int y_stride_shift;
	unsigned int uv_stride_shift;
	unsigned int uv_mem_shift;
	bool is_rgb;
};

static const struct mpp_fmt_info formats_info[] = {
	{MPP_FMT_YUV420P, 3, 0, 1, 2, false},
	{MPP_FMT_YUV422P, 3, 0, 1, 1, false},
	{MPP_FMT_YUV444P, 3, 0, 0, 0, false},
	{MPP_FMT_NV12, 2, 0, 1, 1, false},
	{MPP_FMT_NV21, 2, 0, 1, 1, false},
	{MPP_FMT_NV16, 2, 0, 1, 0, false},
	{MPP_FMT_NV61, 2, 0, 1, 0, false},

	{MPP_FMT_YUV400, 1, 0, 0, 0, false},

	{MPP_FMT_ABGR_8888, 1, 4, 0, 0, true},
	{MPP_FMT_ARGB_8888, 1, 4, 0, 0, true},
	{MPP_FMT_RGBA_8888, 1, 4, 0, 0, true},
	{MPP_FMT_BGRA_8888, 1, 4, 0, 0, true},
	{MPP_FMT_BGR_888, 1, 3, 0, 0, true},
	{MPP_FMT_RGB_888, 1, 3, 0, 0, true},
	{MPP_FMT_BGR_565, 1, 2, 0, 0, true},
	{MPP_FMT_RGB_565, 1, 2, 0, 0, true},

	{MPP_FMT_MAX, 0, 0, 0, 0, false},
};

int dmabuf_device_open()
{
	int dma_fd;

	dma_fd = open(DMABUF_DEV, O_RDWR);
	if (dma_fd < 0) {
		loge("open %s failed!", DMABUF_DEV);
		return -1;
	}

	return dma_fd;
}

void dmabuf_device_close(int dma_fd)
{
	if(dma_fd < 0)
		return;

	close(dma_fd);
}

#ifndef LINUX_VERSION_6
void dmabuf_device_destroy(int dma_fd)
{
	int zero;

	if (dma_fd < 0)
		return;

	if (ioctl(dma_fd, DMA_HEAP_IOCTL_DESTROY, &zero) < 0)
		loge("dmabuf heap destroy failed");
}
#endif

int dmabuf_alloc(int dma_fd, int size)
{
	int ret;
	struct dma_heap_allocation_data data = {0};

	data.fd = 0;
	data.len = size;
	data.fd_flags = O_RDWR | O_CLOEXEC;
	data.heap_flags = 0;
	ret = ioctl(dma_fd, DMA_HEAP_IOCTL_ALLOC, &data);
	if (ret < 0) {
		loge("dmabuf alloc failed, ret %d", ret);
		return ret;
	}

	return data.fd;
}

void dmabuf_free(int buf_fd)
{
	if(buf_fd < 0)
		return;

	close(buf_fd);
}

unsigned char* dmabuf_mmap(int buf_fd, int size)
{
	unsigned char* vir_addr = NULL;

	vir_addr = mmap(NULL, size, PROT_READ|PROT_WRITE, MAP_SHARED, buf_fd, 0);
	if (vir_addr == MAP_FAILED) {
		loge("dmabuf mmap failed!");
		return NULL;
	}

	return vir_addr;
}

void dmabuf_munmap(unsigned char* addr, int size)
{
	munmap(addr, size);
}

int dmabuf_sync(int buf_fd, enum dma_buf_sync_flag flag)
{
	/*
	sync.flag:
		DMA_BUF_SYNC_RW:    DMA_BIDIRECTIONAL
		DMA_BUF_SYNC_WRITE: DMA_TO_DEVICE
		DMA_BUF_SYNC_READ:  DMA_FROM_DEVICE

		DMA_BUF_SYNC_START: invalid
		DMA_BUF_SYNC_END: clean
	*/
	struct dma_buf_sync sync = {0};
	if(flag == CACHE_CLEAN) {
		sync.flags = DMA_BUF_SYNC_WRITE | DMA_BUF_SYNC_END;
	} else if (flag == CACHE_INVALID) {
		sync.flags = DMA_BUF_SYNC_READ | DMA_BUF_SYNC_START;
	} else if (flag == CACHE_FLUSH) {
		sync.flags = DMA_BUF_SYNC_RW | DMA_BUF_SYNC_END;
	} else {
		logw("unkown cache flag: %d", flag);
		return -1;
	}

	return ioctl(buf_fd, DMA_BUF_IOCTL_SYNC, &sync);
}

int dmabuf_sync_range(int buf_fd, unsigned char* start, int size, enum dma_buf_sync_flag flag)
{
#ifdef LINUX_VERSION_6
	(void)start;
	(void)size;
	dmabuf_sync(buf_fd, flag);
#else
	struct dma_buf_range sync = {0};

	sync.start = (unsigned long)start;
	sync.size = size;
	if(flag == CACHE_CLEAN) {
		sync.flags = DMA_BUF_SYNC_WB_RANGE;
	} else if (flag == CACHE_INVALID) {
		sync.flags = DMA_BUF_SYNC_INV_RANGE;
	} else {
		logw("unkown cache flag: %d", flag);
		return -1;
	}

	int ret = ioctl(buf_fd, DMA_BUF_IOCTL_SYNC_RANGE, &sync);
	if(ret < 0)
		loge("dmabuf_sync_range ret: %d", ret);
#endif
	return 0;
}

static int get_info(struct mpp_buf* buf, int *comp, int *mem_size)
{
	int height = buf->size.height;

	mem_size[0] = height * buf->stride[0];
	switch(buf->format) {
	case MPP_FMT_YUV420P:
		*comp = 3;
		mem_size[1] = mem_size[2] = mem_size[0] >> 2;
		break;
	case MPP_FMT_YUV444P:
		*comp = 3;
		mem_size[1] = mem_size[2] = mem_size[0];
		break;

	case MPP_FMT_YUV422P:
		*comp = 3;
		mem_size[1] = mem_size[2] = mem_size[0] >> 1;
		break;
	case MPP_FMT_NV12:
	case MPP_FMT_NV21:
		*comp = 2;
		mem_size[1] = mem_size[0] >> 1;
		break;
	case MPP_FMT_YUV400:
	case MPP_FMT_ABGR_8888:
	case MPP_FMT_ARGB_8888:
	case MPP_FMT_RGBA_8888:
	case MPP_FMT_BGRA_8888:
	case MPP_FMT_BGR_888:
	case MPP_FMT_RGB_888:
	case MPP_FMT_BGR_565:
	case MPP_FMT_RGB_565:
		*comp = 1;
		break;

	default:
		loge("pixel format not support %d", buf->format);
		return -1;
	}

	return 0;
}

int mpp_buf_alloc(int dma_fd, struct mpp_buf* buf)
{
	int i;
	int comp = 0;
	int mem_size[3] = {0, 0, 0};

	buf->buf_type = MPP_DMA_BUF_FD;
	buf->fd[0] = buf->fd[1] = buf->fd[2] = -1;

	get_info(buf, &comp, mem_size);

	for (i = 0; i < comp; i++) {
		buf->fd[i] = dmabuf_alloc(dma_fd, mem_size[i]);
		if(buf->fd[i] < 0) {
			loge("dmabuf(%d) alloc failed, need %d bytes", i, mem_size[i]);
			goto failed;
		}

		if (ioctl(buf->fd[i], DMA_BUF_IOCTL_GET_PHY_ADDR, &buf->phy_addr[i]) < 0)
			loge("dmabuf(%d) get phy addr failed, use fd type", buf->fd[i]);
		else
			buf->buf_type = MPP_PHY_ADDR;
	}

	return 0;

failed:
	for (i = 0; i < comp; i++) {
		if(buf->fd[i] != -1) {
			dmabuf_free(buf->fd[i]);
		}
	}
	return -1;
}

void mpp_buf_free(struct mpp_buf* buf)
{
	int i;
	int comp = 0;
	int mem_size[3] = {0, 0, 0};
	get_info(buf, &comp, mem_size);

	for (i = 0; i < comp; i++) {
		dmabuf_free(buf->fd[i]);
	}
}

int dmabuf_alloc_planar(int dma_fd, unsigned int width, unsigned int height,
			enum mpp_pixel_format format, unsigned int stride[3],
			int fd[3], unsigned int phy_addr[3])
{
	const struct mpp_fmt_info *mpp_fmt = NULL;
	unsigned int mem_size[3] = {0, 0, 0};
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(formats_info); i++) {
		if (formats_info[i].format == format) {
			mpp_fmt = &formats_info[i];
			break;
		}
	}

	if (!mpp_fmt) {
		loge("unsupported format %d", format);
		return -1;
	}

	fd[0] = fd[1] = fd[2] = -1;
	phy_addr[0] = phy_addr[1] = phy_addr[2] = 0;

	stride[0] = mpp_fmt->is_rgb ? ALIGN_8B(width * mpp_fmt->y_stride_shift)
				    : ALIGN_8B(width >> mpp_fmt->y_stride_shift);
	mem_size[0] = stride[0] * height;

	for (i = 0; i < mpp_fmt->plane_num; i++) {
		if(i != 0) {
			stride[i] = stride[0] >> mpp_fmt->uv_stride_shift;
			mem_size[i] = mem_size[0] >> mpp_fmt->uv_mem_shift;
		}

		fd[i] = dmabuf_alloc(dma_fd, mem_size[i]);
		if(fd[i] < 0) {
			loge("dmabuf(%d) alloc failed, need %d bytes", i, mem_size[i]);
			goto failed;
		}

		if (ioctl(fd[i], DMA_BUF_IOCTL_GET_PHY_ADDR, &phy_addr[i]) < 0) {
			loge("dmabuf(%d) get phy addr failed", fd[i]);
			goto failed;
		}
	}

	logd("dmabuf format(%d) %d plane (%d, %d, %d) bytes",
		format, mpp_fmt->plane_num, mem_size[0], mem_size[1], mem_size[2]);

	return 0;

failed:
	for (i = 0; i < mpp_fmt->plane_num; i++) {
		if(fd[i] != -1)
			dmabuf_free(fd[i]);
	}

	return -1;
}
