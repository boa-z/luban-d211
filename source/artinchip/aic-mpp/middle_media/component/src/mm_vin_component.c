/*
 * Copyright (C) 2020-2026 ArtInChip Technology Co. Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 *  author: <che.jiang@artinchip.com>
 *  Desc: middle media vin component
 */

#include "mm_vin_component.h"
#include "aic_message.h"
#include "aic_osd.h"
#include "dma_allocator.h"
#include "mpp_ge.h"
#include "mpp_log.h"
#include "mpp_mem.h"
#include "mpp_time.h"
#include <pthread.h>
#include <string.h>
#include <unistd.h>

#define VIN_FRAME_NUM 2

struct vin_frame_node {
	struct mm_frame frame;
	struct mpp_list list;
};

typedef struct mm_vin_frame_info {
	u32 recv_ok_num;
	u32 recv_fail_num;
	u32 send_ok_num;
	u32 send_fail_num;
	u32 osd_ok_num;
	u32 osd_fail_num;
} mm_vin_frame_info;

typedef struct mm_vin_data {
	MM_STATE_TYPE state;
	pthread_mutex_t state_lock;
	mm_callback *p_callback;
	void *p_app_data;
	mm_handle h_self;

	mm_param_port_def out_port_def;
	mm_bind_info out_port_bind;

	pthread_t thread_id;
	struct aic_message_queue msg;

	/* local frame node pool: empty → ready → processed → empty */
	struct vin_frame_node frame_nodes[VIN_FRAME_NUM];
	struct mpp_list empty_list;
	struct mpp_list ready_list;
	struct mpp_list processed_list;
	pthread_mutex_t frame_lock;

	/* OSD */
	struct aic_osd *osd;
	int frame_w;
	int frame_h;

	/* perf */
	mm_vin_frame_info frame_info;
	MM_BOOL debug_en;
} mm_vin_data;

static void *mm_vin_component_thread(void *p_thread_data);
static void mm_vin_show_debug_info(mm_vin_data *p_vin_data);

static s32 mm_vin_set_user_osd(mm_vin_data *p_vin_data, mm_param_osd *osd)
{
	s32 ret = MM_ERROR_NONE;
	s32 id, enable;

	if (!p_vin_data || !p_vin_data->osd || !osd)
		return MM_ERROR_NULL_POINTER;

	if (osd->id + AIC_OSD_ID_USER > AIC_OSD_MAX_REGIONS)
		return MM_ERROR_BAD_PARAMETER;

	id = osd->id + AIC_OSD_ID_USER;
	enable = osd->enable;

	if (enable == MM_FALSE) {
		ret = aic_osd_enable_region(p_vin_data->osd, id, enable);
		if (ret) {
			loge("Enable osd region %d failed %d.", id, ret);
			return ret;
		}
		return ret;
	}

	ret = aic_osd_set_region(p_vin_data->osd, id, osd->x, osd->y,
				 osd->text);
	if (ret) {
		loge("Set osd region %d failed %d.", id, ret);
		return ret;
	}

	ret = aic_osd_enable_region(p_vin_data->osd, id, enable);
	if (ret) {
		loge("Enable osd region %d failed %d.", id, ret);
		return ret;
	}

	return ret;
}

static s32 mm_vin_set_command(mm_handle h_component, MM_COMMAND_TYPE cmd,
			      u32 param1, void *p_cmd_data)
{
	mm_vin_data *p_vin_data;
	struct aic_message msg;
	p_vin_data =
		(mm_vin_data *)(((mm_component *)h_component)->p_comp_private);
	msg.message_id = cmd;
	msg.param = param1;
	msg.data_size = 0;

	aic_msg_put(&p_vin_data->msg, &msg);
	return MM_ERROR_NONE;
}

static s32 mm_vin_get_parameter(mm_handle h_component, MM_INDEX_TYPE index,
				void *p_param)
{
	mm_vin_data *p_vin_data;
	s32 error = MM_ERROR_NONE;
	p_vin_data =
		(mm_vin_data *)(((mm_component *)h_component)->p_comp_private);

	switch (index) {
	case MM_INDEX_PARAM_PORT_DEFINITION: {
		mm_param_port_def *port = (mm_param_port_def *)p_param;
		if (port->port_index == VIN_PORT_OUT_INDEX) {
			memcpy(port, &p_vin_data->out_port_def,
			       sizeof(mm_param_port_def));
		} else {
			error = MM_ERROR_BAD_PARAMETER;
		}
		break;
	}
	default:
		error = MM_ERROR_UNSUPPORT;
		break;
	}

	return error;
}

static s32 mm_vin_set_parameter(mm_handle h_component, MM_INDEX_TYPE index,
				void *p_param)
{
	mm_vin_data *p_vin_data;
	s32 ret = MM_ERROR_NONE;

	p_vin_data =
		(mm_vin_data *)(((mm_component *)h_component)->p_comp_private);
	switch (index) {
	case MM_INDEX_PARAM_PORT_DEFINITION: {
		mm_param_port_def *port = (mm_param_port_def *)p_param;
		if (port->port_index == VIN_PORT_OUT_INDEX)
			memcpy(&p_vin_data->out_port_def, port,
			       sizeof(mm_param_port_def));
		break;
	}

	case MM_INDEX_PARAM_PRINT_DEBUG_INFO: {
		p_vin_data->debug_en = ((mm_param_u32 *)p_param)->u32;
		mm_vin_show_debug_info(p_vin_data);
		break;
	}

	case MM_INDEX_PARAM_VIDEO_OSD_SET: {
		ret = mm_vin_set_user_osd(p_vin_data, (mm_param_osd *)p_param);
	}

	default:
		break;
	}

	return ret;
}

static s32 mm_vin_get_config(mm_handle h_component, MM_INDEX_TYPE index,
			     void *p_config)
{
	return MM_ERROR_NONE;
}

static s32 mm_vin_set_config(mm_handle h_component, MM_INDEX_TYPE index,
			     void *p_config)
{
	return MM_ERROR_NONE;
}

static s32 mm_vin_process_state(mm_handle h_component,
				    MM_STATE_TYPE *p_state)
{
	mm_vin_data *p_vin_data;
	p_vin_data =
		(mm_vin_data *)(((mm_component *)h_component)->p_comp_private);

	pthread_mutex_lock(&p_vin_data->state_lock);
	*p_state = p_vin_data->state;
	pthread_mutex_unlock(&p_vin_data->state_lock);
	return MM_ERROR_NONE;
}

static s32 mm_vin_bind_request(mm_handle h_comp, u32 port,
			       mm_handle h_bind_port, u32 bind_port)
{
	mm_vin_data *p_vin_data;

	p_vin_data = (mm_vin_data *)(((mm_component *)h_comp)->p_comp_private);
	if (p_vin_data->state != MM_STATE_LOADED) {
		loge("Component is not in MM_STATE_LOADED");
		return MM_ERROR_INVALID_STATE;
	}
	if (port != VIN_PORT_OUT_INDEX) {
		loge("component can not find port :%d", port);
		return MM_ERROR_BAD_PARAMETER;
	}

	if (NULL == h_bind_port && 0 == bind_port) {
		p_vin_data->out_port_bind.flag = MM_FALSE;
		p_vin_data->out_port_bind.bind_port_index = bind_port;
		p_vin_data->out_port_bind.p_bind_comp = h_bind_port;
		return MM_ERROR_NONE;
	}

	p_vin_data->out_port_bind.bind_port_index = bind_port;
	p_vin_data->out_port_bind.p_bind_comp = h_bind_port;
	p_vin_data->out_port_bind.flag = MM_TRUE;

	return MM_ERROR_NONE;
}

/* component method: venc releases vin-local frame, recycle node to empty_list */
static s32 mm_vin_giveback_buffer(mm_handle h_component, mm_buffer *p_buffer)
{
	mm_vin_data *p_vin_data;
	struct mpp_frame *frame;
	mm_buffer mbuf;
	int found = 0;

	if (!h_component || !p_buffer)
		return -1;

	frame = (struct mpp_frame *)&p_buffer->frame.mpp_frame;
	p_vin_data =
		(mm_vin_data *)(((mm_component *)h_component)->p_comp_private);

	pthread_mutex_lock(&p_vin_data->frame_lock);
	struct vin_frame_node *iter, *tmp;
	mpp_list_for_each_entry_safe(iter, tmp, &p_vin_data->processed_list,
				     list) {
		logd("iter.frame.id:%d, frame->id:%d", iter->frame.mpp_frame.id, frame->id);
		if (iter->frame.mpp_frame.id == frame->id) {
			mpp_list_del(&iter->list);
			mpp_list_add_tail(&iter->list, &p_vin_data->empty_list);
			found = 1;
			break;
		}
	}
	pthread_mutex_unlock(&p_vin_data->frame_lock);

	if (found) {
		if (!p_vin_data->p_callback ||
	    	!p_vin_data->p_callback->giveback_buffer) {
			loge("p_callback is null.");
			return -1;
		}
		//giveback frame to user to queue buf
		logd("giveback frame to user, frame->id:%d", frame->id);
		memcpy(&mbuf.frame.mpp_frame, frame, sizeof(struct mpp_frame));
		mbuf.size = sizeof(struct mm_frame);
		mbuf.type = MM_BUFFER_DATA_FRAME;
		return p_vin_data->p_callback->giveback_buffer(
			p_vin_data->h_self, p_vin_data->p_app_data, &mbuf);
	} else {
		loge("giveback: frame id=%d not found in processed_list", frame->id);
	}

	return found ? 0 : -1;
}

static s32 mm_vin_set_callback(mm_handle h_component, mm_callback *p_callback,
			       void *p_app_data)
{
	mm_vin_data *p_vin_data;
	p_vin_data =
		(mm_vin_data *)(((mm_component *)h_component)->p_comp_private);
	p_vin_data->p_callback = p_callback;
	p_vin_data->p_app_data = p_app_data;
	return MM_ERROR_NONE;
}

static void mm_vin_show_debug_info(mm_vin_data *p_vin_data)
{
	if (!p_vin_data->debug_en)
		return;

	printf("**************************Vin comp info***************************\n");
	printf("\nIn:     recv_ok    recv_fail\n");
	printf("\t%7u    %9u\n",
	       p_vin_data->frame_info.recv_ok_num,
	       p_vin_data->frame_info.recv_fail_num);
	printf("\nOSD:    osd_ok    osd_fail\n");
	printf("\t%6u    %8u\n", p_vin_data->frame_info.osd_ok_num,
	       p_vin_data->frame_info.osd_fail_num);

	printf("\nOut:    send_ok    send_fail\n");
	printf("\t%7u    %9u\n", p_vin_data->frame_info.send_ok_num,
	       p_vin_data->frame_info.send_fail_num);
	printf("state: %s\n", mm_component_sta_to_str(p_vin_data->state));
}

static struct vin_frame_node *vin_dequeue_empty(mm_vin_data *p_vin_data)
{
	struct vin_frame_node *node = NULL;

	pthread_mutex_lock(&p_vin_data->frame_lock);
	if (!mpp_list_empty(&p_vin_data->empty_list)) {
		node = mpp_list_first_entry(&p_vin_data->empty_list,
					    struct vin_frame_node, list);
		mpp_list_del(&node->list);
	}
	pthread_mutex_unlock(&p_vin_data->frame_lock);
	return node;
}

static void vin_enqueue_ready(mm_vin_data *p_vin_data,
			      struct vin_frame_node *node)
{
	struct aic_message msg;
	pthread_mutex_lock(&p_vin_data->frame_lock);
	mpp_list_add_tail(&node->list, &p_vin_data->ready_list);
	pthread_mutex_unlock(&p_vin_data->frame_lock);

	msg.message_id = MM_COMMAND_NOPS;
	msg.data_size = 0;
	aic_msg_put(&p_vin_data->msg, &msg);
}

static void vin_enqueue_processed(mm_vin_data *p_vin_data,
				  struct vin_frame_node *node)
{
	pthread_mutex_lock(&p_vin_data->frame_lock);
	mpp_list_add_tail(&node->list, &p_vin_data->processed_list);
	pthread_mutex_unlock(&p_vin_data->frame_lock);
}

static struct vin_frame_node *vin_wait_dequeue_ready(mm_vin_data *p_vin_data)
{
	struct vin_frame_node *node = NULL;

	pthread_mutex_lock(&p_vin_data->frame_lock);
	if (mpp_list_empty(&p_vin_data->ready_list)) {
		pthread_mutex_unlock(&p_vin_data->frame_lock);
		return NULL;
	}
	node = mpp_list_first_entry(&p_vin_data->ready_list,
				    struct vin_frame_node, list);
	mpp_list_del(&node->list);
	pthread_mutex_unlock(&p_vin_data->frame_lock);
	return node;
}

static void mm_vin_do_osd(mm_vin_data *p_vin_data, struct vin_frame_node *node)
{
	char text[128] = { 0 };
	struct tm local_tm;

	if (!p_vin_data->osd)
		return;

	mpp_get_local_tm(&local_tm);
	snprintf(text, sizeof(text), "%04d-%02d-%02d %02d:%02d:%02d",
		 1900 + local_tm.tm_year, 1 + local_tm.tm_mon, local_tm.tm_mday,
		 local_tm.tm_hour, local_tm.tm_min, local_tm.tm_sec);

	aic_osd_update_text(p_vin_data->osd, AIC_OSD_ID_TIME, text);
	aic_osd_draw(p_vin_data->osd, node->frame.vaddr[0], node->frame.vaddr[1],
		     node->frame.mpp_frame.buf.stride[0], node->frame.mpp_frame.buf.stride[1]);
}

static int vin_send_to_venc(mm_vin_data *p_vin_data,
			    struct vin_frame_node *node)
{
	mm_component *p_venc_comp;
	mm_buffer mbuf;
	s32 ret;

	vin_enqueue_processed(p_vin_data, node);

	memcpy(&mbuf.frame, &node->frame, sizeof(struct mm_frame));
	mbuf.size = sizeof(struct mm_frame);
	mbuf.type = MM_BUFFER_DATA_FRAME;

	p_venc_comp = p_vin_data->out_port_bind.p_bind_comp;
	if (p_vin_data->out_port_bind.flag && p_venc_comp)
		ret = mm_send_buffer(p_venc_comp, &mbuf);
	else
		ret = -1;
	if (ret) {
		p_vin_data->frame_info.send_fail_num++;
		/* move back from processed to empty */
		pthread_mutex_lock(&p_vin_data->frame_lock);
		struct vin_frame_node *iter, *tmp;
		mpp_list_for_each_entry_safe(iter, tmp,
					     &p_vin_data->processed_list, list) {
			if (iter == node) {
				mpp_list_del(&iter->list);
				mpp_list_add_tail(&iter->list,
						  &p_vin_data->empty_list);
				break;
			}
		}
		pthread_mutex_unlock(&p_vin_data->frame_lock);
		if (!p_vin_data->p_callback ||
	    	!p_vin_data->p_callback->giveback_buffer) {
			loge("p_callback is null.");
			return -1;
		}
		//giveback frame to user to queue buf
		p_vin_data->p_callback->giveback_buffer(
			p_vin_data->h_self, p_vin_data->p_app_data, &mbuf);
		return -1;
	}
	p_vin_data->frame_info.send_ok_num++;
	return 0;
}

/* send_buffer: called from recorder thread.
 * Share source DVP frame, enqueued to
 * ready_list for OSD processing in the vin thread.
 */
static s32 mm_vin_send_buffer(mm_handle h_component, mm_buffer *buffer)
{
	mm_vin_data *p_vin_data;
	struct mm_frame *src_frame;
	struct vin_frame_node *node;

	if (!h_component || !buffer || !buffer->data)
		return -1;

	p_vin_data =
		(mm_vin_data *)(((mm_component *)h_component)->p_comp_private);

	node = vin_dequeue_empty(p_vin_data);
	if (!node) {
		logw("no empty src_frame node, drop src_frame");
		p_vin_data->frame_info.recv_fail_num++;
		return -1;
	}
	p_vin_data->frame_info.recv_ok_num++;

	src_frame = (struct mm_frame *)&buffer->frame;
	memcpy(&node->frame, src_frame, sizeof(struct mm_frame));

	/* enqueue local buffer for OSD processing in vin thread */
	vin_enqueue_ready(p_vin_data, node);
	return 0;
}

s32 mm_vin_component_deinit(mm_handle h_component)
{
	mm_component *p_comp;
	mm_vin_data *p_vin_data;
	struct aic_message msg;
	s32 error = MM_ERROR_NONE;

	p_comp = (mm_component *)h_component;
	p_vin_data = (mm_vin_data *)p_comp->p_comp_private;

	pthread_mutex_lock(&p_vin_data->state_lock);
	if (p_vin_data->state != MM_STATE_LOADED) {
		logd("component is in %d, not in LOADED", p_vin_data->state);
		pthread_mutex_unlock(&p_vin_data->state_lock);
		return MM_ERROR_INVALID_STATE;
	}
	pthread_mutex_unlock(&p_vin_data->state_lock);

	msg.message_id = MM_COMMAND_STOP;
	msg.data_size = 0;
	aic_msg_put(&p_vin_data->msg, &msg);
	pthread_join(p_vin_data->thread_id, (void *)&error);

	pthread_mutex_destroy(&p_vin_data->state_lock);
	aic_msg_destroy(&p_vin_data->msg);
	pthread_mutex_destroy(&p_vin_data->frame_lock);

	if (p_vin_data->osd)
		aic_osd_destroy(p_vin_data->osd);

	mpp_free(p_vin_data);
	logd("mm_vin_component_deinit");
	return error;
}

s32 mm_vin_component_init(mm_handle h_component)
{
	mm_component *p_comp;
	mm_vin_data *p_vin_data;
	s32 error = MM_ERROR_NONE;
	s8 state_lock_init = 0;
	s8 frame_lock_init = 0;
	s8 msg_create = 0;
	int i;

	logd("mm_vin_component_init....");

	p_comp = (mm_component *)h_component;

	p_vin_data = (mm_vin_data *)mpp_alloc(sizeof(mm_vin_data));
	if (!p_vin_data) {
		loge("mpp_alloc(mm_vin_data) fail!");
		return MM_ERROR_INSUFFICIENT_RESOURCES;
	}

	memset(p_vin_data, 0, sizeof(mm_vin_data));
	p_comp->p_comp_private = (void *)p_vin_data;
	p_vin_data->state = MM_STATE_LOADED;
	p_vin_data->h_self = p_comp;

	p_comp->set_callback = mm_vin_set_callback;
	p_comp->send_command = mm_vin_set_command;
	p_comp->get_state = mm_vin_process_state;
	p_comp->get_parameter = mm_vin_get_parameter;
	p_comp->set_parameter = mm_vin_set_parameter;
	p_comp->get_config = mm_vin_get_config;
	p_comp->set_config = mm_vin_set_config;
	p_comp->bind_request = mm_vin_bind_request;
	p_comp->deinit = mm_vin_component_deinit;
	p_comp->send_buffer = mm_vin_send_buffer;
	p_comp->giveback_buffer = mm_vin_giveback_buffer;

	p_vin_data->out_port_def.port_index = VIN_PORT_OUT_INDEX;
	p_vin_data->out_port_def.enable = MM_TRUE;
	p_vin_data->out_port_def.dir = MM_DIR_OUTPUT;

	if (aic_msg_create(&p_vin_data->msg) < 0) {
		loge("aic_msg_create fail!");
		error = MM_ERROR_INSUFFICIENT_RESOURCES;
		goto _EXIT;
	}
	msg_create = 1;

	if (pthread_mutex_init(&p_vin_data->state_lock, NULL)) {
		loge("state_lock init fail!");
		goto _EXIT;
	}
	state_lock_init = 1;

	if (pthread_mutex_init(&p_vin_data->frame_lock, NULL)) {
		loge("frame_lock init fail!");
		goto _EXIT;
	}
	frame_lock_init = 1;

	/* init three lists: all nodes start in empty_list */
	mpp_list_init(&p_vin_data->empty_list);
	mpp_list_init(&p_vin_data->ready_list);
	mpp_list_init(&p_vin_data->processed_list);
	for (i = 0; i < VIN_FRAME_NUM; i++) {
		mpp_list_init(&p_vin_data->frame_nodes[i].list);
		mpp_list_add_tail(&p_vin_data->frame_nodes[i].list,
				  &p_vin_data->empty_list);
	}

	error = pthread_create(&p_vin_data->thread_id, NULL,
			       mm_vin_component_thread, p_vin_data);
	if (error || !p_vin_data->thread_id) {
		loge("pthread_create fail!");
		error = MM_ERROR_INSUFFICIENT_RESOURCES;
		goto _EXIT;
	}
	return error;

_EXIT:


	if (p_vin_data) {
		if (state_lock_init)
			pthread_mutex_destroy(&p_vin_data->state_lock);
		if (frame_lock_init)
			pthread_mutex_destroy(&p_vin_data->frame_lock);
		if (msg_create)
			aic_msg_destroy(&p_vin_data->msg);
		mpp_free(p_vin_data);
		p_vin_data = NULL;
	}
	return error;
}

static void mm_vin_event_notify(mm_vin_data *p_vin_data, MM_EVENT_TYPE event,
				u32 data1, u32 data2, void *p_event_data)
{
	if (p_vin_data && p_vin_data->p_callback &&
	    p_vin_data->p_callback->event_handler) {
		p_vin_data->p_callback->event_handler(p_vin_data->h_self,
						      p_vin_data->p_app_data,
						      event, data1, data2,
						      p_event_data);
	}
}

static void mm_vin_state_change_to_idle(mm_vin_data *p_vin_data)
{
	if (MM_STATE_EXECUTING != p_vin_data->state &&
	    MM_STATE_LOADED != p_vin_data->state) {
		mm_vin_event_notify(p_vin_data, MM_EVENT_ERROR,
				    MM_ERROR_INCORRECT_STATE_TRANSITION,
				    p_vin_data->state, NULL);
		return;
	}
	p_vin_data->state = MM_STATE_IDLE;
	mm_vin_event_notify(p_vin_data, MM_EVENT_CMD_COMPLETE,
			    MM_COMMAND_STATE_SET, p_vin_data->state, NULL);
}

static void mm_vin_state_change_to_loaded(mm_vin_data *p_vin_data)
{
	if (MM_STATE_IDLE != p_vin_data->state) {
		mm_vin_event_notify(p_vin_data, MM_EVENT_ERROR,
				    MM_ERROR_INCORRECT_STATE_TRANSITION,
				    p_vin_data->state, NULL);
		return;
	}
	p_vin_data->state = MM_STATE_LOADED;
	mm_vin_event_notify(p_vin_data, MM_EVENT_CMD_COMPLETE,
			    MM_COMMAND_STATE_SET, p_vin_data->state, NULL);
}

static void mm_vin_state_change_to_executing(mm_vin_data *p_vin_data)
{
	mm_video_port_def *video_def;

	if (MM_STATE_IDLE != p_vin_data->state) {
		mm_vin_event_notify(p_vin_data, MM_EVENT_ERROR,
				    MM_ERROR_INCORRECT_STATE_TRANSITION,
				    p_vin_data->state, NULL);
		loge("vin in wrong state %d", p_vin_data->state);
		return;
	}

	video_def = &p_vin_data->out_port_def.format.video;
	if (video_def->frame_width <= 0 || video_def->frame_height <= 0) {
		loge("wrong video params: w %d, h %d", video_def->frame_width,
		     video_def->frame_height);
		goto failed;
	}

	p_vin_data->frame_w = video_def->frame_width;
	p_vin_data->frame_h = video_def->frame_height;

	/* create OSD */
	p_vin_data->osd =
		aic_osd_create(p_vin_data->frame_w, p_vin_data->frame_h);
	if (!p_vin_data->osd) {
		loge("create osd failed");
		goto failed;
	}
	aic_osd_set_region(p_vin_data->osd, AIC_OSD_ID_TIME, 0, 0, NULL);
	aic_osd_enable_region(p_vin_data->osd, AIC_OSD_ID_TIME, 1);

	p_vin_data->state = MM_STATE_EXECUTING;
	return;

failed:
	mm_vin_event_notify(p_vin_data, MM_EVENT_ERROR,
			    MM_ERROR_INCORRECT_STATE_TRANSITION,
			    p_vin_data->state, NULL);
}

static int mm_vin_component_process_cmd(mm_vin_data *p_vin_data)
{
	s32 cmd = MM_COMMAND_UNKNOWN;
	struct aic_message message;
	s32 cmd_data = 0;

	if (aic_msg_get(&p_vin_data->msg, &message) == 0) {
		cmd = message.message_id;
		cmd_data = message.param;
		if (MM_COMMAND_STATE_SET == cmd) {
			pthread_mutex_lock(&p_vin_data->state_lock);
			if (p_vin_data->state == (MM_STATE_TYPE)(cmd_data)) {
				mm_vin_event_notify(p_vin_data, MM_EVENT_ERROR,
						    MM_ERROR_SAME_STATE, 0,
						    NULL);
				pthread_mutex_unlock(&p_vin_data->state_lock);
				goto CMD_EXIT;
			}
			switch ((MM_STATE_TYPE)(cmd_data)) {
			case MM_STATE_LOADED:
				mm_vin_state_change_to_loaded(p_vin_data);
				break;
			case MM_STATE_IDLE:
				mm_vin_state_change_to_idle(p_vin_data);
				break;
			case MM_STATE_EXECUTING:
				mm_vin_state_change_to_executing(p_vin_data);
				break;
			default:
				break;
			}
			pthread_mutex_unlock(&p_vin_data->state_lock);
		} else if (MM_COMMAND_STOP == cmd) {
			logi("mm_vin_component_thread ready to exit!!!");
			goto CMD_EXIT;
		}
	}

CMD_EXIT:
	return cmd;
}

static void *mm_vin_component_thread(void *p_thread_data)
{
	mm_vin_data *p_vin_data = (mm_vin_data *)p_thread_data;
	s32 cmd;
	struct vin_frame_node *node;

	while (1) {
		cmd = mm_vin_component_process_cmd(p_vin_data);
		if (MM_COMMAND_STATE_SET == cmd)
			continue;
		if (MM_COMMAND_STOP == cmd)
			break;

		if (p_vin_data->state != MM_STATE_EXECUTING) {
			aic_msg_wait_new_msg(&p_vin_data->msg, 5000);
			continue;
		}

		/* wait for a ready frame (Camera frame shared) */
		node = vin_wait_dequeue_ready(p_vin_data);
		if (!node) {
			aic_msg_wait_new_msg(&p_vin_data->msg, 0);
			continue;
		}

		/* OSD overlay */
		mm_vin_do_osd(p_vin_data, node);

		/* send to venc (node goes to processed_list) */
		vin_send_to_venc(p_vin_data, node);
	}

	return NULL;
}
