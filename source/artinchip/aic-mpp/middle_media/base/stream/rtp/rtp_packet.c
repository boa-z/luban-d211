/*
 * Copyright (C) 2020-2026 ArtInChip Technology Co. Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 *  author: <xiaodong.zhao@artinchip.com>
 *  Desc: rtp_packet
 */

#include "rtp_packet.h"
#include "mpp_log.h"
#include <arpa/inet.h>
#include <endian.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>

#define POOL_PKT_PAYLOAD_MAX (1500)
#define POOL_PKT_SIZE  (sizeof(rtp_packet_t) + POOL_PKT_PAYLOAD_MAX)
#define POOL_LOW_WATER (16)

static struct {
    rtp_packet_t *freelist;
    int free_count;
    void *memory;
    pthread_mutex_t lock;
    int inited;
    struct {
        rtp_packet_recycle_fn fn;
        void *arg;
    } recycle_cbs[RTP_POOL_MAX_RECYCLE_CBS];
    int recycle_cb_count;
} g_pool;

typedef struct rtp_header {
#if __BYTE_ORDER == __BIG_ENDIAN
    uint32_t version : 2;
    uint32_t padding : 1;
    uint32_t ext : 1;
    uint32_t csrc : 4;
    uint32_t mark : 1;
    uint32_t pt : 7;
#else
    uint32_t csrc : 4;
    uint32_t ext : 1;
    uint32_t padding : 1;
    uint32_t version : 2;
    uint32_t pt : 7;
    uint32_t mark : 1;
#endif
    uint16_t seq;
    uint32_t stamp;
    uint32_t ssrc;
    uint8_t payload[0];
} rtp_header_t;

void rtp_packet_pool_init(void)
{
    if (g_pool.inited)
        return;

    pthread_mutex_init(&g_pool.lock, NULL);
    g_pool.memory = malloc(POOL_PKT_SIZE * POOL_PKT_COUNT);
    if (!g_pool.memory)
        return;

    g_pool.freelist = NULL;
    g_pool.free_count = 0;
    for (int i = 0; i < POOL_PKT_COUNT; i++) {
        rtp_packet_t *pkt = (rtp_packet_t *)((char *)g_pool.memory + i * POOL_PKT_SIZE);
        pkt->next = g_pool.freelist;
        g_pool.freelist = pkt;
        g_pool.free_count++;
    }
    g_pool.inited = 1;
}

void rtp_packet_pool_deinit(void)
{
    pthread_mutex_lock(&g_pool.lock);
    g_pool.inited = 0;
    g_pool.free_count = 0;
    g_pool.freelist = NULL;
    g_pool.recycle_cb_count = 0;
    free(g_pool.memory);
    g_pool.memory = NULL;
    pthread_mutex_unlock(&g_pool.lock);
    /* Keep mutex alive: pool_get/pool_put check inited flag under lock
     * and bail out, so no use-after-destroy is possible. */
    pthread_mutex_destroy(&g_pool.lock);
}

int rtp_packet_pool_available(void)
{
    int count;
    pthread_mutex_lock(&g_pool.lock);
    count = g_pool.free_count;
    pthread_mutex_unlock(&g_pool.lock);
    return count;
}

void rtp_packet_pool_add_recycle_cb(rtp_packet_recycle_fn fn, void *arg)
{
    if (g_pool.recycle_cb_count >= RTP_POOL_MAX_RECYCLE_CBS)
        return;
    g_pool.recycle_cbs[g_pool.recycle_cb_count].fn = fn;
    g_pool.recycle_cbs[g_pool.recycle_cb_count].arg = arg;
    g_pool.recycle_cb_count++;
}

void rtp_packet_pool_remove_recycle_cb(rtp_packet_recycle_fn fn, void *arg)
{
    pthread_mutex_lock(&g_pool.lock);
    for (int i = 0; i < g_pool.recycle_cb_count; i++) {
        if (g_pool.recycle_cbs[i].fn == fn &&
            g_pool.recycle_cbs[i].arg == arg) {
            for (int j = i; j < g_pool.recycle_cb_count - 1; j++)
                g_pool.recycle_cbs[j] = g_pool.recycle_cbs[j + 1];
            g_pool.recycle_cb_count--;
            break;
        }
    }
    pthread_mutex_unlock(&g_pool.lock);
}

static rtp_packet_t *pool_get(void)
{
    rtp_packet_t *pkt = NULL;

    pthread_mutex_lock(&g_pool.lock);
    if (!g_pool.inited) {
        pthread_mutex_unlock(&g_pool.lock);
        return NULL;
    }

    if (g_pool.recycle_cb_count > 0) {
        while (g_pool.free_count < POOL_LOW_WATER) {
            int recycled_any = 0;
            for (int i = 0; i < g_pool.recycle_cb_count; i++) {
                pthread_mutex_unlock(&g_pool.lock);
                rtp_packet_t *recycled = g_pool.recycle_cbs[i].fn(
                    g_pool.recycle_cbs[i].arg);
                pthread_mutex_lock(&g_pool.lock);
                if (recycled) {
                    recycled->next = g_pool.freelist;
                    g_pool.freelist = recycled;
                    g_pool.free_count++;
                    recycled_any = 1;
                }
            }
            if (!recycled_any)
                break;
        }
    }

    if (g_pool.freelist) {
        pkt = g_pool.freelist;
        g_pool.freelist = pkt->next;
        g_pool.free_count--;
    }
    pthread_mutex_unlock(&g_pool.lock);
    return pkt;
}

static void pool_put(rtp_packet_t *pkt)
{
    pthread_mutex_lock(&g_pool.lock);
    if (!g_pool.inited) {
        pthread_mutex_unlock(&g_pool.lock);
        return;
    }
    pkt->next = g_pool.freelist;
    g_pool.freelist = pkt;
    g_pool.free_count++;
    pthread_mutex_unlock(&g_pool.lock);
}

rtp_packet_t *rtp_packet_create_from_data(const char *data, size_t size)
{
    if (size < 12)
        return NULL;

    rtp_header_t *hdr = (rtp_header_t *)data;
    size_t header_len = 12 + hdr->csrc * 4;
    if (hdr->ext) {
        if (size < header_len + 4)
            return NULL;
        uint16_t ext_len;
        memcpy(&ext_len, data + header_len + 2, sizeof(ext_len));
        ext_len = ntohs(ext_len);
        header_len += 4 + ext_len * 4;
    }
    if (size < header_len)
        return NULL;

    size_t payload_size = size - header_len;

    /* Fall back to malloc if payload exceeds pool packet size */
    if (payload_size > POOL_PKT_PAYLOAD_MAX) {
        rtp_packet_t *pkt = malloc(sizeof(rtp_packet_t) + payload_size);
        if (!pkt) {
            loge("malloc rtp_packet fail!");
            return NULL;
        }
        memcpy(pkt->payload, data + header_len, payload_size);
        pkt->payload_size = (int)payload_size;
        pkt->seq = ntohs(hdr->seq);
        pkt->ssrc = ntohl(hdr->ssrc);
        pkt->prev = pkt->next = NULL;
        return pkt;
    }

    rtp_packet_t *pkt = pool_get();
    if (!pkt) {
        loge("packet pool exhausted!");
        return NULL;
    }

    memcpy(pkt->payload, data + header_len, payload_size);
    pkt->payload_size = (int)payload_size;
    pkt->seq = ntohs(hdr->seq);
    pkt->ssrc = ntohl(hdr->ssrc);
    pkt->prev = pkt->next = NULL;

    return pkt;
}

void rtp_packet_destroy(rtp_packet_t *pkt)
{
    if (!pkt)
        return;
    if (g_pool.memory && (char *)pkt >= (char *)g_pool.memory &&
        (char *)pkt < (char *)g_pool.memory + POOL_PKT_SIZE * POOL_PKT_COUNT)
        pool_put(pkt);
    else
        free(pkt);
}

rtp_packet_t *rtp_packet_list_append(rtp_packet_t *head, rtp_packet_t *pkt)
{
    if (NULL == pkt) {
        return head;
    }

    if (NULL == head) {
        head = pkt;
        head->prev = head->next = head;
    } else {
        rtp_packet_t *tail = head->prev;
        pkt->prev = tail;
        pkt->next = head;
        tail->next = pkt;
        head->prev = pkt;
    }

    return head;
}

rtp_packet_t *rtp_packet_list_pop(rtp_packet_t *head, rtp_packet_t *pkt)
{
    if (NULL == head || NULL == pkt) {
        return head;
    }

    if (!pkt->prev || !pkt->next) {
        loge("<%s:%d> corrupted list: NULL pointer detected!\n", __func__, __LINE__);
        return head;
    }

    rtp_packet_t *prev = pkt->prev;
    rtp_packet_t *next = pkt->next;

    prev->next = next;
    next->prev = prev;

    if (pkt == head) {
        if (head->next == head) {
            head = NULL;
        } else {
            head = next;
        }
    }

    pkt->prev = NULL;
    pkt->next = NULL;

    return head;
}

void rtp_packet_list_destroy(rtp_packet_t *head)
{
    if (!head)
        return;

    rtp_packet_t *tail = head->prev;
    tail->next = NULL;
    while (head) {
        rtp_packet_t *next = head->next;
        rtp_packet_destroy(head);
        head = next;
    }
}
