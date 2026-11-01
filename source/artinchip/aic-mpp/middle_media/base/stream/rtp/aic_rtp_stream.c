/*
 * Copyright (C) 2020-2026 ArtInChip Technology Co. Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 *  author: <xiaodong.zhao@artinchip.com>
 *  Desc: aic_rtp_stream
 */

#define _LARGEFILE64_SOURCE

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <pthread.h>
#include <sched.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include "aic_rtp_stream.h"
#include "aic_stream.h"
#include "mpp_log.h"
#include "mpp_mem.h"
#include "rtp_packet.h"
#ifdef RTP_SORTER_ENABLE
#include "rtp_sorter.h"
#endif

#define UDP_BUF_SIZE (2 * 1024)
#define RTP_THREAD_PRIORITY 50

/* Minimum history packets to keep for backward seek (~11KB at 1.4KB/pkt) */
#define OUTPUT_HISTORY_MIN 8

/* Maximum history packets before forced recycling. Prevents history from
 * consuming all pool slots. ~90KB at 1.4KB/pkt, covers backward seek. */
#define OUTPUT_HISTORY_MAX 64

/* Proactive recycle threshold: recycle history when pool free_count drops below
 * this. Set higher than POOL_LOW_WATER(16) to give recvfrom thread ample margin. */
#define STREAM_RECYCLE_THRESHOLD 32

/* Pending count above which consumer does skip-to-live after timeout recovery.
 * At ~1.3KB/pkt, 50 pkts ≈ 65KB ≈ 2s buffer at 30KB/s. Balances catch-up
 * speed against not skipping useful data. */
#define SKIP_TO_LIVE_THRESHOLD 50

/* Packets to keep at tail when skip-to-live jumps ahead, avoiding
 * incomplete data at the very edge of the output list. */
#define KEEP_AFTER_SKIP 5

// #define RTP_STREAM_DUMP_ENABLE 1
#ifdef RTP_STREAM_DUMP_ENABLE
#define RTP_DUMP_FILE_PATH "/mnt/sdcard/miracast.ts"
static FILE *g_dump_fp = NULL;
// #define RTP_STREAM_DUMP_ONLY 1
#endif

struct aic_rtp_stream {
    struct aic_stream base;

    char udp_buf[UDP_BUF_SIZE];
    int listenfd;
    int port;
    int sorter_inited;
    char *url;

    s64 file_size;
    s64 file_pos;

    /* Output list: ordered packets for consumer, with history for backward seek.
     * Layout: output_head(oldest) ... [history] ... current_pkt ... [pending] ... output_tail(newest)
     */
    rtp_packet_t *output_head;
    rtp_packet_t *output_tail;
    rtp_packet_t *current_pkt;
    int current_offset;
    int history_count;
    unsigned long history_bytes;
    int pending_count;
    unsigned long pending_bytes;
    unsigned long last_byte_rate;
    long long last_rate_pos;
    int prod_count;
    int cons_count;

    int consecutive_timeouts;

    pthread_t tid;
    atomic_int stop_flag;
    int write_flag;
    int init_flag;
#ifdef RTP_SORTER_ENABLE
    rtp_sorter_t *sorter;
#endif
    pthread_mutex_t mutex;
    pthread_cond_t cond;
};

/* Remove a packet from output list (used in drain). Must hold mutex. */
static void output_list_remove(struct aic_rtp_stream *stream, rtp_packet_t *pkt)
{
    if (pkt->prev)
        pkt->prev->next = pkt->next;
    else
        stream->output_head = pkt->next;

    if (pkt->next)
        pkt->next->prev = pkt->prev;
    else
        stream->output_tail = pkt->prev;

    if (pkt == stream->current_pkt)
        stream->current_pkt = pkt->next;
}



/* Steal the oldest history packet from output list. Returns NULL if none
 * available. The stolen packet can be recycled via rtp_packet_destroy(). */
static rtp_packet_t *output_list_steal_oldest(struct aic_rtp_stream *stream)
{
    if (!stream->output_head || stream->output_head == stream->current_pkt)
        return NULL;

    rtp_packet_t *oldest = stream->output_head;
    stream->history_bytes -= oldest->payload_size;
    stream->history_count--;
    output_list_remove(stream, oldest);
    oldest->prev = oldest->next = NULL;

    return oldest;
}

#ifndef RTP_STREAM_DUMP_ONLY
/* Append packet to output list tail. Must hold mutex.
 * No drop/back-pressure logic here — latency control is handled on the
 * consumer side via skip-to-live. */
static void output_list_append(struct aic_rtp_stream *stream, rtp_packet_t *pkt)
{
    pkt->prev = pkt->next = NULL;

    if (NULL == stream->output_tail) {
        stream->output_head = stream->output_tail = pkt;
        if (NULL == stream->current_pkt) {
            stream->current_pkt = pkt;
            stream->pending_count = 1;
        }
    } else {
        pkt->prev = stream->output_tail;
        stream->output_tail->next = pkt;
        stream->output_tail = pkt;
        stream->pending_count++;
        if (!stream->current_pkt)
            stream->current_pkt = pkt;
    }
    stream->pending_bytes += pkt->payload_size;
    stream->prod_count++;

    pthread_cond_signal(&stream->cond);
}
#endif

/* Process a single RTP packet: append to output list or destroy if empty. */
static void rtp_stream_process_packet(struct aic_rtp_stream *stream, rtp_packet_t *pkt)
{
    if (pkt->payload_size <= 0) {
        rtp_packet_destroy(pkt);
        return;
    }

#if RTP_STREAM_DUMP_ENABLE
    if (g_dump_fp)
        fwrite(pkt->payload, 1, pkt->payload_size, g_dump_fp);
#endif

#ifndef RTP_STREAM_DUMP_ONLY
    pthread_mutex_lock(&stream->mutex);
    output_list_append(stream, pkt);
    pthread_mutex_unlock(&stream->mutex);
#else
    rtp_packet_destroy(pkt);
#endif
}

/* Advance current_pkt to next, old current becomes history. Must hold mutex.
 * Proactively recycles excess history when pool is low zero cross-thread
 * contention since consumer already holds stream->mutex. */
static void output_list_advance(struct aic_rtp_stream *stream)
{
    if (!stream->current_pkt)
        return;

    rtp_packet_t *old = stream->current_pkt;
    stream->current_pkt = stream->current_pkt->next;
    stream->current_offset = 0;
    stream->history_count++;
    stream->history_bytes += old->payload_size;
    stream->pending_bytes -= old->payload_size;
    stream->pending_count--;
    stream->cons_count++;

    /* Recycle excess history in two scenarios:
     * 1. Pool low: free_count < STREAM_RECYCLE_THRESHOLD keep pool healthy.
     * 2. History cap: history_count > OUTPUT_HISTORY_MAX prevent unbounded
     *    growth that starves the pool (93% pool consumption by history alone).
     * Briefly acquires g_pool.lock (in pool_available + pool_put),
     * but recvfrom's pool_get releases g_pool.lock before calling
     * stream_recycle_packet, so no deadlock risk.
     */
    while (stream->history_count > OUTPUT_HISTORY_MIN) {
        int pool_free = rtp_packet_pool_available();
        if (pool_free >= STREAM_RECYCLE_THRESHOLD &&
            stream->history_count <= OUTPUT_HISTORY_MAX)
            break;

        rtp_packet_t *oldest = output_list_steal_oldest(stream);
        if (!oldest)
            break;
        rtp_packet_destroy(oldest);
    }

    if (stream->pending_count <= 0) {
        stream->current_pkt = NULL;
        stream->pending_count = 0;
    }
}

/* Skip old pending data to catch up to live edge after timeout recovery.
 * Jumps current_pkt close to output_tail, recycling all skipped packets.
 * Preserves last KEEP_AFTER_SKIP pending packets to avoid landing on
 * incomplete data at the very tail. Must hold mutex. */
static void output_list_skip_to_live(struct aic_rtp_stream *stream)
{
    rtp_packet_t *target = stream->output_tail;
    int keep = KEEP_AFTER_SKIP;
    int skipped = 0;

    /* Walk back from tail to find the target position */
    while (keep > 0 && target && target->prev && target != stream->current_pkt) {
        target = target->prev;
        keep--;
    }

    if (!target || target == stream->current_pkt)
        return;

    /* Discard packets from current_pkt to target->prev.
     * output_list_remove handles the linked-list surgery;
     * we only do the per-packet accounting and destroy. */
    while (stream->current_pkt && stream->current_pkt != target) {
        rtp_packet_t *old = stream->current_pkt;

        output_list_remove(stream, old);
        stream->pending_count--;
        stream->pending_bytes -= old->payload_size;
        old->prev = old->next = NULL;
        rtp_packet_destroy(old);
        skipped++;
    }

    stream->current_offset = 0;

    if (skipped > 0) {
        loge("skip-to-live: dropped %d pkts, pend=%d", skipped, stream->pending_count);
    }
}

/* Non-blocking recycle callback for pool: trylocks stream mutex, steals oldest.
 * Proactive recycling in consumer output_list_advance keeps pool above threshold
 * most of the time, so this is only a rare fallback.
 *
 * Debug: prints list state to diagnose why pool_get's recycle loop fires
 * despite proactive recycling in output_list_advance. */
static rtp_packet_t *stream_recycle_packet(void *arg)
{
    struct aic_rtp_stream *stream = (struct aic_rtp_stream *)arg;
    rtp_packet_t *pkt = NULL;

    if (pthread_mutex_trylock(&stream->mutex) != 0) {
        int pool_free = rtp_packet_pool_available();
        /* Mutex held by consumer (rtp_stream_read / seek / append).
         * Do not read stream members here — they are protected by mutex. */
        loge("trylock fail! pool_free=%d", pool_free);
        return NULL;
    }

    pkt = output_list_steal_oldest(stream);
    if (pkt) {
        logi("recycle ok: hist=%d pend=%d pool_free=%d",
             stream->history_count, stream->pending_count,
             rtp_packet_pool_available());
    }
    pthread_mutex_unlock(&stream->mutex);

    return pkt;
}

void *rtp_download_thread(void *arg)
{
    struct aic_rtp_stream *stream = (struct aic_rtp_stream *)arg;
    socklen_t len = sizeof(struct sockaddr_in);
    struct sockaddr_in serveraddr;
    struct sockaddr_in clientaddr;
    struct timeval tv;

    int ret;

    pthread_setname_np(pthread_self(), "rtpstreamthd");

    stream->listenfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (stream->listenfd < 0) {
        loge("Create socket fail.");
        return NULL;
    }

    memset((void *)&serveraddr, 0, sizeof(struct sockaddr_in));
    serveraddr.sin_family = AF_INET;
    serveraddr.sin_port = htons(stream->port);
    serveraddr.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(stream->listenfd, (struct sockaddr *)&serveraddr, sizeof(struct sockaddr_in)) < 0) {
        loge("bind error.");
        close(stream->listenfd);
        stream->listenfd = -1;
        return NULL;
    }

    tv.tv_sec = 1;
    tv.tv_usec = 0;
    if (setsockopt(stream->listenfd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) < 0) {
        loge("socket option  SO_RCVTIMEO not support\n");
        close(stream->listenfd);
        stream->listenfd = -1;
        return NULL;
    }

#if RTP_STREAM_DUMP_ENABLE
    g_dump_fp = fopen(RTP_DUMP_FILE_PATH, "wb");
    if (!g_dump_fp) {
        loge("open dump file %s failed", RTP_DUMP_FILE_PATH);
    }
#endif

    while (atomic_load(&stream->stop_flag) == 0) {
        ret = recvfrom(stream->listenfd, stream->udp_buf, UDP_BUF_SIZE, 0, (struct sockaddr *)&clientaddr, &len);
        if (ret < 0) {
            if (errno == EWOULDBLOCK || errno == EAGAIN) {
                loge("timeout!");
                usleep(1000);
                continue;
            } else {
                loge("recvfrom failed");
                break;
            }
        } else if (ret == 0) {
            // UDP recvfrom returning 0 is an empty datagram, not EOF
            continue;
        }

#ifdef RTP_SORTER_ENABLE
        rtp_sorter_input(stream->sorter, stream->udp_buf, ret);
        rtp_packet_t *pkt = NULL;
        while ((pkt = rtp_sorter_get_pkt(stream->sorter)) != NULL)
            rtp_stream_process_packet(stream, pkt);
#else
        rtp_packet_t *pkt = rtp_packet_create_from_data(stream->udp_buf, ret);
        if (pkt)
            rtp_stream_process_packet(stream, pkt);
#endif
    }

#if RTP_STREAM_DUMP_ENABLE
    if (g_dump_fp) {
        fclose(g_dump_fp);
        g_dump_fp = NULL;
    }
#endif

    if (stream->listenfd >= 0) {
        close(stream->listenfd);
        stream->listenfd = -1;
    }

    printf("rtp download thread exit!\n");

    return NULL;
}

static int rtp_stream_parse_param(struct aic_rtp_stream *stream, const char *url)
{
    if (sscanf(url, "rtp://%*[^:]:%d/%*s", &stream->port) != 1) {
        return -1;
    }

    return 0;
}

/* Reset timeout recovery state and per-second counters.
 * Called once per second from consumer context.
 * prod/cons reset belongs here because housekeep consumes prod_count to
 * detect fresh data flow; print_cache snapshots before this reset. */
static void rtp_stream_read_housekeep(struct aic_rtp_stream *stream)
{
    pthread_mutex_lock(&stream->mutex);

    if (stream->consecutive_timeouts > 0 &&
        stream->pending_count < SKIP_TO_LIVE_THRESHOLD &&
        stream->prod_count > 0)
        stream->consecutive_timeouts = 0;

    stream->prod_count = 0;
    stream->cons_count = 0;

    pthread_mutex_unlock(&stream->mutex);
}

// #define PRINT_RTP_STATUS 1
#if PRINT_RTP_STATUS
/* Update byte-rate tracking, snapshot cache stats, and print diagnostics.
 * Called before housekeep so prod/cons snapshots capture the full second. */
static void rtp_stream_print_cache(struct aic_rtp_stream *stream)
{
    long long file_pos_snap;
    int hist_count_snap;
    int pend_count_snap;
    unsigned long pend_bytes_snap;
    unsigned long rate_snap;
    int prod_snap;
    int cons_snap;
    unsigned long buffered_ms;

    pthread_mutex_lock(&stream->mutex);

    /* Update consumption rate */
    if (stream->last_rate_pos > 0) {
        unsigned long byte_delta = (unsigned long)(stream->file_pos - stream->last_rate_pos);
        if (byte_delta >= 10000)
            stream->last_byte_rate = byte_delta;
    }
    stream->last_rate_pos = stream->file_pos;

    /* Snapshot for printf outside lock */
    file_pos_snap = stream->file_pos;
    hist_count_snap = stream->history_count;
    pend_count_snap = stream->pending_count;
    pend_bytes_snap = stream->pending_bytes;
    rate_snap = stream->last_byte_rate;
    prod_snap = stream->prod_count;
    cons_snap = stream->cons_count;
    buffered_ms = 0;
    if (rate_snap >= 10000)
        buffered_ms = pend_bytes_snap * 1000 / rate_snap;

    pthread_mutex_unlock(&stream->mutex);

    printf("[CACHE] pos=%lld hist=%d pend=%d pend_bytes=%lu"
           " rate=%lu buf=%lums"
           " | prod=%d cons=%d delta=%+d\n",
           file_pos_snap, hist_count_snap, pend_count_snap,
           pend_bytes_snap, rate_snap, buffered_ms,
           prod_snap, cons_snap, prod_snap - cons_snap);
}
#endif /* PRINT_RTP_STATUS */



/* One-second gate: returns true on the first call within each 1s window.
 * Drives both housekeep and cache-print cadence from a single clock. */
static int rtp_stream_read_tick(void)
{
    static long long last_tick_ms;
    struct timespec now;
    long long now_ms;

    clock_gettime(CLOCK_REALTIME, &now);
    now_ms = (long long)now.tv_sec * 1000 + now.tv_nsec / 1000000;
    if (now_ms - last_tick_ms < 1000)
        return 0;

    last_tick_ms = now_ms;
    return 1;
}

static s64 rtp_stream_read(struct aic_stream *s, void *buf, s64 len)
{
    struct aic_rtp_stream *stream = (struct aic_rtp_stream *)s;
    char *dst = (char *)buf;
    s64 total = 0;

    if (rtp_stream_read_tick()) {
#if PRINT_RTP_STATUS
        rtp_stream_print_cache(stream);
#endif
        rtp_stream_read_housekeep(stream);
    }

    while (total < len) {
        pthread_mutex_lock(&stream->mutex);

        while (!stream->current_pkt && atomic_load(&stream->stop_flag) == 0) {
            struct timespec ts;
            clock_gettime(CLOCK_REALTIME, &ts);
            ts.tv_sec += 1;
            int rc = pthread_cond_timedwait(&stream->cond, &stream->mutex, &ts);
            if (rc == ETIMEDOUT) {
                pthread_mutex_unlock(&stream->mutex);
                stream->consecutive_timeouts++;
                loge("rtp stream read timeout, %ld bytes already read",
                     total);
                return total > 0 ? total : -EAGAIN;
            }
        }

        if (!stream->current_pkt) {
            pthread_mutex_unlock(&stream->mutex);
            break;
        }

        /* Skip-to-live: after timeout recovery, if pending queue grew
         * large during the stall, jump to near the newest data to
         * eliminate accumulated latency. Reset only in skip function
         * or CACHE print, not on individual reads, to avoid clearing
         * before burst fully arrives. */
        if (stream->consecutive_timeouts > 0 && stream->pending_count >= SKIP_TO_LIVE_THRESHOLD) {
            output_list_skip_to_live(stream);
            if (!stream->current_pkt) {
                pthread_mutex_unlock(&stream->mutex);
                continue;
            }
        }

        rtp_packet_t *pkt = stream->current_pkt;
        int offset = stream->current_offset;
        s64 avail = pkt->payload_size - offset;
        s64 chunk = len - total;
        if (chunk > avail)
            chunk = avail;

        memcpy(dst + total, pkt->payload + offset, chunk);
        total += chunk;
        stream->file_pos += chunk;

        if (offset + chunk < pkt->payload_size) {
            stream->current_offset = offset + chunk;
            pthread_mutex_unlock(&stream->mutex);
            break;
        }

        output_list_advance(stream);
        pthread_mutex_unlock(&stream->mutex);
    }

    return total;
}

static s64 rtp_stream_write(struct aic_stream *s, void *buf, s64 len)
{
    struct aic_rtp_stream *stream = (struct aic_rtp_stream *)s;

    if (!stream->write_flag) {
        loge("not support write");
        return -1;
    }

    return 0;
}

static s64 rtp_stream_tell(struct aic_stream *s)
{
    struct aic_rtp_stream *stream = (struct aic_rtp_stream *)s;

    return stream->file_pos;
}

static s32 rtp_stream_close(struct aic_stream *s)
{
    struct aic_rtp_stream *stream = (struct aic_rtp_stream *)s;
    if (NULL == stream) {
        loge("invalid parameter!");
        return -1;
    }

    logi("rtp_stream_close");

    atomic_store(&stream->stop_flag, 1);
    if (stream->tid) {
        pthread_join(stream->tid, NULL);
        stream->tid = 0;
    }

    /* Drain output list under mutex to prevent data race with consumer
     * (rtp_stream_read). Consumer checks stop_flag and will either:
     * - already have exited the read loop, or
     * - be blocked on stream->mutex until drain completes, then see
     *   current_pkt == NULL and exit. */
    pthread_mutex_lock(&stream->mutex);
    pthread_cond_broadcast(&stream->cond);

    while (stream->output_head) {
        rtp_packet_t *pkt = stream->output_head;
        output_list_remove(stream, pkt);
        rtp_packet_destroy(pkt);
    }
    stream->output_head = stream->output_tail = NULL;
    stream->current_pkt = NULL;
    stream->history_count = 0;
    pthread_mutex_unlock(&stream->mutex);

    /* Remove pool recycle callbacks before freeing any resource they reference,
     * preventing use-after-free when pool later reclaims packets. */
#ifdef RTP_SORTER_ENABLE
    if (stream->sorter_inited && stream->sorter) {
        rtp_packet_pool_remove_recycle_cb(rtp_sorter_recycle, stream->sorter);
        rtp_sorter_destroy(stream->sorter);
        stream->sorter = NULL;
        stream->sorter_inited = 0;
    }
#endif
    rtp_packet_pool_remove_recycle_cb(stream_recycle_packet, stream);

    if (stream->init_flag) {
        pthread_cond_destroy(&stream->cond);
        pthread_mutex_destroy(&stream->mutex);
        stream->init_flag = 0;
    }

    if (stream->url) {
        free(stream->url);
        stream->url = NULL;
    }
    mpp_free(stream);

    return 0;
}

static s64 rtp_stream_seek(struct aic_stream *s, s64 offset, s32 whence)
{
    struct aic_rtp_stream *stream = (struct aic_rtp_stream *)s;
    s64 new_pos;

    pthread_mutex_lock(&stream->mutex);

    switch (whence) {
        case SEEK_SET:
            new_pos = offset;
            break;
        case SEEK_CUR:
            new_pos = stream->file_pos + offset;
            break;
        case SEEK_END:
            if (stream->file_size > 0) {
                new_pos = stream->file_size + offset;
            } else {
                loge("cannot seek from end: unknown file size");
                pthread_mutex_unlock(&stream->mutex);
                return -1;
            }
            break;
        default:
            pthread_mutex_unlock(&stream->mutex);
            return -1;
    }

    if (new_pos > stream->file_pos) {
        /* Forward seek: advance within or past packets */
        s64 target = new_pos;
        while (stream->file_pos < target && stream->current_pkt) {
            s64 avail = stream->current_pkt->payload_size - stream->current_offset;
            s64 need = target - stream->file_pos;
            if (need < avail) {
                stream->current_offset += (int)need;
                stream->file_pos = target;
                break;
            }
            stream->file_pos += avail;
            output_list_advance(stream);
        }
    } else if (new_pos < stream->file_pos) {
        /* Backward seek: undo partial read, then rewind through history */
        stream->file_pos -= stream->current_offset;
        stream->current_offset = 0;

        /* If target is still within current_pkt, seek inside it directly */
        if (stream->current_pkt && new_pos >= stream->file_pos) {
            s64 within_pkt = new_pos - stream->file_pos;
            if (within_pkt < stream->current_pkt->payload_size) {
                stream->current_offset = (int)within_pkt;
                stream->file_pos = new_pos;
                pthread_mutex_unlock(&stream->mutex);
                return stream->file_pos;
            }
        }

        /* If current_pkt is NULL (all pending consumed), restore it to
         * output_tail so we can unwind through history. */
        if (NULL == stream->current_pkt && stream->history_count > 0) {
            stream->current_pkt = stream->output_tail;
            stream->history_count--;
            stream->history_bytes -= stream->current_pkt->payload_size;
            stream->pending_count++;
        }

        s64 back = stream->file_pos - new_pos;
        while (back > 0 && stream->history_count > 0) {
            rtp_packet_t *prev = stream->current_pkt->prev;
            s64 pkt_bytes = prev->payload_size;

            stream->current_pkt = prev;
            stream->history_count--;
            stream->history_bytes -= pkt_bytes;
            stream->pending_count++;

            if (pkt_bytes > back) {
                stream->current_offset = (int)(pkt_bytes - back);
                stream->file_pos -= back;
                back = 0;
                break;
            }
            stream->file_pos -= pkt_bytes;
            back -= pkt_bytes;
        }

        /* Ran out of history but still need to go back further:
         * snap to beginning of oldest available packet. */
        if (back > 0 && stream->current_pkt) {
            stream->current_offset = 0;
        }
    }

    pthread_mutex_unlock(&stream->mutex);

    return stream->file_pos;
}

static s64 rtp_stream_size(struct aic_stream *s)
{
    struct aic_rtp_stream *stream = (struct aic_rtp_stream *)s;

    return stream->file_size;
}

s32 rtp_stream_open(const char *uri, struct aic_stream **s, int flags)
{
    s32 ret = 0;

    struct aic_rtp_stream *stream =
        (struct aic_rtp_stream *)mpp_alloc(sizeof(struct aic_rtp_stream));
    if (stream == NULL) {
        loge("mpp_alloc aic_stream failed!!!!!\n");
        ret = -1;
        goto exit;
    }

    memset(stream, 0, sizeof(struct aic_rtp_stream));
    stream->listenfd = -1;
    atomic_init(&stream->stop_flag, 0);
#if RTP_STREAM_DUMP_ENABLE
    g_dump_fp = NULL;
#endif
    rtp_packet_pool_init();
    rtp_packet_pool_add_recycle_cb(stream_recycle_packet, stream);
    stream->file_size = -1;
    stream->url = strdup(uri);

    ret = rtp_stream_parse_param(stream, uri);
    if (0 != ret) {
        ret = -3;
        goto exit;
    }

    ret = pthread_mutex_init(&stream->mutex, NULL);
    if (ret != 0) {
        loge("pthread_mutex_init failed: %d", ret);
        ret = -4;
        goto exit;
    }
    pthread_cond_init(&stream->cond, NULL);
    stream->init_flag = 1;

#ifdef RTP_SORTER_ENABLE
    stream->sorter = rtp_sorter_create();
    if (NULL == stream->sorter) {
        loge("rtp_sorter_create failed");
        ret = -5;
        goto exit;
    }
    stream->sorter_inited = 1;
    rtp_packet_pool_add_recycle_cb(rtp_sorter_recycle, stream->sorter);
#endif

    pthread_attr_t attr;
    struct sched_param sched_param;

    pthread_attr_init(&attr);
    pthread_attr_setschedpolicy(&attr, SCHED_RR);
    sched_param.sched_priority = RTP_THREAD_PRIORITY;
    pthread_attr_setschedparam(&attr, &sched_param);
    pthread_attr_setinheritsched(&attr, PTHREAD_EXPLICIT_SCHED);

    ret = pthread_create(&stream->tid, &attr, rtp_download_thread, (void *)stream);
    pthread_attr_destroy(&attr);
    if (0 != ret) {
        loge("create rtp_download_thread failed!");
        ret = -6;
        goto exit;
    }

    stream->base.read = rtp_stream_read;
    stream->base.write = rtp_stream_write;
    stream->base.close = rtp_stream_close;
    stream->base.seek = rtp_stream_seek;
    stream->base.size = rtp_stream_size;
    stream->base.tell = rtp_stream_tell;
    *s = &stream->base;

    return ret;

exit:
    if (stream) {
        /* Remove pool recycle callbacks before freeing any resource
         * they reference, preventing use-after-free. */
#ifdef RTP_SORTER_ENABLE
        if (stream->sorter_inited && stream->sorter) {
            rtp_packet_pool_remove_recycle_cb(rtp_sorter_recycle,
                                              stream->sorter);
            rtp_sorter_destroy(stream->sorter);
            stream->sorter = NULL;
            stream->sorter_inited = 0;
        }
#endif
        rtp_packet_pool_remove_recycle_cb(stream_recycle_packet, stream);

        if (stream->url) {
            free(stream->url);
        }
        if (stream->init_flag) {
            pthread_cond_destroy(&stream->cond);
            pthread_mutex_destroy(&stream->mutex);
            stream->init_flag = 0;
        }
        mpp_free(stream);
    }

    *s = NULL;
    return ret;
}
