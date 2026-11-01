/*
 * Copyright (C) 2024-2026 ArtInChip Technology Co., Ltd.
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef DASHCAM_ENGINE_H
#define DASHCAM_ENGINE_H
#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include "lvgl/lvgl.h"
#include "lv_aic_recorder.h"

struct dashcam_engine;

typedef enum {
    DASHCAM_STATE_IDLE = 0,
    DASHCAM_STATE_PREVIEW,
    DASHCAM_STATE_RECORDING,
    DASHCAM_STATE_STOPPED,
    DASHCAM_STATE_ERROR,
} dashcam_state_t;

typedef void (*dashcam_event_cb)(int event, void *data, void *user_data);

struct dashcam_config {
    char storage_path[256];
    int file_duration;
    int file_num;
    int video_width;
    int video_height;
    int video_bitrate;
    int video_framerate;
    int qfactor;
};

/* lifecycle */
struct dashcam_engine *dashcam_engine_create(void);
void dashcam_engine_destroy(struct dashcam_engine *e);
int dashcam_engine_init(struct dashcam_engine *e, struct dashcam_config *cfg);

/* recording */
int dashcam_engine_start_recording(struct dashcam_engine *e);
int dashcam_engine_stop_recording(struct dashcam_engine *e);

/* state */
dashcam_state_t dashcam_engine_get_state(struct dashcam_engine *e);
int dashcam_engine_get_record_time(struct dashcam_engine *e);
const char *dashcam_engine_get_current_file(struct dashcam_engine *e);

/* getter for UI to attach camera views etc. */
lv_obj_t *dashcam_engine_get_recorder(struct dashcam_engine *e);

/* file query — delegates to lv_aic_recorder */
int dashcam_engine_get_record_count(struct dashcam_engine *e);
int dashcam_engine_get_record_list(struct dashcam_engine *e, struct aic_recorder_record *list,
                                   int max);
int dashcam_engine_get_record_by_date(struct dashcam_engine *e, int y, int m, int d,
                                      struct aic_recorder_record *list, int max);
int dashcam_engine_get_locked_list(struct dashcam_engine *e, struct aic_recorder_record *list,
                                   int max);
int dashcam_engine_get_picture_by_date(struct dashcam_engine *e, int y, int m, int d,
                                       struct aic_recorder_picture *list, int max);
int64_t dashcam_engine_get_card_used(struct dashcam_engine *e);

/* event callback */
void dashcam_engine_set_callback(struct dashcam_engine *e, dashcam_event_cb cb, void *ud);

#ifdef __cplusplus
}
#endif
#endif
