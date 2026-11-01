/*
 * Copyright (C) 2024-2026 ArtInChip Technology Co., Ltd.
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>
#include "dashcam_engine.h"

struct dashcam_engine {
    lv_obj_t *recorder_obj;
    struct dashcam_config config;
    dashcam_state_t state;
    dashcam_event_cb event_cb;
    void *event_user_data;
    time_t record_start_time;
    char current_file[256];
};

static s32 engine_recorder_event_cb(void *app_data, s32 event, s32 d1, s32 d2)
{
    struct dashcam_engine *e = (struct dashcam_engine *)app_data;

    switch (event) {
    case AIC_RECORDER_EVENT_NEED_NEXT_FILE:
        e->record_start_time = time(NULL);
        break;
    case AIC_RECORDER_EVENT_COMPLETE:
        e->state = DASHCAM_STATE_STOPPED;
        break;
    case AIC_RECORDER_EVENT_NO_SPACE:
        break;
    case AIC_RECORDER_EVENT_RELEASE_VIDEO_FRAME:
        break;
    default:
        break;
    }

    if (e->event_cb)
        e->event_cb(event, NULL, e->event_user_data);

    return 0;
}

struct dashcam_engine *dashcam_engine_create(void)
{
    struct dashcam_engine *e = calloc(1, sizeof(*e));
    if (e)
        e->state = DASHCAM_STATE_IDLE;
    return e;
}

void dashcam_engine_destroy(struct dashcam_engine *e)
{
    if (!e)
        return;
    if (e->state == DASHCAM_STATE_RECORDING)
        dashcam_engine_stop_recording(e);
    if (e->recorder_obj)
        lv_obj_del(e->recorder_obj);
    free(e);
}

int dashcam_engine_init(struct dashcam_engine *e, struct dashcam_config *cfg)
{
    if (!e || !cfg)
        return -1;

    memcpy(&e->config, cfg, sizeof(*cfg));

    /* create recorder */
    e->recorder_obj = lv_aic_recorder_create(lv_scr_act());
    if (!e->recorder_obj)
        return -1;

    lv_aic_recorder_config_t rcfg;
    memset(&rcfg, 0, sizeof(rcfg));
    strncpy(rcfg.storage_path, cfg->storage_path, 255);
    rcfg.file_duration = cfg->file_duration ? cfg->file_duration : 60;
    rcfg.file_num = cfg->file_num;
    rcfg.video_width = cfg->video_width;
    rcfg.video_height = cfg->video_height;
    rcfg.video_bitrate = cfg->video_bitrate;
    rcfg.video_framerate = cfg->video_framerate;
    rcfg.qfactor = cfg->qfactor ? cfg->qfactor : 80;

    if (lv_aic_recorder_init(e->recorder_obj, &rcfg) != LV_RES_OK) {
        lv_obj_del(e->recorder_obj);
        e->recorder_obj = NULL;
        return -1;
    }

    lv_aic_recorder_set_event_callback(e->recorder_obj, engine_recorder_event_cb, e);
    e->state = DASHCAM_STATE_PREVIEW;
    return 0;
}

int dashcam_engine_start_recording(struct dashcam_engine *e)
{
    if (!e || e->state != DASHCAM_STATE_PREVIEW)
        return -1;

    lv_aic_recorder_set_cmd(e->recorder_obj, LV_AIC_RECORDER_CMD_START_RECORD, NULL);
    e->state = DASHCAM_STATE_RECORDING;
    e->record_start_time = time(NULL);

    const char *f = lv_aic_recorder_get_current_file(e->recorder_obj);
    if (f) {
        strncpy(e->current_file, f, 255);
        e->current_file[255] = '\0';
    }
    return 0;
}

int dashcam_engine_stop_recording(struct dashcam_engine *e)
{
    if (!e || e->state != DASHCAM_STATE_RECORDING)
        return -1;

    lv_aic_recorder_set_cmd(e->recorder_obj, LV_AIC_RECORDER_CMD_STOP_RECORD, NULL);
    e->state = DASHCAM_STATE_STOPPED;
    return 0;
}

dashcam_state_t dashcam_engine_get_state(struct dashcam_engine *e)
{
    return e ? e->state : DASHCAM_STATE_ERROR;
}

int dashcam_engine_get_record_time(struct dashcam_engine *e)
{
    if (!e || e->state != DASHCAM_STATE_RECORDING)
        return 0;
    return (int)(time(NULL) - e->record_start_time);
}

const char *dashcam_engine_get_current_file(struct dashcam_engine *e)
{
    return e ? e->current_file : NULL;
}

lv_obj_t *dashcam_engine_get_recorder(struct dashcam_engine *e)
{
    return e ? e->recorder_obj : NULL;
}

int dashcam_engine_get_record_count(struct dashcam_engine *e)
{
    if (!e || !e->recorder_obj)
        return 0;
    int count = 0;
    lv_aic_recorder_set_cmd(e->recorder_obj, LV_AIC_RECORDER_CMD_GET_RECORD_COUNT, &count);
    return count;
}

int dashcam_engine_get_record_list(struct dashcam_engine *e, struct aic_recorder_record *list,
                                   int max)
{
    if (!e || !e->recorder_obj || !list)
        return 0;

    lv_aic_recorder_query_t q = {0};
    int count = 0;

    q.max = max;
    q.records = list;
    lv_aic_recorder_set_cmd(e->recorder_obj, LV_AIC_RECORDER_CMD_GET_RECORD_COUNT, &count);
    q.count = count;
    lv_aic_recorder_set_cmd(e->recorder_obj, LV_AIC_RECORDER_CMD_GET_RECORD_LIST, &q);
    return q.count;
}

int dashcam_engine_get_record_by_date(struct dashcam_engine *e, int y, int m, int d,
                                      struct aic_recorder_record *list, int max)
{
    if (!e || !e->recorder_obj || !list)
        return 0;

    lv_aic_recorder_query_t q = {0};
    char date[16] = {0};

    snprintf(date, sizeof(date), "%04d-%02d-%02d", y, m, d);
    q.max = max;
    q.records = list;
    q.date = date;
    lv_aic_recorder_set_cmd(e->recorder_obj, LV_AIC_RECORDER_CMD_GET_RECORD_COUNT_BY_DATE, &q);
    lv_aic_recorder_set_cmd(e->recorder_obj, LV_AIC_RECORDER_CMD_GET_RECORD_BY_DATE, &q);

    LV_LOG_INFO("search date:%s, count:%d.", date, q.count);
    return q.count;
}

int dashcam_engine_get_locked_list(struct dashcam_engine *e, struct aic_recorder_record *list,
                                   int max)
{
    if (!e || !e->recorder_obj || !list)
        return 0;

    lv_aic_recorder_query_t q = {0};
    int count = 0;

    q.max = max;
    q.records = list;
    lv_aic_recorder_set_cmd(e->recorder_obj, LV_AIC_RECORDER_CMD_GET_LOCKED_COUNT, &count);
    q.count = count;
    lv_aic_recorder_set_cmd(e->recorder_obj, LV_AIC_RECORDER_CMD_GET_LOCKED_LIST, &q);

    LV_LOG_INFO("locked list, count:%d.", q.count);
    return q.count;
}

int dashcam_engine_get_picture_by_date(struct dashcam_engine *e, int y, int m, int d,
                                       struct aic_recorder_picture *list, int max)
{
    if (!e || !e->recorder_obj || !list)
        return 0;

    lv_aic_recorder_query_t q = {0};
    char date[16] = {0};

    snprintf(date, sizeof(date), "%04d-%02d-%02d", y, m, d);
    q.max = max;
    q.pictures = list;
    q.date = date;
    lv_aic_recorder_set_cmd(e->recorder_obj, LV_AIC_RECORDER_CMD_GET_PICTURE_COUNT_BY_DATE, &q);
    lv_aic_recorder_set_cmd(e->recorder_obj, LV_AIC_RECORDER_CMD_GET_PICTURE_BY_DATE, &q);

    LV_LOG_INFO("picture date:%s, count:%d.", date, q.count);
    return q.count;
}

void dashcam_engine_set_callback(struct dashcam_engine *e, dashcam_event_cb cb, void *ud)
{
    if (e) {
        e->event_cb = cb;
        e->event_user_data = ud;
    }
}
