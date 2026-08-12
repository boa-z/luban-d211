/*
 * Copyright (C) 2020-2026 ArtInChip Technology Co. Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 *  author: <qi.xu@artinchip.com>
 *  Desc:  encoder packet (video bitstream container) manager
 */

#define LOG_TAG "enc_packet_manager"

#include <string.h>
#include <pthread.h>

#include "mpp_dec_type.h"
#include "mpp_mem.h"
#include "ve_buffer.h"
#include "enc_packet_manager.h"
#include "mpp_list.h"
#include "mpp_log.h"

struct enc_packet_impl {
	struct enc_packet pkt;
	struct mpp_list list;
};

struct enc_packet_manager {
	int packet_count;
	int empty_num;
	int ready_num;

	pthread_mutex_t lock;
	struct mpp_list empty_list;
	struct mpp_list ready_list;
	struct enc_packet_impl *packet_node;

	struct ve_buffer *mpp_buf;
	struct ve_buffer_allocator *ve_buf_handle;

	unsigned int buffer_phy;		// phy start addr of stream buffer
	unsigned char *buffer_start;		// virtual start addr of stream buffer
	unsigned char *buffer_end;		// virtual end addr of stream buffer
	int buffer_size;			// total size of stream buffer

	int write_offset;			// offset where encoder writes next packet
	int read_offset;			// offset where reader reads next packet
	int available_size;			// available size in buffer
};

struct enc_packet_manager *enc_pm_create(struct enc_packet_manager_init_cfg *cfg)
{
	struct enc_packet_manager_init_cfg *init_cfg = cfg;
	struct enc_packet_manager *pm;
	struct enc_packet_impl *pkt_impl;
	int i;

	logd("create encoder packet manager");

	if (!init_cfg || !init_cfg->ve_buf_handle || init_cfg->packet_count <= 0 || init_cfg->buffer_size <= 0)
		return NULL;

	pm = (struct enc_packet_manager *)mpp_alloc(sizeof(struct enc_packet_manager));
	if (!pm)
		return NULL;
	memset(pm, 0, sizeof(struct enc_packet_manager));

	pm->packet_count = init_cfg->packet_count;
	pm->empty_num = init_cfg->packet_count;
	pm->ready_num = 0;

	pm->packet_node = (struct enc_packet_impl *)mpp_alloc(pm->packet_count * sizeof(struct enc_packet_impl));
	if (!pm->packet_node) {
		loge("alloc %d count packet failed!", pm->packet_count);
		mpp_free(pm);
		return NULL;
	}
	memset(pm->packet_node, 0, pm->packet_count * sizeof(struct enc_packet_impl));

	pm->ve_buf_handle = init_cfg->ve_buf_handle;
	pm->buffer_size = init_cfg->buffer_size;
	pm->mpp_buf = ve_buffer_alloc(pm->ve_buf_handle, pm->buffer_size, ALLOC_NEED_VIR_ADDR);
	if (!pm->mpp_buf) {
		loge("alloc mpp buffer %d failed!", pm->buffer_size);
		mpp_free(pm->packet_node);
		mpp_free(pm);
		return NULL;
	}

	logd("encoder packet manager create %d count packet, buffer size %d", pm->packet_count, pm->buffer_size);

	pm->buffer_start = pm->mpp_buf->vir_addr;
	pm->buffer_end = pm->mpp_buf->vir_addr + pm->mpp_buf->size - 1;
	pm->buffer_size = pm->mpp_buf->size;
	pm->buffer_phy = pm->mpp_buf->phy_addr;

	pm->read_offset = 0;
	pm->write_offset = 0;
	pm->available_size = pm->mpp_buf->size;

	pthread_mutex_init(&pm->lock, NULL);
	mpp_list_init(&pm->empty_list);
	mpp_list_init(&pm->ready_list);

	pkt_impl = pm->packet_node;
	for (i = 0; i < pm->packet_count; i++) {
		mpp_list_init(&pkt_impl->list);
		mpp_list_add_tail(&pkt_impl->list, &pm->empty_list);
		pkt_impl++;
	}

	logd("create encoder packet manager successful! (%p)", pm);

	return pm;
}

int enc_pm_destroy(struct enc_packet_manager *pm)
{
	logd("destroy encoder packet manager");

	if (!pm)
		return -1;

	pthread_mutex_destroy(&pm->lock);

	if (pm->mpp_buf) {
		ve_buffer_free(pm->ve_buf_handle, pm->mpp_buf);
	}

	if (pm->packet_node)
		mpp_free(pm->packet_node);

	mpp_free(pm);

	return 0;
}

int enc_packet_flush_cache(struct enc_packet_manager *pm,
	unsigned char* buf, int len, enum dma_buf_sync_flag flag)
{
	return ve_buffer_sync_range(pm->mpp_buf, buf, len, flag);
}

int enc_pm_dequeue_empty_packet(struct enc_packet_manager *pm, struct enc_packet *packet, size_t size)
{
	struct enc_packet_impl *pkt_impl;
	int left_size;
	size_t aligned_size;

	if (!pm || !packet || size <= 0)
		return -1;

	// align size to 512 bytes
	aligned_size = (size + 511) & ~511;

	logd("encoder packet manager dequeue empty packet, pm: %p, packet: %p, size: %zu, aligned_size: %zu",
		pm, packet, size, aligned_size);

	pthread_mutex_lock(&pm->lock);

	if (pm->available_size < aligned_size) {
		logw("encoder packet manager dequeue packet aligned_size %zu > available size %d", aligned_size, pm->available_size);
		pthread_mutex_unlock(&pm->lock);
		return -1;
	}

	left_size = pm->buffer_size - pm->write_offset;

	if (pm->write_offset >= pm->read_offset) {
		if (left_size < aligned_size && pm->read_offset < aligned_size) {
			logw("encoder packet manager dequeue packet aligned_size(%zu) failed, read offset %d write offset %d",
				aligned_size, pm->read_offset, pm->write_offset);
			pthread_mutex_unlock(&pm->lock);
			return -1;
		}
	}

	pkt_impl = mpp_list_first_entry_or_null(&pm->empty_list, struct enc_packet_impl, list);
	if (!pkt_impl) {
		logw("encoder packet manager dequeue empty packet failed, no empty packet available!");
		pthread_mutex_unlock(&pm->lock);
		return -1;
	}

	pkt_impl->pkt.size = aligned_size;
	pkt_impl->pkt.len = 0;
	pkt_impl->pkt.phy_base = pm->buffer_phy;

	if (pm->write_offset >= pm->read_offset) {
		if (left_size >= aligned_size) {
			pkt_impl->pkt.data = pm->buffer_start + pm->write_offset;
			pkt_impl->pkt.phy_offset = pm->write_offset;
			pm->write_offset += aligned_size;
			pm->available_size -= aligned_size;

			if (pm->write_offset == pm->buffer_size)
				pm->write_offset = 0;
		} else if (pm->read_offset >= aligned_size) {
			logi("wrap around: left_size: %d, aligned_size: %zu", left_size, aligned_size);
			pkt_impl->pkt.data = pm->buffer_start;
			pkt_impl->pkt.phy_offset = 0;
			pm->write_offset = aligned_size;
			pm->available_size -= (aligned_size + left_size);
		} else {
			logw("encoder packet manager dequeue packet failed! left_size: %d, read_offset: %d, aligned_size: %zu",
				left_size, pm->read_offset, aligned_size);
			pthread_mutex_unlock(&pm->lock);
			return -1;
		}
	} else {
		pkt_impl->pkt.data = pm->buffer_start + pm->write_offset;
		pkt_impl->pkt.phy_offset = pm->write_offset;
		pm->write_offset += aligned_size;
		pm->available_size -= aligned_size;
	}

	logi("get empty packet phy_offset: %zu, aligned_size: %zu", pkt_impl->pkt.phy_offset, aligned_size);

	packet->data = pkt_impl->pkt.data;
	packet->size = pkt_impl->pkt.size;
	packet->phy_offset = pkt_impl->pkt.phy_offset;
	packet->phy_base = pkt_impl->pkt.phy_base;
	packet->len = 0;
	packet->pts = 0;
	packet->flag = 0;

	pm->empty_num--;

	pthread_mutex_unlock(&pm->lock);

	return 0;
}

int enc_pm_enqueue_ready_packet(struct enc_packet_manager *pm, struct enc_packet *packet)
{
	struct enc_packet_impl *pkt_impl = NULL;
	int found = 0;

	logd("encoder packet manager enqueue ready packet");

	if (!pm || !packet)
		return -1;

	pthread_mutex_lock(&pm->lock);

	// find the packet_impl that matches this packet
	struct enc_packet_impl *iter;
	mpp_list_for_each_entry(iter, &pm->empty_list, list) {
		if (iter->pkt.data == packet->data) {
			pkt_impl = iter;
			found = 1;
			break;
		}
	}

	if (!found || !pkt_impl) {
		logw("encoder packet manager enqueue ready packet failed, packet not found in empty list!");
		pthread_mutex_unlock(&pm->lock);
		return -1;
	}

	// update packet info with actual encoded data
	pkt_impl->pkt.len = packet->len;
	pkt_impl->pkt.pts = packet->pts;
	pkt_impl->pkt.flag = packet->flag;

	// move from empty list to ready list
	mpp_list_del_init(&pkt_impl->list);
	mpp_list_add_tail(&pkt_impl->list, &pm->ready_list);
	pm->ready_num++;

	// sync cache for the encoded data
	if (pkt_impl->pkt.len > 0) {
		ve_buffer_sync_range(pm->mpp_buf, pkt_impl->pkt.data, pkt_impl->pkt.len, CACHE_INVALID);
	}

	pthread_mutex_unlock(&pm->lock);

	return 0;
}

int enc_pm_dequeue_ready_packet(struct enc_packet_manager *pm, struct mpp_packet *packet)
{
	struct enc_packet_impl *pkt_impl;

	logd("encoder packet manager dequeue ready packet");

	if (!pm || !packet)
		return -1;

	pthread_mutex_lock(&pm->lock);

	pkt_impl = mpp_list_first_entry_or_null(&pm->ready_list, struct enc_packet_impl, list);
	if (!pkt_impl) {
		logd("encoder packet manager dequeue ready packet failed, no ready packet available!");
		pthread_mutex_unlock(&pm->lock);
		return -1;
	}

	// fill mpp_packet with encoded data info
	packet->data = pkt_impl->pkt.data;
	// real size of encoded data
	packet->size = pkt_impl->pkt.len;
	packet->pts = pkt_impl->pkt.pts;
	packet->flag = pkt_impl->pkt.flag;

	pm->ready_num--;

	pthread_mutex_unlock(&pm->lock);

	return 0;
}

int enc_pm_enqueue_empty_packet(struct enc_packet_manager *pm, struct mpp_packet *packet)
{
	struct enc_packet_impl *pkt_impl = NULL;
	int found = 0;
	size_t read_offset;

	logd("encoder packet manager enqueue empty packet");

	if (!pm || !packet)
		return -1;

	pthread_mutex_lock(&pm->lock);

	// find the packet_impl that matches this packet
	struct enc_packet_impl *iter;
	mpp_list_for_each_entry(iter, &pm->ready_list, list) {
		if (iter->pkt.data == packet->data) {
			pkt_impl = iter;
			found = 1;
			break;
		}
	}

	if (!found || !pkt_impl) {
		logw("encoder packet manager enqueue empty packet failed, packet not found in ready list!");
		pthread_mutex_unlock(&pm->lock);
		return -1;
	}

	// move from ready list to empty list
	mpp_list_del_init(&pkt_impl->list);
	mpp_list_add_tail(&pkt_impl->list, &pm->empty_list);

	// update read_offset and available_size
	read_offset = pkt_impl->pkt.phy_offset + pkt_impl->pkt.size;
	if (read_offset >= pm->buffer_size)
		read_offset -= pm->buffer_size;

	pm->read_offset = read_offset;
	if (pm->read_offset == pm->write_offset) {
		// buffer is empty, reset offsets
		pm->read_offset = 0;
		pm->write_offset = 0;
		pm->available_size = pm->buffer_size;
	} else {
		pm->available_size += pkt_impl->pkt.size;
	}

	pm->empty_num++;

	pthread_mutex_unlock(&pm->lock);

	return 0;
}

int enc_pm_get_empty_packet_num(struct enc_packet_manager *pm)
{
	if (!pm)
		return -1;

	return pm->empty_num;
}

int enc_pm_get_ready_packet_num(struct enc_packet_manager *pm)
{
	if (!pm)
		return -1;

	return pm->ready_num;
}

int enc_pm_reset(struct enc_packet_manager *pm)
{
	if (!pm)
		return -1;

	pthread_mutex_lock(&pm->lock);

	// move all packets from ready list back to empty list
	if (!mpp_list_empty(&pm->ready_list)) {
		struct enc_packet_impl *pkt1 = NULL, *pkt2 = NULL;
		mpp_list_for_each_entry_safe(pkt1, pkt2, &pm->ready_list, list) {
			mpp_list_del_init(&pkt1->list);
			mpp_list_add_tail(&pkt1->list, &pm->empty_list);
		}
	}

	logd("read_offset:%d, write_offset:%d, empty_num:%d, ready_num:%d, available_size:%d",
		pm->read_offset, pm->write_offset, pm->empty_num, pm->ready_num, pm->available_size);

	pm->empty_num = pm->packet_count;
	pm->ready_num = 0;
	pm->read_offset = 0;
	pm->write_offset = 0;
	pm->available_size = pm->buffer_size;

	pthread_mutex_unlock(&pm->lock);

	return 0;
}
