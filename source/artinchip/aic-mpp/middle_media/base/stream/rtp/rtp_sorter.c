/*
 * Copyright (C) 2020-2026 ArtInChip Technology Co. Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 *  author: <xiaodong.zhao@artinchip.com>
 *  Desc: rtp_sorter
 */

#include <stdlib.h>
#include <stdio.h>
#include <sys/time.h>
#include "rtp_sorter.h"
#include "mpp_log.h"

#define SEQ_MAX_DIFF (0xfff)

#define MAX_SEQ      0xFFFF
#define HALF_MAX_SEQ (MAX_SEQ >> 1)

#ifndef RTP_SORTER_QUEUE_LOG_INTERVAL_MS
#define RTP_SORTER_QUEUE_LOG_INTERVAL_MS 0
long long last_log_time_ms;
#endif

#if RTP_SORTER_QUEUE_LOG_INTERVAL_MS > 0
static long long get_time_ms(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (long long)tv.tv_sec * 1000LL + tv.tv_usec / 1000;
}

static void rtp_sorter_log_queue(rtp_sorter_t *sorter)
{
    long long now = get_time_ms();
    if (last_log_time_ms == 0) {
        last_log_time_ms = now;
        return;
    }
    if (now - last_log_time_ms >= RTP_SORTER_QUEUE_LOG_INTERVAL_MS) {
        printf("rtp_sorter queue: cache=%d, output=%d\n", sorter->cache_size,
             sorter->output_size);
        last_log_time_ms = now;
    }
}
#endif

rtp_sorter_t *rtp_sorter_create(void)
{
    rtp_sorter_t *sorter = (rtp_sorter_t *)malloc(sizeof(rtp_sorter_t));
    if (!sorter)
        return NULL;

    sorter->ssrc = 0;
    sorter->next_seq_out = 0;
    sorter->cache_head = NULL;
    sorter->output_head = NULL;
    sorter->cache_size = 0;
    sorter->output_size = 0;
#if RTP_SORTER_QUEUE_LOG_INTERVAL_MS > 0
    last_log_time_ms = 0;
#endif
    return sorter;
}

static void rtp_sorter_clear(rtp_sorter_t *sorter)
{
    rtp_packet_list_destroy(sorter->cache_head);
    sorter->cache_head = NULL;
    sorter->cache_size = 0;

    rtp_packet_list_destroy(sorter->output_head);
    sorter->output_head = NULL;
    sorter->output_size = 0;
}

void rtp_sorter_destroy(rtp_sorter_t *sorter)
{
    if (!sorter)
        return;

    rtp_sorter_clear(sorter);
    free(sorter);
}

static int is_seq_valid(uint16_t base, uint16_t seq)
{
    /*
     * Use signed 16-bit arithmetic to handle wrap-around correctly.
     * If the difference is within [-SEQ_MAX_DIFF, SEQ_MAX_DIFF], it's valid.
     * Note: SEQ_MAX_DIFF (4095) is well within int16_t range.
     */
    int16_t diff = (int16_t)(seq - base);
    return (diff >= -(int16_t)SEQ_MAX_DIFF && diff <= (int16_t)SEQ_MAX_DIFF);
}

static void append_to_output(rtp_sorter_t *sorter, rtp_packet_t *pkt)
{
    sorter->output_head = rtp_packet_list_append(sorter->output_head, pkt);
    sorter->output_size++;
}

static void force_advancement(rtp_sorter_t *sorter)
{
    if (!sorter->cache_head)
        return;

    int skipped = (int)((uint16_t)(sorter->cache_head->seq - sorter->next_seq_out));
    loge("sorter force advance: skip seq %u->%u (dropped %d)",
         sorter->next_seq_out, sorter->cache_head->seq, skipped);
    sorter->next_seq_out = sorter->cache_head->seq;

    while (sorter->cache_head && sorter->cache_head->seq == sorter->next_seq_out) {
        rtp_packet_t *head = sorter->cache_head;
        sorter->cache_head = rtp_packet_list_pop(head, head);
        if (sorter->cache_head == head) {
            loge("pop failed during force advance");
            break;
        }
        sorter->cache_size--;
        append_to_output(sorter, head);
        sorter->next_seq_out++;
    }
}

static int seq_compare(uint16_t seq1, uint16_t seq2)
{
    uint16_t diff = seq1 - seq2;

    if (diff == 0) {
        return 0;   // Equal
    } else if (diff < HALF_MAX_SEQ) {
        return 1;   // seq1 > seq2
    } else {
        return -1;  // seq1 < seq2 (wraparound case)
    }
}

/* Steal the oldest (smallest seq) packet from cache.
 * Called by pool when free packets are low. */
rtp_packet_t *rtp_sorter_recycle(void *arg)
{
    rtp_sorter_t *sorter = (rtp_sorter_t *)arg;
    if (!sorter || !sorter->cache_head)
        return NULL;

    /* Find the oldest: skip ahead to cache_head's seq, then pop it */
    sorter->next_seq_out = sorter->cache_head->seq;

    rtp_packet_t *head = sorter->cache_head;
    sorter->cache_head = rtp_packet_list_pop(head, head);
    if (sorter->cache_head == head) {
        loge("<%s:%d> recycle pop failed\n", __func__, __LINE__);
        return NULL;
    }
    sorter->cache_size--;
    head->prev = head->next = NULL;
    return head;
}

int rtp_sorter_input(rtp_sorter_t *sorter, const char *data, int size)
{
    if (!sorter || !data || size < 12)
        return -1;

#if RTP_SORTER_QUEUE_LOG_INTERVAL_MS > 0
    rtp_sorter_log_queue(sorter);
#endif

    rtp_packet_t *pkt = rtp_packet_create_from_data(data, size);
    if (!pkt)
        return -1;

    if (sorter->ssrc == 0) {
        sorter->ssrc = pkt->ssrc;
        sorter->next_seq_out = pkt->seq;
    } else if (sorter->ssrc != pkt->ssrc) {
        /* SSRC changed, clear all cache and reset */
        rtp_sorter_clear(sorter);
        sorter->ssrc = pkt->ssrc;
        sorter->next_seq_out = pkt->seq;
    }

    /* Check if sequence number is valid (not too old or too new) */
    if (!is_seq_valid(sorter->next_seq_out, pkt->seq)) {
        loge("seq not valid:%d %d", pkt->seq, sorter->next_seq_out);
        rtp_packet_destroy(pkt);
        return 0; /* Discard invalid packet, not counted as error */
    }

    /* If cache is empty and packet seq is smaller than expected seq,
     * discard duplicate/old packet */
    if (!sorter->cache_head && seq_compare(pkt->seq, sorter->next_seq_out) < 0) {
        loge("duplicate/old packet %u, expected %u\n", pkt->seq, sorter->next_seq_out);
        rtp_packet_destroy(pkt);
        return 0;
    }

    /* Handle special case of empty cache */
    if (!sorter->cache_head) {
        sorter->cache_head = pkt;
        pkt->prev = pkt;
        pkt->next = pkt;
        sorter->cache_size = 1;
    } else {
        /* Insert into sorted cache list by sequence number */
        rtp_packet_t *current = sorter->cache_head;
        int inserted = 0;

        /* Check duplicate of head first */
        if (pkt->seq == sorter->cache_head->seq) {
            loge("repeated packet: %u", pkt->seq);
            rtp_packet_destroy(pkt);
            return 0;
        }

        /* If new packet seq is smaller than current head, insert before head */
        if (seq_compare(pkt->seq, current->seq) < 0) {
            /* Check for duplicates before inserting */
            rtp_packet_t *check = current;
            do {
                if (check->seq == pkt->seq) {
                    loge("repeated packet: %u", pkt->seq);
                    rtp_packet_destroy(pkt);
                    return 0;
                }
                check = check->next;
            } while (check != current && check != NULL);

            rtp_packet_t *tail = current->prev;
            pkt->prev = tail;
            pkt->next = current;
            tail->next = pkt;
            current->prev = pkt;
            sorter->cache_head = pkt; /* Update head pointer */
            inserted = 1;
        } else {
            /* Search through the rest of list */
            current = current->next;
            while (current != sorter->cache_head) {
                if (current->seq == pkt->seq) {
                    /* Duplicate packet */
                    loge("repeated packet: %u", pkt->seq);
                    rtp_packet_destroy(pkt);
                    return 0;
                }
                if (seq_compare(pkt->seq, current->seq) < 0) {
                    /* Insert before current */
                    rtp_packet_t *prev = current->prev;
                    prev->next = pkt;
                    pkt->prev = prev;
                    pkt->next = current;
                    current->prev = pkt;
                    inserted = 1;
                    break;
                }
                current = current->next;
            }
            if (!inserted) {
                /* Check if duplicate of head */
                if (sorter->cache_head->seq == pkt->seq) {
                    loge("repeated packet: %u", pkt->seq);
                    rtp_packet_destroy(pkt);
                    return 0;
                }
                /* New packet seq is larger than all in list, insert at tail
                 * (before head) */
                rtp_packet_t *tail = sorter->cache_head->prev;
                tail->next = pkt;
                pkt->prev = tail;
                pkt->next = sorter->cache_head;
                sorter->cache_head->prev = pkt;
            }
        }
        sorter->cache_size++;
    }

    /* Try to pop all consecutive available packets */
    while (sorter->cache_head) {
        if (sorter->cache_head->seq != sorter->next_seq_out)
            break;

        rtp_packet_t *head = sorter->cache_head;
        sorter->cache_head = rtp_packet_list_pop(head, head);
        if (sorter->cache_head == head) {
            /* Pop failed, likely corrupted list */
            loge("<%s:%d> pop failed, breaking loop\n", __func__, __LINE__);
            break;
        }
        sorter->cache_size--;

        /* Put this ordered packet into output queue */
        append_to_output(sorter, head);

        /* Update expected sequence number (handle wraparound) */
        sorter->next_seq_out++;
    }

    /* Large gap: advance immediately packets clearly lost */
    if (sorter->cache_head) {
        int gap = (int)((uint16_t)(sorter->cache_head->seq - sorter->next_seq_out));
        if (gap >= RTP_SORTER_LARGE_GAP_THRESHOLD) {
            force_advancement(sorter);
        } else if (sorter->cache_head->seq != sorter->next_seq_out &&
                   sorter->cache_size >= RTP_SORTER_GAP_PKT_THRESHOLD) {
            force_advancement(sorter);
        }
    }

    /* Safety upper limit for extreme cases */
    if (sorter->cache_size > RTP_SORTER_CACHE_MAX) {
        force_advancement(sorter);
    }

    return 0;
}

rtp_packet_t *rtp_sorter_get_pkt(rtp_sorter_t *sorter)
{
    if (!sorter || !sorter->output_head)
        return NULL;

    rtp_packet_t *pkt = sorter->output_head;
    sorter->output_head = rtp_packet_list_pop(sorter->output_head, pkt);
    if (sorter->output_head == pkt) {
        loge("<%s:%d> pop output pkt failed\n", __func__, __LINE__);
        return NULL;
    }
    sorter->output_size--;

    return pkt;
}
