/*
 * Copyright (C) 2020-2026 Artinchip Technology Co. Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 *  author: qi.xu@artinchip.com
 *  Desc: dma-buf allocator
 */

#ifndef DMA_ALLOCATOR_H
#define DMA_ALLOCATOR_H

#ifdef __cplusplus
extern "C" {
#endif

#include <video/mpp_types.h>

enum dma_buf_sync_flag {
	// clean. check dirty bit of cacheline. if the dirty bit is 1,
	// write the cacheline to dram, then set dirty bit 0
	CACHE_CLEAN = 0,

	// invalid. set the cacheline invalid
	CACHE_INVALID = 1,

	// flush. clean then invalid
	CACHE_FLUSH = 2,
};

/**
 * dmabuf_device_open - open dma-buf heap device
 *
 * return: fd of dma-buf heap device
 */
int dmabuf_device_open();

/**
 * dmabuf_device_close - close dma-buf heap device
 * @dma_fd: fd of dma-buf heap device
 */
void dmabuf_device_close(int dma_fd);

/**
 * dmabuf_device_destroy - destroy dma-buf heap
 * @dma_fd: fd of dma-buf heap device
 */
void dmabuf_device_destroy(int dma_fd);

/**
 * dmabuf_alloc - alloc a dma-buf
 * @dma_fd: fd of dma-buf heap device
 * @size: size of dma-buf
 *
 * return: error if < 0; else return fd of dma-buf
 */
int dmabuf_alloc(int dma_fd, int size);

/**
 * dmabuf_free - free a dma-buf
 * @buf_fd: fd of dma-buf
 */
void dmabuf_free(int buf_fd);

/**
 * dmabuf_mmap - mmap dma-buf to virtual space
 * @buf_fd: fd of dma-buf
 * @size: size of dma-buf
 * return virtual address of dma-buf
 */
unsigned char* dmabuf_mmap(int buf_fd, int size);

/**
 * dmabuf_munmap - munmap dma-buf from virtual space
 * @addr: virtual address of dma-buf
 * @size: size of dma-buf
 */
void dmabuf_munmap(unsigned char* addr, int size);

/**
 * dmabuf_sync - sync data for dma-buf
 * @buf_fd: fd of dma-buf
 * @flag: cache sync flag
 */
int dmabuf_sync(int buf_fd, enum dma_buf_sync_flag flag);

/**
 * dmabuf_sync - sync range data for dma-buf
 * @buf_fd: fd of dma-buf
 * @start_addr: the virtual address of the buffer need flush cache
 * @size: the size of the buffer need flush cache
 * @flag: cache sync flag
 */
int dmabuf_sync_range(int buf_fd, unsigned char* start_addr, int size, enum dma_buf_sync_flag flag);

/**
 * mpp_buf_alloc - alloc a mpp-buf, dma-buf size is stride[i]*height in struct mpp_buf
 * @dma_fd: fd of dma-buf heap device
 * @buf: mpp_buf
 */
int mpp_buf_alloc(int dma_fd, struct mpp_buf* buf);

/**
 * mpp_buf_free - free a mpp-buf
 * @buf: mpp_buf
 */
void mpp_buf_free(struct mpp_buf* buf);

/**
 * dmabuf_alloc_planar - Allocate planar buffers based on dimensions and format
 * @dma_fd: File descriptor of the DMA-BUF heap device
 * @width: Width of the buffer in pixels
 * @height: Height of the buffer in pixels
 * @format: Pixel format of the buffer
 * @stride: Array to store the stride of each plane, 8 byte alignment (output)
 * @fd: Array to store the DMA-BUF file descriptors for each plane (output)
 * @phy_addr: Array to store the physical addresses of each plane (output)
 *
 * This function allocates a set of planar buffers based on the given width, height,
 * and pixel format. It determines the number of planes required by the format and
 * automatically calculates the stride for each plane. The resulting strides, DMA-BUF
 * file descriptors, and physical addresses are returned through the respective output
 * arrays. This function also supports allocating buffers in RGB format. The allocated
 * buffers must be released using dmabuf_free().
 *
 * Return: 0 on success, -1 on failure.
 */
int dmabuf_alloc_planar(int dma_fd, unsigned int width, unsigned int height,
			enum mpp_pixel_format format, unsigned int stride[3],
			int fd[3], unsigned int phy_addr[3]);

#ifdef __cplusplus
}
#endif

#endif /* DMA_ALLOCATOR_H */
