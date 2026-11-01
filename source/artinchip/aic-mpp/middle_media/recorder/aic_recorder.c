/*
 * Copyright (C) 2020-2026 ArtInChip Technology Co. Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 *  author: <jun.ma@artinchip.com>
 *  Desc: aic_recoder
 */

#include <fcntl.h>
#include <malloc.h>
#include <pthread.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <sys/types.h>
#include <unistd.h>

#include "aic_muxer.h"
#include "aic_recorder.h"
#include "aic_storage.h"
#include "mm_component.h"
#include "mm_core.h"
#include "mpp_dec_type.h"
#include "mpp_log.h"
#include "mpp_mem.h"

#define AIC_RECORDER_STATE_IDLE 0
#define AIC_RECORDER_STATE_INITIALIZED 1
#define AIC_RECORDER_STATE_RECORDING 2
#define AIC_RECORDER_STATE_STOPPED 3

#define wait_state(h_component, des_state) {                                  \
		MM_STATE_TYPE state;                                           \
		while (1) {                                                    \
			mm_get_state(h_component, &state);                     \
			if (state == des_state) {                              \
				break;                                         \
			} else {                                               \
				usleep(1000);                                  \
			}                                                      \
		}                                                              \
	} /* Macro End */

struct aic_recorder {
	mm_handle muxer_handle;
	mm_handle venc_handle;
	mm_handle vin_handle;
	mm_handle aenc_handle;
	struct aic_recorder_config config;
	event_handler event_handle;
	giveback_buffer giveback_buffer;
	void *app_data;
	int state;
	struct aic_storage *storage;
	char output_path[256];
};

static s32 component_event_handler(mm_handle h_component, void *p_app_data,
				   MM_EVENT_TYPE event, u32 data1, u32 data2,
				   void *p_event_data)
{
	return -1;
}

s32 component_giveback_buffer(mm_handle h_component, void *p_app_data,
			      mm_buffer *p_buffer)
{
	s32 error = MM_ERROR_NONE;
	struct aic_recorder *recorder = (struct aic_recorder *)p_app_data;
	if (!recorder || !p_buffer)
		return -1;

	if (recorder->giveback_buffer &&
	    (h_component == recorder->vin_handle)) {
		if (p_buffer->type == MM_BUFFER_DATA_FRAME) {
			struct aic_recorder_frame frame = {0};
			memcpy(&frame.mpp_frame, &p_buffer->frame.mpp_frame, sizeof(struct mpp_frame));
			error = recorder->giveback_buffer(recorder->app_data,
				AIC_RECORDER_EVENT_RELEASE_VIDEO_FRAME, &frame);
		}
	} else {
		return -1;
	}
	return error;
}

static mm_callback component_event_callbacks = {
	.event_handler = component_event_handler,
	.giveback_buffer = component_giveback_buffer,
};

struct aic_recorder *aic_recorder_create(void)
{
	s32 error;
	struct aic_recorder *recorder = mpp_alloc(sizeof(struct aic_recorder));
	if (recorder == NULL) {
		loge("mpp_alloc aic_recorder error\n");
		return NULL;
	}
	memset(recorder, 0x00, sizeof(struct aic_recorder));
	error = mm_init();
	if (error != MM_ERROR_NONE) {
		loge("mm_init error!!!\n");
		mpp_free(recorder);
		return NULL;
	}
	recorder->state = AIC_RECORDER_STATE_IDLE;
	return recorder;
}

s32 aic_recorder_destroy(struct aic_recorder *recorder)
{
	mm_deinit();
	mpp_free(recorder);
	return 0;
}

s32 aic_recorder_set_event_callback(struct aic_recorder *recorder,
				    void *app_data, event_handler event_handle)
{
	if (!recorder || !app_data || !event_handle)
		return -1;

	recorder->event_handle = event_handle;
	recorder->app_data = app_data;
	return 0;
}

s32 aic_recorder_set_buf_callback(struct aic_recorder *recorder,
				  giveback_buffer giveback_buffer)
{
	if (!recorder || !giveback_buffer)
		return -1;

	recorder->giveback_buffer = giveback_buffer;
	return 0;
}

s32 aic_recorder_init_video(struct aic_recorder *recorder,
			    struct aic_recorder_config *recorder_config)
{
	struct video_encoding_config *video_config;
	mm_param_port_def port_define = { 0 };
	mm_image_param_qfactor qfactor;
	mm_video_port_def *video_def;
	int ret = 0;

	/* create vin component*/
	ret = mm_get_handle(&recorder->vin_handle, MM_COMPONENT_VIN_NAME,
			    recorder, &component_event_callbacks);
	if (MM_ERROR_NONE != ret) {
		loge("unable to get vin_handle handle ret %d.\n", ret);
		recorder->vin_handle = NULL;
		return -1;
	}

	/* create venc component*/
	ret = mm_get_handle(&recorder->venc_handle, MM_COMPONENT_VENC_NAME,
			    recorder, &component_event_callbacks);
	if (MM_ERROR_NONE != ret) {
		loge("unable to get venc_handle handle ret %d.\n", ret);
		recorder->venc_handle = NULL;
		goto _EXIT;
	}

	/* get and set muxer parameter */
	port_define.port_index = MUX_PORT_VIDEO_INDEX;
	ret = mm_get_parameter(recorder->muxer_handle,
			       MM_INDEX_PARAM_PORT_DEFINITION, &port_define);
	if (MM_ERROR_NONE != ret) {
		loge("mm_get_parameter muxer port def error %d.\n", ret);
		goto _EXIT;
	}

	video_config = &recorder->config.video_config;
	if (video_config->codec_type != MPP_CODEC_VIDEO_ENCODER_MJPEG &&
		video_config->codec_type != MPP_CODEC_VIDEO_ENCODER_H264) {
		loge("not support video codec_type %d.\n", video_config->codec_type);
		goto _EXIT;
	}
	video_def = &port_define.format.video;
	video_def->frame_width = video_config->out_width;
	video_def->frame_height = video_config->out_height;
	video_def->framerate = video_config->out_frame_rate;
	video_def->bitrate = video_config->out_bit_rate;
	video_def->codec_type = video_config->codec_type;

	ret = mm_set_parameter(recorder->muxer_handle,
			       MM_INDEX_PARAM_PORT_DEFINITION, &port_define);
	if (MM_ERROR_NONE != ret) {
		loge("mm_set_parameter set muxer port def error %d.\n", ret);
		goto _EXIT;
	}

	/* get and set venc parameter */
	ret = mm_get_parameter(recorder->venc_handle,
			       MM_INDEX_PARAM_PORT_DEFINITION, &port_define);
	if (MM_ERROR_NONE != ret) {
		loge("mm_get_parameter get venc port def error %d.\n", ret);
		goto _EXIT;
	}
	video_def = &port_define.format.video;
	video_def->frame_width = video_config->out_width;
	video_def->frame_height = video_config->out_height;
	video_def->framerate = video_config->out_frame_rate;
	video_def->bitrate = video_config->out_bit_rate;
	video_def->codec_type = video_config->codec_type;
	ret = mm_set_parameter(recorder->venc_handle,
			       MM_INDEX_PARAM_PORT_DEFINITION, &port_define);
	if (MM_ERROR_NONE != ret) {
		loge("mm_set_parameter set venc port def error %d.\n", ret);
		goto _EXIT;
	}

	if (video_config->codec_type == MPP_CODEC_VIDEO_ENCODER_MJPEG) {
		qfactor.q_factor = recorder->config.qfactor;
		ret = mm_set_parameter(recorder->venc_handle,
				       MM_INDEX_PARAM_QFACTOR, &qfactor);
		if (MM_ERROR_NONE != ret) {
			loge("mm_set_parameter set venc qfactor error %d.\n",
			     ret);
			goto _EXIT;
		}
	}

	/* get and set vin parameter */
	port_define.port_index = VIN_PORT_OUT_INDEX;
	ret = mm_get_parameter(recorder->vin_handle,
			       MM_INDEX_PARAM_PORT_DEFINITION, &port_define);
	if (MM_ERROR_NONE != ret) {
		loge("mm_get_parameter get venc port def error %d.\n", ret);
		goto _EXIT;
	}
	if (video_config->in_pix_fomat != MPP_FMT_NV12) {
		loge("not support video pixel format %d.\n",
		     video_config->in_pix_fomat);
		goto _EXIT;
	}
	video_def = &port_define.format.video;
	video_def->frame_width = video_config->out_width;
	video_def->frame_height = video_config->out_height;
	video_def->framerate = video_config->out_frame_rate;
	video_def->pixel_format = video_config->in_pix_fomat;
	ret = mm_set_parameter(recorder->vin_handle,
			       MM_INDEX_PARAM_PORT_DEFINITION, &port_define);
	if (MM_ERROR_NONE != ret) {
		loge("mm_set_parameter set vin port def error %d.\n", ret);
		goto _EXIT;
	}

	/* bind venc to muxer */
	ret = mm_set_bind(recorder->venc_handle, VENC_PORT_OUT_INDEX,
			  recorder->muxer_handle, MUX_PORT_VIDEO_INDEX);
	if (MM_ERROR_NONE != ret) {
		loge("mm_set_bind venc and muxer error %d.\n", ret);
		goto _EXIT;
	}

	/* bind vin to venc */
	ret = mm_set_bind(recorder->vin_handle, VIN_PORT_OUT_INDEX,
			  recorder->venc_handle, VENC_PORT_IN_INDEX);
	if (MM_ERROR_NONE != ret) {
		loge("mm_set_bind vin and venc error %d.\n", ret);
		goto _EXIT;
	}

	return 0;

_EXIT:
	if (recorder->venc_handle) {
		mm_free_handle(recorder->venc_handle);
		recorder->venc_handle = NULL;
	}
	if (recorder->vin_handle) {
		mm_free_handle(recorder->vin_handle);
		recorder->vin_handle = NULL;
	}
	return -1;
}

static s32 aic_recorder_init_muxer(struct aic_recorder *recorder)
{
	mm_param_record_file_info rec_file_info = {0};

	/* create muxer component */
	if (MM_ERROR_NONE != mm_get_handle(&recorder->muxer_handle,
					   MM_COMPONENT_MUXER_NAME, recorder,
					   &component_event_callbacks)) {
		loge("unable to get muxer_handle handle.\n");
		return -1;
	}

	/* set muxer file info */
	rec_file_info.duration = recorder->config.file_duration;
	rec_file_info.file_num = recorder->config.file_num;
	rec_file_info.muxer_type = recorder->config.file_muxer_type;
	if (MM_ERROR_NONE !=
	    mm_set_parameter(recorder->muxer_handle,
			     MM_INDEX_VENDOR_MUXER_RECORD_FILE_INFO,
			     &rec_file_info)) {
		loge("mm_set_parameter file info error.\n");
		goto err_muxer;
	}

	/* set output path (URI + storage) */
	if (recorder->output_path[0] != '\0') {
		mm_param_content_uri *uri;
		int bytes = strlen(recorder->output_path) + 1;

		uri = (mm_param_content_uri *)mpp_alloc(
			sizeof(mm_param_content_uri) + bytes);
		if (uri) {
			memset(uri, 0, sizeof(mm_param_content_uri) + bytes);
			uri->size = sizeof(mm_param_content_uri) + bytes;
			strncpy((char *)uri->content_uri,
				recorder->output_path, bytes);
			mm_set_parameter(recorder->muxer_handle,
					 MM_INDEX_PARAM_CONTENT_URI, uri);
			mpp_free(uri);
		} else {
			goto err_muxer;
		}

		recorder->storage = aic_storage_create(recorder->output_path);
		if (recorder->storage) {
			mm_param_storage_handle sh = {
				.size = sizeof(sh),
				.handle = recorder->storage,
			};
			mm_set_parameter(recorder->muxer_handle,
					 MM_INDEX_VENDOR_STORAGE_HANDLE, &sh);
		} else {
			goto err_muxer;
		}
	}

	return 0;

err_muxer:
	if (recorder->storage) {
		aic_storage_destroy(recorder->storage);
		recorder->storage = NULL;
	}
	if (recorder->muxer_handle) {
		mm_free_handle(recorder->muxer_handle);
		recorder->muxer_handle = NULL;
	}
	return -1;
}

s32 aic_recorder_init(struct aic_recorder *recorder,
		      struct aic_recorder_config *recorder_config)
{
	int ret = 0;

	if (!recorder || !recorder_config) {
		return -1;
	}
	recorder->config = *recorder_config;
	if (!recorder->config.has_video && !recorder->config.has_audio) {
		loge("para error\n");
		return -1;
	}

	/* init muxer component and set parameter*/
	if (aic_recorder_init_muxer(recorder) != 0) {
		loge("init muxer failed.\n");
		return -1;
	}

	/* init video component and set parameter*/
	if (recorder->config.has_video) {
		if (0 != aic_recorder_init_video(recorder, recorder_config)) {
			ret = -1;
			goto _EXIT;
		}
	}

	/* init audio component and set parameter*/
	if (recorder->config.has_audio) {
	}

	/* set all component to idle state*/
	if (recorder->muxer_handle) {
		mm_send_command(recorder->muxer_handle, MM_COMMAND_STATE_SET,
				MM_STATE_IDLE, NULL);
	}
	if (recorder->venc_handle) {
		mm_send_command(recorder->venc_handle, MM_COMMAND_STATE_SET,
				MM_STATE_IDLE, NULL);
	}
	if (recorder->vin_handle) {
		mm_send_command(recorder->vin_handle, MM_COMMAND_STATE_SET,
				MM_STATE_IDLE, NULL);
	}

	recorder->state = AIC_RECORDER_STATE_INITIALIZED;

	return ret;
_EXIT:
	if (recorder->storage) {
		aic_storage_destroy(recorder->storage);
		recorder->storage = NULL;
	}
	if (recorder->muxer_handle) {
		mm_free_handle(recorder->muxer_handle);
		recorder->muxer_handle = NULL;
	}

	return ret;
}

s32 aic_recorder_start(struct aic_recorder *recorder)
{
	if (recorder->config.has_video && recorder->vin_handle) {
		mm_send_command(recorder->vin_handle, MM_COMMAND_STATE_SET,
				MM_STATE_EXECUTING, NULL);
	}
	if (recorder->config.has_video && recorder->venc_handle) {
		mm_send_command(recorder->venc_handle, MM_COMMAND_STATE_SET,
				MM_STATE_EXECUTING, NULL);
	}
	if (recorder->muxer_handle) {
		mm_send_command(recorder->muxer_handle, MM_COMMAND_STATE_SET,
				MM_STATE_EXECUTING, NULL);
	}
	recorder->state = AIC_RECORDER_STATE_RECORDING;
	return 0;
}

s32 aic_recorder_stop(struct aic_recorder *recorder)
{
	if (recorder->state == AIC_RECORDER_STATE_IDLE) {
		printf("%s:%d\n", __FUNCTION__, __LINE__);
		goto _FREE_HANDLE_;
	}

	if (recorder->muxer_handle) {
		mm_send_command(recorder->muxer_handle, MM_COMMAND_STATE_SET,
				MM_STATE_IDLE, NULL);
		wait_state(recorder->muxer_handle, MM_STATE_IDLE);
		mm_send_command(recorder->muxer_handle, MM_COMMAND_STATE_SET,
				MM_STATE_LOADED, NULL);
		wait_state(recorder->muxer_handle, MM_STATE_LOADED);
	}

	if (recorder->config.has_video) {
		if (recorder->venc_handle) {
			mm_send_command(recorder->venc_handle,
					MM_COMMAND_STATE_SET, MM_STATE_IDLE,
					NULL);
			wait_state(recorder->venc_handle, MM_STATE_IDLE);
			mm_send_command(recorder->venc_handle,
					MM_COMMAND_STATE_SET, MM_STATE_LOADED,
					NULL);
			wait_state(recorder->venc_handle, MM_STATE_LOADED);
		}
		if (recorder->vin_handle) {
			mm_send_command(recorder->vin_handle,
					MM_COMMAND_STATE_SET, MM_STATE_IDLE,
					NULL);
			wait_state(recorder->vin_handle, MM_STATE_IDLE);
			mm_send_command(recorder->vin_handle,
					MM_COMMAND_STATE_SET, MM_STATE_LOADED,
					NULL);
			wait_state(recorder->vin_handle, MM_STATE_LOADED);
		}
	}

	if (recorder->config.has_video) {
		if (recorder->muxer_handle && recorder->venc_handle) {
			mm_set_bind(recorder->venc_handle, VENC_PORT_OUT_INDEX,
				    NULL, 0);
			mm_set_bind(NULL, 0, recorder->muxer_handle,
				    MUX_PORT_VIDEO_INDEX);
		}
		if (recorder->venc_handle && recorder->vin_handle) {
			mm_set_bind(recorder->vin_handle, VIN_PORT_OUT_INDEX,
				    NULL, 0);
			mm_set_bind(NULL, 0, recorder->venc_handle,
				    VENC_PORT_IN_INDEX);
		}
	}

_FREE_HANDLE_:
	if (recorder->storage) {
		aic_storage_destroy(recorder->storage);
		recorder->storage = NULL;
	}
	if (recorder->muxer_handle) {
		mm_free_handle(recorder->muxer_handle);
		recorder->muxer_handle = NULL;
	}
	if (recorder->venc_handle) {
		mm_free_handle(recorder->venc_handle);
		recorder->venc_handle = NULL;
	}
	if (recorder->vin_handle) {
		mm_free_handle(recorder->vin_handle);
		recorder->vin_handle = NULL;
	}

	return 0;
}

s32 aic_recorder_set_output_path(struct aic_recorder *recorder, char *path)
{
	if (!recorder || !path) {
		loge("recorder or path is null.\n");
		return -1;
	}
	strncpy(recorder->output_path, path,
		sizeof(recorder->output_path) - 1);
	recorder->output_path[sizeof(recorder->output_path) - 1] = '\0';
	return 0;
}

s32 aic_recorder_set_max_duration(struct aic_recorder *recorder, void *data)
{
	mm_param_record_file_info rec_file_info = {0};
	if (!recorder || !recorder->muxer_handle || !data) {
		loge("recorder or data is null.\n");
		return -1;
	}

	rec_file_info.duration = *(s32 *)data;
	rec_file_info.file_num = recorder->config.file_num;
	rec_file_info.muxer_type = recorder->config.file_muxer_type;

	if (MM_ERROR_NONE !=
	    mm_set_parameter(recorder->muxer_handle,
			     MM_INDEX_VENDOR_MUXER_RECORD_FILE_INFO,
			     &rec_file_info)) {
		loge("mm_set_parameter file info error.\n");
		return -1;
	}

	return 0;
}

s32 aic_recorder_snapshot(struct aic_recorder *recorder)
{
	struct video_encoding_config *video_config;
	mm_param_u32 params = { 0 };
	s32 ret = 0;
	if (!recorder) {
		loge("recorder is null\n");
		return -1;
	}
	video_config = &recorder->config.video_config;
	if (video_config->codec_type != MPP_CODEC_VIDEO_ENCODER_MJPEG) {
		loge("not support codec_type %d now.",
		     video_config->codec_type);
		return -1;
	}
	params.u32 = 1;
	ret = mm_set_parameter(recorder->muxer_handle,
			       MM_INDEX_VENDOR_VIDEO_ENC_CAPTURE, &params);
	if (ret) {
		loge("set capture failed ret %d\n", ret);
		return -1;
	}
	return 0;
}

s32 aic_recorder_send_frame(struct aic_recorder *recorder,
			    struct aic_recorder_frame *frame)
{
	mm_buffer mbuf = {0};
	s32 ret = 0;

	if (!recorder || !recorder->vin_handle || !frame)
		return -1;

	memcpy(&mbuf.frame.mpp_frame, &frame->mpp_frame, sizeof(struct mpp_frame));
	mbuf.frame.vaddr[0] = frame->vaddr[0];
	mbuf.frame.vaddr[1] = frame->vaddr[1];
	mbuf.frame.vaddr[2] = frame->vaddr[2];
	mbuf.size = sizeof(struct mm_frame);
	mbuf.type = MM_BUFFER_DATA_FRAME;

	ret = mm_send_buffer(recorder->vin_handle, (mm_buffer *)&mbuf);
	if (ret != MM_ERROR_NONE) {
		logw("send buffer failed %d\n", ret);
		return -1;
	}

	return 0;
}

static void set_debug_info(struct aic_recorder *recorder, int debug_en)
{
	mm_param_u32 params = { 0 };
	params.u32 = debug_en;

	if (recorder->config.has_video) {
		if (recorder->vin_handle) {
			mm_set_parameter(recorder->vin_handle,
					 MM_INDEX_PARAM_PRINT_DEBUG_INFO,
					 &params);
		}
		if (recorder->venc_handle) {
			mm_set_parameter(recorder->venc_handle,
					 MM_INDEX_PARAM_PRINT_DEBUG_INFO,
					 &params);
		}
	}

	if (recorder->muxer_handle) {
		mm_set_parameter(recorder->muxer_handle,
				 MM_INDEX_PARAM_PRINT_DEBUG_INFO, &params);
	}
}

static int set_vin_osd(struct aic_recorder *recorder, struct aic_recorder_osd *osd)
{
	mm_param_osd params = {0};
	int ret = 0;
	if (!recorder || !recorder->vin_handle) {
		loge("recorder is null or vin not create.");
		return -1;
	}

	params.enable = osd->enable;
	params.id = osd->id;
	params.x = osd->x;
	params.y = osd->y;
	params.text = osd->text;

	ret = mm_set_parameter(recorder->vin_handle,
				MM_INDEX_PARAM_VIDEO_OSD_SET, &params);
	if (ret != MM_ERROR_NONE)
		return -1;

	return 0;
}

static int query_limit(struct aic_recorder_query *q)
{
	if (q->max > 0) {
		if (q->count > 0)
			return q->count < q->max ? q->count : q->max;
		return q->max;
	}
	return q->count > 0 ? q->count : -1;
}

static int do_control_set(struct aic_recorder *recorder,
			  enum aic_recorder_command cmd, void *data)
{
	struct aic_storage_record rec;

	switch (cmd) {
	case AIC_RECORDER_CMD_SET_DEBUG_INFO:
		set_debug_info(recorder, *(s32 *)data);
		return 0;

	case AIC_RECORDER_CMD_SET_OSD_TEXT:
		return set_vin_osd(recorder, (struct aic_recorder_osd *)data);

	case AIC_RECORDER_CMD_SET_LOCK_RECORD: {
		int locked = *(int *)data;
		if (!recorder || !recorder->storage)
			return -1;
		aic_storage_set_lock_pending(recorder->storage, locked);
		if (aic_storage_get_list(recorder->storage, &rec, 1) > 0)
			aic_storage_lock_file(recorder->storage, rec.file_path, locked);
		return 0;
	}

	case AIC_RECORDER_CMD_SET_USER_RECORD_DATA: {
		const char *user_data = (const char *)data;
		if (!recorder || !recorder->storage || !user_data)
			return -1;
		aic_storage_set_pending_user_data(recorder->storage, user_data);
		if (aic_storage_get_list(recorder->storage, &rec, 1) > 0)
			aic_storage_set_user_data(recorder->storage,
						  rec.file_path, user_data);
		return 0;
	}

	case AIC_RECORDER_CMD_SET_RECORD_DURATION:
		return aic_recorder_set_max_duration(recorder, data);

	default:
		return -1;
	}
}

/* ---- QUERY helper ---- */

static int do_control_query(struct aic_recorder *recorder,
			    enum aic_recorder_command cmd, void *data)
{
	struct aic_storage *s = recorder ? recorder->storage : NULL;
	struct aic_recorder_query *q;

	if (!s) {
		if (cmd == AIC_RECORDER_CMD_GET_RECORD_COUNT ||
		    cmd == AIC_RECORDER_CMD_GET_LOCKED_COUNT ||
		    cmd == AIC_RECORDER_CMD_GET_PICTURE_COUNT) {
			*(int *)data = 0;
			return 0;
		}
		q = (struct aic_recorder_query *)data;
		q->count = 0;
		return 0;
	}

	switch (cmd) {
	case AIC_RECORDER_CMD_GET_RECORD_COUNT:
		*(int *)data = aic_storage_get_count(s);
		return 0;
	case AIC_RECORDER_CMD_GET_LOCKED_COUNT:
		*(int *)data = aic_storage_get_locked_count(s);
		return 0;
	case AIC_RECORDER_CMD_GET_PICTURE_COUNT:
		*(int *)data = aic_storage_get_picture_count(s);
		return 0;

	case AIC_RECORDER_CMD_GET_RECORD_LIST:
		q = (struct aic_recorder_query *)data;
		q->count = aic_storage_get_list(s,
				(struct aic_storage_record *)q->records, query_limit(q));
		return 0;
	case AIC_RECORDER_CMD_GET_RECORD_BY_DATE:
		q = (struct aic_recorder_query *)data;
		q->count = aic_storage_get_list_by_date(s, q->date,
				(struct aic_storage_record *)q->records, query_limit(q));
		return 0;
	case AIC_RECORDER_CMD_GET_RECORD_COUNT_BY_DATE:
		q = (struct aic_recorder_query *)data;
		q->count = aic_storage_get_count_by_date(s, q->date);
		return 0;
	case AIC_RECORDER_CMD_GET_LOCKED_LIST:
		q = (struct aic_recorder_query *)data;
		q->count = aic_storage_get_locked_list(s,
				(struct aic_storage_record *)q->records, query_limit(q));
		return 0;

	case AIC_RECORDER_CMD_GET_PICTURE_LIST:
		q = (struct aic_recorder_query *)data;
		q->count = aic_storage_get_picture_list(s,
				(struct aic_storage_picture *)q->pictures, query_limit(q));
		return 0;
	case AIC_RECORDER_CMD_GET_PICTURE_BY_DATE:
		q = (struct aic_recorder_query *)data;
		q->count = aic_storage_get_picture_list_by_date(s, q->date,
				(struct aic_storage_picture *)q->pictures, query_limit(q));
		return 0;
	case AIC_RECORDER_CMD_GET_PICTURE_COUNT_BY_DATE:
		q = (struct aic_recorder_query *)data;
		q->count = aic_storage_get_picture_count_by_date(s, q->date);
		return 0;

	default:
		return -1;
	}
}

s32 aic_recorder_control(struct aic_recorder *recorder,
			 enum aic_recorder_command cmd, void *data)
{
	switch (cmd) {
	/* ---- SET group ---- */
	case AIC_RECORDER_CMD_SET_DEBUG_INFO:
	case AIC_RECORDER_CMD_SET_OSD_TEXT:
	case AIC_RECORDER_CMD_SET_LOCK_RECORD:
	case AIC_RECORDER_CMD_SET_USER_RECORD_DATA:
	case AIC_RECORDER_CMD_SET_RECORD_DURATION:
		return do_control_set(recorder, cmd, data);

	/* ---- QUERY group ---- */
	case AIC_RECORDER_CMD_GET_RECORD_COUNT:
	case AIC_RECORDER_CMD_GET_RECORD_LIST:
	case AIC_RECORDER_CMD_GET_RECORD_COUNT_BY_DATE:
	case AIC_RECORDER_CMD_GET_RECORD_BY_DATE:
	case AIC_RECORDER_CMD_GET_LOCKED_COUNT:
	case AIC_RECORDER_CMD_GET_LOCKED_LIST:
	case AIC_RECORDER_CMD_GET_PICTURE_COUNT:
	case AIC_RECORDER_CMD_GET_PICTURE_LIST:
	case AIC_RECORDER_CMD_GET_PICTURE_COUNT_BY_DATE:
	case AIC_RECORDER_CMD_GET_PICTURE_BY_DATE:
		return do_control_query(recorder, cmd, data);

	default:
		return -1;
	}
}

