/*
 * Copyright (C) 2020-2026 ArtInChip Technology Co. Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 *  author: <jun.ma@artinchip.com>
 *  Desc: middle media demuxer component
 */

#include "mm_demuxer_component.h"

#include <pthread.h>
#include <malloc.h>
#include <string.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <inttypes.h>

#include "mpp_log.h"
#include "mpp_list.h"
#include "mpp_mem.h"
#include "mpp_ringbuf.h"
#include "aic_message.h"
#include "aic_parser.h"
#include "aic_stream.h"
#include "mpp_decoder.h"
#include "mpp_dec_type.h"
#include "audio_packet_manager.h"

#define DEMUX_SKIP_AUDIO_TRACK 0x01
#define DEMUX_SKIP_VIDEO_TRACK 0x02

#define MAX_VIDEO_CACHE_BUFFER_SIZE  (8 * 1024 * 1024) //256K/S -> 30s
#define MAX_AUDIO_CACHE_BUFFER_SIZE  (512 * 1024)      //44.1K, 16bit, 2-channel -> 30s
#define MAX_CACHE_PACKET_NUM         (1000)            //30fps -> 30s
#define START_PLAY_CACHE_TIME        (5)               //5s

typedef struct mm_demuxer_cache_packet {
    struct aic_parser_packet pkt;
    struct mpp_list list;
} mm_demuxer_cache_packet;

typedef struct mm_demuxer_cache_params {
    s32 cache_buffer_size;
    s32 max_cache_buffer_size;
    s32 start_cache_size;
    s32 max_start_cache_size;
    s32 start_play_cache_time;      //5s
    s32 start_play_size;
    s32 max_buffer_size;
    u32 max_cache_pkt_num;
    u32 start_play_num;
} mm_demuxer_cache_params;

typedef struct mm_demuxer_cache {
    MM_BOOL cache_en;

    // Packet queues (primary cache)
    mm_demuxer_cache_packet *vpkt_base;
    mm_demuxer_cache_packet *apkt_base;
    struct mpp_list v_empty_list;
    struct mpp_list v_ready_list;
    struct mpp_list a_empty_list;
    struct mpp_list a_ready_list;
    s32 v_list_used;
    s32 a_list_used;

    // Stream ringbuf (secondary/future use)
    mpp_ringbuf_t v_ringbuf;
    mpp_ringbuf_t a_ringbuf;
    struct aic_parser_packet vpkt;
    struct aic_parser_packet apkt;

    // Cache buffer
    mm_demuxer_cache_params params; // Video cache params
    mm_demuxer_cache_params aparams; // Audio cache params

    pthread_t cache_thread;
    pthread_mutex_t cache_lock;
    pthread_cond_t cache_cond;
    s32 cache_stop_flag;
    s32 cache_eos_sent;

    MM_BOOL buffering;         // TRUE = cache thread is paused, waiting for preload
} mm_demuxer_cache;

typedef struct mm_demuxer_perf_info {
    MM_BOOL is_audio;
    s64 tm_read_start;
    s64 tm_read_end;
    s64 total_read_tm;
    s64 last_read_tm;
    u64 data_size;
    u32 data_cnt;
    u64 calc_size;
    u32 calc_cnt;
    s32 frame_rate;
    u32 bitrate;
} mm_demuxer_perf_info;

typedef struct mm_demuxer_data {
    MM_STATE_TYPE state;
    pthread_mutex_t state_lock;
    mm_callback *p_callback;
    void *p_app_data;
    mm_handle h_self;
    mm_port_param port_param;

    mm_param_port_def in_port_def;
    mm_param_port_def out_port_def[2];

    mm_bind_info in_port_bind;
    mm_bind_info out_port_bind[2];
    mm_param_content_uri *p_contenturi;

    s32 eos;
    s32 active_stream_index[2];
    mm_param_u32 stream_num[2];
    mm_audio_param_port_format audio_stream[1];
    mm_video_param_port_format video_stream[1];

    struct aic_parser_av_media_info s_media_info;
    struct aic_parser_packet extra_video_pkt;
    struct aic_parser_packet extra_audio_pkt;
    MM_BOOL extra_video_pkt_flag;
    MM_BOOL extra_audio_pkt_flag;

    pthread_t thread_id;
    struct aic_message_queue s_msg;
    struct aic_parser *p_parser;
    mm_demuxer_cache cache;
    mm_demuxer_perf_info vperf;
    mm_demuxer_perf_info aperf;

    u32 video_pkt_num;
    u32 audio_pkt_num;
    u32 get_video_pkt_ok_num;
    u32 put_video_pkt_ok_num;
    u32 put_video_pkt_fail_num;
    u32 get_audio_pkt_ok_num;
    u32 put_audio_pkt_ok_num;
    u32 put_audio_pkt_fail_num;

    s32 seek_flag;
    s32 need_peek;
    s32 skip_track;
    MM_BOOL net_stream;
    MM_BOOL debug_en;
    int current_track_id;
    int audio_track_count;
    long long current_pts;
    struct audio_packet_manager *pm[8];
} mm_demuxer_data;

static void *mm_demuxer_component_thread(void *p_thread_data);
static void *mm_demuxer_cache_thread(void *p_thread_data);
static s32 mm_demuxer_cache_init(mm_demuxer_cache *cache);
static void mm_demuxer_cache_deinit(mm_demuxer_cache *cache);
static void mm_demuxer_show_debug_info(mm_demuxer_data *p_demuxer_data);

static void cache_cond_signal(mm_demuxer_cache *cache)
{
    pthread_mutex_lock(&cache->cache_lock);
    pthread_cond_signal(&cache->cache_cond);
    pthread_mutex_unlock(&cache->cache_lock);
}

static void cache_cond_wait(mm_demuxer_cache *cache, int timeout_ms)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    ts.tv_nsec += timeout_ms * 1000000;
    if (ts.tv_nsec >= 1000000000) {
        ts.tv_sec++;
        ts.tv_nsec -= 1000000000;
    }
    pthread_mutex_lock(&cache->cache_lock);
    if (!cache->cache_stop_flag)
        pthread_cond_timedwait(&cache->cache_cond, &cache->cache_lock, &ts);
    pthread_mutex_unlock(&cache->cache_lock);
}

static s32 mm_demuxer_msg_put(struct aic_message_queue *msg_que,
                              MM_COMMAND_TYPE cmd,
                              u32 param, void *p_cmd_data)
{
    struct aic_message s_msg;
    memset(&s_msg, 0x00, sizeof(struct aic_message));
    s_msg.message_id = cmd;
    s_msg.param = param;
    s_msg.data_size = 0;
    if (p_cmd_data != NULL) {
        s_msg.data = p_cmd_data;
        s_msg.data_size = strlen((char *)p_cmd_data);
    }
    aic_msg_put(msg_que, &s_msg);
    return MM_ERROR_NONE;
}

static s32 mm_demuxer_send_command(mm_handle h_component, MM_COMMAND_TYPE cmd,
                                   u32 param, void *p_cmd_data)
{
    mm_demuxer_data *p_demuxer_data =
        (mm_demuxer_data *)(((mm_component *)h_component)->p_comp_private);

    if (p_demuxer_data->net_stream && p_demuxer_data->cache.cache_en) {
        if (cmd == MM_COMMAND_WKUP && p_demuxer_data->eos) {
            cache_cond_signal(&p_demuxer_data->cache);
            return 0;
        }
    }

    return mm_demuxer_msg_put(&p_demuxer_data->s_msg, cmd, param, p_cmd_data);
}

static s32 mm_demuxer_get_parameter(mm_handle h_component, MM_INDEX_TYPE index,
                                    void *p_param)
{
    mm_demuxer_data *p_demuxer_data;
    s32 error = MM_ERROR_NONE;
    s32 tmp1, tmp2;
    mm_param_port_def *p_audio_port, *p_video_port;
    mm_param_u32 *p_aud_stream_num, *p_vid_stream_num;
    s32 *p_aud_stream_idx, *p_vid_stream_idx;

    p_demuxer_data =
        (mm_demuxer_data *)(((mm_component *)h_component)->p_comp_private);
    p_audio_port = &p_demuxer_data->out_port_def[DEMUX_PORT_AUDIO_INDEX];
    p_video_port = &p_demuxer_data->out_port_def[DEMUX_PORT_VIDEO_INDEX];
    p_aud_stream_num = &p_demuxer_data->stream_num[DEMUX_PORT_AUDIO_INDEX];
    p_vid_stream_num = &p_demuxer_data->stream_num[DEMUX_PORT_VIDEO_INDEX];

    p_aud_stream_idx =
        &p_demuxer_data->active_stream_index[DEMUX_PORT_AUDIO_INDEX];
    p_vid_stream_idx =
        &p_demuxer_data->active_stream_index[DEMUX_PORT_VIDEO_INDEX];

    switch (index) {
    case MM_INDEX_CONFIG_TIME_POSITION:
        break;
    case MM_INDEX_CONFIG_TIME_SEEK_MODE:
        break;
    case MM_INDEX_PARAM_CONTENT_URI:
        memcpy(p_param, p_demuxer_data->p_contenturi,
               ((mm_param_content_uri *)p_param)->size);
        break;
    case MM_INDEX_PARAM_PORT_DEFINITION: { // mm_bind_info
        mm_param_port_def *port = (mm_param_port_def *)p_param;
        if (port->port_index == DEMUX_PORT_AUDIO_INDEX) {
            memcpy(port, p_audio_port, sizeof(mm_param_port_def));
        } else if (port->port_index == DEMUX_PORT_VIDEO_INDEX) {
            memcpy(port, p_video_port, sizeof(mm_param_port_def));
        } else if (port->port_index == DEMUX_PORT_CLOCK_INDEX) {
            memcpy(port, &p_demuxer_data->in_port_def,
                   sizeof(mm_param_port_def));
        } else {
            error = MM_ERROR_BAD_PARAMETER;
        }
        break;
    }
    case MM_INDEX_PARAM_NUM_AVAILABLE_STREAM: // u32
        tmp1 = ((mm_param_u32 *)p_param)->port_index;
        if (tmp1 == DEMUX_PORT_AUDIO_INDEX) {
            ((mm_param_u32 *)p_param)->u32 = p_aud_stream_num->u32;
        } else if (tmp1 == DEMUX_PORT_VIDEO_INDEX) {
            ((mm_param_u32 *)p_param)->u32 = p_vid_stream_num->u32;
        } else {
            error = MM_ERROR_BAD_PARAMETER;
        }
        break;
    case MM_INDEX_PARAM_ACTIVE_STREAM: // u32
        tmp1 = ((mm_param_u32 *)p_param)->port_index;
        tmp2 = ((mm_param_u32 *)p_param)->u32; // start from 0
        if (tmp1 == DEMUX_PORT_AUDIO_INDEX) {
            ((mm_param_u32 *)p_param)->u32 = *p_aud_stream_idx;
        } else if (tmp1 == DEMUX_PORT_VIDEO_INDEX) {
            ((mm_param_u32 *)p_param)->u32 = *p_vid_stream_idx;
        } else {
            error = MM_ERROR_BAD_PARAMETER;
        }

        break;
    case MM_INDEX_PARAM_AUDIO_PORT_FORMAT:
        tmp1 = ((mm_audio_param_port_format *)p_param)->port_index;
        tmp2 = ((mm_audio_param_port_format *)p_param)->index;
        if (tmp1 != DEMUX_PORT_AUDIO_INDEX ||
            tmp2 > p_aud_stream_num->u32 - 1) {
            error = MM_ERROR_BAD_PARAMETER;
            break;
        }
        ((mm_audio_param_port_format *)p_param)->codec_type =
            p_demuxer_data->audio_stream[tmp2].codec_type;
        break;
    case MM_INDEX_PARAM_VIDEO_PORT_FORMAT: // mm_video_param_port_format
        tmp1 = ((mm_video_param_port_format *)p_param)->port_index;
        tmp2 = ((mm_video_param_port_format *)p_param)->index;
        if (tmp1 != DEMUX_PORT_VIDEO_INDEX ||
            tmp2 > (p_vid_stream_num->u32 - 1)) {
            error = MM_ERROR_BAD_PARAMETER;
            break;
        }
        ((mm_video_param_port_format *)p_param)->codec_type =
            p_demuxer_data->video_stream[tmp2].codec_type;
        ((mm_video_param_port_format *)p_param)->pixel_format =
            p_demuxer_data->video_stream[tmp2].pixel_format;
        break;

    default:
        break;
    }
    return error;
}



static void mm_demuxer_event_notify(mm_demuxer_data *p_demuxer_data,
                                    MM_EVENT_TYPE event, u32 data1, u32 data2,
                                    void *p_event_data)
{
    if (p_demuxer_data && p_demuxer_data->p_callback &&
        p_demuxer_data->p_callback->event_handler) {
        p_demuxer_data->p_callback->event_handler(p_demuxer_data->h_self,
                                                  p_demuxer_data->p_app_data,
                                                  event, data1, data2,
                                                  p_event_data);
    }
}

static void mm_demuxer_update_cache_params(mm_demuxer_data *p_demuxer_data)
{
    mm_demuxer_perf_info *vperf = &p_demuxer_data->vperf;
    mm_demuxer_perf_info *aperf = &p_demuxer_data->aperf;
    mm_demuxer_cache *cache = &p_demuxer_data->cache;
    mm_demuxer_cache_params *params = &cache->params;
    mm_demuxer_cache_params *aparams = &cache->aparams;
    int start_play_size, audio_start_play_size;
    int max_buffer_size, audio_max_buffer_size;
    int bitrate, audio_bitrate;
    s64 cache_time;

    if (!p_demuxer_data->net_stream || !p_demuxer_data->cache.cache_en)
        return;

    bitrate = vperf->bitrate * 1024 * 8; //kbps
    audio_bitrate = aperf->bitrate * 1024 * 8; //kbps

    cache_time = params->start_play_cache_time * 1000000; //us

    if (bitrate < 16 * 1024)
        bitrate = 16 * 1024;
    else if (bitrate > 8 *1024 * 1024)
        bitrate = 8 *1024 * 1024;

    if(p_demuxer_data->s_media_info.duration > 0 &&
        cache_time > p_demuxer_data->s_media_info.duration)
        cache_time = p_demuxer_data->s_media_info.duration;

    cache_time = cache_time / 1000; //ms
    start_play_size = (int)(bitrate * cache_time / (8 * 1000));
    audio_start_play_size = (int)(audio_bitrate * cache_time / (8 * 1000));

    if(start_play_size < params->start_cache_size)
        start_play_size = params->start_cache_size;
    else if(start_play_size > params->max_start_cache_size)
        start_play_size = params->max_start_cache_size;

    if(audio_start_play_size < aparams->start_cache_size)
        audio_start_play_size = aparams->start_cache_size;
    else if(audio_start_play_size > aparams->max_start_cache_size)
        audio_start_play_size = params->max_start_cache_size;

    max_buffer_size = start_play_size * 4 / 3;
    if (max_buffer_size < params->cache_buffer_size)
        max_buffer_size = params->cache_buffer_size;
    else if(max_buffer_size > params->max_cache_buffer_size)
        max_buffer_size = params->max_cache_buffer_size;

    audio_max_buffer_size = audio_start_play_size * 4 / 3;
    if (audio_max_buffer_size < aparams->cache_buffer_size)
        audio_max_buffer_size = aparams->cache_buffer_size;
    else if(audio_max_buffer_size > aparams->max_cache_buffer_size)
        audio_max_buffer_size = aparams->max_cache_buffer_size;

    pthread_mutex_lock(&cache->cache_lock);
    params->start_play_size = start_play_size;
    params->max_buffer_size = max_buffer_size;
    aparams->start_play_size = audio_start_play_size;
    aparams->max_buffer_size = audio_max_buffer_size;
    pthread_mutex_unlock(&cache->cache_lock);
}

static s32 mm_demuxer_net_stream_init(mm_demuxer_data *p_demuxer_data)
{
    int ret = 0;

    /* 1. Check if the URI is a network stream */
    if (strncmp((char *)p_demuxer_data->p_contenturi->content_uri, "http://", 7) == 0 ||
        strncmp((char *)p_demuxer_data->p_contenturi->content_uri, "https://", 8) == 0 ||
        strncmp((char *)p_demuxer_data->p_contenturi->content_uri, "rtsp://", 7) == 0 ||
        strncmp((char *)p_demuxer_data->p_contenturi->content_uri, "rtmp://", 7) == 0 ||
        strncmp((char *)p_demuxer_data->p_contenturi->content_uri, "udp://", 6) == 0) {
        p_demuxer_data->net_stream = 1;
    }

    if (!p_demuxer_data->net_stream) {
        p_demuxer_data->cache.cache_en = MM_FALSE;
        return MM_ERROR_NONE;
    }

    /* 2. Initialize cache for network streams (only once) */
    ret = mm_demuxer_cache_init(&p_demuxer_data->cache);
    if (ret != MM_ERROR_NONE) {
        loge("Failed to init demuxer cache\n");
        p_demuxer_data->cache.cache_en = MM_FALSE;
        return MM_ERROR_INSUFFICIENT_RESOURCES;
    }

    /* 3. Start cache thread for network streams */
    if (p_demuxer_data->cache.cache_en &&
        p_demuxer_data->cache.cache_thread == 0) {
        p_demuxer_data->cache.cache_stop_flag = 0;
        p_demuxer_data->cache.cache_eos_sent = 0;
        ret = pthread_create(&p_demuxer_data->cache.cache_thread, NULL,
                             mm_demuxer_cache_thread, p_demuxer_data);
        if (ret != 0) {
            loge("Failed to create cache thread: %d\n", ret);
            p_demuxer_data->cache.cache_en = MM_FALSE;
            mm_demuxer_cache_deinit(&p_demuxer_data->cache);
            return MM_ERROR_INSUFFICIENT_RESOURCES;
        } else {
            logi("Cache thread started\n");
        }
    }
    return MM_ERROR_NONE;
}

static s32 mm_demuxer_net_stream_deinit(mm_demuxer_data *p_demuxer_data)
{
    if (!p_demuxer_data->net_stream)
        return MM_ERROR_NONE;

    if (p_demuxer_data->cache.cache_thread) {
        p_demuxer_data->cache.cache_stop_flag = 1;
        cache_cond_signal(&p_demuxer_data->cache);
        pthread_join(p_demuxer_data->cache.cache_thread, NULL);
        p_demuxer_data->cache.cache_thread = 0;
    }

    mm_demuxer_cache_deinit(&p_demuxer_data->cache);

    return MM_ERROR_NONE;
}

static s32 mm_demuxer_clear_cache(mm_demuxer_cache *cache)
{
    mm_demuxer_cache_packet *pkt_node = NULL;

    pthread_mutex_lock(&cache->cache_lock);

    //1. Clear video cache
    while (!mpp_list_empty(&cache->v_ready_list)) {
        pkt_node = mpp_list_first_entry(&cache->v_ready_list,
                                        mm_demuxer_cache_packet, list);
        mpp_list_del(&pkt_node->list);
        mpp_list_add_tail(&pkt_node->list, &cache->v_empty_list);
        mpp_ringbuffer_skip(cache->v_ringbuf, pkt_node->pkt.size);
        cache->v_list_used--;
    }
    mpp_ringbuffer_reset(cache->v_ringbuf);

    //2. Clear audio cache
    while (!mpp_list_empty(&cache->a_ready_list)) {
        pkt_node = mpp_list_first_entry(&cache->a_ready_list,
                                        mm_demuxer_cache_packet, list);
        mpp_list_del(&pkt_node->list);
        mpp_list_add_tail(&pkt_node->list, &cache->a_empty_list);
        mpp_ringbuffer_skip(cache->a_ringbuf, pkt_node->pkt.size);
        cache->a_list_used--;
    }
    mpp_ringbuffer_reset(cache->a_ringbuf);
    pthread_mutex_unlock(&cache->cache_lock);

    return MM_ERROR_NONE;
}

static s32 mm_demuxer_seek_video_cache(mm_demuxer_cache *cache, s64 time_pos, s64 *video_pos)
{
    mm_demuxer_cache_packet *pkt_node = NULL;

    //1. If the cache is empty, need to seek parser
    pthread_mutex_lock(&cache->cache_lock);
    if (mpp_list_empty(&cache->v_ready_list)) {
        pthread_mutex_unlock(&cache->cache_lock);
        logd("seek video ready cache no packet.");
        return 1;
    }
    pkt_node = mpp_list_first_entry(&cache->v_ready_list,
                                    mm_demuxer_cache_packet, list);
    if (!pkt_node) {
        pthread_mutex_unlock(&cache->cache_lock);
        logd("seek video ready cache no packet.");
        return 1;
    }

    //2. If the time position is current packet's PTS, no need seek
    if (pkt_node->pkt.pts == time_pos) {
        logd("seek cur packet.");
        *video_pos = pkt_node->pkt.pts;
        pthread_mutex_unlock(&cache->cache_lock);
        return 0;
    }
    mpp_list_del(&pkt_node->list);
    mpp_list_add_tail(&pkt_node->list, &cache->v_empty_list);
    mpp_ringbuffer_skip(cache->v_ringbuf, pkt_node->pkt.size);
    cache->v_list_used--;

    //3. If the time position is less than current packet, need to seek parser
    if (pkt_node->pkt.pts > time_pos) {
        pthread_mutex_unlock(&cache->cache_lock);
        logd("seek cur pkt pts:%ld > time_pos:%ld, "
            "need to seek parser for old packets.",
            pkt_node->pkt.pts, time_pos);
        return 1;
    }

    logd("cur vpkt pts:%ld, time_pos:%ld.", pkt_node->pkt.pts, time_pos);

    //4. If the time position is big than current packet, need to seek cache first
    while(!mpp_list_empty(&cache->v_ready_list)) {
        pkt_node = mpp_list_first_entry(&cache->v_ready_list,
                                        mm_demuxer_cache_packet, list);
        if (!pkt_node) {
            pthread_mutex_unlock(&cache->cache_lock);
            return 1;
        }
        //find the suitable packet but may not the key frame
        if (pkt_node->pkt.pts > time_pos) {
            logd("seek suitable vpkt: pts:%ld, time_pos:%ld, may not the key frame.",
                pkt_node->pkt.pts, time_pos);
            break;
        }

        mpp_list_del(&pkt_node->list);
        mpp_list_add_tail(&pkt_node->list, &cache->v_empty_list);
        mpp_ringbuffer_skip(cache->v_ringbuf, pkt_node->pkt.size);
        cache->v_list_used--;
    }

    //5. Find the nearest key frame in cache
    while(!mpp_list_empty(&cache->v_ready_list)) {
        pkt_node = mpp_list_first_entry(&cache->v_ready_list,
                                        mm_demuxer_cache_packet, list);
        if (!pkt_node) {
            pthread_mutex_unlock(&cache->cache_lock);
            return 1;
        }
        //find the key frame
        if (pkt_node->pkt.flag & PACKET_KEY) {
            *video_pos = pkt_node->pkt.pts;
            logd("seek key frame: pts:%ld, time_pos:%ld.",
                pkt_node->pkt.pts, time_pos);
            pthread_mutex_unlock(&cache->cache_lock);
            return 0;
        }

        mpp_list_del(&pkt_node->list);
        mpp_list_add_tail(&pkt_node->list, &cache->v_empty_list);
        mpp_ringbuffer_skip(cache->v_ringbuf, pkt_node->pkt.size);
        cache->v_list_used--;
    }

    //6. No valid packet in cache, need to seek parser
    if (cache->v_list_used > 0 || !mpp_list_empty(&cache->v_ready_list)) {
        loge("seek video cache may happend error.");
    }
    pthread_mutex_unlock(&cache->cache_lock);

    logd("the video cache has no valid packet, seek to parser.");
    return 1;
}

static s32 mm_demuxer_seek_audio_cache(mm_demuxer_cache *cache, s64 time_pos)
{
    mm_demuxer_cache_packet *pkt_node = NULL;

    //1. If the cache is empty, need to seek parser
    pthread_mutex_lock(&cache->cache_lock);
    if (mpp_list_empty(&cache->a_ready_list)) {
        pthread_mutex_unlock(&cache->cache_lock);
        logd("seek audio ready cache no packet.");
        return 1;
    }
    pkt_node = mpp_list_first_entry(&cache->a_ready_list,
                                    mm_demuxer_cache_packet, list);
    if (!pkt_node) {
        pthread_mutex_unlock(&cache->cache_lock);
        logd("seek audio ready cache no packet.");
        return 1;
    }

    //2. If the time position is current packet, no need seek
    if (pkt_node->pkt.pts == time_pos) {
        pthread_mutex_unlock(&cache->cache_lock);
        logd("seek cur packet.");
        return 0;
    }
    mpp_list_del(&pkt_node->list);
    mpp_list_add_tail(&pkt_node->list, &cache->a_empty_list);
    mpp_ringbuffer_skip(cache->a_ringbuf, pkt_node->pkt.size);
    cache->a_list_used--;

    //3. If the time position is less than current packet, need to seek parser
    if (pkt_node->pkt.pts > time_pos) {
        pthread_mutex_unlock(&cache->cache_lock);
        logd("seek cur pkt pts:%ld > time_pos:%ld, "
            "need to seek parser for old packets.",
            pkt_node->pkt.pts, time_pos);
        return 1;
    }

    //4. If the time position is big than current packet, need to seek cache first
    while(!mpp_list_empty(&cache->a_ready_list)) {
        pkt_node = mpp_list_first_entry(&cache->a_ready_list, mm_demuxer_cache_packet, list);
        if (!pkt_node) {
            pthread_mutex_unlock(&cache->cache_lock);
            return 1;
        }
        //find the suitable packet
        if (pkt_node->pkt.pts > time_pos) {
            logd("seek suitable apkt: pts:%ld, time_pos:%ld.",
                pkt_node->pkt.pts, time_pos);
            pthread_mutex_unlock(&cache->cache_lock);
            return 0;
        }

        mpp_list_del(&pkt_node->list);
        mpp_list_add_tail(&pkt_node->list, &cache->a_empty_list);
        mpp_ringbuffer_skip(cache->a_ringbuf, pkt_node->pkt.size);
        cache->a_list_used--;
    }

    //5. No valid packet in cache, need to seek parser
    if (cache->a_list_used > 0 || !mpp_list_empty(&cache->a_ready_list)) {
        loge("seek audio cache may happend error.");
    }
    pthread_mutex_unlock(&cache->cache_lock);
    return 1;
}

static s32 mm_demuxer_seek(mm_demuxer_data *p_demuxer_data, s64 time_pos)
{
    s64 video_pos = 0, audio_pos = 0;
    int ret = 0;

    logd("time_pos:" FMT_d64 "\n", time_pos);

    //1. If the cache is enabled, need to seek cache first
    if (p_demuxer_data->net_stream && p_demuxer_data->cache.cache_en) {
        ret |= mm_demuxer_seek_video_cache(&p_demuxer_data->cache, time_pos, &video_pos);
        //If video_pos is valid, use video_pos to seek audio cache
        audio_pos = (video_pos != 0) ? video_pos : time_pos;
        ret |= mm_demuxer_seek_audio_cache(&p_demuxer_data->cache, audio_pos);
        if (ret == 0) {
            // Seek cache success, reset eos flag to allow continued playback
            p_demuxer_data->eos = 0;
            return 0;
        }

        //clear cache
        mm_demuxer_clear_cache(&p_demuxer_data->cache);
    }

    //2. Seek parser
    ret = aic_parser_seek(p_demuxer_data->p_parser, time_pos);
    if (ret == 0) {
        p_demuxer_data->need_peek = 1;
        // Seek parser success, reset eos flag to allow continued playback
        p_demuxer_data->eos = 0;
    }

    return ret;
}

static s32 mm_demuxer_index_param_contenturi(mm_demuxer_data *p_demuxer_data,
                                             mm_param_content_uri *p_contenturi)
{
    int ret = 0;
    MM_BOOL b_audio_find = MM_FALSE;
    MM_BOOL b_video_find = MM_FALSE;
    mm_param_port_def *p_audio_port, *p_video_port;
    mm_param_u32 *p_aud_stream_num, *p_vid_stream_num;
    s32 *p_aud_stream_idx, *p_vid_stream_idx;

    p_audio_port = &p_demuxer_data->out_port_def[DEMUX_PORT_AUDIO_INDEX];
    p_video_port = &p_demuxer_data->out_port_def[DEMUX_PORT_VIDEO_INDEX];
    p_aud_stream_num = &p_demuxer_data->stream_num[DEMUX_PORT_AUDIO_INDEX];
    p_vid_stream_num = &p_demuxer_data->stream_num[DEMUX_PORT_VIDEO_INDEX];
    p_aud_stream_idx =
        &p_demuxer_data->active_stream_index[DEMUX_PORT_AUDIO_INDEX];
    p_vid_stream_idx =
        &p_demuxer_data->active_stream_index[DEMUX_PORT_VIDEO_INDEX];

    if (p_demuxer_data->p_contenturi == NULL) {
        p_demuxer_data->p_contenturi = (mm_param_content_uri *)mpp_alloc(
            sizeof(mm_param_content_uri) + MM_MAX_STRINGNAME_SIZE);
        if (p_demuxer_data->p_contenturi == NULL) {
            loge("alloc for content uri failed\n");
            return MM_ERROR_FORMAT_NOT_DETECTED;
        }
    }
    memcpy(p_demuxer_data->p_contenturi, p_contenturi, p_contenturi->size);
    mm_demuxer_net_stream_deinit(p_demuxer_data);
    if (p_demuxer_data->p_parser) {
        aic_parser_destroy(p_demuxer_data->p_parser);
        p_demuxer_data->p_parser = NULL;
        p_demuxer_data->eos = 0;
        p_demuxer_data->need_peek = 1;
    }
    p_demuxer_data->net_stream = 0;
    mm_demuxer_net_stream_init(p_demuxer_data);
    ret = aic_parser_create(p_demuxer_data->p_contenturi->content_uri,
                            &p_demuxer_data->p_parser);
    if (NULL == p_demuxer_data->p_parser) { /*create parser fail*/
        mm_demuxer_event_notify(p_demuxer_data, MM_EVENT_ERROR,
                                MM_ERROR_FORMAT_NOT_DETECTED,
                                p_demuxer_data->state, NULL);
        return MM_ERROR_FORMAT_NOT_DETECTED;
    }


    /*******************************************************************************
        Here,it will takes a lot of time,the larger the file,the longer it takes.
        so,if you want to optimize it,please optimize parser.
    *******************************************************************************/
    time_start(aic_parser_init);
    ret = aic_parser_init(p_demuxer_data->p_parser);
    if (0 != ret) {
        mm_demuxer_event_notify(p_demuxer_data, MM_EVENT_ERROR,
                                MM_ERROR_FORMAT_NOT_DETECTED,
                                p_demuxer_data->state, NULL);
        aic_parser_destroy(p_demuxer_data->p_parser);
        p_demuxer_data->p_parser = NULL;
        return MM_ERROR_FORMAT_NOT_DETECTED;
    }
    time_end(aic_parser_init);
    memset(&p_demuxer_data->s_media_info, 0x00,
           sizeof(struct aic_parser_av_media_info));
    ret = aic_parser_get_media_info(p_demuxer_data->p_parser,
                                    &p_demuxer_data->s_media_info);
    if (0 != ret) { /*get_media_info fail*/
        mm_demuxer_event_notify(p_demuxer_data, MM_EVENT_ERROR,
                                MM_ERROR_FORMAT_NOT_DETECTED,
                                p_demuxer_data->state, NULL);
        aic_parser_destroy(p_demuxer_data->p_parser);
        p_demuxer_data->p_parser = NULL;
        return MM_ERROR_FORMAT_NOT_DETECTED;
    }

    // set port info
    if (p_demuxer_data->s_media_info.has_audio) {
        p_demuxer_data->current_track_id = 0;
        p_demuxer_data->audio_track_count = p_demuxer_data->s_media_info.audio_track_count;
        if (1 < p_demuxer_data->audio_track_count && p_demuxer_data->audio_track_count <= 8) {
            struct aic_audio_decode_config cfg = {0};
            cfg.packet_buffer_size = 8 * 1024;
            cfg.packet_count = 32;
            for (int i = 0; i < p_demuxer_data->audio_track_count; i++) {
                p_demuxer_data->pm[i] = audio_pm_create(&cfg);
                if (NULL == p_demuxer_data->pm[i]) {
                    loge("demuxer malloc audio_packet_manager fail!\n");
                    for (int j = 0; j < i; j++) {
                        audio_pm_destroy(p_demuxer_data->pm[j]);
                        p_demuxer_data->pm[j] = NULL;
                    }

                    aic_parser_destroy(p_demuxer_data->p_parser);
                    p_demuxer_data->p_parser = NULL;
                    return MM_ERROR_FORMAT_NOT_DETECTED;
                }
            }
        }

        p_demuxer_data->audio_stream[0].codec_type =
                p_demuxer_data->s_media_info.audio_stream[0].codec_type;
        p_aud_stream_num->u32 = 1;
        p_audio_port->format.audio.codec_type =
            p_demuxer_data->audio_stream[0].codec_type;
        *p_aud_stream_idx = 0;
        b_audio_find = MM_TRUE;
    } else {
        p_aud_stream_num->u32 = 0;
    }
    if (p_demuxer_data->s_media_info.has_video) {
        p_demuxer_data->video_stream[0].codec_type =
                p_demuxer_data->s_media_info.video_stream.codec_type;
        p_vid_stream_num->u32 = 1;
        p_video_port->format.video.codec_type =
            p_demuxer_data->video_stream[0].codec_type;
        *p_vid_stream_idx = 0;
        b_video_find = MM_TRUE;
    } else {
        p_vid_stream_num->u32 = 0;
    }

    p_demuxer_data->audio_pkt_num = 0;
    p_demuxer_data->get_audio_pkt_ok_num = 0;
    p_demuxer_data->put_audio_pkt_ok_num = 0;
    p_demuxer_data->put_audio_pkt_fail_num = 0;
    p_demuxer_data->video_pkt_num = 0;
    p_demuxer_data->get_video_pkt_ok_num = 0;
    p_demuxer_data->put_video_pkt_ok_num = 0;
    p_demuxer_data->put_video_pkt_fail_num = 0;

    if (b_audio_find || b_video_find) {
        mm_demuxer_event_notify(p_demuxer_data, MM_EVENT_PORT_FORMAT_DETECTED,
                                0, 0, &p_demuxer_data->s_media_info);
        if (b_audio_find &&
            p_demuxer_data->s_media_info.audio_stream[0].extra_data_size > 0 &&
            p_demuxer_data->s_media_info.audio_stream[0].extra_data != NULL) {
            memset(&p_demuxer_data->extra_audio_pkt, 0, sizeof(struct aic_parser_packet));
            logi("audio_stream extra_data_size:%d,extra_data:%p\n",
                 p_demuxer_data->s_media_info.audio_stream[0].extra_data_size,
                 p_demuxer_data->s_media_info.audio_stream[0].extra_data);
            p_demuxer_data->extra_audio_pkt.size =
                p_demuxer_data->s_media_info.audio_stream[0].extra_data_size;
            p_demuxer_data->extra_audio_pkt.flag |= PACKET_FLAG_EXTRA_DATA;

            p_demuxer_data->extra_audio_pkt.data =
                mpp_alloc(p_demuxer_data->extra_audio_pkt.size);
            memcpy(p_demuxer_data->extra_audio_pkt.data,
                   p_demuxer_data->s_media_info.audio_stream[0].extra_data,
                   p_demuxer_data->s_media_info.audio_stream[0].extra_data_size);
            p_demuxer_data->extra_audio_pkt.type = MPP_MEDIA_TYPE_AUDIO;
            p_demuxer_data->extra_audio_pkt_flag = MM_TRUE;
            p_demuxer_data->audio_pkt_num++;
        }
        logi("video_stream extra_data_size:%d,extra_data:%p\n",
             p_demuxer_data->s_media_info.video_stream.extra_data_size,
             p_demuxer_data->s_media_info.video_stream.extra_data);

        if (b_video_find &&
            p_demuxer_data->s_media_info.video_stream.extra_data_size > 0 &&
            p_demuxer_data->s_media_info.video_stream.extra_data != NULL) {

            memset(&p_demuxer_data->extra_video_pkt, 0x00,
                   sizeof(struct aic_parser_packet));
            p_demuxer_data->extra_video_pkt.size =
                p_demuxer_data->s_media_info.video_stream.extra_data_size;
            p_demuxer_data->extra_video_pkt.flag |= PACKET_FLAG_EXTRA_DATA;
            logi("s_pkt.flag:0x%x\n", p_demuxer_data->extra_video_pkt.flag);

            p_demuxer_data->extra_video_pkt.data =
                mpp_alloc(p_demuxer_data->extra_video_pkt.size);

            memcpy(p_demuxer_data->extra_video_pkt.data,
                   p_demuxer_data->s_media_info.video_stream.extra_data,
                   p_demuxer_data->s_media_info.video_stream.extra_data_size);
            p_demuxer_data->extra_video_pkt.type = MPP_MEDIA_TYPE_VIDEO;
            p_demuxer_data->extra_video_pkt_flag = MM_TRUE;
            p_demuxer_data->video_pkt_num++;
        }
    } else {
        mm_demuxer_event_notify(p_demuxer_data, MM_EVENT_ERROR,
                                MM_ERROR_FORMAT_NOT_DETECTED, 0, NULL);
        loge("MM_ERROR_FORMAT_NOT_DETECTED\n");
        aic_parser_destroy(p_demuxer_data->p_parser);
        p_demuxer_data->p_parser = NULL;
        return MM_ERROR_FORMAT_NOT_DETECTED;
    }

    return MM_EVENT_PORT_FORMAT_DETECTED;
}

static s32 mm_demuxer_set_parameter(mm_handle h_component, MM_INDEX_TYPE index,
                                    void *p_param)
{
    mm_demuxer_data *p_demuxer_data;
    s32 error = MM_ERROR_NONE;
    s32 tmp1, tmp2;
    mm_param_port_def *p_audio_port, *p_video_port;
    s32 *p_aud_stream_idx, *p_vid_stream_idx;
    mm_param_u32 *p_aud_stream_num, *p_vid_stream_num;
    p_demuxer_data =
        (mm_demuxer_data *)(((mm_component *)h_component)->p_comp_private);
    p_audio_port = &p_demuxer_data->out_port_def[DEMUX_PORT_AUDIO_INDEX];
    p_video_port = &p_demuxer_data->out_port_def[DEMUX_PORT_VIDEO_INDEX];
    p_aud_stream_num = &p_demuxer_data->stream_num[DEMUX_PORT_AUDIO_INDEX];
    p_vid_stream_num = &p_demuxer_data->stream_num[DEMUX_PORT_VIDEO_INDEX];
    p_aud_stream_idx =
        &p_demuxer_data->active_stream_index[DEMUX_PORT_AUDIO_INDEX];
    p_vid_stream_idx =
        &p_demuxer_data->active_stream_index[DEMUX_PORT_VIDEO_INDEX];

    switch (index) {
    case MM_INDEX_CONFIG_TIME_POSITION:
        break;
    case MM_INDEX_CONFIG_TIME_SEEK_MODE:
        break;
    case MM_INDEX_PARAM_CONTENT_URI:
        mm_demuxer_index_param_contenturi(p_demuxer_data,
                                          (mm_param_content_uri *)p_param);
        break;
    case MM_INDEX_PARAM_PORT_DEFINITION:
        break;
    case MM_INDEX_PARAM_NUM_AVAILABLE_STREAM: // u32
        break;
    case MM_INDEX_PARAM_ACTIVE_STREAM: // u32
        tmp1 = ((mm_param_u32 *)p_param)->port_index;
        tmp2 = ((mm_param_u32 *)p_param)->u32; // start from 0
        if (tmp1 == DEMUX_PORT_AUDIO_INDEX) {
            if (tmp2 > p_aud_stream_num->u32 - 1)
                tmp2 = 0;
            p_audio_port->format.audio.codec_type =
                p_demuxer_data->audio_stream[tmp2].codec_type;
            *p_aud_stream_idx = tmp2;
        } else if (tmp1 == DEMUX_PORT_VIDEO_INDEX) {
            if (tmp2 > p_vid_stream_num->u32 - 1)
                tmp2 = 0;
            p_video_port->format.video.codec_type =
                p_demuxer_data->video_stream[tmp2].codec_type;
            p_video_port->format.video.pixel_format =
                p_demuxer_data->video_stream[tmp2].pixel_format;
            *p_vid_stream_idx = tmp2;
        } else {
            error = MM_ERROR_BAD_PARAMETER;
        }
        break;

    case MM_INDEX_VENDOR_DEMUXER_SKIP_TRACK:
        tmp1 = ((mm_param_skip_track*)p_param)->port_index;
        if (tmp1 == DEMUX_PORT_AUDIO_INDEX) {
            p_demuxer_data->skip_track |= DEMUX_SKIP_AUDIO_TRACK;
            aic_parser_control(p_demuxer_data->p_parser, PARSER_AUDIO_SKIP_PACKET, NULL);
        } else if (tmp1 == DEMUX_PORT_VIDEO_INDEX) {
            p_demuxer_data->skip_track |= DEMUX_SKIP_VIDEO_TRACK;
            aic_parser_control(p_demuxer_data->p_parser, PARSER_VIDEO_SKIP_PACKET, NULL);
        }
        break;

    case MM_INDEX_PARAM_PRINT_DEBUG_INFO:
        p_demuxer_data->debug_en = ((mm_param_u32 *)p_param)->u32;
        mm_demuxer_show_debug_info(p_demuxer_data);
        break;

    default:
        break;
    }
    return error;
}

static s32 mm_demuxer_get_config(mm_handle h_component, MM_INDEX_TYPE index,
                                 void *p_config)
{
    s32 error = MM_ERROR_NONE;

    switch (index) {
    case MM_INDEX_CONFIG_TIME_POSITION:
        break;
    case MM_INDEX_CONFIG_TIME_SEEK_MODE:
        break;
    default:
        break;
    }
    return error;
}

static s32 mm_demuxer_set_config(mm_handle h_component, MM_INDEX_TYPE index,
                                 void *p_config)
{
    s32 error = MM_ERROR_NONE;
    mm_demuxer_data *p_demuxer_data =
        (mm_demuxer_data *)(((mm_component *)h_component)->p_comp_private);
    switch ((s32)index) {
    case MM_INDEX_CONFIG_TIME_POSITION: // do seek
    {
        error = mm_demuxer_seek(p_demuxer_data,
            ((mm_time_config_timestamp *)p_config)->timestamp);
        break;
    }
    case MM_INDEX_CONFIG_TIME_SEEK_MODE:
        break;
    case MM_INDEX_VENDOR_CLEAR_BUFFER:
        p_demuxer_data->eos = 0;
        break;
    default:
        break;
    }
    return error;
}

static s32 mm_demuxer_get_state(mm_handle h_component, MM_STATE_TYPE *p_state)
{
    mm_demuxer_data *p_demuxer_data;
    s32 error = MM_ERROR_NONE;
    p_demuxer_data =
        (mm_demuxer_data *)(((mm_component *)h_component)->p_comp_private);
    pthread_mutex_lock(&p_demuxer_data->state_lock);
    *p_state = p_demuxer_data->state;
    pthread_mutex_unlock(&p_demuxer_data->state_lock);
    return error;
}

static s32 mm_demuxer_bind_request(mm_handle h_comp, u32 port,
                                   mm_handle h_bind_comp, u32 bind_port)
{
    s32 error = MM_ERROR_NONE;
    mm_param_port_def *p_port;
    mm_bind_info *p_bind_info;
    mm_demuxer_data *p_demuxer_data;
    p_demuxer_data =
        (mm_demuxer_data *)(((mm_component *)h_comp)->p_comp_private);
    if (p_demuxer_data->state != MM_STATE_LOADED &&
        p_demuxer_data->state != MM_STATE_IDLE) {
        loge(
            "Component is not in MM_STATE_LOADED,it is in%d,it can not tunnel\n",
            p_demuxer_data->state);
        return MM_ERROR_INVALID_STATE;
    }

    if (port == DEMUX_PORT_AUDIO_INDEX) {
        p_port = &p_demuxer_data->out_port_def[DEMUX_PORT_AUDIO_INDEX];
        p_bind_info = &p_demuxer_data->out_port_bind[DEMUX_PORT_AUDIO_INDEX];
    } else if (port == DEMUX_PORT_VIDEO_INDEX) {
        p_port = &p_demuxer_data->out_port_def[DEMUX_PORT_VIDEO_INDEX];
        p_bind_info = &p_demuxer_data->out_port_bind[DEMUX_PORT_VIDEO_INDEX];
    } else if (port == DEMUX_PORT_CLOCK_INDEX) {
        p_port = &p_demuxer_data->in_port_def;
        p_bind_info = &p_demuxer_data->in_port_bind;
    } else {
        loge("component can not find port:%u\n", port);
        return MM_ERROR_BAD_PARAMETER;
    }

    // cancle setup tunnel
    if (NULL == h_bind_comp && 0 == bind_port) {
        p_bind_info->flag = MM_FALSE;
        p_bind_info->bind_port_index = bind_port;
        p_bind_info->p_bind_comp = h_bind_comp;
        return MM_ERROR_NONE;
    }

    if (p_port->dir == MM_DIR_OUTPUT) {
        p_bind_info->bind_port_index = bind_port;
        p_bind_info->p_bind_comp = h_bind_comp;
        p_bind_info->flag = MM_TRUE;
    } else if (p_port->dir == MM_DIR_INPUT) {
        mm_param_port_def bind_port_param;

        bind_port_param.port_index = bind_port;

        mm_get_parameter(h_bind_comp, MM_INDEX_PARAM_PORT_DEFINITION,
                         &bind_port_param);

        if (bind_port_param.dir != MM_DIR_OUTPUT) {
            loge("both ports are input.\n");
            return MM_ERROR_PORT_NOT_COMPATIBLE;
        }

        p_bind_info->bind_port_index = bind_port;
        p_bind_info->p_bind_comp = h_bind_comp;
        p_bind_info->flag = MM_TRUE;
    } else {
        loge("port is neither output nor input.\n");
        return MM_ERROR_PORT_NOT_COMPATIBLE;
    }
    return error;
}

static s32 mm_demuxer_set_callback(mm_handle h_component, mm_callback *p_cb,
                                   void *p_app_data)
{
    s32 error = MM_ERROR_NONE;
    mm_demuxer_data *p_demuxer_data;
    p_demuxer_data =
        (mm_demuxer_data *)(((mm_component *)h_component)->p_comp_private);
    p_demuxer_data->p_callback = p_cb;
    p_demuxer_data->p_app_data = p_app_data;
    return error;
}

s32 mm_demuxer_component_deinit(mm_handle h_component)
{
    s32 error = MM_ERROR_NONE;
    mm_component *p_comp;
    mm_demuxer_data *p_demuxer_data;
    p_comp = (mm_component *)h_component;

    p_demuxer_data = (mm_demuxer_data *)p_comp->p_comp_private;
    pthread_mutex_lock(&p_demuxer_data->state_lock);
    if (p_demuxer_data->state != MM_STATE_LOADED) {
        logd(
            "compoent is in %d,but not in MM_STATE_LOADED(1),can ont FreeHandle.\n",
            p_demuxer_data->state);
        pthread_mutex_unlock(&p_demuxer_data->state_lock);
        return MM_ERROR_INVALID_STATE;
    }
    pthread_mutex_unlock(&p_demuxer_data->state_lock);
    mm_demuxer_msg_put(&p_demuxer_data->s_msg, MM_COMMAND_STOP, 0, NULL);
    pthread_join(p_demuxer_data->thread_id, (void *)&error);

    pthread_mutex_destroy(&p_demuxer_data->state_lock);

    aic_msg_destroy(&p_demuxer_data->s_msg);

    mm_demuxer_net_stream_deinit(p_demuxer_data);

    if (p_demuxer_data->p_parser) {
        aic_parser_destroy(p_demuxer_data->p_parser);
        p_demuxer_data->p_parser = NULL;
    }

    if (p_demuxer_data->extra_video_pkt.data) {
        mpp_free(p_demuxer_data->extra_video_pkt.data);
        p_demuxer_data->extra_video_pkt.data = NULL;
    }
    if (p_demuxer_data->extra_audio_pkt.data) {
        mpp_free(p_demuxer_data->extra_audio_pkt.data);
        p_demuxer_data->extra_audio_pkt.data = NULL;
    }
    if (p_demuxer_data->p_contenturi) {
        mpp_free(p_demuxer_data->p_contenturi);
        p_demuxer_data->p_contenturi = NULL;
    }

    if (1 < p_demuxer_data->audio_track_count && p_demuxer_data->audio_track_count <= 8) {
        for (int i = 0; i < p_demuxer_data->audio_track_count; i++) {
            if (p_demuxer_data->pm[i]) {
                audio_pm_destroy(p_demuxer_data->pm[i]);
            }
        }
    }

    mpp_free(p_demuxer_data);
    p_demuxer_data = NULL;

    logd("mm_demuxer_component_deinit\n");

    return error;
}

s32 mm_demuxer_component_init(mm_handle h_component)
{
    mm_component *p_comp;
    mm_demuxer_data *p_demuxer_data;
    s32 error = MM_ERROR_NONE;
    u32 err;

    mm_param_port_def *p_audio_port, *p_video_port;
    mm_param_u32 *p_aud_stream_num, *p_vid_stream_num;

    MM_BOOL b_msg_creat = MM_FALSE;
    MM_BOOL b_state_lock_init = MM_FALSE;

    logd("mm_demuxer_ComponentInit....");

    p_comp = (mm_component *)h_component;

    p_demuxer_data = (mm_demuxer_data *)mpp_alloc(sizeof(mm_demuxer_data));

    if (NULL == p_demuxer_data) {
        loge("mpp_alloc(sizeof(mm_demuxer_data) fail!\n");
        return MM_ERROR_INSUFFICIENT_RESOURCES;
    }

    memset(p_demuxer_data, 0x0, sizeof(mm_demuxer_data));

    p_comp->p_comp_private = (void *)p_demuxer_data;
    p_demuxer_data->state = MM_STATE_LOADED;
    p_demuxer_data->h_self = p_comp;

    p_comp->set_callback = mm_demuxer_set_callback;
    p_comp->send_command = mm_demuxer_send_command;
    p_comp->get_state = mm_demuxer_get_state;
    p_comp->get_parameter = mm_demuxer_get_parameter;
    p_comp->set_parameter = mm_demuxer_set_parameter;
    p_comp->get_config = mm_demuxer_get_config;
    p_comp->set_config = mm_demuxer_set_config;
    p_comp->bind_request = mm_demuxer_bind_request;
    p_comp->deinit = mm_demuxer_component_deinit;

    p_demuxer_data->port_param.ports = 3;
    p_demuxer_data->port_param.start_port_num = 0x0;
    p_audio_port = &p_demuxer_data->out_port_def[DEMUX_PORT_AUDIO_INDEX];
    p_video_port = &p_demuxer_data->out_port_def[DEMUX_PORT_VIDEO_INDEX];
    p_aud_stream_num = &p_demuxer_data->stream_num[DEMUX_PORT_AUDIO_INDEX];
    p_vid_stream_num = &p_demuxer_data->stream_num[DEMUX_PORT_VIDEO_INDEX];

    p_audio_port->port_index = DEMUX_PORT_AUDIO_INDEX;
    p_audio_port->enable = MM_TRUE;
    p_audio_port->dir = MM_DIR_OUTPUT;
    p_video_port->port_index = DEMUX_PORT_VIDEO_INDEX;
    p_video_port->enable = MM_TRUE;
    p_video_port->dir = MM_DIR_OUTPUT;
    p_demuxer_data->in_port_def.port_index = DEMUX_PORT_CLOCK_INDEX;
    p_demuxer_data->in_port_def.enable = MM_TRUE;
    p_demuxer_data->in_port_def.dir = MM_DIR_INPUT;

    p_demuxer_data->extra_video_pkt.data = NULL;
    p_demuxer_data->extra_video_pkt.size = 0;
    p_demuxer_data->extra_audio_pkt.data = NULL;
    p_demuxer_data->extra_audio_pkt.size = 0;
    p_demuxer_data->extra_video_pkt_flag = MM_FALSE;
    p_demuxer_data->extra_audio_pkt_flag = MM_FALSE;

    p_aud_stream_num->port_index = DEMUX_PORT_AUDIO_INDEX;
    p_vid_stream_num->port_index = DEMUX_PORT_VIDEO_INDEX;

    p_demuxer_data->eos = 0;

    if (aic_msg_create(&p_demuxer_data->s_msg) < 0) {
        loge("aic_msg_create fail!\n");
        error = MM_ERROR_INSUFFICIENT_RESOURCES;
        goto _EXIT;
    }
    b_msg_creat = MM_TRUE;

    if (pthread_mutex_init(&p_demuxer_data->state_lock, NULL)) {
        loge("pthread_mutex_init fail!\n");
        error = MM_ERROR_INSUFFICIENT_RESOURCES;
        goto _EXIT;
    }
    b_state_lock_init = MM_TRUE;

    // Create the component thread
    err = pthread_create(&p_demuxer_data->thread_id, NULL,
                         mm_demuxer_component_thread, p_demuxer_data);
    if (err) {
        loge("pthread_create fail!\n");
        error = MM_ERROR_INSUFFICIENT_RESOURCES;
        goto _EXIT;
    }

    return error;

_EXIT:

    if (b_state_lock_init) {
        pthread_mutex_destroy(&p_demuxer_data->state_lock);
    }
    if (b_msg_creat) {
        aic_msg_destroy(&p_demuxer_data->s_msg);
    }

    if (p_demuxer_data) {
        mpp_free(p_demuxer_data);
        p_demuxer_data = NULL;
    }

    return error;
}

static void mm_demuxer_state_change_to_invalid(mm_demuxer_data *p_demuxer_data)
{
    p_demuxer_data->state = MM_STATE_INVALID;
    mm_demuxer_event_notify(p_demuxer_data, MM_EVENT_ERROR,
                            MM_ERROR_INVALID_STATE, 0, NULL);
    mm_demuxer_event_notify(p_demuxer_data, MM_EVENT_CMD_COMPLETE,
                            MM_COMMAND_STATE_SET, p_demuxer_data->state, NULL);
}

static void mm_demuxer_state_change_to_idle(mm_demuxer_data *p_demuxer_data)
{
    if ((MM_STATE_LOADED != p_demuxer_data->state) &&
        (MM_STATE_EXECUTING != p_demuxer_data->state) &&
        (MM_STATE_PAUSE != p_demuxer_data->state)) {
        mm_demuxer_event_notify(p_demuxer_data, MM_EVENT_ERROR,
                                MM_ERROR_INCORRECT_STATE_TRANSITION,
                                p_demuxer_data->state, NULL);
        loge("MM_ERROR_INCORRECT_STATE_TRANSITION\n");
        return;
    }
    p_demuxer_data->state = MM_STATE_IDLE;
    mm_demuxer_event_notify(p_demuxer_data, MM_EVENT_CMD_COMPLETE,
                            MM_COMMAND_STATE_SET, p_demuxer_data->state, NULL);
}

static void mm_demuxer_state_change_to_loaded(mm_demuxer_data *p_demuxer_data)
{
    if (MM_STATE_IDLE == p_demuxer_data->state) {
        p_demuxer_data->eos = 0;
        p_demuxer_data->skip_track = 0;

        p_demuxer_data->state = MM_STATE_LOADED;
        mm_demuxer_event_notify(p_demuxer_data, MM_EVENT_CMD_COMPLETE,
                                MM_COMMAND_STATE_SET, p_demuxer_data->state, NULL);
        mm_demuxer_net_stream_deinit(p_demuxer_data);
        if (p_demuxer_data->p_parser) {
            aic_parser_destroy(p_demuxer_data->p_parser);
            p_demuxer_data->p_parser = NULL;
            p_demuxer_data->eos = 0;
            p_demuxer_data->need_peek = 1;
        }
        memset(&p_demuxer_data->s_media_info,0x00,
            sizeof(struct aic_parser_av_media_info));
        if (p_demuxer_data->extra_audio_pkt_flag) {
            if (p_demuxer_data->extra_audio_pkt.data)
                mpp_free(p_demuxer_data->extra_audio_pkt.data);
            p_demuxer_data->extra_audio_pkt.data = NULL;
            p_demuxer_data->extra_audio_pkt.size = 0;
            p_demuxer_data->extra_audio_pkt_flag = MM_FALSE;
        }
        if (p_demuxer_data->extra_video_pkt_flag) {
            if (p_demuxer_data->extra_video_pkt.data)
                mpp_free(p_demuxer_data->extra_video_pkt.data);
            p_demuxer_data->extra_video_pkt.data = NULL;
            p_demuxer_data->extra_video_pkt.size = 0;
            p_demuxer_data->extra_video_pkt_flag = MM_FALSE;
        }
    } else {
        mm_demuxer_event_notify(p_demuxer_data, MM_EVENT_ERROR,
                                MM_ERROR_INCORRECT_STATE_TRANSITION,
                                p_demuxer_data->state, NULL);
        loge("MM_ERROR_INCORRECT_STATE_TRANSITION\n");
    }
}

static void
mm_demuxer_state_change_to_executing(mm_demuxer_data *p_demuxer_data)
{
    if (MM_STATE_IDLE == p_demuxer_data->state) {
        if (NULL == p_demuxer_data->p_parser) {
            mm_demuxer_event_notify(p_demuxer_data, MM_EVENT_ERROR,
                                    MM_ERROR_INCORRECT_STATE_TRANSITION,
                                    p_demuxer_data->state, NULL);
            loge(
                "p_demuxer_data->p_parser is not created,please set param uri!!!!!\n");
            return;
        }
    } else if (MM_STATE_PAUSE == p_demuxer_data->state) {
        //
    } else {
        mm_demuxer_event_notify(p_demuxer_data, MM_EVENT_ERROR,
                                MM_ERROR_INCORRECT_STATE_TRANSITION,
                                p_demuxer_data->state, NULL);
        loge("MM_ERROR_INCORRECT_STATE_TRANSITION\n");
        return;
    }
    p_demuxer_data->state = MM_STATE_EXECUTING;
}

static void mm_demuxer_state_change_to_pause(mm_demuxer_data *p_demuxer_data)
{
    if (MM_STATE_EXECUTING == p_demuxer_data->state) {
        //
    } else {
        mm_demuxer_event_notify(p_demuxer_data, MM_EVENT_ERROR,
                                MM_ERROR_INCORRECT_STATE_TRANSITION,
                                p_demuxer_data->state, NULL);
        logd("MM_ERROR_INCORRECT_STATE_TRANSITION\n");
        return;
    }
    p_demuxer_data->state = MM_STATE_PAUSE;
}

static int mm_demuxer_component_switch_track(mm_demuxer_data *p_demuxer_data, int track_id)
{
    struct audio_packet_manager *pm = NULL;
    struct mpp_packet *pkt = NULL;
    long long last_pts = 0;

    if (NULL == p_demuxer_data) {
        loge("invalid parameter!\n");
        return -1;
    }

    if ((track_id < 0) || (track_id >= p_demuxer_data->audio_track_count)) {
        loge("invalid track_id:%d, need between [0, %d]\n", track_id, p_demuxer_data->audio_track_count - 1);
        return -1;
    }

    if (track_id == p_demuxer_data->current_track_id) {
        logi("track id not change!\n");
        return 0;
    }

    // 1.stop audio decoder
    // render deinit
    // audio decoder deinit

    // 2.switch audio track
    pm = p_demuxer_data->pm[track_id];
    while (1) {
        pkt = audio_pm_dequeue_ready_packet(pm);
        if (NULL == pkt) {
            p_demuxer_data->current_track_id = track_id;
            logi("demuxer buffer is empty, start adec!\n");
            break;
        }

        /*p_demuxer_data->current_pts need reduce audio decoder buffer data length*/
        if ((p_demuxer_data->current_pts > last_pts) && (p_demuxer_data->current_pts <= pkt->pts)) {
            logi("found pts:%llx in new track:%d\n", pkt->pts, track_id);
            audio_pm_reclaim_ready_packet(pm, pkt);
            p_demuxer_data->current_track_id = track_id;
            break;
        }

        audio_pm_enqueue_empty_packet(pm, pkt);
        last_pts = pkt->pts;
    }

    // 3.restart audio decoder

    return 0;
}

static int mm_demuxer_component_process_cmd(mm_demuxer_data *p_demuxer_data)
{
    s32 cmd = MM_COMMAND_UNKNOWN;
    s32 cmd_data;
    struct aic_message message;

    if (aic_msg_get(&p_demuxer_data->s_msg, &message) == 0) {
        cmd = message.message_id;
        cmd_data = message.param;
        logi("cmd:%d, cmd_data:%d data:%p data_size:%d\n", cmd, cmd_data, message.data, message.data_size);
        if (MM_COMMAND_STATE_SET == cmd) {
            pthread_mutex_lock(&p_demuxer_data->state_lock);
            if (p_demuxer_data->state == (MM_STATE_TYPE)(cmd_data)) {
                mm_demuxer_event_notify(p_demuxer_data, MM_EVENT_ERROR,
                                        MM_ERROR_SAME_STATE, 0, NULL);
                pthread_mutex_unlock(&p_demuxer_data->state_lock);
                goto CMD_EXIT;
            }
            switch ((MM_STATE_TYPE)(cmd_data)) {
            case MM_STATE_INVALID:
                mm_demuxer_state_change_to_invalid(p_demuxer_data);
                break;
            case MM_STATE_LOADED:
                mm_demuxer_state_change_to_loaded(p_demuxer_data);
                break;
            case MM_STATE_IDLE:
                mm_demuxer_state_change_to_idle(p_demuxer_data);
                break;
            case MM_STATE_EXECUTING:
                mm_demuxer_state_change_to_executing(p_demuxer_data);
                break;
            case MM_STATE_PAUSE:
                mm_demuxer_state_change_to_pause(p_demuxer_data);
                break;
            case MM_STATE_SWITCH_TRACK:
                if (message.data) {
                    s8 track_id = *(s8 *)message.data - 1;
                    mm_demuxer_component_switch_track(p_demuxer_data, track_id);
                }
                break;
            default:
                break;
            }
            pthread_mutex_unlock(&p_demuxer_data->state_lock);
        } else if (MM_COMMAND_STOP == cmd) {
            logi("mm_demuxer_component_thread ready to exit!!!\n");
        }
    }

CMD_EXIT:
    return cmd;
}

static int mm_demuxer_component_process_eos_pkt(mm_demuxer_data *p_demuxer_data)
{
    /*Get video decoder handle*/
    mm_component *h_vdec_comp =
        p_demuxer_data->out_port_bind[DEMUX_PORT_VIDEO_INDEX].p_bind_comp;
    mm_component *h_adec_comp =
        p_demuxer_data->out_port_bind[DEMUX_PORT_AUDIO_INDEX].p_bind_comp;

    /*when the final video or audio packet can't get PACKET_EOS flag,
     *then parser send a empty packet to demuxer , and demuxer need
     *wakup dec comp and send end flag to dec comp
     */
    if (h_vdec_comp != NULL && (p_demuxer_data->skip_track == 0 ||
                                p_demuxer_data->skip_track & DEMUX_SKIP_AUDIO_TRACK)) {
        mm_send_command(h_vdec_comp, MM_COMMAND_EOS, 0, NULL);
    }

    if (h_adec_comp != NULL && (p_demuxer_data->skip_track == 0 ||
                                p_demuxer_data->skip_track & DEMUX_SKIP_VIDEO_TRACK)) {
        mm_send_command(h_adec_comp, MM_COMMAND_EOS, 0, NULL);
    }

    return MM_ERROR_NONE;
}

static void mm_demuxer_calc_perf_info(mm_demuxer_perf_info *perf, int size, MM_BOOL debug_en)
{
    s64 time_period, time_diff;

    time_diff = perf->tm_read_end - perf->tm_read_start;
    if (time_diff > 100000)
        printf("\tdemuxer:read one video packet cost too much time %ld ms\n", time_diff / 1000);

    perf->total_read_tm += time_diff;
    perf->data_size += size;
    perf->data_cnt++;
    perf->calc_size += size;
    perf->calc_cnt++;
    time_period = perf->tm_read_start - perf->last_read_tm;

    if (time_period < MM_MEDIA_PERF_PERIOD_TIME)
        return;

    perf->frame_rate = perf->calc_cnt * 1000000 / time_period;
    perf->bitrate = (perf->calc_size * 1000000) / (time_period * 1024);
    if (debug_en) {
        if (perf->is_audio)
            printf("audio demuxer perf info:\n");
        else
            printf("video demuxer perf info:\n");
        printf("\tAvgReadTm(ms)    FPS    Bitrate(KB/S)    Period(ms)\n");
        printf("\t%13ld    %3d    %13d    %10ld\n\n",
            perf->total_read_tm / (perf->calc_cnt * 1000),
            perf->frame_rate, perf->bitrate, perf->total_read_tm / 1000);
    }

    perf->calc_cnt = 0;
    perf->calc_size = 0;
    perf->total_read_tm = 0;
    perf->last_read_tm = perf->tm_read_start;
}


static int process_video_extra_data(mm_demuxer_data *p_demuxer_data,
                                    struct mpp_decoder *p_decoder)
{
    struct mpp_packet pkt = {0};
    s32 ret = MM_ERROR_NONE;

    pkt.size = p_demuxer_data->extra_video_pkt.size;
    ret = mpp_decoder_get_packet(p_decoder, &pkt, pkt.size);
    if (ret != 0) {
        return MM_ERROR_EMPTY_DATA;
    }
    p_demuxer_data->get_video_pkt_ok_num++;
    memcpy(pkt.data, p_demuxer_data->extra_video_pkt.data, pkt.size);
    pkt.flag = p_demuxer_data->extra_video_pkt.flag;
    pkt.pts = p_demuxer_data->extra_video_pkt.pts;
    ret = mpp_decoder_put_packet(p_decoder, &pkt);
    if (ret != 0) {
        loge("put extra pkt to decoder failed %x!!!\n", ret);
        p_demuxer_data->put_video_pkt_fail_num++;
        return ret;
    }

    p_demuxer_data->put_video_pkt_ok_num++;
    p_demuxer_data->extra_video_pkt_flag = MM_FALSE;

    if (p_demuxer_data->extra_video_pkt.data) {
        mpp_free(p_demuxer_data->extra_video_pkt.data);
        p_demuxer_data->extra_video_pkt.data = NULL;
        p_demuxer_data->extra_video_pkt.size = 0;
    }
    return MM_ERROR_NONE;
}

static int
mm_demuxer_component_process_video_pkt(mm_demuxer_data *p_demuxer_data,
                                       struct aic_parser_packet *p_pkt)
{
    struct mpp_decoder *p_decoder = NULL;
    struct mpp_packet pkt = {0};
    s32 ret = MM_ERROR_NONE;

    /*Get video decoder handle*/
    mm_component *h_vdec_comp =
        p_demuxer_data->out_port_bind[DEMUX_PORT_VIDEO_INDEX].p_bind_comp;

    if (h_vdec_comp == NULL) {
        logd("get h_vdec_comp is null\n");
        /*if viddec format not support, should peek next video packet*/
        mm_component *h_adec_comp =
            p_demuxer_data->out_port_bind[DEMUX_PORT_AUDIO_INDEX].p_bind_comp;
        if (h_adec_comp) {
            p_demuxer_data->need_peek = MM_TRUE;
        }
        return MM_ERROR_NULL_POINTER;
    }

    /*New source need to recrete video decoder*/
    if (p_pkt->flag & PACKET_FLAG_SOS) {
        logd("start of stream packet, pts:%ld.", p_pkt->pts);
        ret = mm_set_parameter(h_vdec_comp, MM_INDEX_PARAM_VIDEO_STREAM_START_FLAG, NULL);
        if (ret) {
            logd("set video stream start flag failed\n");
            return MM_ERROR_BAD_PARAMETER;
        }
        p_pkt->flag &= ~PACKET_FLAG_SOS;
    }

    mm_get_parameter(h_vdec_comp, MM_INDEX_PARAM_VIDEO_DECODER_HANDLE, (void *)&p_decoder);
    if (p_decoder == NULL) {
        logd("get video decoder is null\n");
        return MM_ERROR_NULL_POINTER;
    }

    /*process extra video pkt and put it to decoder*/
    if (p_demuxer_data->extra_video_pkt_flag) {
        ret = process_video_extra_data(p_demuxer_data, p_decoder);
        if (ret == MM_ERROR_NONE)
            mm_send_command(h_vdec_comp, MM_COMMAND_WKUP, 0, NULL);
    }

    /*Get empty packet from decoder*/
    pkt.size = p_pkt->size;
    ret = mpp_decoder_get_packet(p_decoder, &pkt, pkt.size);
    if (ret != 0) {
        aic_msg_wait_new_msg(&p_demuxer_data->s_msg, 0);
        p_demuxer_data->need_peek = MM_FALSE;
        return MM_ERROR_EMPTY_DATA;
    }
    p_demuxer_data->get_video_pkt_ok_num++;

    /*Read video data from parser and fill it to decoder packet*/
    p_pkt->data = pkt.data;
    p_demuxer_data->vperf.tm_read_start = mm_get_time_us();
    ret = aic_parser_read(p_demuxer_data->p_parser, p_pkt);
    p_demuxer_data->vperf.tm_read_end = mm_get_time_us();

    logi("video aic_parser_read,pts:" FMT_d64 ",type = %d,size:%d,flag:0x%x\n",
         p_pkt->pts, p_pkt->type, p_pkt->size, p_pkt->flag);
    if (!ret) { // read ok
        p_demuxer_data->need_peek = MM_TRUE;
    } else { // now  nothing to do ,becase no other return val
        loge("read video data fail ret %d\n", ret);
        p_demuxer_data->need_peek = MM_FALSE;
        return MM_ERROR_READ_FAILED;
    }

    /*Put packet to decoder*/
    pkt.flag = p_pkt->flag;
    pkt.pts = p_pkt->pts;
    ret = mpp_decoder_put_packet(p_decoder, &pkt);
    if (pkt.flag & PACKET_FLAG_EOS) {
        mm_set_parameter(h_vdec_comp, MM_INDEX_PARAM_VIDEO_STREAM_END_FLAG,
                         NULL);
        logi("strem end flag!!!\n");
    }
    if (ret != 0) {
        p_demuxer_data->put_video_pkt_fail_num++;
    } else {
        p_demuxer_data->put_video_pkt_ok_num++;
    }
    mm_send_command(h_vdec_comp, MM_COMMAND_WKUP, 0, NULL);

    mm_demuxer_calc_perf_info(&p_demuxer_data->vperf, p_pkt->size, p_demuxer_data->debug_en);

    return ret;
}

static int process_audio_extra_data(mm_demuxer_data *p_demuxer_data,
                                    struct aic_audio_decoder *p_decoder)
{
    struct mpp_packet pkt = {0};
    s32 ret = MM_ERROR_NONE;

    pkt.size = p_demuxer_data->extra_audio_pkt.size;
    ret = aic_audio_decoder_get_packet(p_decoder, &pkt, pkt.size);
    if (ret != 0) {
        return MM_ERROR_EMPTY_DATA;
    }

    memcpy(pkt.data, p_demuxer_data->extra_audio_pkt.data, pkt.size);
    p_demuxer_data->get_audio_pkt_ok_num++;
    pkt.flag = p_demuxer_data->extra_audio_pkt.flag;
    pkt.pts = p_demuxer_data->extra_audio_pkt.pts;
    ret = aic_audio_decoder_put_packet(p_decoder, &pkt);
    if (ret != 0) {
        loge("put extra pkt to decoder failed %x!!!\n", ret);
        p_demuxer_data->put_audio_pkt_fail_num++;
        return ret;
    }

    p_demuxer_data->put_audio_pkt_ok_num++;
    p_demuxer_data->extra_audio_pkt_flag = MM_FALSE;

    if (p_demuxer_data->extra_audio_pkt.data) {
        mpp_free(p_demuxer_data->extra_audio_pkt.data);
        p_demuxer_data->extra_audio_pkt.data = NULL;
        p_demuxer_data->extra_audio_pkt.size = 0;
    }
    return MM_ERROR_NONE;
}

static int
mm_demuxer_component_process_audio_pkt(mm_demuxer_data *p_demuxer_data,
                                       struct aic_parser_packet *p_pkt)
{
    s32 ret = MM_ERROR_NONE;
    long diff = 0;
    struct mpp_packet pkt = {0};
    struct aic_audio_decoder *p_decoder = NULL;
    struct timespec before = {0}, after = {0};

    /*Get audio decoder handle*/
    mm_component *h_adec_comp =
        p_demuxer_data->out_port_bind[DEMUX_PORT_AUDIO_INDEX].p_bind_comp;
    if (h_adec_comp == NULL) {
        logd("get h_adec_comp is null\n");

        /*if auddec format not support, should peek next video packet*/
        mm_component *h_vdec_comp =
            p_demuxer_data->out_port_bind[DEMUX_PORT_VIDEO_INDEX].p_bind_comp;
        if (h_vdec_comp) {
            p_demuxer_data->need_peek = MM_TRUE;
        }
        return MM_ERROR_NULL_POINTER;
    }
    mm_get_parameter(h_adec_comp, MM_INDEX_PARAM_AUDIO_DECODER_HANDLE,
                     (void *)&p_decoder);
    if (p_decoder == NULL) {
        logd("get audio decoder is null\n");
        return MM_ERROR_NULL_POINTER;
    }

    /*process extra audio pkt and put it to decoder*/
    if (p_demuxer_data->extra_audio_pkt_flag) {
        ret = process_audio_extra_data(p_demuxer_data, p_decoder);
        if (ret == MM_ERROR_NONE)
            mm_send_command(h_adec_comp, MM_COMMAND_WKUP, 0, NULL);
    }

    /*Get empty packet from decoder*/
    pkt.size = p_pkt->size;
    ret = aic_audio_decoder_get_packet(p_decoder, &pkt, pkt.size);
    if (ret != 0) {
        aic_msg_wait_new_msg(&p_demuxer_data->s_msg, 0);
        p_demuxer_data->need_peek = MM_FALSE;
        return MM_ERROR_EMPTY_DATA;
    }
    p_demuxer_data->get_audio_pkt_ok_num++;
    /*Read audio data from parser and fill it to decoder packet*/
    p_pkt->data = pkt.data;
    clock_gettime(CLOCK_REALTIME, &before);
    ret = aic_parser_read(p_demuxer_data->p_parser, p_pkt);
    clock_gettime(CLOCK_REALTIME, &after);
    diff = (after.tv_sec - before.tv_sec) * 1000 * 1000 +
           (after.tv_nsec - before.tv_nsec) / 1000;
    if (diff > 42 * 1000) {
        printf("[%s:%d]:%ld\n", __FUNCTION__, __LINE__, diff);
    }

    logi("audio aic_parser_read,pts:" FMT_d64 ",type = %d,size:%d,flag:0x%x\n",
         p_pkt->pts, p_pkt->type, p_pkt->size, p_pkt->flag);

    if (!ret) { // read ok
        p_demuxer_data->need_peek = MM_TRUE;
    } else { // now  nothing to do ,becase no other return val
        loge("read audio data fail\n");
        p_demuxer_data->need_peek = MM_FALSE;
        return MM_ERROR_READ_FAILED;
    }

    /*Put packet to decoder*/
    pkt.flag = p_pkt->flag;
    pkt.pts = p_pkt->pts;
    ret = aic_audio_decoder_put_packet(p_decoder, &pkt);
    if (ret != 0) {
        p_demuxer_data->put_audio_pkt_fail_num++;
    } else {
        p_demuxer_data->put_audio_pkt_ok_num++;
    }
    mm_send_command(h_adec_comp, MM_COMMAND_WKUP, MM_TRUE, NULL);
    return ret;
}

static void mm_demuxer_show_debug_info(mm_demuxer_data *p_demuxer_data)
{
    if (!p_demuxer_data->debug_en)
        return;

    printf("**************************Demuxer comp info***************************\n");
    printf("net stream:  %d\n", p_demuxer_data->net_stream);
    printf("cache_en:    %d\n", p_demuxer_data->cache.cache_en);
    if (p_demuxer_data->cache.cache_en) {  
        printf("video cache info:\n");
        printf("param:    sta_sz    max_sz   sta_num    max_num\n");
        printf("\t%8u  %8u   %7u    %7u\n",
                p_demuxer_data->cache.params.start_play_size,
                p_demuxer_data->cache.params.max_cache_buffer_size,
                p_demuxer_data->cache.params.start_play_num,
                p_demuxer_data->cache.params.max_cache_pkt_num);
        printf("vring:     total      used      free\n");
        printf("\t%8u  %8u  %8u\n",
                MAX_VIDEO_CACHE_BUFFER_SIZE,
                mpp_ringbuffer_data_len(p_demuxer_data->cache.v_ringbuf),
                mpp_ringbuffer_space_len(p_demuxer_data->cache.v_ringbuf));
        printf("vlist:     total      used      free\n");
        printf("\t%8u  %8u  %8u\n",
                MAX_CACHE_PACKET_NUM,
                p_demuxer_data->cache.v_list_used,
                MAX_CACHE_PACKET_NUM - p_demuxer_data->cache.v_list_used);
        printf("vdata:    dat_sz   dat_cnt\n");
        printf("\t%8lu  %8u\n",
                p_demuxer_data->vperf.data_size,
                p_demuxer_data->vperf.data_cnt);

        printf("\naudio cache info:\n");
        printf("param:    sta_sz    max_sz   sta_num    max_num\n");
        printf("\t%8u  %8u   %7u    %7u\n",
                p_demuxer_data->cache.aparams.start_play_size,
                p_demuxer_data->cache.aparams.max_cache_buffer_size,
                p_demuxer_data->cache.aparams.start_play_num,
                p_demuxer_data->cache.aparams.max_cache_pkt_num);
        printf("aring:     total      used      free\n");
        printf("\t%8u  %8u  %8u\n",
                MAX_AUDIO_CACHE_BUFFER_SIZE,
                mpp_ringbuffer_data_len(p_demuxer_data->cache.a_ringbuf),
                mpp_ringbuffer_space_len(p_demuxer_data->cache.a_ringbuf));
        printf("alist:     total      used      free\n");
        printf("\t%8u  %8u  %8u\n",
                MAX_CACHE_PACKET_NUM,
                p_demuxer_data->cache.a_list_used,
                MAX_CACHE_PACKET_NUM - p_demuxer_data->cache.a_list_used);
        printf("adata:    dat_sz   dat_cnt\n");
        printf("\t%8lu  %8u\n",
                p_demuxer_data->aperf.data_size,
                p_demuxer_data->aperf.data_cnt);
    }

    printf("\nvideo:  pkt_num    get_ok    put_ok    put_fail\n");
    printf("\t%7u    %6u    %6u   %8u\n",
            p_demuxer_data->video_pkt_num,
            p_demuxer_data->get_video_pkt_ok_num,
            p_demuxer_data->put_video_pkt_ok_num,
            p_demuxer_data->put_video_pkt_fail_num);
    printf("audio:  pkt_num    get_ok    put_ok    put_fail\n");
    printf("\t%7u    %6u    %6u   %8u\n",
            p_demuxer_data->audio_pkt_num,
            p_demuxer_data->get_audio_pkt_ok_num,
            p_demuxer_data->put_audio_pkt_ok_num,
            p_demuxer_data->put_audio_pkt_fail_num);

    printf("\nstate: %s\n", mm_component_sta_to_str(p_demuxer_data->state));
}

static int mm_demuxer_component_cache_audio_pkt(mm_demuxer_data *p_demuxer_data, struct aic_parser_packet *p_pkt)
{
    struct timespec before = {0}, after = {0};
    struct audio_packet_manager *pm = NULL;
    struct mpp_packet pkt = {0};
    s32 ret = MM_ERROR_NONE;
    long diff = 0;

    if (p_pkt->stream_index < 0 || p_pkt->stream_index >= p_demuxer_data->audio_track_count) {
        loge("some error happened, track_id:%d track_count:%d\n", p_pkt->stream_index, p_demuxer_data->audio_track_count);
        p_demuxer_data->need_peek = MM_FALSE;
        return MM_ERROR_EMPTY_DATA;
    }

    pm = p_demuxer_data->pm[p_pkt->stream_index];
    pkt.size = p_pkt->size;
    if (p_pkt->stream_index == p_demuxer_data->current_track_id) {
        ret = audio_pm_dequeue_empty_packet(pm, &pkt, pkt.size);
        if (0 != ret) {
            aic_msg_wait_new_msg(&p_demuxer_data->s_msg, 0);
            p_demuxer_data->need_peek = MM_FALSE;
            return MM_ERROR_EMPTY_DATA;
        }
    } else {
        do {
            ret = audio_pm_dequeue_empty_packet(pm, &pkt, pkt.size);
            if (0 != ret) {
                struct mpp_packet *mpkt = audio_pm_dequeue_ready_packet(pm);
                if (NULL == mpkt) {
                    loge("audio parser queue have memory leak!!!\n");
                    return MM_ERROR_EMPTY_DATA;
                }
                audio_pm_enqueue_empty_packet(pm, mpkt);
            }
        } while (0 != ret);
    }

    /*Read audio data from parser and fill it to demuxer buffer*/
    p_pkt->data = pkt.data;
    clock_gettime(CLOCK_REALTIME, &before);
    ret = aic_parser_read(p_demuxer_data->p_parser, p_pkt);
    clock_gettime(CLOCK_REALTIME, &after);
    diff = (after.tv_sec - before.tv_sec) * 1000 * 1000 +
        (after.tv_nsec - before.tv_nsec) / 1000;
    if (diff > 42 * 1000) {
        printf("[%s:%d]:%ld\n", __FUNCTION__, __LINE__, diff);
    }

    logi("audio aic_parser_read,pts:" FMT_d64 ",type = %d,size:%d,flag:0x%x\n",
        p_pkt->pts, p_pkt->type, p_pkt->size, p_pkt->flag);

    if (0 == ret) { // read ok
        p_demuxer_data->need_peek = MM_TRUE;
    } else { // now  nothing to do ,becase no other return val
        loge("read audio data fail\n");
        p_demuxer_data->need_peek = MM_FALSE;
        return MM_ERROR_READ_FAILED;
    }

    /*Put packet to demuxer buffer*/
    pkt.flag = p_pkt->flag;
    pkt.pts = p_pkt->pts;
    ret = audio_pm_enqueue_ready_packet(pm, &pkt);

    return ret;
}

static int mm_demuxer_component_send_data_to_adec(mm_demuxer_data *p_demuxer_data)
{
    s32 ret = MM_ERROR_NONE;
    struct mpp_packet pkt = {0};
    struct audio_packet_manager *pm = NULL;
    struct aic_audio_decoder *p_decoder = NULL;

    /*Get audio decoder handle*/
    mm_component *h_adec_comp =
        p_demuxer_data->out_port_bind[DEMUX_PORT_AUDIO_INDEX].p_bind_comp;
    if (h_adec_comp == NULL) {
        loge("get h_adec_comp is null\n");

        /*if auddec format not support, should peek next video packet*/
        mm_component *h_vdec_comp =
            p_demuxer_data->out_port_bind[DEMUX_PORT_VIDEO_INDEX].p_bind_comp;
        if (h_vdec_comp) {
            p_demuxer_data->need_peek = MM_TRUE;
        }
        return MM_ERROR_NULL_POINTER;
    }
    mm_get_parameter(h_adec_comp, MM_INDEX_PARAM_AUDIO_DECODER_HANDLE, (void *)&p_decoder);
    if (p_decoder == NULL) {
        loge("get audio decoder is null\n");
        return MM_ERROR_NULL_POINTER;
    }

    int current_track_id = p_demuxer_data->current_track_id;
    if (current_track_id < 0 || current_track_id >= p_demuxer_data->audio_track_count) {
        loge("invalid trace_id:%d need between [%d:%d]\n", current_track_id, 0, p_demuxer_data->audio_track_count - 1);
        current_track_id = 0;
        // return 0;
    }

    /*process extra audio pkt and put it to decoder*/
    if (p_demuxer_data->extra_audio_pkt_flag) {
        ret = process_audio_extra_data(p_demuxer_data, p_decoder);
        if (ret == MM_ERROR_NONE)
            mm_send_command(h_adec_comp, MM_COMMAND_WKUP, 0, NULL);
    }

    pm = p_demuxer_data->pm[current_track_id];
    struct mpp_packet *mpkt = audio_pm_dequeue_ready_packet(pm);
    if (NULL == mpkt) {
        return MM_ERROR_EMPTY_DATA;
    }

    /*Get empty packet from decoder*/
    pkt.size = mpkt->size;
    ret = aic_audio_decoder_get_packet(p_decoder, &pkt, pkt.size);
    if (ret != 0) {
        audio_pm_reclaim_ready_packet(pm, mpkt);
        return MM_ERROR_EMPTY_DATA;
    } else {
    }
    p_demuxer_data->get_audio_pkt_ok_num++;

    /*Put packet to decoder*/
    pkt.flag = mpkt->flag;
    pkt.pts = mpkt->pts;
    memcpy(pkt.data, mpkt->data, pkt.size);
    ret = aic_audio_decoder_put_packet(p_decoder, &pkt);
    if (ret != 0) {
        p_demuxer_data->put_audio_pkt_fail_num++;
    } else {
        p_demuxer_data->put_audio_pkt_ok_num++;
    }
    p_demuxer_data->current_pts = pkt.pts;
    audio_pm_enqueue_empty_packet(pm, mpkt);
    mm_send_command(h_adec_comp, MM_COMMAND_WKUP, MM_TRUE, NULL);

    return ret;
}


static int mm_demuxer_component_cache_video(mm_demuxer_data *p_demuxer_data,
                                            struct aic_parser_packet *pkt)
{
    mm_demuxer_cache *cache = &p_demuxer_data->cache;
    mm_demuxer_cache_packet *pkt_node = NULL;
    s32 ret = MM_ERROR_NONE;
    s32 size = 0;

    size = mpp_ringbuffer_space_len(cache->v_ringbuf);
    if (size < pkt->size) {
        cache_cond_signal(&p_demuxer_data->cache);
        logd("v_ringbuf space size(%d < %d) not enough", size, pkt->size);
        goto FAIL;
    }

    /*1. Get an empty packet from the empty list*/
    pthread_mutex_lock(&cache->cache_lock);
    if (mpp_list_empty(&cache->v_empty_list)) {
        pthread_mutex_unlock(&cache->cache_lock);
        cache_cond_signal(&p_demuxer_data->cache);
        goto FAIL;
    }
    pkt_node = mpp_list_first_entry(&cache->v_empty_list,
                                    mm_demuxer_cache_packet, list);
    if (!pkt_node) {
        pthread_mutex_unlock(&cache->cache_lock);
        cache_cond_signal(&p_demuxer_data->cache);
        logw("v_empty_list, then not peek");
        goto FAIL;
    }
    pthread_mutex_unlock(&cache->cache_lock);

    /*2. Get empty space from the ringbuffer*/
    memcpy(&pkt_node->pkt, pkt, sizeof(struct aic_parser_packet));
    ret = mpp_ringbuffer_get_empty_space(cache->v_ringbuf,
            (unsigned char **)&pkt_node->pkt.data, &size);
    if (ret != 0 || !pkt_node->pkt.data) {
        cache_cond_signal(&p_demuxer_data->cache);
        logw("v_ringbuf empty space(%d)", ret);
        goto FAIL;
    }

    /*3. Read data from the parser to ringbuf*/
    p_demuxer_data->vperf.tm_read_start = mm_get_time_us();
    if (size >= pkt->size) {
        /*zero-copy: direct read parser data to ringbuf*/
        pkt->data = pkt_node->pkt.data;
        ret = aic_parser_read(p_demuxer_data->p_parser, pkt);
        if (ret) {
            logw("Read video parser data failed %d.", ret);
            goto FAIL;
        }
        mpp_ringbuffer_flush(cache->v_ringbuf, pkt->size);
    } else {
        /*ringbuf boundary:wrap-around split, using temp buffer*/
        if (pkt->size > cache->vpkt.size) {
            void *new_data = mpp_realloc(cache->vpkt.data, pkt->size);
            if (!new_data) {
                loge("Read video parser data failed %d.", ret);
                goto FAIL;
            }
            cache->vpkt.data = new_data;
            cache->vpkt.size = pkt->size;
        }
        pkt->data = cache->vpkt.data;
        ret = aic_parser_read(p_demuxer_data->p_parser, pkt);
        if (ret) {
            logw("Read video parser data failed %d.", ret);
            goto FAIL;
        }
        mpp_ringbuffer_put(cache->v_ringbuf, pkt->data, pkt->size);
    }
    p_demuxer_data->need_peek = MM_TRUE;
    p_demuxer_data->vperf.tm_read_end = mm_get_time_us();

    /*4. Move packet from empty list to ready list*/
    pthread_mutex_lock(&cache->cache_lock);
    mpp_list_del(&pkt_node->list);
    mpp_list_add_tail(&pkt_node->list, &cache->v_ready_list);
    cache->v_list_used++;
    pthread_mutex_unlock(&cache->cache_lock);
    cache_cond_signal(&p_demuxer_data->cache);

    /*5. calc video perf and update cache strategy params*/
    mm_demuxer_calc_perf_info(&p_demuxer_data->vperf, pkt->size,
                              p_demuxer_data->debug_en);
    mm_demuxer_update_cache_params(p_demuxer_data);

    return MM_ERROR_NONE;

FAIL:
    p_demuxer_data->need_peek = MM_FALSE;
    usleep(5000);

    return MM_ERROR_EMPTY_DATA;
}


static int mm_demuxer_component_cache_audio(mm_demuxer_data *p_demuxer_data,
                                            struct aic_parser_packet *pkt)
{
    mm_demuxer_cache *cache = &p_demuxer_data->cache;
    mm_demuxer_cache_packet *pkt_node = NULL;
    s32 ret = MM_ERROR_NONE;
    s32 size = 0;

    size = mpp_ringbuffer_space_len(cache->a_ringbuf);
    if (size < pkt->size) {
        cache_cond_signal(&p_demuxer_data->cache);
        logd("a_ringbuf space size(%d < %d) not enough",
            size, pkt->size);
        goto FAIL;
    }

    /*1. Get an empty packet from the empty list*/
    pthread_mutex_lock(&cache->cache_lock);
    if (mpp_list_empty(&cache->a_empty_list)) {
        pthread_mutex_unlock(&cache->cache_lock);
        cache_cond_signal(&p_demuxer_data->cache);
        goto FAIL;
    }
    pkt_node = mpp_list_first_entry(&cache->a_empty_list, mm_demuxer_cache_packet, list);
    if (!pkt_node) {
        pthread_mutex_unlock(&cache->cache_lock);
        cache_cond_signal(&p_demuxer_data->cache);
        logw("a_empty_list, then not peek");
        goto FAIL;
    }
    pthread_mutex_unlock(&cache->cache_lock);

    /*2. Get empty space from the ringbuffer*/
    memcpy(&pkt_node->pkt, pkt, sizeof(struct aic_parser_packet));

    ret = mpp_ringbuffer_get_empty_space(cache->a_ringbuf,
            (unsigned char **)&pkt_node->pkt.data, &size);
    if (ret != 0 || !pkt_node->pkt.data) {
        cache_cond_signal(&p_demuxer_data->cache);
        logw("a_ringbuf empty space(%d)", ret);
        goto FAIL;
    }

    /*3. Read data from the parser to ringbuf*/
    p_demuxer_data->aperf.tm_read_start = mm_get_time_us();
    if (size >= pkt->size) {
        /*zero-copy: direct read parser data to ringbuf*/
        pkt->data = pkt_node->pkt.data;
        ret = aic_parser_read(p_demuxer_data->p_parser, pkt);
        if (ret) {
            logw("Read audio parser data failed %d.", ret);
            goto FAIL;
        }
        mpp_ringbuffer_flush(cache->a_ringbuf, pkt->size);
    } else {
        /*ringbuf boundary:wrap-around split, using temp buffer*/
        if (pkt->size > cache->apkt.size) {
            void *new_data = mpp_realloc(cache->apkt.data, pkt->size);
            if (!new_data) {
                loge("Read audio parser data failed %d.", ret);
                goto FAIL;
            }
            cache->apkt.data = new_data;
            cache->apkt.size = pkt->size;
        }
        pkt->data = cache->apkt.data;
        ret = aic_parser_read(p_demuxer_data->p_parser, pkt);
        if (ret) {
            logw("Read audio parser data failed %d.", ret);
            goto FAIL;
        }
        mpp_ringbuffer_put(cache->a_ringbuf, pkt->data, pkt->size);
    }
    p_demuxer_data->need_peek = MM_TRUE;
    p_demuxer_data->aperf.tm_read_end = mm_get_time_us();

    /*4. Move packet from empty list to ready list*/
    pthread_mutex_lock(&cache->cache_lock);
    mpp_list_del(&pkt_node->list);
    mpp_list_add_tail(&pkt_node->list, &cache->a_ready_list);
    cache->a_list_used++;
    pthread_mutex_unlock(&cache->cache_lock);
    cache_cond_signal(&p_demuxer_data->cache);

    /*5. calc audio perf and update cache strategy params*/
    p_demuxer_data->aperf.is_audio = 1;
    mm_demuxer_calc_perf_info(&p_demuxer_data->aperf, pkt->size,
                              p_demuxer_data->debug_en);
    return MM_ERROR_NONE;

FAIL:
    p_demuxer_data->need_peek = MM_FALSE;
    usleep(5000);
    return MM_ERROR_EMPTY_DATA;
}

static void *mm_demuxer_component_thread(void *p_thread_data)
{
    s32 ret = MM_ERROR_NONE;
    s32 cmd = MM_COMMAND_UNKNOWN;
    MM_BOOL b_notify_frame_end = MM_FALSE;
    struct aic_parser_packet s_pkt;
    mm_demuxer_data *p_demuxer_data = NULL;

    memset(&s_pkt, 0x00, sizeof(struct aic_parser_packet));
    p_demuxer_data = (mm_demuxer_data *)p_thread_data;
    p_demuxer_data->need_peek = MM_TRUE;

    while (1) {
    _AIC_MSG_GET_:
        /* process cmd and change state*/
        cmd = mm_demuxer_component_process_cmd(p_demuxer_data);
        if (MM_COMMAND_STATE_SET == cmd) {
            continue;
        } else if (MM_COMMAND_STOP == cmd) {
            goto _EXIT;
        }

        if (p_demuxer_data->state != MM_STATE_EXECUTING) {
            aic_msg_wait_new_msg(&p_demuxer_data->s_msg, 0);
            continue;
        }

        if (p_demuxer_data->eos) {
            if (!b_notify_frame_end) {
                mm_demuxer_event_notify(p_demuxer_data, MM_EVENT_BUFFER_FLAG, 0,
                                        0, NULL);
                b_notify_frame_end = MM_TRUE;
            }
            aic_msg_wait_new_msg(&p_demuxer_data->s_msg, 0);
            continue;
        }
        b_notify_frame_end = 0;

        /* peek pkt info from parser*/
        if (p_demuxer_data->need_peek) {
            s_pkt.flag = 0;
            s_pkt.size = 0;
            ret = aic_parser_peek(p_demuxer_data->p_parser, &s_pkt);
            if (!ret) {
                logd("peek type %d ok\n", s_pkt.type);
                p_demuxer_data->need_peek = MM_FALSE;
                if (s_pkt.type == MPP_MEDIA_TYPE_VIDEO) {
                    p_demuxer_data->video_pkt_num++;
                } else if (s_pkt.type == MPP_MEDIA_TYPE_AUDIO) {
                    p_demuxer_data->audio_pkt_num++;
                }
            } else if (ret == PARSER_EOS) { //peek end
                p_demuxer_data->eos = 1;
                if (s_pkt.size == 0 && s_pkt.flag == PACKET_EOS) {
                    mm_demuxer_component_process_eos_pkt(p_demuxer_data);
                }
                goto _AIC_MSG_GET_;
            } else { // now  nothing to do ,becase no other return val
                logw("peek fail\n");
                goto _AIC_MSG_GET_;
            }
        }
        /*skip other pkt type*/
        if (s_pkt.type != MPP_MEDIA_TYPE_VIDEO &&
            s_pkt.type != MPP_MEDIA_TYPE_AUDIO) {
            p_demuxer_data->need_peek = MM_TRUE;
            goto _AIC_MSG_GET_;
        }

        if (p_demuxer_data->net_stream && p_demuxer_data->cache.cache_en) {
            /* Network stream: cache packets for cache thread */
            if (s_pkt.type == MPP_MEDIA_TYPE_VIDEO) {
                if (p_demuxer_data->skip_track & DEMUX_SKIP_VIDEO_TRACK) {
                    p_demuxer_data->need_peek = MM_TRUE;
                    goto _AIC_MSG_GET_;
                }
                mm_demuxer_component_cache_video(p_demuxer_data, &s_pkt);
            } else if (s_pkt.type == MPP_MEDIA_TYPE_AUDIO) {
                if (p_demuxer_data->skip_track & DEMUX_SKIP_AUDIO_TRACK) {
                    p_demuxer_data->need_peek = MM_TRUE;
                    goto _AIC_MSG_GET_;
                }
                mm_demuxer_component_cache_audio(p_demuxer_data, &s_pkt);
            }
        } else {
            /* Local file: direct path (original code)： put pkt to decoder */
            if (s_pkt.type == MPP_MEDIA_TYPE_VIDEO) {
                if (p_demuxer_data->skip_track & DEMUX_SKIP_VIDEO_TRACK) {
                    p_demuxer_data->need_peek = MM_TRUE;
                    goto _AIC_MSG_GET_;
                }
                mm_demuxer_component_process_video_pkt(p_demuxer_data, &s_pkt);
            } else if (s_pkt.type == MPP_MEDIA_TYPE_AUDIO) {
                if (p_demuxer_data->skip_track & DEMUX_SKIP_AUDIO_TRACK) {
                    p_demuxer_data->need_peek = MM_TRUE;
                    goto _AIC_MSG_GET_;
                }
                if (1 == p_demuxer_data->s_media_info.audio_track_count) {
                    mm_demuxer_component_process_audio_pkt(p_demuxer_data, &s_pkt);
                } else if (p_demuxer_data->s_media_info.audio_track_count > 1) {
                    mm_demuxer_component_cache_audio_pkt(p_demuxer_data, &s_pkt);
                }
            }
            if (p_demuxer_data->s_media_info.audio_track_count > 1) {
                mm_demuxer_component_send_data_to_adec(p_demuxer_data);
            }
        }
    }

_EXIT:
    mm_demuxer_show_debug_info(p_demuxer_data);
    return (void *)MM_ERROR_NONE;
}


static void mm_demuxer_cache_deinit(mm_demuxer_cache *cache)
{
    mm_demuxer_cache_packet *pkt_node = NULL, *pkt_node1 = NULL;

    cache->cache_en = MM_FALSE;

    pthread_mutex_lock(&cache->cache_lock);
    if (!mpp_list_empty(&cache->v_empty_list)) {
        mpp_list_for_each_entry_safe(pkt_node, pkt_node1,
                                     &cache->v_empty_list, list)
        {
            mpp_list_del(&pkt_node->list);
        }
    }
    if (!mpp_list_empty(&cache->v_ready_list)) {
        mpp_list_for_each_entry_safe(pkt_node, pkt_node1,
                                     &cache->v_ready_list, list)
        {
            mpp_list_del(&pkt_node->list);
        }
    }
    if (!mpp_list_empty(&cache->a_empty_list)) {
        mpp_list_for_each_entry_safe(pkt_node, pkt_node1,
                                     &cache->a_empty_list, list)
        {
            mpp_list_del(&pkt_node->list);
        }
    }
    if (!mpp_list_empty(&cache->a_ready_list)) {
        mpp_list_for_each_entry_safe(pkt_node, pkt_node1,
                                     &cache->a_ready_list, list)
        {
            mpp_list_del(&pkt_node->list);
        }
    }
    if (cache->vpkt.data) {
        mpp_free(cache->vpkt.data);
        cache->vpkt.data = NULL;
    }
    if (cache->apkt.data) {
        mpp_free(cache->apkt.data);
        cache->apkt.data = NULL;
    }
    if (cache->vpkt_base) {
        mpp_free(cache->vpkt_base);
        cache->vpkt_base = NULL;
    }
    if (cache->apkt_base) {
        mpp_free(cache->apkt_base);
        cache->apkt_base = NULL;
    }
    pthread_mutex_unlock(&cache->cache_lock);

    if (cache->v_ringbuf) {
        mpp_ringbuffer_destroy(cache->v_ringbuf);
        cache->v_ringbuf = NULL;
    }
    if (cache->a_ringbuf) {
        mpp_ringbuffer_destroy(cache->a_ringbuf);
        cache->a_ringbuf = NULL;
    }

    pthread_cond_destroy(&cache->cache_cond);
    pthread_mutex_destroy(&cache->cache_lock);
}

static void mm_demuxer_cache_params_init(mm_demuxer_cache *cache)
{
    mm_demuxer_cache_params *vparams = &cache->params;
    mm_demuxer_cache_params *aparams = &cache->aparams;

    /*video cache params*/
    vparams->cache_buffer_size = MAX_VIDEO_CACHE_BUFFER_SIZE / 2;
    vparams->start_cache_size = MAX_VIDEO_CACHE_BUFFER_SIZE / 16;
    vparams->max_start_cache_size = MAX_VIDEO_CACHE_BUFFER_SIZE * 15 / 16;
    vparams->start_play_cache_time = START_PLAY_CACHE_TIME;
    vparams->max_cache_buffer_size = MAX_VIDEO_CACHE_BUFFER_SIZE;
    vparams->max_cache_pkt_num = MAX_CACHE_PACKET_NUM - 16;
    vparams->start_play_num = 25;

    /*audio cache params*/
    aparams->cache_buffer_size = MAX_AUDIO_CACHE_BUFFER_SIZE / 2;
    aparams->start_cache_size = MAX_AUDIO_CACHE_BUFFER_SIZE / 16;
    aparams->max_start_cache_size = MAX_AUDIO_CACHE_BUFFER_SIZE * 15 / 16;
    aparams->start_play_cache_time = START_PLAY_CACHE_TIME;
    aparams->max_cache_buffer_size = MAX_AUDIO_CACHE_BUFFER_SIZE;
    aparams->max_cache_pkt_num = MAX_CACHE_PACKET_NUM - 16;
    aparams->start_play_num = 25;
}

static s32 mm_demuxer_cache_init(mm_demuxer_cache *cache)
{
    mm_demuxer_cache_packet *pkt_node = NULL;
    int i = 0, pkt_size = 0;

    memset(cache, 0, sizeof(mm_demuxer_cache));

    mm_demuxer_cache_params_init(cache);

    /*1. Initialize packet lists */
    mpp_list_init(&cache->v_empty_list);
    mpp_list_init(&cache->v_ready_list);
    mpp_list_init(&cache->a_empty_list);
    mpp_list_init(&cache->a_ready_list);
    pthread_mutex_init(&cache->cache_lock, NULL);
    pthread_cond_init(&cache->cache_cond, NULL);

    /*2. Create video packet pool */
    pkt_size = sizeof(mm_demuxer_cache_packet) * MAX_CACHE_PACKET_NUM;
    cache->vpkt_base = (mm_demuxer_cache_packet *)mpp_alloc(pkt_size);
    if (!cache->vpkt_base) {
        loge("failed to create video cache list\n");
        goto FAIL;
    }
    memset((char *)cache->vpkt_base, 0, pkt_size);
    for (i = 0; i < MAX_CACHE_PACKET_NUM; i++) {
        pkt_node = &cache->vpkt_base[i];
        mpp_list_add_tail(&pkt_node->list, &cache->v_empty_list);
    }
    cache->v_ringbuf = mpp_ringbuffer_create(MAX_VIDEO_CACHE_BUFFER_SIZE);
    if (!cache->v_ringbuf) {
        loge("failed to create video ringbuffer\n");
        goto FAIL;
    }
    cache->vpkt.size = 64 * 1024;
    cache->vpkt.data = mpp_alloc(cache->vpkt.size);
    if (!cache->vpkt.data) {
        loge("failed to create video tempbuffer\n");
        goto FAIL;
    }
    memset(cache->vpkt.data, 0, cache->vpkt.size);
    /*3. Create audio packet pool */
    cache->apkt_base = (mm_demuxer_cache_packet *)mpp_alloc(pkt_size);
    if (!cache->apkt_base) {
        loge("failed to create audio cache list\n");
        goto FAIL;
    }
    memset((char *)cache->apkt_base, 0, pkt_size);
    for (i = 0; i < MAX_CACHE_PACKET_NUM; i++) {
        pkt_node = &cache->apkt_base[i];
        mpp_list_add_tail(&pkt_node->list, &cache->a_empty_list);
    }
    cache->a_ringbuf = mpp_ringbuffer_create(MAX_AUDIO_CACHE_BUFFER_SIZE);
    if (!cache->a_ringbuf) {
        loge("failed to create audio ringbuffer\n");
        goto FAIL;
    }
    cache->apkt.size = 8 * 1024;
    cache->apkt.data = mpp_alloc(cache->apkt.size);
    if (!cache->apkt.data) {
        loge("failed to create audio tempbuffer\n");
        goto FAIL;
    }
    memset(cache->apkt.data, 0, cache->apkt.size);

    cache->cache_en = MM_TRUE;

    return MM_ERROR_NONE;

FAIL:
    mm_demuxer_cache_deinit(cache);
    return MM_ERROR_INSUFFICIENT_RESOURCES;
}


static s32 mm_demuxer_cache_process_video(mm_demuxer_data *p_demuxer_data)
{
    mm_demuxer_cache *cache = &p_demuxer_data->cache;
    mm_demuxer_cache_packet *pkt_node = NULL;
    struct mpp_decoder *vdecoder = NULL;
    mm_component *h_vdec_comp = NULL;
    struct mpp_packet pkt = {0};
    s32 ret = MM_ERROR_NONE;

    /*1. Get video decoder handle*/
    h_vdec_comp = p_demuxer_data->out_port_bind[DEMUX_PORT_VIDEO_INDEX].p_bind_comp;
    if (!h_vdec_comp) {
        logw("h_vdec is NULL");
        return MM_ERROR_INSUFFICIENT_RESOURCES;
    }

    /*2. Get a ready packet from the video ready list*/
    pthread_mutex_lock(&cache->cache_lock);
    if (mpp_list_empty(&cache->v_ready_list)) {
        pthread_mutex_unlock(&cache->cache_lock);
        return MM_ERROR_EMPTY_DATA;
    }
    pkt_node = mpp_list_first_entry(&cache->v_ready_list, mm_demuxer_cache_packet, list);
    mpp_list_del(&pkt_node->list);
    pthread_mutex_unlock(&cache->cache_lock);

    /*3. Check packet flag and set stream start flag if needed*/
    if (pkt_node->pkt.flag & PACKET_FLAG_SOS) {
        logd("start of stream packet, pts:%ld.", pkt_node->pkt.pts);
        mm_set_parameter(h_vdec_comp, MM_INDEX_PARAM_VIDEO_STREAM_START_FLAG, NULL);
        pkt_node->pkt.flag &= ~PACKET_FLAG_SOS;
    }
    ret = mm_get_parameter(h_vdec_comp, MM_INDEX_PARAM_VIDEO_DECODER_HANDLE,
                            (void *)&vdecoder);
    if (ret || !vdecoder) {
        logw("get video decoder failed, dec is %p, ret %d", vdecoder, ret);
        return MM_ERROR_INSUFFICIENT_RESOURCES;
    }

    /*4. Send extra data to the decoder*/
    if (p_demuxer_data->extra_video_pkt_flag) {
        ret = process_video_extra_data(p_demuxer_data, vdecoder);
        if (ret == MM_ERROR_NONE)
            mm_send_command(h_vdec_comp, MM_COMMAND_WKUP, 0, NULL);
    }

    /*5. Get empty packet from the decoder and fill data*/
    ret = mpp_decoder_get_packet(vdecoder, &pkt, pkt_node->pkt.size);
    if (ret != 0) {
        mm_send_command(h_vdec_comp, MM_COMMAND_WKUP, 0, NULL);
        return MM_ERROR_EMPTY_DATA;
    }
    ret = mpp_ringbuffer_get(cache->v_ringbuf, (unsigned char *)pkt.data, pkt_node->pkt.size);
    if (ret != pkt_node->pkt.size) {
        loge("failed to get packet from ringbuffer, ret %d != %d", ret, pkt_node->pkt.size);
    }
    pkt.size = pkt_node->pkt.size;
    pkt.flag = pkt_node->pkt.flag;
    pkt.pts = pkt_node->pkt.pts;
    p_demuxer_data->get_video_pkt_ok_num++;

    /*6. Return packet node to empty list (already removed from ready list) */
    pthread_mutex_lock(&cache->cache_lock);
    mpp_list_add_tail(&pkt_node->list, &cache->v_empty_list);
    cache->v_list_used--;
    pthread_mutex_unlock(&cache->cache_lock);

    /*7. Put packet to the decoder*/
    ret = mpp_decoder_put_packet(vdecoder, &pkt);
    if (pkt.flag & PACKET_FLAG_EOS) {
        mm_set_parameter(h_vdec_comp, MM_INDEX_PARAM_VIDEO_STREAM_END_FLAG, NULL);
        logi("strem end flag!!!\n");
    }
    if (ret != 0) {
        p_demuxer_data->put_video_pkt_fail_num++;
    } else {
        p_demuxer_data->put_video_pkt_ok_num++;
    }

    /*8. Send wakeup command to the vdec component*/
    mm_send_command(h_vdec_comp, MM_COMMAND_WKUP, 0, NULL);

    return MM_ERROR_NONE;
}


static s32 mm_demuxer_cache_process_audio(mm_demuxer_data *p_demuxer_data)
{
    mm_demuxer_cache *cache = &p_demuxer_data->cache;
    mm_demuxer_cache_packet *pkt_node = NULL;
    struct aic_audio_decoder *adecoder = NULL;
    mm_component *h_adec_comp = NULL;
    struct mpp_packet pkt = {0};
    s32 ret = MM_ERROR_NONE;

    /*1. Get audio decoder handle*/
    h_adec_comp = p_demuxer_data->out_port_bind[DEMUX_PORT_AUDIO_INDEX].p_bind_comp;
    if (!h_adec_comp) {
        logw("h_vdh_adec_compec is NULL");
        return MM_ERROR_INSUFFICIENT_RESOURCES;
    }
    ret = mm_get_parameter(h_adec_comp, MM_INDEX_PARAM_AUDIO_DECODER_HANDLE, (void *)&adecoder);
    if (ret || !adecoder) {
        logw("get audio decoder failed, dec is %p, ret %d", adecoder, ret);
        return MM_ERROR_INSUFFICIENT_RESOURCES;
    }

    /*2. Send extra data to the decoder*/
    if (p_demuxer_data->extra_audio_pkt_flag) {
        ret = process_audio_extra_data(p_demuxer_data, adecoder);
        if (ret == MM_ERROR_NONE)
            mm_send_command(h_adec_comp, MM_COMMAND_WKUP, 0, NULL);
    }

    /*3. Get a ready packet from the audio ready list*/
    pthread_mutex_lock(&cache->cache_lock);
    if (mpp_list_empty(&cache->a_ready_list)) {
        pthread_mutex_unlock(&cache->cache_lock);
        return MM_ERROR_EMPTY_DATA;
    }
    pkt_node = mpp_list_first_entry(&cache->a_ready_list, mm_demuxer_cache_packet, list);
    mpp_list_del(&pkt_node->list);
    pthread_mutex_unlock(&cache->cache_lock);

    /*4. Get empty packet from the decoder and fill data*/
    ret = aic_audio_decoder_get_packet(adecoder, &pkt, pkt_node->pkt.size);
    if (ret != 0) {
        mm_send_command(h_adec_comp, MM_COMMAND_WKUP, 0, NULL);
        return MM_ERROR_EMPTY_DATA;
    }
    ret = mpp_ringbuffer_get(cache->a_ringbuf, (unsigned char *)pkt.data, pkt_node->pkt.size);
    if (ret != pkt_node->pkt.size) {
        loge("failed to get packet from ringbuffer, ret %d != %d", ret, pkt_node->pkt.size);
    }
    pkt.size = pkt_node->pkt.size;
    pkt.flag = pkt_node->pkt.flag;
    pkt.pts = pkt_node->pkt.pts;

    p_demuxer_data->get_audio_pkt_ok_num++;

    /*5. Return packet node to empty list (already removed from ready list) */
    pthread_mutex_lock(&cache->cache_lock);
    mpp_list_add_tail(&pkt_node->list, &cache->a_empty_list);
    cache->a_list_used--;
    pthread_mutex_unlock(&cache->cache_lock);

    /*6. Put packet to the decoder*/
    ret = aic_audio_decoder_put_packet(adecoder, &pkt);
    if (ret != 0) {
        p_demuxer_data->put_audio_pkt_fail_num++;
    } else {
        p_demuxer_data->put_audio_pkt_ok_num++;
    }

    /*7. Send wakeup command to the adec component*/
    mm_send_command(h_adec_comp, MM_COMMAND_WKUP, 0, NULL);
    return MM_ERROR_NONE;
}

static int stream_cache_is_enough(mm_demuxer_data *p_demuxer_data)
{
    mm_component *h_vdec_comp =
        p_demuxer_data->out_port_bind[DEMUX_PORT_VIDEO_INDEX].p_bind_comp;
    mm_component *h_adec_comp =
        p_demuxer_data->out_port_bind[DEMUX_PORT_AUDIO_INDEX].p_bind_comp;
    mm_demuxer_perf_info *vperf = &p_demuxer_data->vperf;
    mm_demuxer_perf_info *aperf = &p_demuxer_data->aperf;
    mm_demuxer_cache *cache = &p_demuxer_data->cache;
    int is_enough = 0;

    pthread_mutex_lock(&cache->cache_lock);
    /*Initial status*/
    if ((vperf->data_size >= cache->params.start_play_size &&
        vperf->data_cnt >= cache->params.start_play_num) ||
        vperf->data_size >= cache->params.max_cache_buffer_size ||
        vperf->data_cnt >= cache->params.max_cache_pkt_num)
        is_enough = 1;

    if ((aperf->data_size >= cache->aparams.start_play_size &&
        aperf->data_cnt >= cache->aparams.start_play_num) ||
        aperf->data_size >= cache->aparams.max_cache_buffer_size ||
        aperf->data_cnt >= cache->aparams.max_cache_pkt_num)
        is_enough = 1;

    pthread_mutex_unlock(&cache->cache_lock);

    /* cache data not enouth, notify demuxer thread read parser fast*/
    if (cache->v_list_used < cache->params.start_play_num ||
        mpp_ringbuffer_data_len(p_demuxer_data->cache.v_ringbuf) <
        cache->params.start_play_size) {
        mm_demuxer_msg_put(&p_demuxer_data->s_msg, MM_COMMAND_WKUP, 0, NULL);
    }

    if (cache->a_list_used < cache->aparams.start_play_num ||
        mpp_ringbuffer_data_len(p_demuxer_data->cache.a_ringbuf) <
        cache->aparams.start_play_size) {
        mm_demuxer_msg_put(&p_demuxer_data->s_msg, MM_COMMAND_WKUP, 0, NULL);
    }

    /* cache data too much, notify vdec thread decode fast*/
    if (cache->v_list_used > cache->params.max_cache_pkt_num ||
        mpp_ringbuffer_data_len(p_demuxer_data->cache.v_ringbuf) >
        cache->params.max_cache_buffer_size) {
        if (h_vdec_comp)
            mm_send_command(h_vdec_comp, MM_COMMAND_WKUP, 0, NULL);
    }
    if (cache->a_list_used > cache->params.max_cache_pkt_num ||
        mpp_ringbuffer_data_len(p_demuxer_data->cache.a_ringbuf) >
        cache->aparams.max_cache_buffer_size) {
        if (h_adec_comp)
            mm_send_command(h_adec_comp, MM_COMMAND_WKUP, 0, NULL);
    }

    return is_enough;
}

static void *mm_demuxer_cache_thread(void *arg)
{
    mm_demuxer_data *p_demuxer_data = (mm_demuxer_data *)arg;
    mm_demuxer_cache *cache = &p_demuxer_data->cache;
    int has_video, has_audio;

    while (!cache->cache_stop_flag) {
        if (p_demuxer_data->state != MM_STATE_EXECUTING) {
            cache_cond_wait(cache, 10);
            continue;
        }

        pthread_mutex_lock(&cache->cache_lock);
        has_video = !mpp_list_empty(&cache->v_ready_list);
        has_audio = !mpp_list_empty(&cache->a_ready_list);
        pthread_mutex_unlock(&cache->cache_lock);

        if (!has_video && !has_audio) {
            if (p_demuxer_data->eos) {
                logd("no video or audio, send the last frame.");
                mm_demuxer_component_process_eos_pkt(p_demuxer_data);
            }
            cache_cond_wait(cache, 10);
            continue;
        }

        if (!p_demuxer_data->eos && !stream_cache_is_enough(p_demuxer_data)) {
            mm_demuxer_msg_put(&p_demuxer_data->s_msg, MM_COMMAND_WKUP, 0, NULL);
            cache_cond_wait(cache, 10);
            continue;
        }

        if (has_video && !(p_demuxer_data->skip_track & DEMUX_SKIP_VIDEO_TRACK)) {
            mm_demuxer_cache_process_video(p_demuxer_data);
        }

        if (has_audio && !(p_demuxer_data->skip_track & DEMUX_SKIP_AUDIO_TRACK)) {
            mm_demuxer_cache_process_audio(p_demuxer_data);
        }

        cache_cond_wait(cache, 10);
    }

    return NULL;
}
