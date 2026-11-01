/*
 * Copyright (C) 2020-2026 ArtInChip Technology Co. Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * author: <che.jiang@artinchip.com>
 * Desc: hls_parser - HLS parser implementation
 */

#define LOG_TAG "hls_parser"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <inttypes.h>
#include <sys/time.h>
#include "mpp_mem.h"
#include "mpp_log.h"
#include "mpp_dec_type.h"
#include "aic_parser.h"
#include "aic_stream.h"
#include "aic_ts_parser.h"
#include "aic_mov_parser.h"
#include "m3u.h"

#define HLS_MAX_STREAMS       2
#define HLS_LIVE_MAX_URL_SIZE (32 * 1024)
// Playlist reload interval: RFC 8216 Sec 6.3.4 suggests target_duration/2
#define HLS_LIVE_RELOAD_INTERVAL_MS_MIN 500  // min reload interval (ms)
#define HLS_LIVE_RELOAD_INTERVAL_MS_MAX 8000 // max reload interval (ms)
#define HLS_LIVE_MAX_RETRIES            3    // 3 retries before giving up
#define HLS_LIVE_RETRY_DELAY_MS         500  // 500ms delay between retries

// Segment download retry: exponential backoff with jitter (RFC 8216)
#define HLS_SEG_RETRY_BASE_MS 200  // base delay = 200ms
#define HLS_SEG_RETRY_MAX_MS  3200 // max delay = 3.2s
#define HLS_SEG_MAX_RETRIES   3    // max retry count


struct hls_stream_ctx {
    int index;
    int type_mask;

    // video info
    int video_id;     // video codec ID
    int width;
    int height;
    int max_ref_frames;
    int video_extra_data_size;
    unsigned char *video_extra_data;

    // audio info
    int audio_id;     // audio codec ID
    int channels;
    int sample_rate;
    int bits_per_sample;
    int audio_extra_data_size;
    unsigned char *audio_extra_data;

    // current segment
    int current_segment;
    char segment_url[M3U_MAX_URL_LEN];
    unsigned char *segment_data;
    size_t segment_size;
    size_t segment_pos;

    // ts or mp4 parser for current segment
    struct aic_parser *sub_parser;

    // temp file for byte-range segment
    char temp_file[M3U_MAX_URL_LEN];
};

struct aic_hls_parser {
    struct aic_parser base;
    struct aic_stream *stream;

    char *url;
    struct m3u_playlist playlist;

    int nb_streams;
    int stream_switched;
    struct hls_stream_ctx *streams[HLS_MAX_STREAMS];

    // cache for prefetch
    struct segment_cache {
        int index;
        unsigned char *data;
        size_t size;
        struct segment_cache *next;
    } *cache_head, *cache_tail;
    int cache_count;

    // current playback state
    int current_segment_idx;
    struct hls_stream_ctx *current_ctx;

    // status
    int eos;
    int find_playlist;
    int last_discontinuity_seg;  // track which seg already sent SOS

    // live streaming support
    int is_live_stream;                // 1 = live stream, 0 = VOD
    int64_t last_playlist_reload_ms;   // Timestamp of last successful reload
    int live_retry_count;              // Current retry counter
    int64_t last_media_sequence;           // Last seen media sequence number
    char base_url[M3U_MAX_URL_LEN];
};

static void get_base_url(const char *url, char *base, int size)
{
    if (!url || !base || size <= 0)
        return;

    strncpy(base, url, size);
    base[size - 1] = '\0';

    char *last_slash = strrchr(base, '/');
    if (last_slash)
        *(last_slash + 1) = '\0';
}

static int64_t get_time_ms(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (int64_t)tv.tv_sec * 1000 + tv.tv_usec / 1000;
}


static int hls_stream_read_line(void *userdata, char *line, int max_len)
{
    struct aic_stream *stream = (struct aic_stream *)userdata;
    int i = 0;
    char c;

    while (i < max_len - 1) {
        if (aic_stream_read(stream, &c, 1) != 1) {
            break;
        }
        if (c == '\n') {
            break;
        }
        line[i++] = c;
    }
    line[i] = '\0';
    return i;
}

static struct hls_stream_ctx *create_stream_ctx(struct aic_hls_parser *c)
{
    struct hls_stream_ctx *ctx;

    ctx = (struct hls_stream_ctx*)mpp_alloc(sizeof(struct hls_stream_ctx));
    if (!ctx)
        return NULL;

    memset(ctx, 0, sizeof(struct hls_stream_ctx));
    ctx->index = c->nb_streams;

    return ctx;
}

static int reset_stream_info(struct aic_hls_parser *c, struct hls_stream_ctx *st,
                            struct aic_parser_av_media_info *sub_info)
{
    if (c->nb_streams == 0)
        return 0;

    if (sub_info->has_video) {
        //res may be 0 if the same video stream
        if (sub_info->video_stream.width <= 0 || sub_info->video_stream.height <= 0 ||
            sub_info->video_stream.max_ref_frames <= 0)
            return 0;

        if (st->width != sub_info->video_stream.width ||
            st->height != sub_info->video_stream.height ||
            st->max_ref_frames != sub_info->video_stream.max_ref_frames) {
            c->nb_streams = 0;
            c->stream_switched = 1;
            logd("resolution changed:%dx%d->%dx%d,", st->width, st->height,
                sub_info->video_stream.width, sub_info->video_stream.height);
            logd("ref frames changed:%d->%d\n", st->max_ref_frames,
                sub_info->video_stream.max_ref_frames);
        }
    }

    return 0;
}

static int get_segment_format(const char *url)
{
    const char *path_end = NULL;
    const char *last_dot = NULL;
    const char *query = NULL;
    const char *p = NULL;

    // Find query string start
    query = strchr(url, '?');
    path_end = query ? query : (url + strlen(url));
    logd("url len is %zd\n", strlen(url));
    // Find last '.' in the path (before query)
    last_dot = NULL;
    for (p = path_end - 1; p >= url; p--) {
        if (*p == '.') {
            last_dot = p;
            break;
        }
        if (*p == '/')  // Stop at path boundary
            break;
    }

    // No extension found, check for HLS/TS indicators
    if (!last_dot) {
        // Check URL hints for TS streaming
        if (query) {
            logd("query is %s\n", query);
            if (strstr(query, "hls_type=") ||
                strstr(query, "HlsSubType=") ||
                strstr(query, ".ts")) {
                return AIC_MUXER_TYPE_TS;  // TS
            }
        }
        logd("last_dot is null, default to ts\n");
        return AIC_MUXER_TYPE_TS;  // Default to TS
    }

    // Get actual extension (after last dot, before query)
    const char *ext = last_dot + 1;
    size_t ext_len = path_end - ext;

    // Check for MP4 variants
    if ((ext_len == 3 && !strncasecmp(ext, "mp4", 3)) ||
        (ext_len == 3 && !strncasecmp(ext, "mov", 3)) ||
        (ext_len == 3 && !strncasecmp(ext, "m4s", 3))) {
        logd("check for mp4 varaiants, ext is %s\n", ext);
        return AIC_MUXER_TYPE_MP4;  // MP4/MOV/M4S
    }

    // Check for TS
    if ((ext_len == 2 && !strncasecmp(ext, "ts", 2))) {
        logd("check for ts , ext is %s\n", ext);
        return AIC_MUXER_TYPE_TS;  // TS
    }

    // Check for other formats
    if ((ext_len == 3 && !strncasecmp(ext, "aac", 3)) ||
        (ext_len == 3 && !strncasecmp(ext, "ac3", 3))) {
        logd("check for other formats, ext is %s\n", ext);
        return AIC_MUXER_TYPE_TS;  // Audio elementary stream, treat as TS container
    }

    // Default: check if middle part contains mp4 indicator
    // For URLs like: xxx.mp4_0-0.ts or xxx.mp4_seg1.ts
    if (query && strstr(url, ".mp4")) {
        // Has .mp4 somewhere but ends with other extension
        // Check if it's actually a TS segment from MP4 source
        if (strstr(url, ".ts") || strstr(url, "hls_type=")) {
            logd("MP4 source but TS segment format detected");
            return AIC_MUXER_TYPE_TS;  // TS segment
        }
    }

    logd("default to ts\n");
    return AIC_MUXER_TYPE_TS; // Default to TS for HLS
}

// Get file extension from URL for temp file naming
static const char *get_url_ext(const char *url)
{
    const char *query = strchr(url, '?');
    const char *path_end = query ? query : url + strlen(url);
    const char *p;

    for (p = path_end - 1; p >= url; p--) {
        if (*p == '.')
            return p;
        if (*p == '/')
            break;
    }
    return NULL;
}

// Download a byte range from a URL into a temp file.
// Returns 0 on success (temp_path holds the file path), -1 on failure.
static int download_byterange(const char *url, int64_t offset, int64_t length, char *temp_path,
                              int temp_path_size)
{
    struct aic_stream *stream = NULL;
    unsigned char buf[65536];
    const char *ext;
    int fd, ret = -1;
    s64 total_read = 0;

    ext = get_url_ext(url);
    if (!ext)
        ext = ".ts";

    static int br_counter = 0;

    snprintf(temp_path, temp_path_size, "/tmp/hls_br_%d_%lld_%d%s", getpid(),
             (long long)get_time_ms(), ++br_counter, ext);

    fd = open(temp_path, O_CREAT | O_RDWR | O_TRUNC, 0600);
    if (fd < 0) {
        loge("create temp file failed: %s", temp_path);
        return -1;
    }

    if (aic_stream_open((char *)url, &stream, O_RDONLY) < 0) {
        loge("open stream failed for byte range: %s", url);
        close(fd);
        unlink(temp_path);
        temp_path[0] = '\0';
        return -1;
    }

    {
        int http_err = 0;
        aic_stream_control(stream, STREAM_GET_LAST_HTTP_ERROR, &http_err);
        if (http_err == 403 || http_err == 404 || http_err == 410) {
            loge("byte range download HTTP %d: %s", http_err, url);
            goto fail;
        }
    }

    // Per RFC 8216, offset=-1 means contiguous from previous range
    if (offset < 0)
        offset = 0;
    if (offset > 0) {
        if (aic_stream_seek(stream, offset, SEEK_SET) != offset) {
            loge("seek to offset %lld failed", (long long)offset);
            goto fail;
        }
    }

    while (total_read < length) {
        s64 to_read = length - total_read;
        s64 rd;

        if (to_read > (s64)sizeof(buf))
            to_read = sizeof(buf);
        rd = aic_stream_read(stream, buf, to_read);
        if (rd <= 0) {
            loge("read byte range failed at %lld/%lld", (long long)total_read, (long long)length);
            goto fail;
        }
        if (write(fd, buf, rd) != rd) {
            loge("write temp file failed");
            goto fail;
        }
        total_read += rd;
    }

    logi("byte range downloaded: %s [%lld-%lld] -> %s (%lld bytes)", url, (long long)offset,
         (long long)(offset + length - 1), temp_path, (long long)total_read);
    ret = 0;

fail:
    aic_stream_close(stream);
    close(fd);
    if (ret != 0) {
        unlink(temp_path);
        temp_path[0] = '\0';
    }
    return ret;
}

static int init_sub_parser(struct aic_hls_parser *c, struct hls_stream_ctx *st, const char *url);

// Try init_sub_parser with exponential backoff retry.
// Returns 1 on success, 0 if all retries failed.
static int init_sub_parser_with_retry(struct aic_hls_parser *c, struct hls_stream_ctx *st,
                                      const char *url)
{
    int attempt;
    int delay_ms = HLS_SEG_RETRY_BASE_MS;
    int64_t t0 = get_time_ms();

    for (attempt = 0; attempt <= HLS_SEG_MAX_RETRIES; attempt++) {
        int64_t t1 = get_time_ms();
        if (init_sub_parser(c, st, url) == 0) {
            logd("seg download OK attempt=%d elapsed=%lldms\n",
                   attempt, (long long)(get_time_ms() - t0));
            return 1;
        }

        logd("seg download attempt %d/%d fail, elapsed=%lldms\n",
               attempt + 1, HLS_SEG_MAX_RETRIES + 1, (long long)(get_time_ms() - t1));
        if (attempt < HLS_SEG_MAX_RETRIES) {
            logd("retry in %dms: %s\n", delay_ms, url);
            usleep(delay_ms * 1000);
            delay_ms *= 2;
            if (delay_ms > HLS_SEG_RETRY_MAX_MS)
                delay_ms = HLS_SEG_RETRY_MAX_MS;
        }
    }
    loge("seg download failed after %d retries, total elapsed=%lldms: %s",
         HLS_SEG_MAX_RETRIES + 1, (long long)(get_time_ms() - t0), url);
    return 0;
}

static int init_sub_parser(struct aic_hls_parser *c, struct hls_stream_ctx *st, const char *url)
{
    struct aic_parser_av_media_info info;
    int ret = 0;
    if (st->sub_parser) {
        st->sub_parser->destroy(st->sub_parser);
        st->sub_parser = NULL;
    }

    // Clean up previous temp file
    if (st->temp_file[0]) {
        unlink(st->temp_file);
        st->temp_file[0] = '\0';
    }

    logd("init_sub seg[%d/%d] med_seq=%lld url=%s\n",
           c->current_segment_idx, c->playlist.segment_count,
           (long long)c->playlist.media_sequence, url);

    // Resolve byte-range segment: download range to temp file
    const char *resolved_url = url;
    if (c->current_segment_idx >= 0 && c->current_segment_idx < c->playlist.segment_count &&
        c->playlist.segments[c->current_segment_idx].has_byterange) {
        struct m3u_segment *seg = &c->playlist.segments[c->current_segment_idx];
        if (download_byterange(url, seg->byterange_offset, seg->byterange_length, st->temp_file,
                               sizeof(st->temp_file)) == 0) {
            resolved_url = st->temp_file;
            logi("using temp file for byte range: %s", resolved_url);
        } else {
            loge("byte range download failed seg %d", c->current_segment_idx);
            return -1;
        }
    }

    {
        int64_t t_create = get_time_ms();
        if (get_segment_format(resolved_url) == AIC_MUXER_TYPE_MP4) {
            const char *parser_url = resolved_url;
            if (c->playlist.has_init_segment && c->playlist.init_segment_url[0]) {
                parser_url = c->playlist.init_segment_url;
                logi("Using fMP4 init segment: %s", parser_url);
            }
            if (aic_mov_parser_create((unsigned char *)parser_url, &st->sub_parser) < 0) {
                if (parser_url != resolved_url) {
                    logw("init seg failed, fallback: %s", resolved_url);
                    parser_url = resolved_url;
                    if (aic_mov_parser_create((unsigned char *)parser_url, &st->sub_parser) < 0) {
                        loge("mov parser create fail, elapsed=%lldms: %s",
                             (long long)(get_time_ms() - t_create), resolved_url);
                        return -1;
                    }
                } else {
                    loge("mov parser create fail, elapsed=%lldms: %s",
                         (long long)(get_time_ms() - t_create), resolved_url);
                    return -1;
                }
            }
        } else {
            if (aic_ts_parser_create((unsigned char *)resolved_url, &st->sub_parser) < 0) {
                loge("ts parser create fail, elapsed=%lldms: %s",
                     (long long)(get_time_ms() - t_create), resolved_url);
                return -1;
            }
        }
        logd("parser create OK, elapsed=%lldms\n", (long long)(get_time_ms() - t_create));
    }

    ret = st->sub_parser->init(st->sub_parser);
    if (ret < 0) {
        loge("init ts parser failed %d", ret);
        st->sub_parser->destroy(st->sub_parser);
        st->sub_parser = NULL;
        return -1;
    }
    strncpy(st->segment_url, url, sizeof(st->segment_url) - 1);

    // get stream media info by the first segment
    ret = st->sub_parser->get_media_info(st->sub_parser, &info);
    if (ret != 0) {
        loge("sub_parser get media info failed ret %d", ret);
        return -1;
    }
    reset_stream_info(c, st, &info);

    logi("c->nb_streams %d", c->nb_streams);
    if (c->nb_streams == 0) {
        logd("has_video=%d, has_audio=%d", info.has_video, info.has_audio);
        st->type_mask = 0;
        if (info.has_video) {
            st->type_mask |= 1 << MPP_MEDIA_TYPE_VIDEO;
            st->video_id = info.video_stream.codec_type;
            st->width = info.video_stream.width;
            st->height = info.video_stream.height;
            st->max_ref_frames = info.video_stream.max_ref_frames;
            st->video_extra_data_size = 0;
            if (info.video_stream.extra_data_size > 0) {
                if (st->video_extra_data) {
                    mpp_free(st->video_extra_data);
                    st->video_extra_data = NULL;
                }
                st->video_extra_data_size = info.video_stream.extra_data_size;
                st->video_extra_data = (unsigned char *)mpp_alloc(st->video_extra_data_size);
                if (st->video_extra_data)
                    memcpy(st->video_extra_data, info.video_stream.extra_data,
                           st->video_extra_data_size);
            }
            logd("video: %dx%d, codec=%d", st->width, st->height, st->video_id);
        }
        if (info.has_audio) {
            st->type_mask |= 1 << MPP_MEDIA_TYPE_AUDIO;
            st->audio_id = info.audio_stream[0].codec_type;
            st->channels = info.audio_stream[0].nb_channel;
            st->sample_rate = info.audio_stream[0].sample_rate;
            st->bits_per_sample = info.audio_stream[0].bits_per_sample;
            st->audio_extra_data_size = 0;
            if (info.audio_stream[0].extra_data_size > 0) {
                st->audio_extra_data_size = info.audio_stream[0].extra_data_size;
                if (st->audio_extra_data)
                    mpp_free(st->audio_extra_data);
                st->audio_extra_data = (unsigned char *)mpp_alloc(st->audio_extra_data_size);
                if (st->audio_extra_data)
                    memcpy(st->audio_extra_data, info.audio_stream[0].extra_data,
                           st->audio_extra_data_size);
            }
            logd("audio: %dHz %dch, codec=%d", st->sample_rate, st->channels, st->audio_id);
        }

        c->streams[c->nb_streams++] = st;
    }

    return 0;
}

// Reload playlist for live streaming
static int hls_reload_playlist(struct aic_hls_parser *c)
{
    struct aic_stream *new_stream = NULL;
    struct m3u_playlist *new_pl = NULL;
    int m3u8_len = HLS_LIVE_MAX_URL_SIZE;
    int64_t now = get_time_ms();
    char *m3u8_data = NULL;
    int64_t cur_seq = 0;
    int64_t new_idx = 0;
    int ret = -1;
    int len;

    // Rate limiting: RFC 8216 Sec 6.3.4, reload at target_duration/2
    {
        int interval_ms = (c->playlist.target_duration > 0)
                              ? (c->playlist.target_duration * 1000 / 2)
                              : HLS_LIVE_RELOAD_INTERVAL_MS_MAX;
        if (interval_ms < HLS_LIVE_RELOAD_INTERVAL_MS_MIN)
            interval_ms = HLS_LIVE_RELOAD_INTERVAL_MS_MIN;
        if (interval_ms > HLS_LIVE_RELOAD_INTERVAL_MS_MAX)
            interval_ms = HLS_LIVE_RELOAD_INTERVAL_MS_MAX;
        if (now - c->last_playlist_reload_ms < interval_ms) {
            logd("Playlist reload too soon, skip (interval=%dms)", interval_ms);
            return 0;
        }
    }

    m3u8_data = mpp_alloc(m3u8_len);
    if (!m3u8_data) {
        loge("Failed to alloc m3u8_data for reload");
        return -1;
    }
    new_pl = mpp_alloc(sizeof(*new_pl));
    if (!new_pl) {
        loge("Failed to alloc new_pl for reload");
        mpp_free(m3u8_data);
        return -1;
    }

    memset(m3u8_data, 0, m3u8_len);
    // Try to open a fresh stream to get the latest playlist
    logd("[reload] requesting playlist: %s\n", c->url);
    if (aic_stream_open(c->url, &new_stream, O_RDONLY) == 0) {
        int http_err = 0;
        aic_stream_control(new_stream, STREAM_GET_LAST_HTTP_ERROR, &http_err);
        if (http_err == 403) {
            logw("Playlist reload got 403, retry after backoff");
            aic_stream_close(new_stream);
            new_stream = NULL;
            usleep(100000);
            if (aic_stream_open(c->url, &new_stream, O_RDONLY) != 0) {
                logw("403 retry failed, keep using old playlist");
                mpp_free(new_pl);
                mpp_free(m3u8_data);
                return 0;
            }
        } else if (http_err == 404 || http_err == 410) {
            loge("Playlist %d, stream ended", http_err);
            aic_stream_close(new_stream);
            mpp_free(new_pl);
            mpp_free(m3u8_data);
            c->eos = 1;
            return -1;
        }
        len = aic_stream_read(new_stream, m3u8_data, m3u8_len - 1);
        aic_stream_close(new_stream);
        new_stream = NULL;
    } else {
        loge("Open new stream failed for playlist reload");
        goto fail;
    }

    if (len <= 0) {
        loge("Failed to read playlist during reload, len=%d", len);
        goto fail;
    }
    m3u8_data[len] = '\0';

    memset(new_pl, 0, sizeof(*new_pl));
    if (m3u_parse(m3u8_data, c->base_url, new_pl) < 0) {
        loge("Failed to parse reloaded playlist");
        goto fail;
    }
    memcpy(&c->playlist, new_pl, sizeof(*new_pl));
    // Handle master playlist with variants
    if (c->playlist.is_master && c->playlist.variant_count > 0) {
        struct aic_stream *sub_stream;
        int selected = m3u_select_variant(&c->playlist, 2000000);
        if (selected < 0)
            selected = 0;
        memset(m3u8_data, 0, m3u8_len);
        logi("master playlist, select variant %d", selected);
        ret = aic_stream_open(c->playlist.variants[selected].url,
                            &sub_stream, O_RDONLY);
        if (ret != 0) {
            loge("Failed to open variant stream %d", selected);
            goto fail;
        }
        len = aic_stream_read(sub_stream, m3u8_data, m3u8_len - 1);
        if (len <= 0) {
            loge("Failed to read playlist during reload, len=%d", len);
            aic_stream_close(sub_stream);
            goto fail;
        }
        aic_stream_close(sub_stream);
        m3u8_data[len] = '\0';
        memset(new_pl, 0, sizeof(*new_pl));
        if (m3u_parse(m3u8_data, c->base_url, new_pl) < 0) {
            loge("Failed to parse reloaded playlist");
            goto fail;
        }
        memcpy(&c->playlist, new_pl, sizeof(*new_pl));
    }
    // Validate: check if we got new segments
    if (c->playlist.segment_count == 0) {
        loge("Reloaded playlist has no segments");
        goto fail;
    }

    // Check if media sequence advanced
    logd("Reloading live playlist, current sequence: %ld, old sequence: %ld",
         c->playlist.media_sequence, c->last_media_sequence);
    cur_seq = c->last_media_sequence + c->current_segment_idx + 1;
    new_idx = cur_seq - c->playlist.media_sequence;

    if (new_idx < 0) {
        logw("Media seq %ld behind new window[%ld,%ld], start from first", cur_seq,
             c->playlist.media_sequence,
             c->playlist.media_sequence + c->playlist.segment_count - 1);
        new_idx = 0;
    } else if (new_idx >= c->playlist.segment_count) {
        logw("Media seq %ld ahead of new window[%ld,%ld], stay at last", cur_seq,
             c->playlist.media_sequence,
             c->playlist.media_sequence + c->playlist.segment_count - 1);
        new_idx = c->playlist.segment_count - 1;
    }
    if (new_idx >= 0 && new_idx < c->playlist.segment_count) {
        c->current_segment_idx = new_idx;
        c->last_media_sequence = c->playlist.media_sequence;
        c->last_playlist_reload_ms = now;
    }

    logd("Playlist reloaded: %d segs, med_seq=%lld->%lld, target=%ds, cur_idx=%lld, live=%d, endlist=%d\n",
           c->playlist.segment_count,
           (long long)c->last_media_sequence, (long long)c->playlist.media_sequence,
           c->playlist.target_duration, (long long)new_idx,
           c->playlist.is_live, c->playlist.has_endlist);
    if (c->playlist.segment_count > 0) {
        logd("seg[%lld] %s\n",
               (long long)c->playlist.media_sequence,
               c->playlist.segments[0].url);
        logd("seg[%lld] %s\n",
               (long long)(c->playlist.media_sequence + c->playlist.segment_count - 1),
               c->playlist.segments[c->playlist.segment_count - 1].url);
    }

    /* Detect abnormal med_seq jump: if new < old, CDN likely switched stream.
     * Signal caller to retry reload instead of using this suspicious manifest. */
    if (c->playlist.media_sequence < c->last_media_sequence) {
        logd("WARNING: med_seq jumped backwards %lld->%lld, CDN stream switch, retry reload\n",
               (long long)c->last_media_sequence, (long long)c->playlist.media_sequence);
        ret = -2;
        goto fail;
    }

    ret = 0;

fail:
    if (new_pl)
        mpp_free(new_pl);
    if (m3u8_data)
        mpp_free(m3u8_data);

    return ret;
}

static int hls_peek_live(struct aic_parser *parser, struct aic_parser_packet *pkt)
{
    struct aic_hls_parser *c = (struct aic_hls_parser *)parser;
    struct hls_stream_ctx *st = c->current_ctx;
    int ret = 0;

    while (c->live_retry_count < HLS_LIVE_MAX_RETRIES) {
        logd("Live reload attempt %d/%d\n", c->live_retry_count + 1, HLS_LIVE_MAX_RETRIES);

        {
            int reload_ret = hls_reload_playlist(c);
            if (reload_ret == -2) {
                /* med_seq jumped backwards, suspicious manifest, retry immediately */
                c->live_retry_count++;
                if (c->live_retry_count < HLS_LIVE_MAX_RETRIES) {
                    logd("Suspicious manifest, retry reload in %dms\n",
                           HLS_LIVE_RETRY_DELAY_MS);
                    usleep(HLS_LIVE_RETRY_DELAY_MS * 1000);
                }
                continue;
            }
            if (reload_ret != 0) {
                c->live_retry_count++;
                if (c->live_retry_count < HLS_LIVE_MAX_RETRIES) {
                    usleep(HLS_LIVE_RETRY_DELAY_MS * 1000);
                }
                continue;
            }
        }

        c->live_retry_count = 0;
        if (c->current_segment_idx >= 0 && c->current_segment_idx < c->playlist.segment_count) {

            if (st->sub_parser) {
                st->sub_parser->destroy(st->sub_parser);
                st->sub_parser = NULL;
            }

            while (c->current_segment_idx < c->playlist.segment_count) {
                const char *next_url = c->playlist.segments[c->current_segment_idx].url;
                int seg_ok;

                logd("Live seg[%d/%d] url=%s\n",
                       c->current_segment_idx, c->playlist.segment_count, next_url);

                seg_ok = init_sub_parser_with_retry(c, st, next_url);

                if (seg_ok) {
                    ret = st->sub_parser->peek(st->sub_parser, pkt);
                    if (!ret && c->playlist.segments[c->current_segment_idx].discontinuity) {
                        pkt->flag |= PACKET_FLAG_SOS;
                        c->last_discontinuity_seg = c->current_segment_idx;
                        logd("discontinuity seg(%d), set SOS\n",
                               c->current_segment_idx);
                    }
                    return (ret == PARSER_EOS) ? PARSER_OK : ret;
                }
                /* Segment failed after all retries: don't try next segment
                 * in the same window (likely same CDN node, will also fail).
                 * Break out and reload playlist to get a fresh manifest. */
                logd("seg[%d] download failed, trigger playlist reload\n",
                       c->current_segment_idx);
                break;
            }
            if (c->current_segment_idx >= c->playlist.segment_count)
                logd("all segments in reloaded playlist exhausted\n");
        } else {
            logd("Live stream: reloaded but no new segments available\n");
        }

        c->live_retry_count++;
        if (c->live_retry_count < HLS_LIVE_MAX_RETRIES) {
            logd("Live reload retry %d/%d, delay %dms\n",
                   c->live_retry_count, HLS_LIVE_MAX_RETRIES, HLS_LIVE_RETRY_DELAY_MS);
            usleep(HLS_LIVE_RETRY_DELAY_MS * 1000);
        }
    }

    loge("Live stream: max retries (%d) exceeded, giving up", HLS_LIVE_MAX_RETRIES);
    c->eos = 1;
    return PARSER_OK;
}

static int hls_peek(struct aic_parser *parser, struct aic_parser_packet *pkt)
{
    struct aic_hls_parser *c = (struct aic_hls_parser *)parser;
    struct hls_stream_ctx *st = c->current_ctx;
    int ret;

    if (!st || !st->sub_parser)
        return PARSER_EOS;

    ret = st->sub_parser->peek(st->sub_parser, pkt);

    if (ret == PARSER_EOS) {
        // current segment ended, free it
        if (st->sub_parser) {
            st->sub_parser->destroy(st->sub_parser);
            st->sub_parser = NULL;
        }

        if (c->current_segment_idx + 1 < c->playlist.segment_count) {
            ret = 0;
            logd("seg[%d/%d] EOS, switching to seg[%d], med_seq=%lld, is_live=%d\n",
                   c->current_segment_idx, c->playlist.segment_count,
                   c->current_segment_idx + 1,
                   (long long)c->playlist.media_sequence, c->is_live_stream);

            while (c->current_segment_idx + 1 < c->playlist.segment_count) {
                c->current_segment_idx++;
                const char *next_url = c->playlist.segments[c->current_segment_idx].url;
                int seg_ok;

                seg_ok = init_sub_parser_with_retry(c, st, next_url);

                if (seg_ok) {
                    ret = st->sub_parser->peek(st->sub_parser, pkt);
                    if (!ret && c->stream_switched) {
                        c->stream_switched = 0;
                        logd("stream switched");
                    }
                    return (ret == PARSER_EOS) ? PARSER_OK : ret;
                }
                logd("skip failed seg[%d/%d], live=%d\n", c->current_segment_idx,
                       c->playlist.segment_count, c->is_live_stream);
                if (c->is_live_stream)
                    break;
            }
        }

        // all segments exhausted or all failed
        if (c->is_live_stream) {
            logd("VOD segment loop ended, entering live reload\n");
            ret = hls_peek_live(parser, pkt);
        } else {
            c->eos = 1;
        }
    } else if (!ret) {
        if (c->stream_switched) {
            c->stream_switched = 0;
            logd("stream switched");
        }
        if (c->current_segment_idx > 0 &&
            c->current_segment_idx != c->last_discontinuity_seg &&
            c->playlist.segments[c->current_segment_idx].discontinuity) {
            pkt->flag |= PACKET_FLAG_SOS;
            c->last_discontinuity_seg = c->current_segment_idx;
            logd("discontinuity seg(%d), set SOS\n", c->current_segment_idx);
        }
    }

    return ret;
}

static s32 hls_read(struct aic_parser *parser, struct aic_parser_packet *pkt)
{
    struct aic_hls_parser *c = (struct aic_hls_parser*)parser;
    struct hls_stream_ctx *st = c->current_ctx;

    if (!st || !st->sub_parser)
        return -1;

    return st->sub_parser->read(st->sub_parser, pkt);
}

static s32 hls_get_media_info(struct aic_parser *parser, struct aic_parser_av_media_info *media)
{
    struct aic_hls_parser *c = (struct aic_hls_parser*)parser;
    s64 max_duration = 0;
    int i;

    memset(media, 0, sizeof(struct aic_parser_av_media_info));

    for (i = 0; i < c->nb_streams; i++) {
        struct hls_stream_ctx *st = c->streams[i];

        if (st->type_mask & (1 << MPP_MEDIA_TYPE_VIDEO)) {
            media->has_video = 1;
            media->video_stream.codec_type = st->video_id;
            media->video_stream.width = st->width;
            media->video_stream.height = st->height;
            media->video_stream.extra_data_size = st->video_extra_data_size;
            media->video_stream.extra_data = NULL;
            if (st->video_extra_data_size > 0 && st->video_extra_data) {
                media->video_stream.extra_data = st->video_extra_data;
            }
            logi("video: %dx%d, codec=%d, extra_data_size %d",
                st->width, st->height, st->video_id, st->video_extra_data_size);
        }
        if (st->type_mask & (1 << MPP_MEDIA_TYPE_AUDIO)) {
            media->has_audio = 1;
            media->audio_stream[media->audio_track_count].codec_type = st->audio_id;
            media->audio_stream[media->audio_track_count].nb_channel = st->channels;
            media->audio_stream[media->audio_track_count].sample_rate = st->sample_rate;
            media->audio_stream[media->audio_track_count].bits_per_sample = st->bits_per_sample;
            media->audio_stream[media->audio_track_count].extra_data_size = st->audio_extra_data_size;
            media->audio_stream[media->audio_track_count].extra_data = NULL;
            if (st->audio_extra_data_size > 0 && st->audio_extra_data) {
                media->audio_stream[media->audio_track_count].extra_data = st->audio_extra_data;
            }
            media->audio_track_count++;
            logi("audio: %dHz %dch, codec=%d", st->sample_rate, st->channels, st->audio_id);
        }
    }

    if (c->is_live_stream) {
        media->seek_able = 0;
        media->duration = -1;  // Unknown/infinite
        logi("Live stream: duration unknown, not seekable");
    } else {
        media->seek_able = 1;
        max_duration = c->playlist.segment_count * c->playlist.target_duration * 1000000LL;
        media->duration = c->playlist.total_duration <= 0 ? max_duration : c->playlist.total_duration;
    }

    logi("duration: %ld, segment_cnt:%d, target_duration:%d",
         media->duration, c->playlist.segment_count, c->playlist.target_duration);

    return 0;
}

static s32 hls_seek(struct aic_parser *parser, s64 time)
{
    struct aic_hls_parser *c = (struct aic_hls_parser*)parser;
    struct hls_stream_ctx *st = c->current_ctx;
    const char *target_url = NULL;
    int target_seg = 0, i = 0;
    int ret = 0;

    if (c->is_live_stream) {
        loge("Live stream: seek is not supported");
        return -1;
    }
    if (!st || !st->sub_parser || c->playlist.target_duration == 0)
        return -1;

    logi("seek_time:%ld, total_duration:%ld", time, c->playlist.total_duration);
    // Accumulate actual segment durations to find target segment
    {
        int64_t accumulated = 0;
        target_seg = 0;
        for (i = 0; i < c->playlist.segment_count; i++) {
            accumulated += c->playlist.segments[i].duration;
            if (accumulated > time) {
                target_seg = i;
                break;
            }
        }
        if (target_seg >= c->playlist.segment_count)
            target_seg = c->playlist.segment_count - 1;
    }
    logi("seek time: %ld, search segment: %d-%d, duration: %ld.",
         time, target_seg, c->playlist.segment_count,
         c->playlist.segments[target_seg].duration);

    c->current_segment_idx = target_seg;
    target_url = c->playlist.segments[target_seg].url;
    if (st->sub_parser) {
        st->sub_parser->destroy(st->sub_parser);
        st->sub_parser = NULL;
    }
    logi("target_url:%s.", target_url);
    ret = init_sub_parser(c, st, target_url);
    if (ret != 0) {
        loge("init sub parser failed %d", ret);
    }
    return 0;
}

static s32 hls_init(struct aic_parser *parser)
{
    struct aic_hls_parser *c = (struct aic_hls_parser*)parser;

    if (!c->url) {
        loge("url is null");
        return -1;
    }

    // get base url
    memset(c->base_url, 0, sizeof(c->base_url));
    get_base_url(c->url, c->base_url, sizeof(c->base_url));

    // parse m3u8
    if (m3u_parse_stream(hls_stream_read_line, c->stream,
                         c->base_url, &c->playlist) < 0) {
        loge("parse m3u8 failed");
        return -1;
    }

    c->is_live_stream = c->playlist.is_live;
    c->last_media_sequence = c->playlist.media_sequence;
    c->last_playlist_reload_ms = get_time_ms();
    c->live_retry_count = 0;

    // Check HTTP error before proceeding
    {
        int http_err = 0;
        aic_stream_control(c->stream, STREAM_GET_LAST_HTTP_ERROR, &http_err);
        if (http_err == 403) {
            loge("HTTP 403 Forbidden - Access denied, abort init");
            return -1;
        }
    }

    // if master playlist, select best variant based on bandwidth
    if (c->playlist.is_master && c->playlist.variant_count > 0) {
        struct aic_stream *sub_stream;
        int selected = m3u_select_variant(&c->playlist, 2000000);
        if (selected < 0)
            selected = 0;

        logd("master playlist, select variant %d", selected);

        if (aic_stream_open(c->playlist.variants[selected].url,
                            &sub_stream, O_RDONLY) == 0) {
            memset(&c->playlist, 0, sizeof(c->playlist));
            m3u_parse_stream(hls_stream_read_line, sub_stream,
                             c->base_url, &c->playlist);
            aic_stream_close(sub_stream);
        }
    }

    // create stream context
    struct hls_stream_ctx *st = create_stream_ctx(c);
    if (!st) {
        loge("create stream context failed");
        return -1;
    }

    c->current_ctx = st;

    // download first segment
    if (c->playlist.segment_count > 0) {
        st->current_segment = 0;
        c->current_segment_idx = 0;
        const char *first_url = c->playlist.segments[0].url;
        if (init_sub_parser(c, st, first_url) < 0) {
            loge("init first segment failed");
            mpp_free(st);
            c->current_ctx = NULL;
            return -1;
        }
    }

    c->find_playlist = 1;

    return 0;
}

static s32 hls_destroy(struct aic_parser *parser)
{
    struct aic_hls_parser *c = (struct aic_hls_parser*)parser;
    int i;

    if (!c)
        return -1;

    for (i = 0; i < c->nb_streams; i++) {
        struct hls_stream_ctx *st = c->streams[i];
        if (st) {
            if (st->sub_parser)
                st->sub_parser->destroy(st->sub_parser);
            if (st->video_extra_data)
                mpp_free(st->video_extra_data);
            if (st->audio_extra_data)
                mpp_free(st->audio_extra_data);
            mpp_free(st);
        }
    }

    while (c->cache_head) {
        struct segment_cache *cache = c->cache_head;
        c->cache_head = cache->next;
        if (cache->data)
            mpp_free(cache->data);
        mpp_free(cache);
    }

    if (c->url)
        mpp_free(c->url);

    if (c->stream)
        aic_stream_close(c->stream);

    mpp_free(c);
    return 0;
}

s32 aic_hls_parser_create(char *uri, struct aic_parser **parser)
{
    struct aic_hls_parser *c = NULL;

    c = (struct aic_hls_parser*)mpp_alloc(sizeof(struct aic_hls_parser));
    if (!c) {
        loge("alloc hls parser failed");
        return -1;
    }
    memset(c, 0, sizeof(struct aic_hls_parser));

    if (aic_stream_open(uri, &c->stream, O_RDONLY) < 0) {
        loge("stream open failed");
        goto exit;
    }
    // get url from stream
    c->url = strdup(uri);
    if (!c->url)
        goto exit;

    c->base.peek = hls_peek;
    c->base.read = hls_read;
    c->base.get_media_info = hls_get_media_info;
    c->base.seek = hls_seek;
    c->base.init = hls_init;
    c->base.destroy = hls_destroy;
    c->base.control = NULL;

    *parser = &c->base;

    return 0;

exit:
    if (c->stream)
        aic_stream_close(c->stream);
    if (c->url)
        free(c->url);
    mpp_free(c);
    return -1;
}
