/*
 * Copyright (C) 2020-2026 ArtInChip Technology Co. Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 *  author: <xiaodong.zhao@artinchip.com>
 *  Desc: rtp_sorter
 */

#ifndef RTP_SORTER_H
#define RTP_SORTER_H

#include "rtp_packet.h"

/* Sorter cache upper limit: ~1/3 of total pool, leave room for output lists */
#define RTP_SORTER_CACHE_MAX 16

/* Small gap: force advancement after this many cached packets without resolving */
#define RTP_SORTER_GAP_PKT_THRESHOLD 5

/* Large gap: force advancement immediately without waiting */
#define RTP_SORTER_LARGE_GAP_THRESHOLD 50

typedef struct rtp_sorter {
    uint32_t ssrc;              /* Current tracked SSRC */
    uint16_t next_seq_out;      /* Expected sequence number for next output */

    rtp_packet_t *cache_head;   /* Out-of-order cache linked list head */
    rtp_packet_t *output_head;  /* Output linked list head */
    int cache_size;
    int output_size;
} rtp_sorter_t;

rtp_sorter_t *rtp_sorter_create(void);
void rtp_sorter_destroy(rtp_sorter_t *sorter);
int rtp_sorter_input(rtp_sorter_t *sorter, const char *data, int size);
rtp_packet_t *rtp_sorter_get_pkt(rtp_sorter_t *sorter);

/* Recycle callback for pool: steal oldest packet from sorter cache */
rtp_packet_t *rtp_sorter_recycle(void *arg);

#endif /* RTP_SORTER_H */
