/*
 * Copyright (C) 2020-2026 ArtInChip Technology Co. Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 *  author: <qi.xu@artinchip.com>
 *  Desc:  encoder packet (video bitstream container) manager
 */

#ifndef ENC_PACKET_MANAGER_H
#define ENC_PACKET_MANAGER_H

#include "ve_buffer.h"

struct enc_packet_manager;

struct enc_packet {
	unsigned char *data;	// virtual address of encoded data
	size_t size;		// buffer size allocated for this packet
	size_t len;		// actual encoded data length
	int64_t pts;		// presentation timestamp
	unsigned int flag;	// packet flags
	size_t phy_offset;	// physical offset in stream buffer
	unsigned int phy_base;	// physical base address of stream buffer
};

struct enc_packet_manager_init_cfg {
	struct ve_buffer_allocator *ve_buf_handle;	// mpp buffer handle
	size_t buffer_size;				// video bytestream buffer size
	int packet_count;				// packet buffer count
};

/*
	create encoder packet manager
*/
struct enc_packet_manager *enc_pm_create(struct enc_packet_manager_init_cfg *cfg);

/*
	destroy encoder packet manager
*/
int enc_pm_destroy(struct enc_packet_manager *pm);

/*
	get an empty packet for encoder to write encoded data
*/
int enc_pm_dequeue_empty_packet(struct enc_packet_manager *pm, struct enc_packet *packet, size_t size);

/*
	encoder put the packet to ready list after encoding is done
*/
int enc_pm_enqueue_ready_packet(struct enc_packet_manager *pm, struct enc_packet *packet);

/*
	get a ready packet containing encoded data
*/
int enc_pm_dequeue_ready_packet(struct enc_packet_manager *pm, struct mpp_packet *packet);

/*
	return the packet to empty list after use
*/
int enc_pm_enqueue_empty_packet(struct enc_packet_manager *pm, struct mpp_packet *packet);

/*
	get the packet number of empty list
*/
int enc_pm_get_empty_packet_num(struct enc_packet_manager *pm);

/*
	get the packet number of ready list
*/
int enc_pm_get_ready_packet_num(struct enc_packet_manager *pm);

/*
	reset encoder packet manager
*/
int enc_pm_reset(struct enc_packet_manager *pm);

int enc_packet_flush_cache(struct enc_packet_manager *pm,
	unsigned char* buf, int len, enum dma_buf_sync_flag flag);

#endif /* ENC_PACKET_MANAGER_H */
