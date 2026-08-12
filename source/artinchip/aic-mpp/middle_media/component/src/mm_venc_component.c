/*
 * Copyright (C) 2020-2026 ArtInChip Technology Co. Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 *  author: <jun.ma@artinchip.com>
 *  Desc: middle media venc component
 */

#include "mm_venc_component.h"
#include <malloc.h>
#include <pthread.h>
#include <stddef.h>
#include <string.h>
#include <sys/prctl.h>

#include "aic_message.h"
#include "aic_muxer.h"
#include "mpp_encoder.h"
#include "mpp_list.h"
#include "mpp_log.h"
#include "mpp_mem.h"

typedef struct mm_venc_coder_config {
	struct aic_av_video_stream video_stream;
	s32 quality; // 0- 100
} mm_mm_venc_coder_config;

typedef struct mm_venc_frame_info {
	u32 receive_ok_num;
	u32 receive_fail_num;
	u32 giveback_ok_num;
	u32 giveback_fail_num;
	u32 encode_ok_num;
	u32 encode_fail_num;
} mm_venc_frame_info;

typedef struct mm_venc_data {
	MM_STATE_TYPE state;
	pthread_mutex_t state_lock;
	mm_callback *p_callback;
	void *p_app_data;
	mm_handle h_self;
	mm_port_param port_param;

	mm_param_port_def in_port_def;
	mm_param_port_def out_port_def;

	mm_bind_info in_port_bind;
	mm_bind_info out_port_bind;

	pthread_t thread_id;
	struct aic_message_queue msg;

	struct aic_av_video_stream video_stream;
	struct mpp_encoder *encoder;
	enum mpp_codec_type code_type;
	int quality; // 0- 100
	mm_venc_frame_info frame_info;
	MM_BOOL debug_en;
} mm_venc_data;

static void *mm_venc_component_thread(void *p_thread_data);
static void mm_venc_show_debug_info(mm_venc_data *p_venc_data);

static s32 mm_venc_send_command(mm_handle h_component, MM_COMMAND_TYPE cmd,
				u32 param1, void *p_cmd_data)
{
	mm_venc_data *p_venc_data;
	s32 error = MM_ERROR_NONE;
	struct aic_message msg;
	p_venc_data =
		(mm_venc_data *)(((mm_component *)h_component)->p_comp_private);
	msg.message_id = cmd;
	msg.param = param1;
	msg.data_size = 0;

	aic_msg_put(&p_venc_data->msg, &msg);
	return error;
}

static s32 mm_venc_get_parameter(mm_handle h_component, MM_INDEX_TYPE index,
				 void *p_param)
{
	mm_venc_data *p_venc_data;
	s32 error = MM_ERROR_NONE;

	p_venc_data =
		(mm_venc_data *)(((mm_component *)h_component)->p_comp_private);

	switch (index) {
	case MM_INDEX_PARAM_PORT_DEFINITION: {
		mm_param_port_def *port = (mm_param_port_def *)p_param;
		if (port->port_index == VENC_PORT_IN_INDEX) {
			memcpy(port, &p_venc_data->in_port_def,
			       sizeof(mm_param_port_def));
		} else if (port->port_index == VENC_PORT_OUT_INDEX) {
			memcpy(port, &p_venc_data->out_port_def,
			       sizeof(mm_param_port_def));
		} else {
			error = MM_ERROR_BAD_PARAMETER;
		}
		break;
	}

	case MM_INDEX_PARAM_VIDEO_ENCODER_HANDLE: {
		*(struct mpp_encoder **)p_param = p_venc_data->encoder;
		break;
	}

	default:
		error = MM_ERROR_UNSUPPORT;
		break;
	}

	return error;
}

static s32 mm_venc_set_parameter(mm_handle h_component, MM_INDEX_TYPE index,
				 void *p_param)
{
	mm_venc_data *p_venc_data;
	s32 error = MM_ERROR_NONE;

	p_venc_data =
		(mm_venc_data *)(((mm_component *)h_component)->p_comp_private);
	switch (index) {
	case MM_INDEX_PARAM_PORT_DEFINITION: { // width height
		mm_param_port_def *port = (mm_param_port_def *)p_param;
		p_venc_data->video_stream.width =
			port->format.video.frame_width;
		p_venc_data->video_stream.height =
			port->format.video.frame_height;
		break;
	}
	case MM_INDEX_PARAM_QFACTOR: { // quality
		mm_image_param_qfactor *q = (mm_image_param_qfactor *)p_param;
		p_venc_data->quality = q->q_factor;
		if (!p_venc_data->encoder) {
			loge("encoder is not created.");
			return MM_ERROR_INSUFFICIENT_RESOURCES;
		}
		logd("set jpeg quality %d.", p_venc_data->quality);
		mpp_encoder_set_parameter(p_venc_data->encoder, ENC_CMD_JPEG_QUALITY,
				  &p_venc_data->quality);
		break;
	}

	case MM_INDEX_PARAM_PRINT_DEBUG_INFO: {
		p_venc_data->debug_en = ((mm_param_u32 *)p_param)->u32;
		mm_venc_show_debug_info(p_venc_data);
		break;
	}

	default:
		break;
	}
	return error;
}

static s32 mm_venc_get_config(mm_handle h_component, MM_INDEX_TYPE nIndex,
			      void *p_config)
{
	s32 error = MM_ERROR_NONE;
	return error;
}

static s32 mm_venc_set_config(mm_handle h_component, MM_INDEX_TYPE nIndex,
			      void *p_config)
{
	s32 error = MM_ERROR_NONE;
	return error;
}

static s32 mm_venc_get_state(mm_handle h_component, MM_STATE_TYPE *p_state)
{
	s32 error = MM_ERROR_NONE;
	mm_venc_data *p_venc_data;
	p_venc_data =
		(mm_venc_data *)(((mm_component *)h_component)->p_comp_private);

	pthread_mutex_lock(&p_venc_data->state_lock);
	*p_state = p_venc_data->state;
	pthread_mutex_unlock(&p_venc_data->state_lock);

	return error;
}

static s32 mm_venc_bind_request(mm_handle h_comp, u32 port,
				mm_handle h_bind_comp, u32 bind_port)
{
	s32 error = MM_ERROR_NONE;
	mm_param_port_def *p_port;
	mm_bind_info *p_bind_info;
	mm_venc_data *p_venc_data;
	p_venc_data =
		(mm_venc_data *)(((mm_component *)h_comp)->p_comp_private);
	if (p_venc_data->state != MM_STATE_LOADED) {
		loge("Component is not in MM_STATE_LOADED,it is in%d,it can not tunnel\n",
		     p_venc_data->state);
		return MM_ERROR_INVALID_STATE;
	}
	if (port == VENC_PORT_IN_INDEX) {
		p_port = &p_venc_data->in_port_def;
		p_bind_info = &p_venc_data->in_port_bind;
	} else if (port == VENC_PORT_OUT_INDEX) {
		p_port = &p_venc_data->out_port_def;
		p_bind_info = &p_venc_data->out_port_bind;
	} else {
		loge("component can not find port :%d\n", port);
		return MM_ERROR_BAD_PARAMETER;
	}

	// cancel setup tunnel
	if (NULL == h_bind_comp && 0 == bind_port) {
		p_bind_info->flag = MM_FALSE;
		p_bind_info->port_index = bind_port;
		p_bind_info->p_bind_comp = h_bind_comp;
		return MM_ERROR_NONE;
	}

	if (p_port->dir == MM_DIR_OUTPUT) {
		p_bind_info->port_index = bind_port;
		p_bind_info->p_bind_comp = h_bind_comp;
		p_bind_info->flag = MM_TRUE;
	} else if (p_port->dir == MM_DIR_INPUT) {
		mm_param_port_def port_def;
		port_def.port_index = bind_port;
		mm_get_parameter(h_bind_comp, MM_INDEX_PARAM_PORT_DEFINITION,
				 &port_def);
		if (port_def.dir != MM_DIR_OUTPUT) {
			loge("both ports are input.\n");
			return MM_ERROR_PORT_NOT_COMPATIBLE;
		}
		p_bind_info->port_index = bind_port;
		p_bind_info->p_bind_comp = h_bind_comp;
		p_bind_info->flag = MM_TRUE;
	} else {
		loge("port is neither output nor input.\n");
		return MM_ERROR_PORT_NOT_COMPATIBLE;
	}
	return error;
}

static s32 mm_venc_send_buffer(mm_handle h_component, mm_buffer *buffer)
{
	s32 error = MM_ERROR_NONE;
	mm_venc_data *p_venc_data;
	struct aic_message msg;
	struct mpp_frame frame;

	if (!h_component || !(((mm_component *)h_component)->p_comp_private))
		return -1;

	if (!buffer)
		return -1;

	if (buffer->type != MM_BUFFER_DATA_FRAME ||
	    buffer->size != sizeof(struct mm_frame)) {
		loge("buf type: %d, size: %d", buffer->type, buffer->size);
		return -1;
	}

	p_venc_data =
		(mm_venc_data *)(((mm_component *)h_component)->p_comp_private);

	memcpy(&frame, &buffer->frame.mpp_frame, sizeof(struct mpp_frame));
	logd("frame: id %d, pts: %lld.", frame.id, frame.pts);
	error = mpp_encoder_put_frame(p_venc_data->encoder, &frame);
	if (error < 0) {
		logd("Failed to put frame to encoder");
		p_venc_data->frame_info.receive_fail_num++;
		return -1;
	}

	p_venc_data->frame_info.receive_ok_num++;
	msg.message_id = MM_COMMAND_NOPS;
	msg.data_size = 0;
	aic_msg_put(&p_venc_data->msg, &msg);
	return 0;
}


static s32 mm_venc_set_callback(mm_handle h_component, mm_callback *p_callback,
				void *p_app_data)
{
	s32 error = MM_ERROR_NONE;
	mm_venc_data *p_venc_data;
	p_venc_data =
		(mm_venc_data *)(((mm_component *)h_component)->p_comp_private);
	p_venc_data->p_callback = p_callback;
	p_venc_data->p_app_data = p_app_data;
	return error;
}

static void release_frame_callback(struct mpp_frame *frame, void *user_data)
{
	mm_venc_data *p_venc_data = (mm_venc_data *)user_data;
	mm_component *p_vin_comp;
	s32 ret = MM_ERROR_NONE;
	mm_buffer mbuf;

	if (!frame || !user_data) {
		loge("frame or user_data is null.");
		return;
	}
	if (!p_venc_data->p_callback ||
	    !p_venc_data->p_callback->giveback_buffer) {
		loge("p_callback is null.");
		return;
	}

	memcpy(&mbuf.frame.mpp_frame, frame, sizeof(struct mpp_frame));
	mbuf.size = sizeof(struct mm_frame);
	mbuf.type = MM_BUFFER_DATA_FRAME;

	/* if bound to a vin component, return buffer directly to it */
	p_vin_comp = p_venc_data->in_port_bind.p_bind_comp;
	if (p_venc_data->in_port_bind.flag && p_vin_comp) {
		ret = mm_giveback_buffer(p_vin_comp, &mbuf);
	} else if (p_venc_data->p_callback &&
		   p_venc_data->p_callback->giveback_buffer) {
		ret = p_venc_data->p_callback->giveback_buffer(
			p_venc_data->h_self, p_venc_data->p_app_data, &mbuf);
	} else {
		loge("no giveback path for frame");
		return;
	}
	if (ret) {
		p_venc_data->frame_info.giveback_fail_num++;
		loge("release frame callback failed %d, pts: %lld, id:%u", ret,
		     frame->pts, frame->id);
		return;
	}
	p_venc_data->frame_info.giveback_ok_num++;

	logd("release frame callback, pts: %lld, id:%u", frame->pts, frame->id);
}

static s32 mm_venc_create_encoder(mm_venc_data *p_venc_data)
{
	struct encode_config enc_cfg = { 0 };
	int def_quality = 80;

	if (!p_venc_data) {
		loge("p_venc_data is null.");
		return MM_ERROR_INSUFFICIENT_RESOURCES;
	}

	p_venc_data->encoder =
		mpp_encoder_create(MPP_CODEC_VIDEO_ENCODER_MJPEG);
	if (!p_venc_data->encoder) {
		loge("Failed to create encoder");
		return MM_ERROR_INSUFFICIENT_RESOURCES;
	}

	enc_cfg.packet_buffer_size = 2 * 1024 * 1024;
	if (mpp_encoder_init(p_venc_data->encoder, &enc_cfg) < 0) {
		loge("Failed to init encoder");
		mpp_encoder_destory(p_venc_data->encoder);
		p_venc_data->encoder = NULL;
		return MM_ERROR_INSUFFICIENT_RESOURCES;
	}

	mpp_encoder_set_callback(p_venc_data->encoder, release_frame_callback,
				 p_venc_data);
	mpp_encoder_set_parameter(p_venc_data->encoder, ENC_CMD_JPEG_QUALITY,
				  &def_quality);
	return MM_ERROR_NONE;
}

static s32 mm_venc_destroy_encoder(mm_venc_data *p_venc_data)
{
	if (!p_venc_data || !p_venc_data->encoder) {
		return MM_ERROR_NONE;
	}
	mpp_encoder_destory(p_venc_data->encoder);
	p_venc_data->encoder = NULL;
	return MM_ERROR_NONE;
}

static void mm_venc_show_debug_info(mm_venc_data *p_venc_data)
{
	if (!p_venc_data->debug_en)
		return;

	printf("**************************Venc comp info***************************\n");
	printf("\nFrame:  recv_ok    recv_fail    gb_ok    gb_fail\n");
	printf("\t%7u    %9u    %5u    %7u\n",
	       p_venc_data->frame_info.receive_ok_num,
	       p_venc_data->frame_info.receive_fail_num,
	       p_venc_data->frame_info.giveback_ok_num,
	       p_venc_data->frame_info.giveback_fail_num);
	printf("\nEncode:  enc_ok    enc_fail\n");
	printf("\t%6u    %8u\n", p_venc_data->frame_info.encode_ok_num,
	       p_venc_data->frame_info.encode_fail_num);
	printf("\nstate: %s\n", mm_component_sta_to_str(p_venc_data->state));
	printf("\n");
}

s32 mm_venc_component_deinit(mm_handle h_component)
{
	mm_component *p_comp;
	mm_venc_data *p_venc_data;
	s32 error = MM_ERROR_NONE;
	struct aic_message msg;

	p_comp = (mm_component *)h_component;
	p_venc_data = (mm_venc_data *)p_comp->p_comp_private;

	pthread_mutex_lock(&p_venc_data->state_lock);
	if (p_venc_data->state != MM_STATE_LOADED) {
		logw("compoent is in %d,but not in MM_STATE_LOADED(1),can not FreeHandle.\n",
		     p_venc_data->state);
		pthread_mutex_unlock(&p_venc_data->state_lock);
		return MM_ERROR_INVALID_STATE;
	}
	pthread_mutex_unlock(&p_venc_data->state_lock);

	msg.message_id = MM_COMMAND_STOP;
	msg.data_size = 0;
	aic_msg_put(&p_venc_data->msg, &msg);
	pthread_join(p_venc_data->thread_id, (void *)&error);

	mm_venc_destroy_encoder(p_venc_data);

	pthread_mutex_destroy(&p_venc_data->state_lock);

	aic_msg_destroy(&p_venc_data->msg);

	mpp_free(p_venc_data);
	p_venc_data = NULL;
	return error;
}

s32 mm_venc_component_init(mm_handle h_component)
{
	mm_component *p_comp;
	mm_venc_data *p_venc_data;
	s32 error = MM_ERROR_NONE;
	s8 msg_create = 0;
	s8 state_lock_init = 0;

	p_comp = (mm_component *)h_component;

	p_venc_data = (mm_venc_data *)mpp_alloc(sizeof(mm_venc_data));

	if (NULL == p_venc_data) {
		loge("mpp_alloc(sizeof(mm_venc_data) fail!");
		return MM_ERROR_INSUFFICIENT_RESOURCES;
	}

	memset(p_venc_data, 0x0, sizeof(mm_venc_data));
	p_comp->p_comp_private = (void *)p_venc_data;
	p_venc_data->state = MM_STATE_LOADED;
	p_venc_data->h_self = p_comp;

	p_comp->set_callback = mm_venc_set_callback;
	p_comp->send_command = mm_venc_send_command;
	p_comp->get_state = mm_venc_get_state;
	p_comp->get_parameter = mm_venc_get_parameter;
	p_comp->set_parameter = mm_venc_set_parameter;
	p_comp->get_config = mm_venc_get_config;
	p_comp->set_config = mm_venc_set_config;
	p_comp->bind_request = mm_venc_bind_request;
	p_comp->deinit = mm_venc_component_deinit;
	p_comp->giveback_buffer = NULL;
	p_comp->send_buffer = mm_venc_send_buffer;

	p_venc_data->in_port_def.port_index = VENC_PORT_IN_INDEX;
	p_venc_data->in_port_def.enable = MM_TRUE;
	p_venc_data->in_port_def.dir = MM_DIR_INPUT;

	p_venc_data->out_port_def.port_index = VENC_PORT_OUT_INDEX;
	p_venc_data->out_port_def.enable = MM_TRUE;
	p_venc_data->out_port_def.dir = MM_DIR_OUTPUT;

	p_venc_data->in_port_bind.port_index = VENC_PORT_IN_INDEX;
	p_venc_data->in_port_bind.p_self_comp = h_component;
	p_venc_data->out_port_bind.port_index = VENC_PORT_OUT_INDEX;
	p_venc_data->out_port_bind.p_self_comp = h_component;

	if (pthread_mutex_init(&p_venc_data->state_lock, NULL)) {
		loge("pthread_mutex_init fail!\n");
		goto _EXIT;
	}
	state_lock_init = 1;

	if (mm_venc_create_encoder(p_venc_data)) {
		error = MM_ERROR_INSUFFICIENT_RESOURCES;
		goto _EXIT;
	}

	if (aic_msg_create(&p_venc_data->msg) < 0) {
		loge("aic_msg_create fail!\n");
		error = MM_ERROR_INSUFFICIENT_RESOURCES;
		goto _EXIT;
	}
	msg_create = 1;

	// Create the component thread
	error = pthread_create(&p_venc_data->thread_id, NULL,
			       mm_venc_component_thread, p_venc_data);
	if (error || !p_venc_data->thread_id) {
		loge("pthread_create venc component fail!");
		error = MM_ERROR_INSUFFICIENT_RESOURCES;
		goto _EXIT;
	}

	return error;

_EXIT:
	mm_venc_destroy_encoder(p_venc_data);
	if (state_lock_init) {
		pthread_mutex_destroy(&p_venc_data->state_lock);
	}
	if (msg_create) {
		aic_msg_destroy(&p_venc_data->msg);
	}

	if (p_venc_data) {
		mpp_free(p_venc_data);
		p_venc_data = NULL;
	}
	return error;
}

static void mm_venc_event_notify(mm_venc_data *p_venc_data, MM_EVENT_TYPE event,
				 u32 data1, u32 data2, void *p_event_data)
{
	if (p_venc_data && p_venc_data->p_callback &&
	    p_venc_data->p_callback->event_handler) {
		p_venc_data->p_callback->event_handler(p_venc_data->h_self,
						       p_venc_data->p_app_data,
						       event, data1, data2,
						       p_event_data);
	}
}

static void mm_venc_state_change_to_invalid(mm_venc_data *p_venc_data)
{
	p_venc_data->state = MM_STATE_INVALID;
	mm_venc_event_notify(p_venc_data, MM_EVENT_ERROR,
			     MM_ERROR_INVALID_STATE, 0, NULL);
	mm_venc_event_notify(p_venc_data, MM_EVENT_CMD_COMPLETE,
			     MM_COMMAND_STATE_SET, p_venc_data->state, NULL);
}

static void mm_venc_state_change_to_idle(mm_venc_data *p_venc_data)
{
	if (MM_STATE_LOADED != p_venc_data->state &&
		MM_STATE_EXECUTING != p_venc_data->state &&
		MM_STATE_PAUSE != p_venc_data->state) {
		mm_venc_event_notify(p_venc_data, MM_EVENT_ERROR,
				     MM_ERROR_INCORRECT_STATE_TRANSITION,
				     p_venc_data->state, NULL);
		loge("venc in wrong state %d.\n", p_venc_data->state);
		return;
	}

	p_venc_data->state = MM_STATE_IDLE;
	mm_venc_event_notify(p_venc_data, MM_EVENT_CMD_COMPLETE,
			     MM_COMMAND_STATE_SET, p_venc_data->state, NULL);
}

static void mm_venc_state_change_to_loaded(mm_venc_data *p_venc_data)
{
	if (MM_STATE_IDLE == p_venc_data->state) {
		logi("mm_venc_state_change_to_loaded\n");

		p_venc_data->state = MM_STATE_LOADED;
		mm_venc_event_notify(p_venc_data, MM_EVENT_CMD_COMPLETE,
				     MM_COMMAND_STATE_SET, p_venc_data->state,
				     NULL);
	} else {
		mm_venc_event_notify(p_venc_data, MM_EVENT_ERROR,
				     MM_ERROR_INCORRECT_STATE_TRANSITION,
				     p_venc_data->state, NULL);
		loge("venc in wrong state %d.\n", p_venc_data->state);
	}
}

static void mm_venc_state_change_to_executing(mm_venc_data *p_venc_data)
{
	if (MM_STATE_IDLE != p_venc_data->state &&
		MM_STATE_PAUSE != p_venc_data->state) {
		mm_venc_event_notify(p_venc_data, MM_EVENT_ERROR,
				     MM_ERROR_INCORRECT_STATE_TRANSITION,
				     p_venc_data->state, NULL);
		loge("venc in wrong state %d.\n", p_venc_data->state);
		return;
	}
	p_venc_data->state = MM_STATE_EXECUTING;
}

static void mm_venc_state_change_to_pause(mm_venc_data *p_venc_data)
{
	if (MM_STATE_EXECUTING != p_venc_data->state) {
		mm_venc_event_notify(p_venc_data, MM_EVENT_ERROR,
				     MM_ERROR_INCORRECT_STATE_TRANSITION,
				     p_venc_data->state, NULL);
		loge("venc in wrong state %d.\n", p_venc_data->state);
		return;
	}
	p_venc_data->state = MM_STATE_PAUSE;
}

static int mm_venc_component_process_cmd(mm_venc_data *p_venc_data)
{
	s32 cmd = MM_COMMAND_UNKNOWN;
	s32 cmd_data;
	struct aic_message message;

	if (aic_msg_get(&p_venc_data->msg, &message) == 0) {
		cmd = message.message_id;
		cmd_data = message.param;
		logi("cmd:%d, cmd_data:%d\n", cmd, cmd_data);
		if (MM_COMMAND_STATE_SET == cmd) {
			pthread_mutex_lock(&p_venc_data->state_lock);
			if (p_venc_data->state == (MM_STATE_TYPE)(cmd_data)) {
				mm_venc_event_notify(p_venc_data,
						     MM_EVENT_ERROR,
						     MM_ERROR_SAME_STATE, 0,
						     NULL);
				pthread_mutex_unlock(&p_venc_data->state_lock);
				goto CMD_EXIT;
			}
			switch ((MM_STATE_TYPE)(cmd_data)) {
			case MM_STATE_INVALID:
				mm_venc_state_change_to_invalid(p_venc_data);
				break;
			case MM_STATE_LOADED:
				mm_venc_state_change_to_loaded(p_venc_data);
				break;
			case MM_STATE_IDLE:
				mm_venc_state_change_to_idle(p_venc_data);
				break;
			case MM_STATE_EXECUTING:
				mm_venc_state_change_to_executing(p_venc_data);
				break;
			case MM_STATE_PAUSE:
				mm_venc_state_change_to_pause(p_venc_data);
				break;
			default:
				break;
			}
			pthread_mutex_unlock(&p_venc_data->state_lock);
		} else if (MM_COMMAND_STOP == cmd) {
			logi("mm_venc_component_thread ready to exit!!!\n");
			goto CMD_EXIT;
		}
	}

CMD_EXIT:
	return cmd;
}

static void *mm_venc_component_thread(void *p_thread_data)
{
	s32 ret = MM_ERROR_NONE;
	s32 cmd = MM_COMMAND_UNKNOWN;
	mm_venc_data *p_venc_data = (mm_venc_data *)p_thread_data;

	while (1) {
		cmd = mm_venc_component_process_cmd(p_venc_data);
		if (MM_COMMAND_STATE_SET == cmd) {
			continue;
		} else if (MM_COMMAND_STOP == cmd) {
			goto _EXIT;
		}

		if (p_venc_data->state != MM_STATE_EXECUTING) {
			aic_msg_wait_new_msg(&p_venc_data->msg, 0);
			continue;
		}

		/* do encode */
		ret = mpp_encoder_encode(p_venc_data->encoder);
		if (ret == ENC_OK) {
			p_venc_data->frame_info.encode_ok_num++;
			mm_send_command(p_venc_data->out_port_bind.p_bind_comp,
					MM_COMMAND_WKUP, 0, NULL);
		} else if (ret == ENC_NO_EMPTY_PACKET) {
			p_venc_data->frame_info.encode_fail_num++;
			mm_send_command(p_venc_data->out_port_bind.p_bind_comp,
					MM_COMMAND_WKUP, 0, NULL);
		} else if (ret == ENC_NO_READY_FRAME ||
			   ret == ENC_NO_EMPTY_FRAME ||
			   ret == ENC_NO_READY_PACKET) {
			p_venc_data->frame_info.encode_fail_num++;
			aic_msg_wait_new_msg(&p_venc_data->msg, 5000);
		}
	}

_EXIT:
	return (void *)MM_ERROR_NONE;
}
