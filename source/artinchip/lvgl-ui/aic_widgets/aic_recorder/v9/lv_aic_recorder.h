/*
 * Copyright (c) 2024-2026, ArtInChip Technology Co., Ltd
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef LV_AIC_RECORDER_H
#define LV_AIC_RECORDER_H

#ifdef __cplusplus
extern "C" {
#endif

#include "lvgl.h"
#if LVGL_VERSION_MAJOR == 9
#include "aic_recorder.h"
#include "mpp_dec_type.h"

struct aic_recorder_ctx_s;

typedef struct {
    lv_obj_t obj;
    uint32_t draw_layer;
    struct aic_recorder_ctx_s *aic_ctx;
} lv_aic_recorder_t;

extern const lv_obj_class_t lv_aic_recorder_class;

typedef enum {
    LV_AIC_RECORDER_LAYER_DEFAULT,
    LV_AIC_RECORDER_LAYER_VIDEO,
    LV_AIC_RECORDER_LAYER_UI_SINGLE_BUF,
    LV_AIC_RECORDER_LAYER_UI_DOUBLE_BUF,
} lv_aic_recorder_draw_layer_t;

typedef enum {
    LV_AIC_RECORDER_CAM_FRONT = 0,
    LV_AIC_RECORDER_CAM_REAR = 1,
    LV_AIC_RECORDER_CAM_MAX = 2,
} lv_aic_recorder_cam_id_t;

typedef enum {
    LV_AIC_RECORDER_PIP_NONE,
    LV_AIC_RECORDER_PIP_TOP_LEFT,
    LV_AIC_RECORDER_PIP_TOP_RIGHT,
    LV_AIC_RECORDER_PIP_BOTTOM_LEFT,
    LV_AIC_RECORDER_PIP_BOTTOM_RIGHT,
} lv_aic_recorder_pip_pos_t;

/* --- Commands matching aic_recorder_command --- */

typedef enum {
    LV_AIC_RECORDER_CMD_START_RECORD,
    LV_AIC_RECORDER_CMD_STOP_RECORD,

    LV_AIC_RECORDER_CMD_GET_STATE,
    LV_AIC_RECORDER_CMD_GET_RECORD_TIME,
    LV_AIC_RECORDER_CMD_GET_CURRENT_FILE,

    LV_AIC_RECORDER_CMD_SNAPSHOT,
    LV_AIC_RECORDER_CMD_SET_OSD_TEXT,
    LV_AIC_RECORDER_CMD_SET_DEBUG_INFO,
    LV_AIC_RECORDER_CMD_SET_LOCK,
    LV_AIC_RECORDER_CMD_SET_USER_DATA,
    LV_AIC_RECORDER_CMD_SET_DUARTION,
    LV_AIC_RECORDER_CMD_SET_AUDIO_ENABLE,
    LV_AIC_RECORDER_CMD_SET_OUTPUT_PATH,

    LV_AIC_RECORDER_CMD_GET_RECORD_COUNT,
    LV_AIC_RECORDER_CMD_GET_RECORD_LIST,
    LV_AIC_RECORDER_CMD_GET_RECORD_COUNT_BY_DATE,
    LV_AIC_RECORDER_CMD_GET_RECORD_BY_DATE,
    LV_AIC_RECORDER_CMD_GET_LOCKED_COUNT,
    LV_AIC_RECORDER_CMD_GET_LOCKED_LIST,

    LV_AIC_RECORDER_CMD_GET_PICTURE_COUNT,
    LV_AIC_RECORDER_CMD_GET_PICTURE_LIST,
    LV_AIC_RECORDER_CMD_GET_PICTURE_COUNT_BY_DATE,
    LV_AIC_RECORDER_CMD_GET_PICTURE_BY_DATE,
} lv_aic_recorder_cmd_t;

/* --- Query struct matching aic_recorder_query --- */

typedef struct {
    int max;
    int count;
    const char *date; /* "YYYY-MM-DD" for date queries */
    union {
        struct aic_recorder_record *records;
        struct aic_recorder_picture *pictures;
        void *data;
    };
} lv_aic_recorder_query_t;

/* --- Config (simplified, mirrors aic_recorder_config) --- */

typedef struct {
    char storage_path[256];
    int file_duration; /* seconds per file */
    int file_num;      /* 0=loop, >0=stop after N files */
    int video_width;
    int video_height;
    int video_bitrate;
    int video_framerate;
    int qfactor;    /* 1-100 */
    int codec_type; /* MPP_CODEC_VIDEO_ENCODER_MJPEG etc. */
} lv_aic_recorder_config_t;

/* --- State --- */

typedef enum {
    LV_AIC_RECORDER_STATE_IDLE = 0,
    LV_AIC_RECORDER_STATE_PREVIEW,
    LV_AIC_RECORDER_STATE_RECORDING,
    LV_AIC_RECORDER_STATE_STOPPED,
    LV_AIC_RECORDER_STATE_ERROR,
} lv_aic_recorder_state_t;

/* --- API --- */

lv_obj_t *lv_aic_recorder_create(lv_obj_t *parent);
lv_res_t lv_aic_recorder_init(lv_obj_t *obj, lv_aic_recorder_config_t *cfg);
void lv_aic_recorder_set_draw_layer(lv_obj_t *obj, lv_aic_recorder_draw_layer_t layer);
void lv_aic_recorder_set_cmd(lv_obj_t *obj, lv_aic_recorder_cmd_t cmd, void *data);
void lv_aic_recorder_set_event_callback(lv_obj_t *obj, s32 (*cb)(void *, s32, s32, s32),
                                        void *user_data);
void lv_aic_recorder_set_buf_callback(lv_obj_t *obj, s32 (*cb)(void *, s32, void *),
                                      void *user_data);
s32 lv_aic_recorder_send_frame(lv_obj_t *obj, struct aic_recorder_frame *frame);

lv_aic_recorder_state_t lv_aic_recorder_get_state(lv_obj_t *obj);
int lv_aic_recorder_get_record_time(lv_obj_t *obj);
const char *lv_aic_recorder_get_current_file(lv_obj_t *obj);

/* Camera view helpers */
lv_obj_t *lv_aic_recorder_add_camera(lv_obj_t *obj, lv_aic_recorder_cam_id_t cam_id, int w, int h);
void lv_aic_recorder_set_pip_position(lv_obj_t *obj, lv_aic_recorder_pip_pos_t pos);
void lv_aic_recorder_swap_cameras(lv_obj_t *obj);
s32 lv_aic_recorder_send_camera_frame(lv_obj_t *obj, lv_aic_recorder_cam_id_t cam_id,
                                      struct aic_recorder_frame *frame);

#endif
#ifdef __cplusplus
}
#endif
#endif
