/*
 * Copyright (C) 2020-2026 ArtInChip Technology Co. Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 *  author: <xiaodong.zhao@artinchip.com>
 *  Desc: aic_http_stream
 */

/*why the macro definition is placed here:
after the header file ,the complier error*/

#define _LARGEFILE64_SOURCE

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <curl/curl.h>
#include <pthread.h>
#include <inttypes.h>

#include "aic_http_stream.h"
#include "aic_stream.h"
#include "mpp_log.h"
#include "mpp_mem.h"

//#define HTTP_STREAM_DEBUG
#define HTTP_STREAM_INFINITE_RETRY   (200)
#define HTTP_STREAM_BUFFER_HALF_SIZE (1024 * 1024)
#define HTTP_STREAM_BUFFER_SIZE (HTTP_STREAM_BUFFER_HALF_SIZE * 2)

struct aic_http_stream {
    struct aic_stream base;

    CURLM *multi_handle;
    CURL *easy;
    char *url;
    char *redirect_url;

    s64 file_size;
    s64 file_pos;

    unsigned char *buf;
    s64 buf_size;
    s64 rd;
    s64 wt;

    pthread_t tid;
    int eos;
    int stop_flag;
    int write_flag;
    int init_flag;
    int live_stream;
    int retry_count;
    int last_http_code;

    pthread_mutex_t mutex;

    time_t last_progress_time;
    curl_off_t last_dlnow;
};

/* Context for precheck redirect URL capture */
struct precheck_ctx {
    char *redirect_url;
};

static int curl_easy_opt_init(struct aic_http_stream *stream, s64 resume_from);

static inline unsigned long buf_readable(const struct aic_http_stream *stream)
{
    return stream->wt - stream->rd;
}

static inline unsigned long buf_writable(const struct aic_http_stream *stream)
{
    unsigned long ret = 0;
    s64 buf_writeable = stream->buf_size - buf_readable(stream);

    if (buf_writeable >= HTTP_STREAM_BUFFER_HALF_SIZE) {
        ret = buf_writeable - HTTP_STREAM_BUFFER_HALF_SIZE;
    }

    return ret;
}

static int reset_http_stream(struct aic_http_stream *stream)
{
    pthread_mutex_lock(&stream->mutex);
    stream->rd = 0;
    stream->wt = 0;
    stream->eos = 0;
    stream->stop_flag = 0;
    pthread_mutex_unlock(&stream->mutex);

    return 0;
}

static size_t write_callback(void *ptr, size_t sz, size_t nmemb, void *userdata)
{
    struct aic_http_stream *stream = (struct aic_http_stream *)userdata;
    const char *data = (const char *)ptr;
    unsigned long wt, first, avail;
    size_t size = sz * nmemb;

    if (NULL == stream) {
        loge("invalid parameter!");
        return 0;
    }

    if (size > HTTP_STREAM_BUFFER_HALF_SIZE) {
        loge("buffer too small, need:%ld actual:%d", size, HTTP_STREAM_BUFFER_HALF_SIZE);
    }

    // wait buffer
    while (1) {
        avail = buf_writable(stream);
        if (size <= avail) {
            break;
        }

        if (stream->stop_flag) {
            return 0;
        }

        usleep(100);
    }

    pthread_mutex_lock(&stream->mutex);
    wt = stream->wt % stream->buf_size;
    first = stream->buf_size - wt;
    if (first > size) {
        first = size;
    }
    memcpy(stream->buf + wt, data, first);

    if (size > first) {
        data += first;
        memcpy(stream->buf, data, size - first);
    }
    stream->wt += size;
    pthread_mutex_unlock(&stream->mutex);

    return size;
}

static size_t header_callback(void *ptr, size_t size, size_t nmemb, void *userdata)
{
    struct aic_http_stream *stream = (struct aic_http_stream *)userdata;
    size_t total_size = size * nmemb;
    char *header = (char *)ptr;

    if (NULL == stream) {
        loge("invalid parameter!");
        return -1;
    }

    logd("header callback: %s", header);

    if (strstr(header, "HTTP/") == header) {
        int code = 0;
        char *ip = NULL;
        char *end;
        if (sscanf(header, "HTTP/%*d.%*d %d", &code) >= 1)
            stream->last_http_code = code;
        if (stream->easy)
            curl_easy_getinfo(stream->easy, CURLINFO_PRIMARY_IP, &ip);
        /* strip trailing \r\n before print, or \r in header would overwrite output */
        end = (char *)header + strlen(header) - 1;
        while (end >= header && (*end == '\r' || *end == '\n')) end--;
        *(end + 1) = '\0';
        logd("[HTTP] %s (server_ip=%s)\n", header, ip ? ip : "unknown");
        if (strstr(header, "302")) {
            printf("Received 302 Redirect - Following redirect...\n");
        } else if (strstr(header, "403")) {
            loge("Received 403 Forbidden - Access denied");
        } else if (strstr(header, "404")) {
            loge("Received 404 Not Found - URL may be expired");
        } else if (strstr(header, "503")) {
            loge("Received 503 Service Unavailable - Server busy");
        }
    }

    if (0 == stream->init_flag) {
        if (0 == strncmp(header, "Content-Length:", 15) ||
            0 == strncmp(header, "content-length:", 15)) {
            char *value = header + 15;
            while (*value == ' ') {
                value++;
            }

            stream->file_size = atoll(value);
            stream->init_flag = 1;

            logd("init_flag:%d file_size:%ld\n", stream->init_flag, stream->file_size);
        } else if ((strncasecmp(header, "Transfer-Encoding:", 18) == 0 && strstr(header, "chunked")) ||
                 (strncasecmp(header, "Accept-Ranges:", 14) == 0 && strstr(header, "none"))) {
            //Live stream detection - no Content-Length but has Transfer-Encoding or Accept-Ranges
            stream->file_size = -1;
            stream->init_flag = 1;
            stream->live_stream = 1;
            logd("Live stream detected (chunked/no ranges), init_flag set");
        } else if (stream->file_size == 0 && strncasecmp(header, "Connection:", 11) == 0 &&
                 strstr(header, "close")) {
            // Live stream with Connection: close and no Content-Length
            stream->file_size = -1;
            stream->init_flag = 1;
            stream->live_stream = 1;
            logd("Live stream detected (Connection: close, no Content-Length), init_flag set");
        }
    }


    return total_size;
}

static size_t simple_header_callback(void *ptr, size_t size, size_t nmemb, void *userdata)
{
    struct precheck_ctx *ctx = (struct precheck_ctx *)userdata;
    const char *header = (const char *)ptr;

    if (strncasecmp(header, "Location:", 9) == 0) {
        const char *start = header + 9;
        while (*start == ' ') start++;

        const char *end = start + strlen(start) - 1;
        while (end > start && (*end == '\r' || *end == '\n')) end--;

        int len = end - start + 1;
        char *location = (char *)malloc(len + 1);
        if (location) {
            memcpy(location, start, len);
            location[len] = '\0';

            if (ctx->redirect_url) {
                free(ctx->redirect_url);
            }
            ctx->redirect_url = location;
            logi("Captured redirect URL: %s", location);
        }
    }
    return size * nmemb;
}

static size_t discard_write_cb(void *ptr, size_t size, size_t nmemb, void *userdata)
{
    (void)ptr;
    (void)userdata;
    return size * nmemb;
}

static int test_url_connect(char *uri, char **final_url)
{
    CURL *easy_handle = NULL;
    CURLcode res = CURLE_OK;
    long resp_code = 0;
    int use_head = 1;
    struct precheck_ctx ctx = {0};

    easy_handle = curl_easy_init();
    if (!easy_handle) {
        loge("curl_easy_init() failed");
        *final_url = strdup(uri);
        return 0;
    }

retry:
    /* Configure common options */
    curl_easy_setopt(easy_handle, CURLOPT_URL, uri);
    curl_easy_setopt(easy_handle, CURLOPT_CONNECTTIMEOUT, 10L);
    curl_easy_setopt(easy_handle, CURLOPT_TIMEOUT, 10L);
    curl_easy_setopt(easy_handle, CURLOPT_FOLLOWLOCATION, 0L);
    curl_easy_setopt(easy_handle, CURLOPT_HEADERFUNCTION, simple_header_callback);
    curl_easy_setopt(easy_handle, CURLOPT_HEADERDATA, &ctx);
    curl_easy_setopt(easy_handle, CURLOPT_WRITEFUNCTION, discard_write_cb);
    curl_easy_setopt(easy_handle, CURLOPT_USERAGENT, "MPP-HttpStream/1.0");
    curl_easy_setopt(easy_handle, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(easy_handle, CURLOPT_SSL_VERIFYHOST, 0L);

    /* HEAD for first try, GET for fallback */
    curl_easy_setopt(easy_handle, CURLOPT_NOBODY, use_head ? 1L : 0L);

    res = curl_easy_perform(easy_handle);
    curl_easy_getinfo(easy_handle, CURLINFO_RESPONSE_CODE, &resp_code);

    if (res == CURLE_OK && (resp_code == 301 || resp_code == 302)) {
        logi("%s res:%d, resp_code:%ld", use_head ? "HEAD" : "GET", (int)res, resp_code);
        if (ctx.redirect_url) {
            logi("URL redirect detected: %s", ctx.redirect_url);
            *final_url = ctx.redirect_url;
        } else {
            logw("Redirect(%ld) but no Location, use original", resp_code);
            *final_url = strdup(uri);
        }
    } else if (res == CURLE_OK) {
        /* Success but no redirect */
        *final_url = strdup(uri);
    } else {
        /* Failed */
        if (use_head) {
            logw("HEAD precheck failed: res=%d code=%ld, fallback to GET",
                 (int)res, resp_code);
            use_head = 0;
            curl_easy_reset(easy_handle);
            if (ctx.redirect_url) {
                free(ctx.redirect_url);
                ctx.redirect_url = NULL;
            }
            goto retry;
        } else {
            logw("GET precheck also failed: res=%d code=%ld, use original URL",
                 (int)res, resp_code);
            *final_url = strdup(uri);
        }
    }

    curl_easy_cleanup(easy_handle);
    return 0;
}


static int progress_cb(void *clientp, curl_off_t dltotal, curl_off_t dlnow, curl_off_t ultotal, curl_off_t ulnow)
{
    struct aic_http_stream *stream = (struct aic_http_stream *)clientp;
    time_t now = time(NULL);

    // Reset timeout if making progress
    if (dlnow > stream->last_dlnow) {
        stream->last_progress_time = now;
        stream->last_dlnow = dlnow;
    }

    if (dltotal > 0) {
        logi("\r[%5.1f%%]  %lld / %lld  bytes\n", 100.0 * dlnow / dltotal,
                (long long)dlnow, (long long)dltotal);
    }
    // Check for stall (no progress for 30s)
    if ((now - stream->last_progress_time) > 30 && dlnow < dltotal) {
        logi("Download stalled, no progress for 30s");
        // Return non-zero to abort? Or let low-speed timeout handle it
    }
    if ((dltotal > 0) && (dlnow == dltotal)) {
        logi("http stream eos");
        stream->eos = 1;
    }

    return stream->stop_flag;
}


static int check_transfer_errors(struct aic_http_stream *stream, int *need_resume)
{
    int transfer_error = 0;
    CURLcode result;
    CURLMsg *msg;
    int msgs_left;

    *need_resume = 0;

    if (stream->live_stream)
        return 0;

    while ((msg = curl_multi_info_read(stream->multi_handle, &msgs_left))) {
        if (msg->msg != CURLMSG_DONE)
            continue;

        result = msg->data.result;
        if (result == CURLE_OK) {
            // Transfer completed successfully, reset retry counter
            stream->retry_count = 0;
            continue;
        }

        logw("Transfer failed: %s (code %d)", curl_easy_strerror(result), result);
        transfer_error = 1;

        if (stream->retry_count >= HTTP_STREAM_INFINITE_RETRY) {
            loge("Max retries (%d) exceeded, giving up", HTTP_STREAM_INFINITE_RETRY);
            continue;
        }

        // This is a recoverable error for VOD
        int recoverable = (result == CURLE_PARTIAL_FILE ||
                           result == CURLE_RECV_ERROR ||
                           result == CURLE_OPERATION_TIMEDOUT ||
                           result == CURLE_COULDNT_CONNECT ||
                           result == CURLE_HTTP_RETURNED_ERROR);

        if (recoverable && stream->file_pos > 0) {
            loge("VOD interrupted at %ld bytes, will retry %d/%d",
                    stream->file_pos, stream->retry_count + 1,
                    HTTP_STREAM_INFINITE_RETRY);
            stream->retry_count++;
            *need_resume = 1;
        } else if (recoverable) {
            logw("Transfer failed at start, cannot resume (no data received yet)");
            stream->retry_count++;
        }
    }

    return transfer_error;
}

void *http_download_thread(void *arg)
{
    struct aic_http_stream *stream = (struct aic_http_stream *)arg;
    int running = 0, need_resume = 0;
    CURLMcode mc;
    int has_error = 0;

    do {
        if (need_resume) {
            curl_multi_remove_handle(stream->multi_handle, stream->easy);
            curl_easy_cleanup(stream->easy);
            stream->easy = NULL;
            reset_http_stream(stream);
            if (curl_easy_opt_init(stream, stream->file_pos) != 0) {
                loge("Failed to re-init for resume.");
                has_error = 1;
                break;
            }
            need_resume = 0;
            has_error = 0; /* Reset error flag after successful reconnection */
        }
        mc = curl_multi_perform(stream->multi_handle, &running);

        if (running) {
            /* wait for activity, timeout or "nothing" */
            mc = curl_multi_poll(stream->multi_handle, NULL, 0, 1000, NULL);
        } else {
            logi("\nno more transfers\n");
        }

        if (mc) {
            printf("curl_multi_perform(), code %d\n", mc);
            has_error = 1;
            break;
        }

        //check if transfer closed prematurely
        if (check_transfer_errors(stream, &need_resume)) {
            has_error = 1;
        }
        if (need_resume) {
            logd("need resume: %ld", stream->file_pos);
            running = 1;
        }

        if (stream->stop_flag) {
            logi("\nstop download thread!\n");
            break;
        }
    } while (running);

    // Only set eos on normal completion (no error, not stopped by user)
    if (!stream->stop_flag && !has_error) {
        stream->eos = 1;
        logd("http stream eos");
    }

    /* Cleanup easy handle if not already cleaned by http_stream_open exit path */
    if (stream->easy) {
        curl_multi_remove_handle(stream->multi_handle, stream->easy);
        curl_easy_cleanup(stream->easy);
        stream->easy = NULL;
    }

    logi("http download thread exit!\n");

    return NULL;
}

static int start_http_down_thread(struct aic_http_stream *stream)
{
    return pthread_create(&stream->tid, NULL, http_download_thread, (void *)stream);
}

static int curl_easy_opt_init(struct aic_http_stream *stream, s64 resume_from)
{
    stream->easy = curl_easy_init();
    if (NULL == stream->easy) {
        loge("curl_easy_init failed!");
        return -1;
    }
    const char *effective_url = stream->redirect_url ? stream->redirect_url : stream->url;
    logi("effective_url:%s.", effective_url);
    curl_easy_setopt(stream->easy, CURLOPT_URL, effective_url);
    curl_easy_setopt(stream->easy, CURLOPT_WRITEDATA, stream);
    curl_easy_setopt(stream->easy, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(stream->easy, CURLOPT_HEADERDATA, stream);
    curl_easy_setopt(stream->easy, CURLOPT_HEADERFUNCTION, header_callback);
    curl_easy_setopt(stream->easy, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(stream->easy, CURLOPT_MAXREDIRS, 10L);
    curl_easy_setopt(stream->easy, CURLOPT_POSTREDIR, CURL_REDIR_POST_ALL);
    curl_easy_setopt(stream->easy, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(stream->easy, CURLOPT_SSL_VERIFYHOST, 0L);

    // Timeout settings - KEY CHANGES
    curl_easy_setopt(stream->easy, CURLOPT_TIMEOUT, 0L);           // No total timeout
    curl_easy_setopt(stream->easy, CURLOPT_CONNECTTIMEOUT, 30L);   // Connection only
    curl_easy_setopt(stream->easy, CURLOPT_LOW_SPEED_TIME, 60L);   // 60s low speed limit
    curl_easy_setopt(stream->easy, CURLOPT_LOW_SPEED_LIMIT, 512L); // 512B/s minimum

    // TCP keepalive to prevent middlebox drops
    curl_easy_setopt(stream->easy, CURLOPT_TCP_KEEPALIVE, 1L);
    curl_easy_setopt(stream->easy, CURLOPT_TCP_KEEPIDLE, 120L);
    curl_easy_setopt(stream->easy, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(stream->easy, CURLOPT_XFERINFODATA, stream);
    curl_easy_setopt(stream->easy, CURLOPT_XFERINFOFUNCTION, progress_cb);
    curl_easy_setopt(stream->easy, CURLOPT_USERAGENT, "MPP-HttpStream/1.0");
    curl_easy_setopt(stream->easy, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(stream->easy, CURLOPT_DNS_CACHE_TIMEOUT, 300L);
#ifdef HTTP_STREAM_DEBUG
    curl_easy_setopt(stream->easy, CURLOPT_VERBOSE, 1L);
#else
    curl_easy_setopt(stream->easy, CURLOPT_VERBOSE, 0L);
#endif
    if (resume_from > 0) {
        char range[64] = {0};
        snprintf(range, sizeof(range), "%"PRId64"-", (long)resume_from);
        curl_easy_setopt(stream->easy, CURLOPT_RANGE, range);
        logi("Resume from bytes: %ld, range: %s.\n", (long)resume_from, range);
    }
    /* add the individual transfer */
    curl_multi_add_handle(stream->multi_handle, stream->easy);
    curl_multi_setopt(stream->multi_handle, CURLMOPT_PIPELINING, CURLPIPE_MULTIPLEX);

    return 0;
}

static s64 http_stream_read(struct aic_stream *s, void *buf, s64 len)
{
    struct aic_http_stream *stream = (struct aic_http_stream *)s;
    unsigned long rd, avail, first;
    int time_out = 100000;
    s64 read_len = len;

    if (len > HTTP_STREAM_BUFFER_HALF_SIZE) {
        loge("buffer too small, need:%ld actual:%d", len, HTTP_STREAM_BUFFER_HALF_SIZE);
    }

    // wait date ready
    while (time_out--) {
        pthread_mutex_lock(&stream->mutex);
        avail = buf_readable(stream);
        pthread_mutex_unlock(&stream->mutex);
        if (avail > len) {
            break;
        } else {
            if (stream->eos) {
                if (avail > 0) {
                    read_len = avail;
                    goto read_out;
                }
                return 0;
            } else if (stream->stop_flag) {
                return 0;
            }
            usleep(100);
        }
    }
    if (time_out <= 0) {
        loge("http stream read %ld timeout, stop_flag:%d", len, stream->stop_flag);
        return -1;
    }

read_out:
    pthread_mutex_lock(&stream->mutex);
    rd = stream->rd % stream->buf_size;
    first = stream->buf_size - rd;
    if (first > read_len) {
        first = read_len;
    }

    memcpy((char *)buf, stream->buf + rd, first);
    if (read_len > first) {
        memcpy((char *)buf + first, stream->buf, read_len - first);
    }
    stream->rd += read_len;
    stream->file_pos += read_len;

    pthread_mutex_unlock(&stream->mutex);

    return read_len;
}

static s64 http_stream_write(struct aic_stream *s, void *buf, s64 len)
{
    struct aic_http_stream *stream = (struct aic_http_stream *)s;

    if (!stream->write_flag) {
        loge("not support write");
        return -1;
    }

    return 0;
}

static s64 http_stream_tell(struct aic_stream *s)
{
    struct aic_http_stream *stream = (struct aic_http_stream *)s;
    s64 pos = 0;

    pthread_mutex_lock(&stream->mutex);
    pos = (stream->write_flag) ? (stream->file_pos + buf_readable(stream)) : (stream->file_pos);
    pthread_mutex_unlock(&stream->mutex);

    return pos;
}

static s32 http_stream_close(struct aic_stream *s)
{
    struct aic_http_stream *stream = (struct aic_http_stream *)s;
    if (NULL == stream) {
        loge("invalid parameter!");
        return -1;
    }

    logd("file_stream_close");
    stream->stop_flag = 1;
    if (stream->multi_handle)
        curl_multi_wakeup(stream->multi_handle);

    pthread_join(stream->tid, NULL);
    if (stream->multi_handle) {
        curl_multi_cleanup(stream->multi_handle);
        stream->multi_handle = NULL;
    }

    pthread_mutex_destroy(&stream->mutex);

    if (stream->redirect_url && stream->redirect_url != stream->url) {
        free(stream->redirect_url);
        stream->redirect_url = NULL;
    }

    if (stream->url) {
        free(stream->url);
        stream->url = NULL;
    }

    mpp_free(stream->buf);
    mpp_free(stream);

    return 0;
}

static s64 http_stream_seek(struct aic_stream *s, s64 offset, s32 whence)
{
    struct aic_http_stream *stream = (struct aic_http_stream *)s;
    s64 left_data, buf_start, buf_end, new_pos, file_pos;

    pthread_mutex_lock(&stream->mutex);
    file_pos = stream->file_pos;
    switch (whence) {
        case SEEK_SET:
            new_pos = offset;
            break;
        case SEEK_CUR:
            new_pos = file_pos + offset;
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

    // Boundary Check
    if (new_pos < 0) {
        new_pos = 0;
    }
    if (stream->file_size > 0 && new_pos > stream->file_size) {
        new_pos = stream->file_size;
    }

    // Check if it is within the buffer
    left_data = stream->buf_size - buf_readable(stream);
    if (left_data > file_pos) {
        left_data = file_pos;
    }
    buf_start = file_pos - left_data;
    buf_end = file_pos + buf_readable(stream);

    if (new_pos >= buf_start && new_pos <= buf_end) {
        s64 new_rd = stream->rd + (new_pos - file_pos);
        if (new_rd >= 0) {
            stream->rd = new_rd;
            stream->file_pos = new_pos;
            pthread_mutex_unlock(&stream->mutex);
            return new_pos;
        }
    }

    stream->file_pos = new_pos;
    pthread_mutex_unlock(&stream->mutex);

    logi("stop download thread!\n");
    stream->stop_flag = 1;

    if (stream->multi_handle) {
        curl_multi_wakeup(stream->multi_handle);
    }

    pthread_join(stream->tid, NULL);
    logd("download thread finish!\n");
    reset_http_stream(stream);

    curl_easy_opt_init(stream, stream->file_pos);
    start_http_down_thread(stream);

    return new_pos;
}

static s64 http_stream_size(struct aic_stream *s)
{
    struct aic_http_stream *stream = (struct aic_http_stream *)s;

    return stream->file_size;
}


static s32 http_stream_control(struct aic_stream *s, enum stream_command cmd, void *params)
{
    struct aic_http_stream *stream = (struct aic_http_stream *)s;

    switch (cmd) {
        case STREAM_GET_LIVE_STATE:
            if (!params)
                return -1;
            *(int *)params = stream->live_stream;
            break;
        case STREAM_GET_LAST_HTTP_ERROR:
            if (!params)
                return -1;
            *(int *)params = stream->last_http_code;
            break;
        case STREAM_GET_MEASURED_BANDWIDTH:
            if (!params)
                return -1;
            *(int *)params = 0;
            break;

        default:
            return -1;
    }

    return 0;
}

s32 http_stream_open(const char *uri, struct aic_stream **s, int flags)
{
    struct aic_http_stream *stream = NULL;
    char *effective_url = NULL;
    s32 thread_started = 0;
    s32 ret = 0;

    if (test_url_connect((char *)uri, &effective_url) != 0)
        return -1;

    stream = (struct aic_http_stream *)mpp_alloc(sizeof(struct aic_http_stream));
    if (stream == NULL) {
        loge("mpp_alloc aic_stream ailed!!!!!\n");
        ret = -1;
        goto exit;
    }

    memset(stream, 0, sizeof(struct aic_http_stream));
    stream->url = strdup(uri);
    stream->redirect_url = effective_url;
    stream->buf_size = HTTP_STREAM_BUFFER_SIZE;
    pthread_mutex_init(&stream->mutex, NULL);

    stream->buf = (unsigned char *)mpp_alloc(stream->buf_size);
    if (stream->buf == NULL) {
        loge("alloc buf failed");
        ret = -2;
        goto exit;
    }

    stream->multi_handle = curl_multi_init();
    curl_easy_opt_init(stream, 0);

    if (start_http_down_thread(stream) != 0) {
        loge("create http_download_thread failed!");
        ret = -4;
        goto exit;
    }
    thread_started = 1;

    stream->base.read = http_stream_read;
    stream->base.write = http_stream_write;
    stream->base.close = http_stream_close;
    stream->base.seek = http_stream_seek;
    stream->base.size = http_stream_size;
    stream->base.tell = http_stream_tell;
    stream->base.control = http_stream_control;
    *s = &stream->base;

    int n = 0;
    while (1) {
        if (stream->init_flag) {
            break;
        }

        /* Early exit on HTTP error: server returned 4xx/5xx, no point waiting */
        if (stream->last_http_code >= 400) {
            loge("HTTP error %d, abort early", stream->last_http_code);
            ret = -5;
            goto exit;
        }

        if (++n > 10000) {
            loge("wait for init timeout");
            ret = -5;
            goto exit;
        }
        usleep(1000);
    }

    return ret;

exit:
    if (effective_url && effective_url != uri)
        free(effective_url);

    if (stream) {
        /* Ensure download thread is stopped and joined before cleanup */
        if (thread_started) {
            stream->stop_flag = 1;
            if (stream->multi_handle)
                curl_multi_wakeup(stream->multi_handle);
            pthread_join(stream->tid, NULL);
        }

        /* Cleanup curl handles only if thread hasn't already cleared them */
        if (stream->easy) {
            curl_multi_remove_handle(stream->multi_handle, stream->easy);
            curl_easy_cleanup(stream->easy);
            stream->easy = NULL;
        }
        if (stream->multi_handle) {
            curl_multi_cleanup(stream->multi_handle);
            stream->multi_handle = NULL;
        }
        if (stream->url) {
            free(stream->url);
        }
        if (stream->buf) {
            mpp_free(stream->buf);
        }
        pthread_mutex_destroy(&stream->mutex);
        stream->redirect_url = NULL;
        mpp_free(stream);
    }

    *s = NULL;
    return ret;
}
