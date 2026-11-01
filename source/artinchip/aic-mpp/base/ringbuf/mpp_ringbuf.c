/*
 * Copyright (C) 2020-2026 ArtInChip Technology Co. Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Author: <che.jiang@artinchip.com>
 * Desc: mpp ringbuffer - lightweight ring buffer based on lwrb
 */

#include "mpp_ringbuf.h"
#include "mpp_log.h"
#include "mpp_mem.h"
#include <inttypes.h>
#include <string.h>
#include <unistd.h>
#ifdef LWRB
#include <lwrb/lwrb.h>
#endif

struct mpp_ringbuf_handle {
#ifdef LWRB
	lwrb_t lwrb;
#endif
	unsigned char *buf_pool;
	int buf_len;
};

mpp_ringbuf_t mpp_ringbuffer_create(int length)
{
#ifdef LWRB
	struct mpp_ringbuf_handle *h_ringbuf = NULL;

	if (length <= 0) {
		loge("Invalid ringbuffer length: %d\n", length);
		return NULL;
	}

	h_ringbuf = (struct mpp_ringbuf_handle *)mpp_alloc(sizeof(struct mpp_ringbuf_handle));
	if (!h_ringbuf)
		return NULL;
	memset(h_ringbuf, 0, sizeof(struct mpp_ringbuf_handle));

	h_ringbuf->buf_pool = (unsigned char *)mpp_alloc(length);
	if (!h_ringbuf->buf_pool) {
		loge("ERROR: allocating buffer pool(%d) failed\n", length);
		mpp_free(h_ringbuf);
		return NULL;
	}
	memset(h_ringbuf->buf_pool, 0, length);
	lwrb_init(&h_ringbuf->lwrb, h_ringbuf->buf_pool, length);
	h_ringbuf->buf_len = length;

	return (mpp_ringbuf_t)h_ringbuf;
#else
	return NULL;
#endif
}

void mpp_ringbuffer_destroy(mpp_ringbuf_t rb)
{
#ifdef LWRB
	struct mpp_ringbuf_handle *h_ringbuf;
	if (!rb)
		return;

	h_ringbuf = (struct mpp_ringbuf_handle *)rb;
	if (h_ringbuf->buf_pool) {
		mpp_free(h_ringbuf->buf_pool);
		h_ringbuf->buf_pool = NULL;
	}
	mpp_free(rb);
#endif
}

void mpp_ringbuffer_reset(mpp_ringbuf_t rb)
{
#ifdef LWRB
	struct mpp_ringbuf_handle *h_ringbuf;
	if (!rb)
		return;

	h_ringbuf = (struct mpp_ringbuf_handle *)rb;
	lwrb_reset(&h_ringbuf->lwrb);
#endif
}

int mpp_ringbuffer_get_empty_space(mpp_ringbuf_t rb, unsigned char **addr, int *size)
{
#ifdef LWRB
	struct mpp_ringbuf_handle *h_ringbuf;
	if (!rb || !addr || !size) {
		return -1;
	}
	h_ringbuf = (struct mpp_ringbuf_handle *)rb;
	*addr = lwrb_get_linear_block_write_address(&h_ringbuf->lwrb);
	*size = lwrb_get_linear_block_write_length(&h_ringbuf->lwrb);

	if (*size <= 0 || *addr == NULL)
		return -1;
	return 0;
#endif
	return -1;
}

int mpp_ringbuffer_flush(mpp_ringbuf_t rb, int len)
{
#ifdef LWRB
	struct mpp_ringbuf_handle *h_ringbuf;
	if (!rb || len <= 0) {
		return -1;
	}
	h_ringbuf = (struct mpp_ringbuf_handle *)rb;

	if (lwrb_advance(&h_ringbuf->lwrb, len) != len) {
		loge("ERROR: advance failed\n");
		return -1;
	}
	return 0;
#endif
	return -1;
}

int mpp_ringbuffer_skip(mpp_ringbuf_t rb, int len)
{
#ifdef LWRB
	struct mpp_ringbuf_handle *h_ringbuf;
	if (!rb || len <= 0) {
		return -1;
	}
	h_ringbuf = (struct mpp_ringbuf_handle *)rb;
	return lwrb_skip(&h_ringbuf->lwrb, len);
#endif
	return -1;
}

int mpp_ringbuffer_put(mpp_ringbuf_t rb, const unsigned char *ptr, int length)
{
#ifdef LWRB
	struct mpp_ringbuf_handle *h_ringbuf;
	if (!rb || !ptr || length <= 0) {
		return -1;
	}
	h_ringbuf = (struct mpp_ringbuf_handle *)rb;
	size_t written = lwrb_write(&h_ringbuf->lwrb, ptr, length);
	return (int)written;
#endif
	return -1;
}

int mpp_ringbuffer_get(mpp_ringbuf_t rb, unsigned char *ptr, int length)
{
#ifdef LWRB
	struct mpp_ringbuf_handle *h_ringbuf;
	if (!rb || !ptr || length <= 0) {
		return -1;
	}
	h_ringbuf = (struct mpp_ringbuf_handle *)rb;
	size_t read = lwrb_read(&h_ringbuf->lwrb, ptr, length);
	return (int)read;
#endif
	return -1;
}

int mpp_ringbuffer_data_len(mpp_ringbuf_t rb)
{
#ifdef LWRB
	struct mpp_ringbuf_handle *h_ringbuf;
	if (!rb) {
		return -1;
	}
	h_ringbuf = (struct mpp_ringbuf_handle *)rb;
	return (int)lwrb_get_full(&h_ringbuf->lwrb);
#endif
	return -1;
}

int mpp_ringbuffer_space_len(mpp_ringbuf_t rb)
{
#ifdef LWRB
	struct mpp_ringbuf_handle *h_ringbuf;
	if (!rb) {
		return -1;
	}
	h_ringbuf = (struct mpp_ringbuf_handle *)rb;
	return (int)lwrb_get_free(&h_ringbuf->lwrb);
#endif
	return -1;
}
