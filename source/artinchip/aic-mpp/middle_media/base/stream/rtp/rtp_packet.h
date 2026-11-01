/*
 * Copyright (C) 2020-2026 ArtInChip Technology Co. Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 *  author: <xiaodong.zhao@artinchip.com>
 *  Desc: rtp_packet
 */

#ifndef RTP_PACKET_H
#define RTP_PACKET_H

#include <stddef.h>
#include <stdint.h>

typedef struct rtp_packet {
    int payload_size;
    uint16_t seq;
    uint32_t ssrc;
    struct rtp_packet *prev;
    struct rtp_packet *next;
    char payload[];
} rtp_packet_t;

/* Pool sizing: total packets, shared by sorter cache + sorter output + stream output */
#define POOL_PKT_COUNT (512)


/* Pool */
void rtp_packet_pool_init(void);
void rtp_packet_pool_deinit(void);
int rtp_packet_pool_available(void);

/* Create / destroy */
rtp_packet_t *rtp_packet_create_from_data(const char *data, size_t size);
void rtp_packet_destroy(rtp_packet_t *pkt);

typedef rtp_packet_t *(*rtp_packet_recycle_fn)(void *arg);

#define RTP_POOL_MAX_RECYCLE_CBS 4

void rtp_packet_pool_add_recycle_cb(rtp_packet_recycle_fn fn, void *arg);
void rtp_packet_pool_remove_recycle_cb(rtp_packet_recycle_fn fn, void *arg);

/* Circular doubly-linked list helpers */
rtp_packet_t *rtp_packet_list_append(rtp_packet_t *head, rtp_packet_t *pkt);
rtp_packet_t *rtp_packet_list_pop(rtp_packet_t *head, rtp_packet_t *pkt);
void rtp_packet_list_destroy(rtp_packet_t *head);

#endif /* RTP_PACKET_H */
