/*
 * Copyright (C) 2020-2026 ArtInChip Technology Co. Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * author: <che.jiang@artinchip.com>
 * Desc: m3u - M3U8 parser implementation
 */

#define LOG_TAG "m3u"

#include "m3u.h"
#include "mpp_log.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

void m3u_build_url(const char *base_url, const char *relative_url, char *full_url, int size)
{
    if (!base_url || !relative_url || !full_url || size <= 0)
        return;

    if (strstr(relative_url, "http://") || strstr(relative_url, "https://")) {
        strncpy(full_url, relative_url, size - 1);
        full_url[size - 1] = '\0';
    } else {
        snprintf(full_url, size, "%s%s", base_url, relative_url);
    }
}

static double parse_extinf_duration(char *line)
{
    char *duration_str = line + 8;
    char *comma = strchr(duration_str, ',');
    double duration = 0;

    if (comma) {
        *comma = '\0';
        duration = atof(duration_str);
        *comma = ',';
    } else {
        duration = atof(duration_str);
    }

    logd("  duration: %f", duration);
    return duration;
}

static void parse_stream_inf_params(char *params, struct m3u_variant *variant)
{
    char *bw_str = strstr(params, "BANDWIDTH=");
    char *res_str = strstr(params, "RESOLUTION=");

    if (bw_str) {
        bw_str += 10;
        variant->bandwidth = atoi(bw_str);
    }

    if (res_str) {
        res_str += 11;
        sscanf(res_str, "%dx%d", &variant->width, &variant->height);
    }
}

static int handle_stream_inf_line(struct m3u_playlist *pl, char *line, char **next_line)
{
    char *params = line + 18;

    if (M3U_MAX_VARIANTS <= pl->variant_count) {
        loge("too many variants %d, ignored", pl->variant_count);
        mpp_assert(0);
        return -1;
    }

    parse_stream_inf_params(params, &pl->variants[pl->variant_count]);

    if (*next_line) {
        char *url_line = *next_line;
        char *next = strchr(url_line, '\n');
        if (next) {
            *next = '\0';
            *next_line = next + 1;
        }

        char *end = url_line + strlen(url_line) - 1;
        if (end >= url_line && *end == '\r')
            *end = '\0';

        if (*url_line && *url_line != '#') {
            m3u_build_url(pl->base_url, url_line, pl->variants[pl->variant_count].url,
                          sizeof(pl->variants[pl->variant_count].url));
            pl->variant_count++;
        }
    }

    return 0;
}

static int handle_url_line(struct m3u_playlist *pl, char *line, int64_t segment_duration,
                           int *segment_idx, int *current_discontinuity, int *current_has_byterange,
                           int64_t *current_byterange_length, int64_t *current_byterange_offset,
                           char *current_pdt, int current_pdt_size)
{
    if (pl->variant_count > 0) {
        logw("unexpected URL in master playlist: %s", line);
        return 0;
    }

    if (*segment_idx < M3U_MAX_SEGMENTS) {
        m3u_build_url(pl->base_url, line, pl->segments[*segment_idx].url,
                      sizeof(pl->segments[*segment_idx].url));

        pl->segments[*segment_idx].duration = segment_duration;
        pl->segments[*segment_idx].sequence = pl->media_sequence + *segment_idx;
        pl->segments[*segment_idx].discontinuity = *current_discontinuity;
        *current_discontinuity = 0;
        if (*current_has_byterange) {
            pl->segments[*segment_idx].has_byterange = 1;
            pl->segments[*segment_idx].byterange_length = *current_byterange_length;
            pl->segments[*segment_idx].byterange_offset = *current_byterange_offset;
            *current_has_byterange = 0;
            *current_byterange_length = 0;
            *current_byterange_offset = -1;
        }
        if (current_pdt && current_pdt[0]) {
            strncpy(pl->segments[*segment_idx].program_date_time, current_pdt,
                    sizeof(pl->segments[*segment_idx].program_date_time) - 1);
            current_pdt[0] = '\0';
        }
        logd("  segment %d: duration=%ld, url=%s, discontinuity=%d, br=%d", *segment_idx,
             segment_duration, pl->segments[*segment_idx].url,
             pl->segments[*segment_idx].discontinuity, pl->segments[*segment_idx].has_byterange);
        (*segment_idx)++;

        return 0;
    }

    return -1;
}

/* Mutable state shared across tag handlers during M3U parsing */
struct m3u_parse_ctx {
    double *current_duration;
    int *current_discontinuity;
    int *current_has_byterange;
    int64_t *current_byterange_length;
    int64_t *current_byterange_offset;
    char *current_pdt;
    int current_pdt_size;
};

/* ---- individual tag handlers ---- */

static int handle_extm3u(struct m3u_playlist *pl, char *line, struct m3u_parse_ctx *ctx)
{
    (void)pl; (void)line; (void)ctx;
    return 0;
}

static int handle_ext_x_version(struct m3u_playlist *pl, char *line, struct m3u_parse_ctx *ctx)
{
    (void)ctx;
    pl->version = atoi(line + 15);
    return 0;
}

static int handle_ext_x_targetduration(struct m3u_playlist *pl, char *line,
                                        struct m3u_parse_ctx *ctx)
{
    (void)ctx;
    pl->target_duration = atoi(line + 22);
    return 0;
}

static int handle_ext_x_media_sequence(struct m3u_playlist *pl, char *line,
                                        struct m3u_parse_ctx *ctx)
{
    (void)ctx;
    pl->media_sequence = atoll(line + 22);
    return 0;
}

static int handle_extinf(struct m3u_playlist *pl, char *line, struct m3u_parse_ctx *ctx)
{
    (void)pl;
    *ctx->current_duration = parse_extinf_duration(line);
    return 0;
}

static int handle_ext_x_discontinuity_sequence(struct m3u_playlist *pl, char *line,
                                                struct m3u_parse_ctx *ctx)
{
    (void)ctx;
    pl->discontinuity_sequence = atoll(line + 30);
    return 0;
}

static int handle_ext_x_discontinuity(struct m3u_playlist *pl, char *line,
                                       struct m3u_parse_ctx *ctx)
{
    (void)pl; (void)line;
    *ctx->current_discontinuity = 1;
    return 0;
}

static int handle_ext_x_byterange(struct m3u_playlist *pl, char *line,
                                   struct m3u_parse_ctx *ctx)
{
    char *p = line + 17;
    long long length_val = 0, offset_val = -1;
    (void)pl;
    if (sscanf(p, "%lld@%lld", &length_val, &offset_val) >= 1) {
        *ctx->current_has_byterange = 1;
        *ctx->current_byterange_length = length_val;
        if (offset_val >= 0)
            *ctx->current_byterange_offset = offset_val;
    }
    return 0;
}

static int handle_ext_x_playlist_type(struct m3u_playlist *pl, char *line,
                                       struct m3u_parse_ctx *ctx)
{
    (void)ctx;
    if (strstr(line + 21, "EVENT"))
        pl->playlist_type = 1;
    else if (strstr(line + 21, "VOD"))
        pl->playlist_type = 2;
    return 0;
}

static int handle_ext_x_program_date_time(struct m3u_playlist *pl, char *line,
                                           struct m3u_parse_ctx *ctx)
{
    (void)pl;
    if (ctx->current_pdt && ctx->current_pdt_size > 0) {
        strncpy(ctx->current_pdt, line + 25, ctx->current_pdt_size - 1);
        ctx->current_pdt[ctx->current_pdt_size - 1] = '\0';
    }
    return 0;
}

static int handle_ext_x_stream_inf(struct m3u_playlist *pl, char *line,
                                    struct m3u_parse_ctx *ctx)
{
    (void)pl; (void)line; (void)ctx;
    return 1;
}

static int handle_ext_x_endlist(struct m3u_playlist *pl, char *line, struct m3u_parse_ctx *ctx)
{
    (void)line; (void)ctx;
    pl->has_endlist = 1;
    return 0;
}

static int handle_ext_x_allow_cache(struct m3u_playlist *pl, char *line,
                                     struct m3u_parse_ctx *ctx)
{
    (void)ctx;
    if (strstr(line + 19, "NO"))
        pl->allow_cache = 0;
    else
        pl->allow_cache = 1;
    return 0;
}

static int parse_key_tag(struct m3u_playlist *pl, char *line, struct m3u_parse_ctx *ctx)
{
    char *p = line + 11;
    char *method_str = strstr(p, "METHOD=");
    (void)ctx;
    if (method_str) {
        method_str += 7;
        char *end = strchr(method_str, ',');
        if (!end)
            end = method_str + strlen(method_str);
        int len = (int)(end - method_str);
        if (len >= (int)sizeof(pl->key_method))
            len = sizeof(pl->key_method) - 1;
        strncpy(pl->key_method, method_str, len);
        pl->key_method[len] = '\0';
        pl->has_encryption_key =
            (strncmp(pl->key_method, "AES-128", sizeof(pl->key_method)) == 0) ? 1 : 0;
    }
    char *uri_str = strstr(p, "URI=\"");
    if (uri_str) {
        uri_str += 5;
        char *end = strchr(uri_str, '"');
        if (end) {
            *end = '\0';
            m3u_build_url(pl->base_url, uri_str, pl->key_uri, sizeof(pl->key_uri));
            *end = '"';
        }
    }
    char *iv_str = strstr(p, "IV=0x");
    if (iv_str) {
        iv_str += 5;
        char *end = strchr(iv_str, ',');
        if (!end)
            end = iv_str + strlen(iv_str);
        int len = (int)(end - iv_str);
        if (len > 32)
            len = 32;
        strncpy(pl->key_iv, iv_str, len);
        pl->key_iv[len] = '\0';
    }
    return 0;
}

static int parse_map_tag(struct m3u_playlist *pl, char *line, struct m3u_parse_ctx *ctx)
{
    char *p = line + 11;
    (void)ctx;
    pl->has_init_segment = 1;
    pl->init_segment_byterange_offset = -1;
    pl->init_segment_byterange_length = -1;
    char *uri_str = strstr(p, "URI=\"");
    if (uri_str) {
        uri_str += 5;
        char *end = strchr(uri_str, '"');
        if (end) {
            *end = '\0';
            m3u_build_url(pl->base_url, uri_str, pl->init_segment_url,
                          sizeof(pl->init_segment_url));
            *end = '"';
        }
    }
    char *br_str = strstr(p, "BYTERANGE=");
    if (br_str) {
        br_str += 10;
        long long length = 0, offset = -1;
        if (sscanf(br_str, "%lld@%lld", &length, &offset) >= 1) {
            pl->init_segment_byterange_length = length;
            if (offset >= 0)
                pl->init_segment_byterange_offset = offset;
        }
    }
    return 0;
}

/* Dispatch table: longer prefixes must precede shorter ones that share the same root */
static const struct {
    const char *prefix;
    int prefix_len;
    int (*handler)(struct m3u_playlist *pl, char *line, struct m3u_parse_ctx *ctx);
} m3u_tag_table[] = {
    { "#EXT-X-DISCONTINUITY-SEQUENCE:", 30, handle_ext_x_discontinuity_sequence },
    { "#EXT-X-DISCONTINUITY",          20, handle_ext_x_discontinuity },
    { "#EXT-X-TARGETDURATION:",        22, handle_ext_x_targetduration },
    { "#EXT-X-MEDIA-SEQUENCE:",        22, handle_ext_x_media_sequence },
    { "#EXT-X-PROGRAM-DATE-TIME:",     25, handle_ext_x_program_date_time },
    { "#EXT-X-PLAYLIST-TYPE:",         21, handle_ext_x_playlist_type },
    { "#EXT-X-ALLOW-CACHE:",           19, handle_ext_x_allow_cache },
    { "#EXT-X-STREAM-INF:",            18, handle_ext_x_stream_inf },
    { "#EXT-X-BYTERANGE:",             17, handle_ext_x_byterange },
    { "#EXT-X-VERSION:",               15, handle_ext_x_version },
    { "#EXT-X-ENDLIST",                14, handle_ext_x_endlist },
    { "#EXT-X-KEY:",                   11, parse_key_tag },
    { "#EXT-X-MAP:",                   11, parse_map_tag },
    { "#EXTINF:",                       8, handle_extinf },
    { "#EXTM3U",                        7, handle_extm3u },
};

static int handle_m3u_tag(struct m3u_playlist *pl, char *line, double *current_duration,
                          int *current_discontinuity, int *current_has_byterange,
                          int64_t *current_byterange_length, int64_t *current_byterange_offset,
                          char *current_pdt, int current_pdt_size)
{
    struct m3u_parse_ctx ctx = {
        .current_duration = current_duration,
        .current_discontinuity = current_discontinuity,
        .current_has_byterange = current_has_byterange,
        .current_byterange_length = current_byterange_length,
        .current_byterange_offset = current_byterange_offset,
        .current_pdt = current_pdt,
        .current_pdt_size = current_pdt_size,
    };
    int i;

    for (i = 0; i < (int)(sizeof(m3u_tag_table) / sizeof(m3u_tag_table[0])); i++) {
        if (strncmp(line, m3u_tag_table[i].prefix, m3u_tag_table[i].prefix_len) == 0)
            return m3u_tag_table[i].handler(pl, line, &ctx);
    }

    return -1;
}

int m3u_parse_stream(m3u_read_line_cb read_cb, void *userdata, const char *base_url,
                     struct m3u_playlist *pl)
{
    char line[M3U_MAX_URL_LEN];
    double current_duration = 0;
    int64_t segment_duration = 0;
    int current_discontinuity = 0;
    int current_has_byterange = 0;
    int64_t current_byterange_length = 0;
    int64_t current_byterange_offset = -1;
    char current_pdt[64] = {0};
    int segment_idx = 0;
    int ret;

    if (!read_cb || !pl)
        return -1;

    memset(pl, 0, sizeof(struct m3u_playlist));

    if (base_url)
        strncpy(pl->base_url, base_url, sizeof(pl->base_url) - 1);

    while (1) {
        int len = read_cb(userdata, line, sizeof(line));
        if (len <= 0)
            break; // End of file or error

        logd("line(%zd): %s", strlen(line), line);
        // Remove trailing \r\n
        char *end = line + strlen(line) - 1;
        while (end >= line && (*end == '\r' || *end == '\n')) {
            *end = '\0';
            end--;
        }

        if (strlen(line) == 0)
            continue;

        ret = handle_m3u_tag(pl, line, &current_duration, &current_discontinuity,
                             &current_has_byterange, &current_byterange_length,
                             &current_byterange_offset, current_pdt, sizeof(current_pdt));
        if (ret == 0) {
            continue;
        } else if (ret == 1) {
            // Handle master playlist variants (requires next line logic, simplified here)
            // For simplicity in streaming, we might need to peek or handle differently
            // But typically variants are just URL lines following the tag
            char url_line[M3U_MAX_URL_LEN] = {0};
            if (read_cb(userdata, url_line, sizeof(url_line)) > 0) {
                // Process variant URL
                char *end_url = url_line + strlen(url_line) - 1;
                while (end_url >= url_line && (*end_url == '\r' || *end_url == '\n')) {
                    *end_url = '\0';
                    end_url--;
                }
                if (M3U_MAX_VARIANTS > pl->variant_count) {
                    parse_stream_inf_params(line + 18, &pl->variants[pl->variant_count]);
                    m3u_build_url(pl->base_url, url_line, pl->variants[pl->variant_count].url,
                                  M3U_MAX_URL_LEN);
                    pl->variant_count++;
                }
            }
            continue;
        }

        if (*line && *line != '#') {
            segment_duration = (int64_t)(current_duration * 1000000);
            if (segment_idx < M3U_MAX_SEGMENTS) {
                m3u_build_url(pl->base_url, line, pl->segments[segment_idx].url, M3U_MAX_URL_LEN);
                pl->segments[segment_idx].duration = segment_duration;
                pl->segments[segment_idx].sequence = pl->media_sequence + segment_idx;
                pl->segments[segment_idx].discontinuity = current_discontinuity;
                current_discontinuity = 0;
                if (current_has_byterange) {
                    pl->segments[segment_idx].has_byterange = 1;
                    pl->segments[segment_idx].byterange_length = current_byterange_length;
                    pl->segments[segment_idx].byterange_offset = current_byterange_offset;
                    current_has_byterange = 0;
                    current_byterange_length = 0;
                    current_byterange_offset = -1;
                }
                if (current_pdt[0]) {
                    strncpy(pl->segments[segment_idx].program_date_time, current_pdt,
                            sizeof(pl->segments[segment_idx].program_date_time) - 1);
                    current_pdt[0] = '\0';
                }
                pl->total_duration += segment_duration;
                segment_idx++;
            }
            current_duration = 0;
        }
    }

    pl->segment_count = segment_idx;
    pl->is_master = (pl->variant_count > 0);
    pl->is_live = !pl->has_endlist;

    logd("m3u parsed (stream): %d segments, %ld total_duration, %d variants, is_live=%d",
         pl->segment_count, pl->total_duration, pl->variant_count, pl->is_live);

    return 0;
}

int m3u_parse(const char *content, const char *base_url, struct m3u_playlist *pl)
{
    char *lines, *line, *next;
    double current_duration = 0;
    int64_t segment_duration = 0;
    int current_discontinuity = 0;
    int current_has_byterange = 0;
    int64_t current_byterange_length = 0;
    int64_t current_byterange_offset = -1;
    char current_pdt[64] = {0};
    int segment_idx = 0;
    int line_num = 0;
    int ret;

    if (!content || !pl)
        return -1;

    memset(pl, 0, sizeof(struct m3u_playlist));

    if (base_url)
        strncpy(pl->base_url, base_url, sizeof(pl->base_url) - 1);

    lines = strdup(content);
    if (!lines)
        return -1;

    line = lines;

    while (line && *line) {
        next = strchr(line, '\n');
        if (next) {
            *next = '\0';
            next++;
        }

        char *end = line + strlen(line) - 1;
        if (end >= line && *end == '\r')
            *end = '\0';

        line_num++;

        if (strlen(line) == 0) {
            line = next;
            continue;
        }

        logd("line %d: %s", line_num, line);

        ret = handle_m3u_tag(pl, line, &current_duration, &current_discontinuity,
                             &current_has_byterange, &current_byterange_length,
                             &current_byterange_offset, current_pdt, sizeof(current_pdt));
        if (ret == 0) {
            line = next;
            continue;
        } else if (ret == 1) {
            if (handle_stream_inf_line(pl, line, &next) < 0) {
                free(lines);
                return -1;
            }
            line = next;
            continue;
        }

        if (*line && *line != '#') {
            segment_duration = (int64_t)(current_duration * 1000000);
            if (handle_url_line(pl, line, segment_duration, &segment_idx, &current_discontinuity,
                                &current_has_byterange, &current_byterange_length,
                                &current_byterange_offset, current_pdt, sizeof(current_pdt)) < 0) {
                free(lines);
                return -1;
            }
            pl->total_duration += segment_duration;
            current_duration = 0;
        }

        line = next;
    }

    pl->segment_count = segment_idx;
    pl->is_master = (pl->variant_count > 0);
    pl->is_live = !pl->has_endlist;

    logi("m3u parsed: %d segments, %ld total_duration, %d variants, media_sequence=%ld, is_live=%d",
         pl->segment_count, pl->total_duration, pl->variant_count, pl->media_sequence, pl->is_live);

    if (pl->segment_count > 0) {
        logi("first segment: %s", pl->segments[0].url);
    }

    free(lines);
    return 0;
}

int m3u_select_variant(struct m3u_playlist *pl, int bandwidth)
{
    int i, selected = 0;
    int max_bandwidth = 0;

    if (!pl || pl->variant_count == 0)
        return -1;

    for (i = 0; i < pl->variant_count; i++) {
        if (pl->variants[i].bandwidth <= bandwidth && pl->variants[i].bandwidth > max_bandwidth) {
            max_bandwidth = pl->variants[i].bandwidth;
            selected = i;
        }
    }

    if (max_bandwidth == 0) {
        int min_bandwidth = pl->variants[0].bandwidth;
        selected = 0;
        for (i = 1; i < pl->variant_count; i++) {
            if (pl->variants[i].bandwidth < min_bandwidth) {
                min_bandwidth = pl->variants[i].bandwidth;
                selected = i;
            }
        }
    }

    return selected;
}
