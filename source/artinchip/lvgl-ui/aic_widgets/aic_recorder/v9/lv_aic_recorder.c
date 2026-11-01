/*
 * Copyright (c) 2024-2026, ArtInChip Technology Co., Ltd
 * SPDX-License-Identifier: Apache-2.0
 */

#include "lv_aic_recorder.h"
#if LV_USE_AIC_SIMULATOR == 0 && LVGL_VERSION_MAJOR == 9

#include <unistd.h>
#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/statfs.h>
#include <pthread.h>
#include "aic_recorder.h"
#include "aic_storage.h"

#define MY_CLASS       &lv_aic_recorder_class
#define FILE_PATH_LEN  256

typedef enum {
    STATUS_INIT,
    STATUS_CONFIGURED,
    STATUS_RECORDING,
    STATUS_STOPPED,
    STATUS_ERROR,
} rec_status_t;

struct aic_recorder_ctx_s {
    struct aic_recorder *recorder;
    lv_aic_recorder_config_t config;
    rec_status_t status;
    char current_file[FILE_PATH_LEN];
    time_t record_start_time;
    int card_mounted;
    int start_record_pending;
    int thread_running;
    pthread_t thread_id;

    s32 (*event_cb)(void *, s32, s32, s32);
    void *event_user_data;
    s32 (*buf_cb)(void *, s32, void *);
    void *buf_user_data;

    pthread_mutex_t lock;

    /* camera views */
    lv_obj_t *cam_views[LV_AIC_RECORDER_CAM_MAX];
    lv_aic_recorder_pip_pos_t pip_pos;
    int cam_active[LV_AIC_RECORDER_CAM_MAX];
    int cam_swapped;
};

static s32 recorder_event_cb(void *app_data, s32 event, s32 d1, s32 d2);
static s32 recorder_buf_cb(void *app_data, s32 event, void *buffer);
static void constructor(const lv_obj_class_t *c, lv_obj_t *obj);
static void destructor(const lv_obj_class_t *c, lv_obj_t *obj);
static void cmd_lifecycle(struct aic_recorder_ctx_s *ctx, lv_aic_recorder_cmd_t cmd);

const lv_obj_class_t lv_aic_recorder_class = {
    .constructor_cb = constructor,
    .destructor_cb = destructor,
    .width_def = LV_SIZE_CONTENT,
    .height_def = LV_SIZE_CONTENT,
    .instance_size = sizeof(lv_aic_recorder_t),
    .base_class = &lv_obj_class,
    .name = "lv_aic_recorder",
};

/**********************
 *  PUBLIC API
 **********************/

lv_obj_t *lv_aic_recorder_create(lv_obj_t *parent)
{
    lv_obj_t *o = lv_obj_class_create_obj(MY_CLASS, parent);
    lv_obj_class_init_obj(o);
    return o;
}


static void *recorder_init_thread(void *arg)
{
    struct aic_recorder_ctx_s *ctx = (struct aic_recorder_ctx_s *)arg;
    struct aic_recorder_config rec_cfg;
    char dir[FILE_PATH_LEN] = {0};
    struct statfs st;

    ctx->thread_running = 1;

    /* wait for SD card to mount */
    while (ctx->thread_running && statfs(ctx->config.storage_path, &st) != 0) {
        LV_LOG_INFO("recorder: waiting for SD card at %s", ctx->config.storage_path);
        sleep(1);
    }
    if (!ctx->thread_running)
        return NULL;

    /* create output directory */
    strncpy(dir, ctx->config.storage_path, FILE_PATH_LEN - 1);
    dir[FILE_PATH_LEN - 1] = '\0';
    mkdir(dir, 0755);
    ctx->card_mounted = 1;
    LV_LOG_INFO("recorder: SD card mounted, start_record:%d", ctx->start_record_pending);

    /* create recorder */
    ctx->recorder = aic_recorder_create();
    if (!ctx->recorder) {
        LV_LOG_ERROR("aic_recorder_create failed");
        return NULL;
    }

    aic_recorder_set_output_path(ctx->recorder, ctx->config.storage_path);
    aic_recorder_set_event_callback(ctx->recorder, ctx, recorder_event_cb);
    aic_recorder_set_buf_callback(ctx->recorder, recorder_buf_cb);

    /* build native config */
    memset(&rec_cfg, 0, sizeof(rec_cfg));
    rec_cfg.file_duration = ctx->config.file_duration ? ctx->config.file_duration : 60;
    rec_cfg.file_num = ctx->config.file_num;
    rec_cfg.file_muxer_type = 0;
    rec_cfg.qfactor = ctx->config.qfactor ? ctx->config.qfactor : 80;
    rec_cfg.has_video = 1;
    rec_cfg.has_audio = 0;

    rec_cfg.video_config.codec_type =
        ctx->config.codec_type ? ctx->config.codec_type : MPP_CODEC_VIDEO_ENCODER_MJPEG;
    rec_cfg.video_config.in_width  = rec_cfg.video_config.out_width  = ctx->config.video_width;
    rec_cfg.video_config.in_height = rec_cfg.video_config.out_height = ctx->config.video_height;
    rec_cfg.video_config.out_bit_rate = ctx->config.video_bitrate;
    rec_cfg.video_config.out_frame_rate = ctx->config.video_framerate;
    rec_cfg.video_config.in_pix_fomat = MPP_FMT_NV12;

    LV_LOG_INFO("Recorder: width %d, height %d, bitrate %d.",
                ctx->config.video_width, ctx->config.video_height,
                ctx->config.video_bitrate);

    if (aic_recorder_init(ctx->recorder, &rec_cfg) != 0) {
        LV_LOG_ERROR("aic_recorder_init failed");
        aic_recorder_destroy(ctx->recorder);
        ctx->recorder = NULL;
        ctx->status = STATUS_ERROR;
        return NULL;
    }
    ctx->status = STATUS_CONFIGURED;
    LV_LOG_INFO("recorder: init done");

    /* replay saved lifecycle command */
    if (ctx->start_record_pending) {
        LV_LOG_INFO("recorder: start_rec pending %d", ctx->start_record_pending);
        pthread_mutex_lock(&ctx->lock);
        ctx->start_record_pending = 0;
        pthread_mutex_unlock(&ctx->lock);
        cmd_lifecycle(ctx, LV_AIC_RECORDER_CMD_START_RECORD);
    }
    return NULL;
}

lv_res_t lv_aic_recorder_init(lv_obj_t *obj, lv_aic_recorder_config_t *cfg)
{
    lv_aic_recorder_t *rec = (lv_aic_recorder_t *)obj;
    struct aic_recorder_ctx_s *ctx;
    pthread_attr_t attr;

    if (!cfg || cfg->video_width <= 0)
        return LV_RES_INV;

    ctx = malloc(sizeof(struct aic_recorder_ctx_s));
    if (!ctx)
        return LV_RES_INV;
    memset(ctx, 0, sizeof(struct aic_recorder_ctx_s));
    memcpy(&ctx->config, cfg, sizeof(lv_aic_recorder_config_t));
    ctx->status = STATUS_INIT;
    pthread_mutex_init(&ctx->lock, NULL);
    rec->aic_ctx = ctx;

    pthread_attr_init(&attr);
    if (pthread_create(&ctx->thread_id, &attr, recorder_init_thread, ctx) != 0) {
        pthread_mutex_destroy(&rec->aic_ctx->lock);
        free(ctx);
        rec->aic_ctx = NULL;
        return LV_RES_INV;
    }
    pthread_attr_destroy(&attr);
    return LV_RES_OK;
}

void lv_aic_recorder_set_draw_layer(lv_obj_t *obj, lv_aic_recorder_draw_layer_t layer)
{
    ((lv_aic_recorder_t *)obj)->draw_layer = layer;
}

/* ---- set_cmd handlers (split by category to keep CCN low) ---- */

static void cmd_lifecycle(struct aic_recorder_ctx_s *ctx, lv_aic_recorder_cmd_t cmd)
{
    pthread_mutex_lock(&ctx->lock);
    switch (cmd) {
    case LV_AIC_RECORDER_CMD_START_RECORD:
        if (ctx->status == STATUS_CONFIGURED || ctx->status == STATUS_STOPPED) {
            aic_recorder_start(ctx->recorder);
            ctx->status = STATUS_RECORDING;
            ctx->record_start_time = time(NULL);
        } else if (ctx->status == STATUS_INIT) {
            ctx->start_record_pending = 1;
        }
        break;
    case LV_AIC_RECORDER_CMD_STOP_RECORD:
        if (ctx->status == STATUS_RECORDING) {
            aic_recorder_stop(ctx->recorder);
            ctx->status = STATUS_STOPPED;
        }
        break;
    default:
        break;
    }
    pthread_mutex_unlock(&ctx->lock);
}

static void cmd_passthrough(struct aic_recorder_ctx_s *ctx, lv_aic_recorder_cmd_t cmd,
                            void *data)
{
    switch (cmd) {
    case LV_AIC_RECORDER_CMD_SNAPSHOT:
        aic_recorder_snapshot(ctx->recorder);
        break;
    case LV_AIC_RECORDER_CMD_SET_OSD_TEXT:
        aic_recorder_control(ctx->recorder, AIC_RECORDER_CMD_SET_OSD_TEXT, data);
        break;
    case LV_AIC_RECORDER_CMD_SET_DEBUG_INFO:
        aic_recorder_control(ctx->recorder, AIC_RECORDER_CMD_SET_DEBUG_INFO, data);
        break;
    case LV_AIC_RECORDER_CMD_SET_LOCK:
        aic_recorder_control(ctx->recorder, AIC_RECORDER_CMD_SET_LOCK_RECORD, data);
        break;
    case LV_AIC_RECORDER_CMD_SET_USER_DATA:
        aic_recorder_control(ctx->recorder, AIC_RECORDER_CMD_SET_USER_RECORD_DATA, data);
        break;
    case LV_AIC_RECORDER_CMD_SET_OUTPUT_PATH:
        if (data) {
            strncpy(ctx->current_file, (const char *)data, FILE_PATH_LEN - 1);
            aic_recorder_set_output_path(ctx->recorder, (char *)data);
        }
        break;
    case LV_AIC_RECORDER_CMD_SET_DUARTION:
        aic_recorder_control(ctx->recorder, AIC_RECORDER_CMD_SET_RECORD_DURATION, data);
        break;
    default:
        break;
    }
}

static void cmd_get_count(struct aic_recorder_ctx_s *ctx, lv_aic_recorder_cmd_t cmd,
                          void *data)
{
    if (!data)
        return;
    enum aic_recorder_command c = (enum aic_recorder_command)(
        AIC_RECORDER_CMD_GET_RECORD_COUNT + (cmd - LV_AIC_RECORDER_CMD_GET_RECORD_COUNT));
    aic_recorder_control(ctx->recorder, c, (int *)data);
}

static void cmd_count_by_date(struct aic_recorder_ctx_s *ctx, lv_aic_recorder_cmd_t cmd,
                              void *data)
{
    if (!data)
        return;
    lv_aic_recorder_query_t *q = (lv_aic_recorder_query_t *)data;
    struct aic_recorder_query nq;
    enum aic_recorder_command c = (enum aic_recorder_command)(
        AIC_RECORDER_CMD_GET_RECORD_COUNT + (cmd - LV_AIC_RECORDER_CMD_GET_RECORD_COUNT));
    nq.max = q->max;
    nq.date = q->date;
    nq.count = 0;
    aic_recorder_control(ctx->recorder, c, &nq);
    q->count = nq.count;
}

static void cmd_get_list(struct aic_recorder_ctx_s *ctx, lv_aic_recorder_cmd_t cmd,
                         void *data)
{
    if (!data)
        return;
    lv_aic_recorder_query_t *q = (lv_aic_recorder_query_t *)data;
    struct aic_recorder_query nq;
    enum aic_recorder_command c = (enum aic_recorder_command)(
        AIC_RECORDER_CMD_GET_RECORD_LIST + (cmd - LV_AIC_RECORDER_CMD_GET_RECORD_LIST));
    nq.max = q->max;
    nq.data = q->data;
    nq.date = q->date;
    nq.count = q->count;
    aic_recorder_control(ctx->recorder, c, &nq);
}

static void cmd_data_get(struct aic_recorder_ctx_s *ctx, lv_aic_recorder_cmd_t cmd,
                         void *data)
{
    switch (cmd) {
    case LV_AIC_RECORDER_CMD_GET_STATE:
        if (data) {
            s32 v;
            switch (ctx->status) {
            case STATUS_CONFIGURED:  v = 0; break;
            case STATUS_RECORDING:   v = 1; break;
            case STATUS_STOPPED:     v = 2; break;
            default:                 v = 3; break;
            }
            *(s32 *)data = v;
        }
        break;
    case LV_AIC_RECORDER_CMD_GET_RECORD_TIME:
        if (data)
            *(s32 *)data = (ctx->status == STATUS_RECORDING)
                ? (s32)(time(NULL) - ctx->record_start_time) : 0;
        break;
    case LV_AIC_RECORDER_CMD_GET_CURRENT_FILE:
        if (data)
            *(const char **)data = ctx->current_file;
        break;
    default:
        break;
    }
}

/* ---- entry point: lightweight dispatcher ---- */

void lv_aic_recorder_set_cmd(lv_obj_t *obj, lv_aic_recorder_cmd_t cmd, void *data)
{
    lv_aic_recorder_t *rec = (lv_aic_recorder_t *)obj;
    struct aic_recorder_ctx_s *ctx = rec->aic_ctx;

    if (!ctx || !ctx->recorder) {
        /* save START_RECORD for replay once init thread completes */
        if (ctx && cmd == LV_AIC_RECORDER_CMD_START_RECORD) {
            pthread_mutex_lock(&ctx->lock);
            ctx->start_record_pending = 1;
            pthread_mutex_unlock(&ctx->lock);
            LV_LOG_INFO("recorder: start_rec %d for replay", cmd);
        }
        return;
    }

    if (cmd == LV_AIC_RECORDER_CMD_START_RECORD ||
        cmd == LV_AIC_RECORDER_CMD_STOP_RECORD) {
        cmd_lifecycle(ctx, cmd);
    } else if (cmd >= LV_AIC_RECORDER_CMD_SNAPSHOT &&
               cmd <= LV_AIC_RECORDER_CMD_SET_OUTPUT_PATH) {
        cmd_passthrough(ctx, cmd, data);
    } else if (cmd >= LV_AIC_RECORDER_CMD_GET_STATE &&
               cmd <= LV_AIC_RECORDER_CMD_GET_CURRENT_FILE) {
        cmd_data_get(ctx, cmd, data);
    } else if (cmd == LV_AIC_RECORDER_CMD_GET_RECORD_COUNT ||
               cmd == LV_AIC_RECORDER_CMD_GET_LOCKED_COUNT ||
               cmd == LV_AIC_RECORDER_CMD_GET_PICTURE_COUNT) {
        cmd_get_count(ctx, cmd, data);
    } else if (cmd == LV_AIC_RECORDER_CMD_GET_RECORD_COUNT_BY_DATE ||
               cmd == LV_AIC_RECORDER_CMD_GET_PICTURE_COUNT_BY_DATE) {
        cmd_count_by_date(ctx, cmd, data);
    } else if (cmd == LV_AIC_RECORDER_CMD_GET_RECORD_LIST ||
               cmd == LV_AIC_RECORDER_CMD_GET_RECORD_BY_DATE ||
               cmd == LV_AIC_RECORDER_CMD_GET_LOCKED_LIST ||
               cmd == LV_AIC_RECORDER_CMD_GET_PICTURE_LIST ||
               cmd == LV_AIC_RECORDER_CMD_GET_PICTURE_BY_DATE) {
        cmd_get_list(ctx, cmd, data);
    } else { 
        LV_LOG_ERROR("unknown recorde widget cmd %d.", cmd);
    }
}

void lv_aic_recorder_set_event_callback(lv_obj_t *obj, s32 (*cb)(void *, s32, s32, s32),
                                        void *user_data)
{
    lv_aic_recorder_t *rec = (lv_aic_recorder_t *)obj;
    if (rec->aic_ctx) {
        rec->aic_ctx->event_cb = cb;
        rec->aic_ctx->event_user_data = user_data;
    }
}

/* set buffer release callback — called when recorder releases a frame,
 * app should call lv_aic_vin_q_buf or equivalent to return buffer to DVP */
void lv_aic_recorder_set_buf_callback(lv_obj_t *obj, s32 (*cb)(void *, s32, void *),
                                      void *user_data)
{
    lv_aic_recorder_t *rec = (lv_aic_recorder_t *)obj;
    if (rec->aic_ctx) {
        rec->aic_ctx->buf_cb = cb;
        rec->aic_ctx->buf_user_data = user_data;
    }
}

s32 lv_aic_recorder_send_frame(lv_obj_t *obj, struct aic_recorder_frame *frame)
{
    lv_aic_recorder_t *rec = (lv_aic_recorder_t *)obj;
    struct aic_recorder_ctx_s *ctx = rec->aic_ctx;

    if (!ctx || !frame || ctx->status != STATUS_RECORDING)
        return -1;
    // if (!ctx->card_mounted)
    //     return -1;
    return aic_recorder_send_frame(ctx->recorder, frame);
}

lv_aic_recorder_state_t lv_aic_recorder_get_state(lv_obj_t *obj)
{
    s32 s = 4;
    lv_aic_recorder_set_cmd(obj, LV_AIC_RECORDER_CMD_GET_STATE, &s);
    return (lv_aic_recorder_state_t)s;
}

int lv_aic_recorder_get_record_time(lv_obj_t *obj)
{
    lv_aic_recorder_t *rec = (lv_aic_recorder_t *)obj;
    if (!rec->aic_ctx || rec->aic_ctx->status != STATUS_RECORDING)
        return 0;
    return (int)(time(NULL) - rec->aic_ctx->record_start_time);
}

const char *lv_aic_recorder_get_current_file(lv_obj_t *obj)
{
    lv_aic_recorder_t *rec = (lv_aic_recorder_t *)obj;
    return rec->aic_ctx ? rec->aic_ctx->current_file : NULL;
}

/**********************
 *  CAMERA HELPERS
 **********************/

lv_obj_t *lv_aic_recorder_add_camera(lv_obj_t *obj, lv_aic_recorder_cam_id_t cam_id, int w, int h)
{
    lv_aic_recorder_t *rec = (lv_aic_recorder_t *)obj;
    struct aic_recorder_ctx_s *ctx = rec->aic_ctx;

    if (!ctx || cam_id >= LV_AIC_RECORDER_CAM_MAX)
        return NULL;

    lv_obj_t *view = lv_obj_create(obj);
    lv_obj_set_size(view, w, h);
    lv_obj_set_style_bg_color(view, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(view, LV_OPA_COVER, 0);
    lv_obj_set_style_border_opa(view, LV_OPA_TRANSP, 0);
    lv_obj_set_style_radius(view, 6, 0);
    lv_obj_set_style_clip_corner(view, true, 0);

    lv_obj_t *label = lv_label_create(view);
    lv_obj_set_style_text_color(label, lv_color_hex(0x444466), 0);
    lv_label_set_text(label, cam_id == LV_AIC_RECORDER_CAM_FRONT ? "Front" : "Rear");
    lv_obj_center(label);

    ctx->cam_views[cam_id] = view;
    ctx->cam_active[cam_id] = 1;
    return view;
}

void lv_aic_recorder_set_pip_position(lv_obj_t *obj, lv_aic_recorder_pip_pos_t pos)
{
    lv_aic_recorder_t *rec = (lv_aic_recorder_t *)obj;
    struct aic_recorder_ctx_s *ctx = rec->aic_ctx;

    if (!ctx)
        return;

    ctx->pip_pos = pos;
    lv_obj_t *pip = ctx->cam_views[LV_AIC_RECORDER_CAM_REAR];
    if (!pip)
        return;

    int pw = lv_obj_get_width(pip);
    int ph = lv_obj_get_height(pip);
    int m = 8;
    int W = lv_obj_get_width(obj);
    int H = lv_obj_get_height(obj);

    switch (pos) {
    case LV_AIC_RECORDER_PIP_TOP_LEFT:
        lv_obj_set_pos(pip, m, m);
        break;
    case LV_AIC_RECORDER_PIP_TOP_RIGHT:
        lv_obj_set_pos(pip, W - pw - m, m);
        break;
    case LV_AIC_RECORDER_PIP_BOTTOM_LEFT:
        lv_obj_set_pos(pip, m, H - ph - m);
        break;
    case LV_AIC_RECORDER_PIP_BOTTOM_RIGHT:
        lv_obj_set_pos(pip, W - pw - m, H - ph - m);
        break;
    default:
        break;
    }
}

void lv_aic_recorder_swap_cameras(lv_obj_t *obj)
{
    lv_aic_recorder_t *rec = (lv_aic_recorder_t *)obj;
    struct aic_recorder_ctx_s *ctx = rec->aic_ctx;

    if (!ctx)
        return;

    ctx->cam_swapped = !ctx->cam_swapped;

    lv_obj_t *f = ctx->cam_views[LV_AIC_RECORDER_CAM_FRONT];
    lv_obj_t *r = ctx->cam_views[LV_AIC_RECORDER_CAM_REAR];

    if (f && r) {
        int fw = lv_obj_get_width(f);
        int fh = lv_obj_get_height(f);
        int rw = lv_obj_get_width(r);
        int rh = lv_obj_get_height(r);
        lv_obj_set_size(f, rw, rh);
        lv_obj_set_size(r, fw, fh);
        lv_aic_recorder_set_pip_position(obj, ctx->pip_pos);
    }
}

s32 lv_aic_recorder_send_camera_frame(lv_obj_t *obj, lv_aic_recorder_cam_id_t cam_id,
                                      struct aic_recorder_frame *frame)
{
    lv_aic_recorder_t *rec = (lv_aic_recorder_t *)obj;
    struct aic_recorder_ctx_s *ctx = rec->aic_ctx;

    if (!ctx || !frame)
        return -1;

    /* visual feedback on camera view */
    lv_obj_t *view = ctx->cam_views[cam_id];
    if (view)
        lv_obj_set_style_bg_color(view, lv_color_hex(cam_id ? 0x1a001a : 0x001a00), 0);

    return lv_aic_recorder_send_frame(obj, frame);
}

/**********************
 *  INTERNAL CALLBACKS
 **********************/

static s32 recorder_event_cb(void *app_data, s32 event, s32 d1, s32 d2)
{
    struct aic_recorder_ctx_s *ctx = (struct aic_recorder_ctx_s *)app_data;

    switch (event) {
    case AIC_RECORDER_EVENT_NEED_NEXT_FILE:
        ctx->record_start_time = time(NULL);
        break;
    case AIC_RECORDER_EVENT_COMPLETE:
        ctx->status = STATUS_STOPPED;
        break;
    case AIC_RECORDER_EVENT_NO_SPACE:
        break;
    case AIC_RECORDER_EVENT_RELEASE_VIDEO_FRAME:
        break;
    default:
        break;
    }

    if (ctx->event_cb)
        ctx->event_cb(ctx->event_user_data, event, d1, d2);

    return 0;
}

static s32 recorder_buf_cb(void *app_data, s32 event, void *buffer)
{
    struct aic_recorder_ctx_s *ctx = (struct aic_recorder_ctx_s *)app_data;

    /* call user's buffer callback if set (e.g. QBUF back to DVP) */
    if (ctx->buf_cb)
        return ctx->buf_cb(ctx->buf_user_data, event, buffer);

    return 0;
}

/**********************
 *  CLASS CALLBACKS
 **********************/

static void constructor(const lv_obj_class_t *c, lv_obj_t *obj)
{
    LV_UNUSED(c);
    lv_aic_recorder_t *rec = (lv_aic_recorder_t *)obj;
    rec->aic_ctx = NULL;
    rec->draw_layer = LV_AIC_RECORDER_LAYER_DEFAULT;
    lv_obj_set_style_bg_color(obj, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    lv_obj_set_style_border_opa(obj, LV_OPA_TRANSP, 0);
}

static void destructor(const lv_obj_class_t *c, lv_obj_t *obj)
{
    LV_UNUSED(c);
    lv_aic_recorder_t *rec = (lv_aic_recorder_t *)obj;

    if (rec->aic_ctx) {
        rec->aic_ctx->thread_running = 0;
        pthread_join(rec->aic_ctx->thread_id, NULL);
        if (rec->aic_ctx->status == STATUS_RECORDING)
            aic_recorder_stop(rec->aic_ctx->recorder);
        if (rec->aic_ctx->recorder)
            aic_recorder_destroy(rec->aic_ctx->recorder);
        pthread_mutex_destroy(&rec->aic_ctx->lock);
        free(rec->aic_ctx);
        rec->aic_ctx = NULL;
    }
}
#endif
