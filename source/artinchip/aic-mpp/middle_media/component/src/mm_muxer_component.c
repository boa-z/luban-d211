/*
 * Copyright (C) 2020-2026 ArtInChip Technology Co. Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 *  author: <che.jiang@artinchip.com>
 *  Desc: middle media muxer component
 */
#include "mm_muxer_component.h"
#include "aic_message.h"
#include "aic_muxer.h"
#include "aic_storage.h"
#include "mpp_encoder.h"
#include "mpp_list.h"
#include "mpp_log.h"
#include "mpp_mem.h"
#include "mpp_time.h"
#include <malloc.h>
#include <pthread.h>
#include <stddef.h>
#include <string.h>
#include <sys/prctl.h>
#include <sys/stat.h>

typedef struct mm_muxer_packet_info {
	u32 get_ok_num;
	u32 get_fail_num;
	u32 put_ok_num;
	u32 put_fail_num;
	u32 write_ok_num;
	u32 write_fail_num;
} mm_muxer_packet_info;

typedef struct mm_demuxer_perf_info {
	u64 tm_write_start;
	u64 tm_write_end;
	u64 total_write_tm;
	u32 total_cnt;
	u64 total_bytes;
	u64 last_write_tm;
} mm_demuxer_perf_info;

typedef struct mm_muxer_data {
	MM_STATE_TYPE state;
	pthread_mutex_t state_lock;
	mm_callback *p_callback;
	void *p_app_data;
	mm_handle h_self;
	mm_port_param port_param;

	mm_param_port_def in_port_def[2];
	mm_bind_info in_port_bind[2];

	pthread_t thread_id;
	struct aic_message_queue msg;

	struct aic_av_media_info media_info;
	u8 path[256];

	struct aic_muxer *p_muxer;

	u32 max_duration;
	u32 file_num;
	s32 muxer_type;

	int64_t last_pkt_pts;
	int64_t last_apkt_pts;
	s32 create_file_flag;
	MM_BOOL debug_en;
	MM_BOOL snapshot_en;
	mm_muxer_packet_info vpkt_info;
	mm_demuxer_perf_info perf_info;

	struct aic_storage *storage;
	char cur_file_path[512];
	int64_t cur_start_time;
} mm_muxer_data;

static void *mm_muxer_component_thread(void *p_thread_data);
static void mm_muxer_show_debug_info(mm_muxer_data *p_muxer_data);

static s32 mm_muxer_set_command(mm_handle h_component, MM_COMMAND_TYPE cmd,
				u32 param1, void *p_cmd_data)
{
	mm_muxer_data *p_muxer_data;
	s32 error = MM_ERROR_NONE;
	struct aic_message msg;
	p_muxer_data =
		(mm_muxer_data *)(((mm_component *)h_component)->p_comp_private);
	msg.message_id = cmd;
	msg.param = param1;
	msg.data_size = 0;

	aic_msg_put(&p_muxer_data->msg, &msg);
	return error;
}

static s32 mm_muxer_get_parameter(mm_handle h_component, MM_INDEX_TYPE index,
				  void *p_param)
{
	mm_muxer_data *p_muxer_data;
	s32 error = MM_ERROR_NONE;
	p_muxer_data =
		(mm_muxer_data *)(((mm_component *)h_component)->p_comp_private);

	switch (index) {
	case MM_INDEX_PARAM_PORT_DEFINITION: {
		mm_param_port_def *port = (mm_param_port_def *)p_param;
		if (port->port_index == MUX_PORT_VIDEO_INDEX) {
			memcpy(port,
			       &p_muxer_data->in_port_def[MUX_PORT_VIDEO_INDEX],
			       sizeof(mm_param_port_def));
		} else if (port->port_index == MUX_PORT_AUDIO_INDEX) {
			memcpy(port,
			       &p_muxer_data->in_port_def[MUX_PORT_AUDIO_INDEX],
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

static void set_media_info(mm_muxer_data *p_muxer_data, mm_param_port_def *port)
{
	struct aic_av_video_stream *video_stream;
	struct aic_av_audio_stream *audio_stream;
	mm_video_port_def *video_port;
	mm_audio_port_def *audio_port;

	if (port->port_index == MUX_PORT_VIDEO_INDEX) {
		p_muxer_data->media_info.has_video = 1;
		video_port = &port->format.video;
		video_stream = &p_muxer_data->media_info.video_stream;
		if (video_port->codec_type == MPP_CODEC_VIDEO_ENCODER_MJPEG)
			video_stream->codec_type = MPP_CODEC_VIDEO_ENCODER_MJPEG;
		else
			video_stream->codec_type =
				MPP_CODEC_VIDEO_ENCODER_H264;
		video_stream->width = video_port->frame_width;
		video_stream->height = video_port->frame_height;
		video_stream->frame_rate = video_port->framerate;
		video_stream->bit_rate = video_port->bitrate;
	} else if ((port->port_index == MUX_PORT_AUDIO_INDEX)) {
		p_muxer_data->media_info.has_audio = 1;
		audio_port = &port->format.audio;
		audio_stream = &p_muxer_data->media_info.audio_stream[0];
		audio_stream->codec_type = audio_port->codec_type;
		audio_stream->bit_rate = audio_port->bitrate;
		audio_stream->nb_channel = audio_port->channels;
		audio_stream->sample_rate = audio_port->sample_rate;
		audio_stream->bits_per_sample = 16;
	}
}

static s32 mm_muxer_set_parameter(mm_handle h_component, MM_INDEX_TYPE index,
				  void *p_param)
{
	mm_muxer_data *p_muxer_data;
	s32 error = MM_ERROR_NONE;

	p_muxer_data =
		(mm_muxer_data *)(((mm_component *)h_component)->p_comp_private);
	switch (index) {
	case MM_INDEX_PARAM_PORT_DEFINITION: {
		set_media_info(p_muxer_data, (mm_param_port_def *)p_param);
		break;
	}

	case MM_INDEX_PARAM_CONTENT_URI: {
		// file path
		mm_param_content_uri *p_contenturi =
			(mm_param_content_uri *)p_param;

		if (sizeof(p_muxer_data->path) < p_contenturi->size) {
			loge("record url size is too long %d.", p_contenturi->size);
			return MM_ERROR_BAD_PARAMETER;
		}
		memcpy(p_muxer_data->path, p_contenturi->content_uri,
				p_contenturi->size);
		break;
	}
	case MM_INDEX_VENDOR_MUXER_RECORD_FILE_INFO: {
		mm_param_record_file_info *p_record_file =
			(mm_param_record_file_info *)p_param;
		if (p_record_file->muxer_type != AIC_MUXER_TYPE_MP4) {
			loge("not suport muxer type, now only sup_port mp4");
			error = MM_ERROR_BAD_PARAMETER;
			break;
		}
		p_muxer_data->max_duration = p_record_file->duration;
		p_muxer_data->muxer_type = p_record_file->muxer_type;
		p_muxer_data->file_num = p_record_file->file_num;
		break;
	}

	case MM_INDEX_PARAM_PRINT_DEBUG_INFO: {
		p_muxer_data->debug_en = ((mm_param_u32 *)p_param)->u32;
		mm_muxer_show_debug_info(p_muxer_data);
		break;
	}

	case MM_INDEX_VENDOR_VIDEO_ENC_CAPTURE: {
		p_muxer_data->snapshot_en = MM_TRUE;
		break;
	}

	case MM_INDEX_VENDOR_STORAGE_HANDLE: {
		mm_param_storage_handle *p =
			(mm_param_storage_handle *)p_param;
		p_muxer_data->storage =
			(struct aic_storage *)p->handle;
		break;
	}
	default:
		break;
	}

	return error;
}

static s32 mm_muxer_get_config(mm_handle h_component, MM_INDEX_TYPE index,
			       void *p_config)
{
	s32 error = MM_ERROR_NONE;
	return error;
}

static s32 mm_muxer_set_config(mm_handle h_component, MM_INDEX_TYPE index,
			       void *p_config)
{
	s32 error = MM_ERROR_NONE;
	return error;
}

static s32 mm_muxer_get_vpacket(mm_muxer_data *p_muxer_data,
				struct mpp_packet *packet)
{
	struct mpp_encoder *encoder = NULL;
	mm_component *h_venc_comp = NULL;
	s32 ret = MM_ERROR_NONE;

	h_venc_comp =
		p_muxer_data->in_port_bind[MUX_PORT_VIDEO_INDEX].p_bind_comp;
	if (h_venc_comp == NULL) {
		loge("get h_venc_comp is null\n");
		return -MM_ERROR_NULL_POINTER;
	}

	ret = mm_get_parameter(h_venc_comp, MM_INDEX_PARAM_VIDEO_ENCODER_HANDLE,
			       (void *)&encoder);
	if (ret || encoder == NULL) {
		loge("get video encoder is null\n");
		return -MM_ERROR_NULL_POINTER;
	}

	ret = mpp_encoder_get_packet(encoder, packet);
	if (ret) {
		p_muxer_data->vpkt_info.get_fail_num++;
		return ret;
	}

	p_muxer_data->vpkt_info.get_ok_num++;
	return ret;
}

static s32 mm_muxer_put_vpacket(mm_muxer_data *p_muxer_data,
				struct mpp_packet *packet)
{
	struct mpp_encoder *encoder = NULL;
	mm_component *h_venc_comp = NULL;
	s32 ret = MM_ERROR_NONE;

	h_venc_comp =
		p_muxer_data->in_port_bind[MUX_PORT_VIDEO_INDEX].p_bind_comp;
	if (h_venc_comp == NULL) {
		logd("get h_venc_comp is null\n");
		return -MM_ERROR_NULL_POINTER;
	}

	ret = mm_get_parameter(h_venc_comp, MM_INDEX_PARAM_VIDEO_ENCODER_HANDLE,
			       (void *)&encoder);
	if (ret || encoder == NULL) {
		logd("get video encoder is null\n");
		return -MM_ERROR_NULL_POINTER;
	}
	ret = mpp_encoder_put_packet(encoder, packet);
	if (ret) {
		p_muxer_data->vpkt_info.put_fail_num++;
		return ret;
	}

	p_muxer_data->vpkt_info.put_ok_num++;
	return ret;
}

static s32 mm_muxer_get_state(mm_handle h_component, MM_STATE_TYPE *p_state)
{
	s32 error = MM_ERROR_NONE;
	mm_muxer_data *p_muxer_data;
	p_muxer_data =
		(mm_muxer_data *)(((mm_component *)h_component)->p_comp_private);

	pthread_mutex_lock(&p_muxer_data->state_lock);
	*p_state = p_muxer_data->state;
	pthread_mutex_unlock(&p_muxer_data->state_lock);
	return error;
}

static s32 mm_muxer_bind_request(mm_handle h_comp, u32 port,
				 mm_handle h_bind_port, u32 bind_port)
{
	s32 error = MM_ERROR_NONE;
	mm_param_port_def *p_port;
	mm_bind_info *p_bind_info;
	mm_muxer_data *p_muxer_data;

	p_muxer_data =
		(mm_muxer_data *)(((mm_component *)h_comp)->p_comp_private);
	if (p_muxer_data->state != MM_STATE_LOADED) {
		loge("Component is not in MM_STATE_LOADED,it is in%d,it can not tunnel\n",
		     p_muxer_data->state);
		return MM_ERROR_INVALID_STATE;
	}
	if (port == MUX_PORT_AUDIO_INDEX) {
		p_port = &p_muxer_data->in_port_def[MUX_PORT_AUDIO_INDEX];
		p_bind_info = &p_muxer_data->in_port_bind[MUX_PORT_AUDIO_INDEX];
	} else if (port == MUX_PORT_VIDEO_INDEX) {
		p_port = &p_muxer_data->in_port_def[MUX_PORT_VIDEO_INDEX];
		p_bind_info = &p_muxer_data->in_port_bind[MUX_PORT_VIDEO_INDEX];
	} else {
		loge("component can not find port :%d\n", port);
		return MM_ERROR_BAD_PARAMETER;
	}

	// cancel setup tunnel
	if (NULL == h_bind_port && 0 == bind_port) {
		p_bind_info->flag = MM_FALSE;
		p_bind_info->bind_port_index = bind_port;
		p_bind_info->p_bind_comp = h_bind_port;
		return MM_ERROR_NONE;
	}

	if (p_port->dir == MM_DIR_OUTPUT) {
		p_bind_info->bind_port_index = bind_port;
		p_bind_info->p_bind_comp = h_bind_port;
		p_bind_info->flag = MM_TRUE;
	} else if (p_port->dir == MM_DIR_INPUT) {
		mm_param_port_def bind_port_param;
		bind_port_param.port_index = bind_port;
		mm_get_parameter(h_bind_port, MM_INDEX_PARAM_PORT_DEFINITION,
				 &bind_port_param);
		if (bind_port_param.dir != MM_DIR_OUTPUT) {
			loge("both ports are input.\n");
			return MM_ERROR_PORT_NOT_COMPATIBLE;
		}

		p_bind_info->bind_port_index = bind_port;
		p_bind_info->p_bind_comp = h_bind_port;
		p_bind_info->flag = MM_TRUE;
	} else {
		loge("port is neither output nor input.\n");
		return MM_ERROR_PORT_NOT_COMPATIBLE;
	}
	return error;
}

static s32 mm_muxer_set_callback(mm_handle h_component, mm_callback *p_callback,
				 void *p_app_data)
{
	s32 error = MM_ERROR_NONE;
	mm_muxer_data *p_muxer_data;
	p_muxer_data =
		(mm_muxer_data *)(((mm_component *)h_component)->p_comp_private);
	p_muxer_data->p_callback = p_callback;
	p_muxer_data->p_app_data = p_app_data;
	return error;
}

static void mm_muxer_show_debug_info(mm_muxer_data *p_muxer_data)
{
	if (!p_muxer_data->debug_en)
		return;

	printf("**************************Muxer comp info***************************\n");
	printf("\nVideo:  get_ok    get_fail    put_ok    put_fail    wr_ok    wr_fail\n");
	printf("\t%6u    %8u    %6u    %8u    %5u    %7u\n",
	       p_muxer_data->vpkt_info.get_ok_num,
	       p_muxer_data->vpkt_info.get_fail_num,
	       p_muxer_data->vpkt_info.put_ok_num,
	       p_muxer_data->vpkt_info.put_fail_num,
	       p_muxer_data->vpkt_info.write_ok_num,
	       p_muxer_data->vpkt_info.write_fail_num);

	printf("\nstate: %s\n", mm_component_sta_to_str(p_muxer_data->state));
}

void mm_muxer_show_perf_info(mm_muxer_data *p_muxer_data, u32 len)
{
	if (!p_muxer_data->debug_en)
		return;

	u64 time_diff = 0;

	p_muxer_data->perf_info.total_write_tm +=
		(p_muxer_data->perf_info.tm_write_end -
		 p_muxer_data->perf_info.tm_write_start);
	p_muxer_data->perf_info.total_cnt++;
	p_muxer_data->perf_info.total_bytes += len;
	if (p_muxer_data->perf_info.total_cnt >= 90) {
		time_diff = p_muxer_data->perf_info.tm_write_end -
			    p_muxer_data->perf_info.last_write_tm;
		if (time_diff > 0) {
			printf("****************************Muxer video perf info****************************\n");
			printf("FPS    Bitrate(KB/S)    TotalTime(ms)    AvgSize    AvgWriteTime(ms)\n");
			printf("%3lu    %13lu    %13lu    %7lu    %16lu\n\n",
			       p_muxer_data->perf_info.total_cnt * 1000 /
				       time_diff,
			       p_muxer_data->perf_info.total_bytes / time_diff,
			       time_diff,
			       p_muxer_data->perf_info.total_bytes /
				       p_muxer_data->perf_info.total_cnt,
			       p_muxer_data->perf_info.total_write_tm /
				       p_muxer_data->perf_info.total_cnt);
		}

		p_muxer_data->perf_info.total_write_tm = 0;
		p_muxer_data->perf_info.total_cnt = 0;
		p_muxer_data->perf_info.total_bytes = 0;
		p_muxer_data->perf_info.last_write_tm =
			p_muxer_data->perf_info.tm_write_end;
	}
	if (p_muxer_data->perf_info.tm_write_end -
		    p_muxer_data->perf_info.tm_write_start >
	    80)
		printf("muxer: write one frame size %u cost too much time %lu ms\n",
		       len,
		       p_muxer_data->perf_info.tm_write_end -
			       p_muxer_data->perf_info.tm_write_start);
}

s32 mm_muxer_component_deinit(mm_handle h_component)
{
	s32 error = MM_ERROR_NONE;
	mm_component *p_comp;
	mm_muxer_data *p_muxer_data;
	struct aic_message msg;

	p_comp = (mm_component *)h_component;
	if (!p_comp || !p_comp->p_comp_private) {
		return MM_ERROR_INVALID_STATE;
	}
	p_muxer_data = (mm_muxer_data *)p_comp->p_comp_private;

	pthread_mutex_lock(&p_muxer_data->state_lock);
	if (p_muxer_data->state != MM_STATE_LOADED) {
		logd("compoent is in %d,but not in MM_STATE_LOADED(1),can ont FreeHandle.\n",
		     p_muxer_data->state);
		pthread_mutex_unlock(&p_muxer_data->state_lock);
		return MM_ERROR_INVALID_STATE;
	}
	pthread_mutex_unlock(&p_muxer_data->state_lock);

	msg.message_id = MM_COMMAND_STOP;
	msg.data_size = 0;
	aic_msg_put(&p_muxer_data->msg, &msg);
	pthread_join(p_muxer_data->thread_id, (void *)&error);

	pthread_mutex_destroy(&p_muxer_data->state_lock);
	aic_msg_destroy(&p_muxer_data->msg);

	if (p_muxer_data->p_muxer) {
		aic_muxer_destroy(p_muxer_data->p_muxer);
		p_muxer_data->p_muxer = NULL;
	}
	mpp_free(p_muxer_data);
	p_muxer_data = NULL;
	logd("mm_muxer_component_deinit\n");
	return error;
}

s32 mm_muxer_component_init(mm_handle h_component)
{
	mm_component *p_comp;
	mm_muxer_data *p_muxer_data;
	s32 error = MM_ERROR_NONE;
	mm_param_port_def *p_audio_port, *p_video_port;

	s8 msg_create = 0;
	s8 state_lock_init = 0;

	logd("mm_muxer_component_init....");

	p_comp = (mm_component *)h_component;

	p_muxer_data = (mm_muxer_data *)mpp_alloc(sizeof(mm_muxer_data));

	if (NULL == p_muxer_data) {
		loge("mpp_alloc(sizeof(MuxerDATATYPE) fail!\n");
		return MM_ERROR_INSUFFICIENT_RESOURCES;
	}

	memset(p_muxer_data, 0x0, sizeof(mm_muxer_data));
	p_comp->p_comp_private = (void *)p_muxer_data;
	p_muxer_data->state = MM_STATE_LOADED;
	p_muxer_data->h_self = p_comp;

	p_comp->set_callback = mm_muxer_set_callback;
	p_comp->send_command = mm_muxer_set_command;
	p_comp->get_state = mm_muxer_get_state;
	p_comp->get_parameter = mm_muxer_get_parameter;
	p_comp->set_parameter = mm_muxer_set_parameter;
	p_comp->get_config = mm_muxer_get_config;
	p_comp->set_config = mm_muxer_set_config;
	p_comp->bind_request = mm_muxer_bind_request;
	p_comp->deinit = mm_muxer_component_deinit;
	p_comp->send_buffer = NULL;
	p_comp->giveback_buffer = NULL;

	p_audio_port = &p_muxer_data->in_port_def[MUX_PORT_AUDIO_INDEX];
	p_video_port = &p_muxer_data->in_port_def[MUX_PORT_VIDEO_INDEX];

	p_audio_port->port_index = MUX_PORT_AUDIO_INDEX;
	p_audio_port->enable = MM_TRUE;
	p_audio_port->dir = MM_DIR_INPUT;

	p_video_port->port_index = MUX_PORT_VIDEO_INDEX;
	p_video_port->enable = MM_TRUE;
	p_video_port->dir = MM_DIR_INPUT;
	p_muxer_data->last_pkt_pts = -1;

	if (aic_msg_create(&p_muxer_data->msg) < 0) {
		loge("aic_msg_create fail!\n");
		error = MM_ERROR_INSUFFICIENT_RESOURCES;
		goto _EXIT;
	}
	msg_create = 1;
	if (pthread_mutex_init(&p_muxer_data->state_lock, NULL)) {
		loge("pthread_mutex_init fail!\n");
		goto _EXIT;
	}
	state_lock_init = 1;

	// Create the component thread
	error = pthread_create(&p_muxer_data->thread_id, NULL,
			       mm_muxer_component_thread, p_muxer_data);
	if (error || !p_muxer_data->thread_id) {
		loge("pthread_create fail!\n");
		error = MM_ERROR_INSUFFICIENT_RESOURCES;
		goto _EXIT;
	}
	return error;

_EXIT:
	if (state_lock_init)
		pthread_mutex_destroy(&p_muxer_data->state_lock);
	if (msg_create)
		aic_msg_destroy(&p_muxer_data->msg);

	if (p_muxer_data) {
		mpp_free(p_muxer_data);
		p_muxer_data = NULL;
		p_comp->p_comp_private = NULL;
	}
	return error;
}

static void mm_muxer_event_notify(mm_muxer_data *p_muxer_data,
				  MM_EVENT_TYPE event, u32 data1, u32 data2,
				  void *p_event_data)
{
	if (p_muxer_data && p_muxer_data->p_callback &&
	    p_muxer_data->p_callback->event_handler) {
		p_muxer_data->p_callback->event_handler(
			p_muxer_data->h_self, p_muxer_data->p_app_data, event,
			data1, data2, p_event_data);
	}
}

static void mm_muxer_state_change_to_invalid(mm_muxer_data *p_muxer_data)
{
	p_muxer_data->state = MM_STATE_INVALID;
	mm_muxer_event_notify(p_muxer_data, MM_EVENT_ERROR,
			      MM_ERROR_INVALID_STATE, 0, NULL);
	mm_muxer_event_notify(p_muxer_data, MM_EVENT_CMD_COMPLETE,
			      MM_COMMAND_STATE_SET, p_muxer_data->state, NULL);
}

static void mm_muxer_state_change_to_idle(mm_muxer_data *p_muxer_data)
{
	if (MM_STATE_EXECUTING == p_muxer_data->state) {
		// drain remaining encoded packets from encoder and write to muxer
		if (p_muxer_data->p_muxer) {
			struct mpp_packet vpkt;
			int ret;
			while ((ret = mm_muxer_get_vpacket(p_muxer_data,
							   &vpkt)) == 0) {
				struct aic_av_packet av_pkt;
				memset(&av_pkt, 0, sizeof(av_pkt));
				av_pkt.type = MPP_MEDIA_TYPE_VIDEO;
				av_pkt.data = vpkt.data;
				av_pkt.size = vpkt.size;
				if (vpkt.pts > 0)
					av_pkt.pts = vpkt.pts / 1000;
				else
					av_pkt.pts = mpp_get_time_ms();
				av_pkt.flag = vpkt.flag;

				if (aic_muxer_write_packet(
					    p_muxer_data->p_muxer, &av_pkt) ==
				    0) {
					p_muxer_data->vpkt_info.write_ok_num++;
				}
				mm_muxer_put_vpacket(p_muxer_data, &vpkt);
			}
			logd("drained to idle, cur_file_write_frame_num:%d",
			     p_muxer_data->vpkt_info.write_ok_num);

			aic_muxer_write_trailer(p_muxer_data->p_muxer);
			aic_muxer_destroy(p_muxer_data->p_muxer);
			p_muxer_data->p_muxer = NULL;

			if (p_muxer_data->storage &&
			    p_muxer_data->cur_file_path[0] != '\0') {
				struct stat st;
				int64_t file_size = 0;
				int64_t end_time = mpp_get_utc_time();

				if (stat(p_muxer_data->cur_file_path,
					 &st) == 0)
					file_size = st.st_size;

				aic_storage_record_file(
					p_muxer_data->storage,
					p_muxer_data->cur_file_path, "",
					p_muxer_data->cur_start_time,
					end_time,
					(int)(end_time -
					      p_muxer_data->cur_start_time),
					file_size, "");
			}
		}
	} else if (MM_STATE_PAUSE != p_muxer_data->state &&
		MM_STATE_LOADED != p_muxer_data->state) {
		mm_muxer_event_notify(p_muxer_data, MM_EVENT_ERROR,
				      MM_ERROR_INCORRECT_STATE_TRANSITION,
				      p_muxer_data->state, NULL);
		loge("muxer in wrong state %d.\n", p_muxer_data->state);
		return;
	}
	p_muxer_data->state = MM_STATE_IDLE;
	mm_muxer_event_notify(p_muxer_data, MM_EVENT_CMD_COMPLETE,
			      MM_COMMAND_STATE_SET, p_muxer_data->state, NULL);
}

static void mm_muxer_state_change_to_loaded(mm_muxer_data *p_muxer_data)
{
	if (MM_STATE_IDLE == p_muxer_data->state) {
		p_muxer_data->state = MM_STATE_LOADED;
		mm_muxer_event_notify(p_muxer_data, MM_EVENT_CMD_COMPLETE,
				      MM_COMMAND_STATE_SET, p_muxer_data->state,
				      NULL);
	} else {
		mm_muxer_event_notify(p_muxer_data, MM_EVENT_ERROR,
				      MM_ERROR_INCORRECT_STATE_TRANSITION,
				      p_muxer_data->state, NULL);
		loge("muxer in wrong state %d.\n", p_muxer_data->state);
	}
}

static void mm_muxer_state_change_to_executing(mm_muxer_data *p_muxer_data)
{
	if (MM_STATE_IDLE != p_muxer_data->state &&
	    MM_STATE_PAUSE != p_muxer_data->state) {
		mm_muxer_event_notify(p_muxer_data, MM_EVENT_ERROR,
				      MM_ERROR_INCORRECT_STATE_TRANSITION,
				      p_muxer_data->state, NULL);
		loge("muxer in wrong state %d.\n", p_muxer_data->state);
		return;
	}
	p_muxer_data->state = MM_STATE_EXECUTING;
}

static void mm_muxer_state_change_to_pause(mm_muxer_data *p_muxer_data)
{
	if (MM_STATE_EXECUTING != p_muxer_data->state) {
		mm_muxer_event_notify(p_muxer_data, MM_EVENT_ERROR,
				      MM_ERROR_INCORRECT_STATE_TRANSITION,
				      p_muxer_data->state, NULL);
		loge("muxer in wrong state %d.\n", p_muxer_data->state);
		return;
	}
	p_muxer_data->state = MM_STATE_PAUSE;
}

static int mm_muxer_component_process_cmd(mm_muxer_data *p_muxer_data)
{
	s32 cmd = MM_COMMAND_UNKNOWN;
	s32 cmd_data;
	struct aic_message message;

	if (aic_msg_get(&p_muxer_data->msg, &message) == 0) {
		cmd = message.message_id;
		cmd_data = message.param;
		logi("cmd:%d, cmd_data:%d\n", cmd, cmd_data);
		if (MM_COMMAND_STATE_SET == cmd) {
			pthread_mutex_lock(&p_muxer_data->state_lock);
			if (p_muxer_data->state == (MM_STATE_TYPE)(cmd_data)) {
				mm_muxer_event_notify(p_muxer_data,
						      MM_EVENT_ERROR,
						      MM_ERROR_SAME_STATE, 0,
						      NULL);
				pthread_mutex_unlock(&p_muxer_data->state_lock);
				goto CMD_EXIT;
			}
			switch ((MM_STATE_TYPE)(cmd_data)) {
			case MM_STATE_INVALID:
				mm_muxer_state_change_to_invalid(p_muxer_data);
				break;
			case MM_STATE_LOADED:
				mm_muxer_state_change_to_loaded(p_muxer_data);
				break;
			case MM_STATE_IDLE:
				mm_muxer_state_change_to_idle(p_muxer_data);
				break;
			case MM_STATE_EXECUTING:
				mm_muxer_state_change_to_executing(
					p_muxer_data);
				break;
			case MM_STATE_PAUSE:
				mm_muxer_state_change_to_pause(p_muxer_data);
				break;
			default:
				break;
			}
			pthread_mutex_unlock(&p_muxer_data->state_lock);
		} else if (MM_COMMAND_STOP == cmd) {
			logi("mm_muxer_component_thread ready to exit!!!\n");
			goto CMD_EXIT;
		}
	}

CMD_EXIT:
	return cmd;
}

static int aic_muxer_create_new_file(mm_muxer_data *p_muxer_data)
{
	unsigned char file_path[512] = { 0 };
	s32 ret = MM_ERROR_NONE;
	struct tm local_tm;

	if (strlen((char *)p_muxer_data->path) <= 0) {
		loge("user not set output path.");
		return -1;
	}

	// close current file and record to storage
	if (p_muxer_data->p_muxer) {
		aic_muxer_write_trailer(p_muxer_data->p_muxer);
		aic_muxer_destroy(p_muxer_data->p_muxer);
		p_muxer_data->p_muxer = NULL;

		if (p_muxer_data->storage &&
		    p_muxer_data->cur_file_path[0] != '\0') {
			struct stat st;
			int64_t file_size = 0;
			int64_t end_time = mpp_get_utc_time();

			if (stat(p_muxer_data->cur_file_path, &st) == 0)
				file_size = st.st_size;

			aic_storage_record_file(p_muxer_data->storage,
					       p_muxer_data->cur_file_path,
					       "", /* thumbnail_path */
					       p_muxer_data->cur_start_time,
					       end_time,
					       (int)(end_time -
						     p_muxer_data->cur_start_time),
					       file_size, "");
		}
	}
	p_muxer_data->vpkt_info.write_ok_num = 0;

	mpp_get_local_tm(&local_tm);
	snprintf((char *)file_path, sizeof(file_path),
		 "%s%04d%02d%02d_%02d%02d%02d.mp4", p_muxer_data->path,
		 1900 + local_tm.tm_year, 1 + local_tm.tm_mon, local_tm.tm_mday,
		 local_tm.tm_hour, local_tm.tm_min, local_tm.tm_sec);

	/* track new file path and start time */
	strncpy(p_muxer_data->cur_file_path, (char *)file_path,
		sizeof(p_muxer_data->cur_file_path) - 1);
	p_muxer_data->cur_start_time = mpp_get_utc_time();

	printf("set recorder file:%s\n", file_path);
	ret = aic_muxer_create(file_path, &p_muxer_data->p_muxer,
			       p_muxer_data->muxer_type);
	if (ret != 0 || !p_muxer_data->p_muxer) {
		loge("aic_muxer_create error %d.", ret);
		return -1;
	}
	p_muxer_data->media_info.duration = p_muxer_data->max_duration;
	ret = aic_muxer_init(p_muxer_data->p_muxer, &p_muxer_data->media_info);
	if (ret != 0) {
		loge("aic_muxer_init error %d.", ret);
		goto failed;
	}
	ret = aic_muxer_write_header(p_muxer_data->p_muxer);
	if (ret != 0) {
		loge("aic_muxer_write_header error %d.", ret);
		goto failed;
	}

	return 0;

failed:
	aic_muxer_destroy(p_muxer_data->p_muxer);
	p_muxer_data->p_muxer = NULL;
	return ret;
}

static int mm_muxer_snapshot(mm_muxer_data *p_muxer_data, struct mpp_packet *vpkt)
{
	char file_path[512] = { 0 };
	struct tm local_tm;
	FILE *fp;

	if (!p_muxer_data || !vpkt)
		return -1;

	if (MM_FALSE == p_muxer_data->snapshot_en) {
		return 0;
	}

	if (strlen((char *)p_muxer_data->path) <= 0) {
		loge("user not set output path.");
		return -1;
	}

	mpp_get_local_tm(&local_tm);
	snprintf(file_path, sizeof(file_path),
		 "%s%04d%02d%02d_%02d%02d%02d.jpg", p_muxer_data->path,
		 1900 + local_tm.tm_year, 1 + local_tm.tm_mon, local_tm.tm_mday,
		 local_tm.tm_hour, local_tm.tm_min, local_tm.tm_sec);

	printf("set snapshot file:%s\n", file_path);

	fp = fopen(file_path, "wb");
	if (fp == NULL) {
		loge("fopen %s error\n", file_path);
		return MM_ERROR_NULL_POINTER;
	}
	fwrite(vpkt->data, 1, vpkt->size, fp);
	fflush(fp);
	fclose(fp);
	p_muxer_data->snapshot_en = MM_FALSE;

	if (p_muxer_data->storage) {
		aic_storage_record_picture(p_muxer_data->storage,
					  file_path, "",
					  (int64_t)mpp_get_utc_time(),
					  vpkt->size);
	}

	return MM_ERROR_NONE;
}


static s32 mm_muxer_process_vpkt(mm_muxer_data *p_muxer_data)
{
	struct aic_av_packet av_pkt;
	struct mpp_packet vpkt;
	s32 ret = MM_ERROR_NONE;
	if (!p_muxer_data->p_muxer)
		return -1;

	/*1. get encoded packet from encoder*/
	ret = mm_muxer_get_vpacket(p_muxer_data, &vpkt);
	if (ret == ENC_NO_READY_PACKET) {
		p_muxer_data->vpkt_info.get_fail_num++;
		return -1;
	} else if (ret < 0) {
		logd("get video packet failed %d.", ret);
		p_muxer_data->vpkt_info.get_fail_num++;
		return -1;
	}
	p_muxer_data->vpkt_info.get_ok_num++;
	mm_muxer_snapshot(p_muxer_data, &vpkt);

	/*2. convert mpp_packet to aic_av_packet*/
	memset(&av_pkt, 0, sizeof(av_pkt));
	av_pkt.data = vpkt.data;
	av_pkt.size = vpkt.size;
	if (vpkt.pts > 0)
		av_pkt.pts = vpkt.pts / 1000;
	else
		av_pkt.pts = mpp_get_time_ms();
	av_pkt.flag = vpkt.flag;
	av_pkt.type = MPP_MEDIA_TYPE_VIDEO;


	/*3. duration tracking for file splitting*/
	if (p_muxer_data->last_pkt_pts <= 0) {
		p_muxer_data->last_pkt_pts = av_pkt.pts;
	}

	logd("avpkt: size: %d, pts: %ld, last_pts: %ld.",
		av_pkt.size, av_pkt.pts, p_muxer_data->last_pkt_pts);
	if (av_pkt.pts - p_muxer_data->last_pkt_pts >
		p_muxer_data->media_info.duration * 1000) {
		p_muxer_data->create_file_flag = 1;
		logd("create new file.");
	}

	/*4. write packet to muxer*/
	p_muxer_data->perf_info.tm_write_start = mpp_get_time_ms();
	ret = aic_muxer_write_packet(p_muxer_data->p_muxer, &av_pkt);
	if (ret == 0) {
		p_muxer_data->vpkt_info.write_ok_num++;
		p_muxer_data->perf_info.tm_write_end = mpp_get_time_ms();
		mm_muxer_show_perf_info(p_muxer_data, av_pkt.size);
	} else if (ret == -2) { // AIC_NO_SPACE
		p_muxer_data->vpkt_info.write_fail_num++;
		loge("AIC_NO_SPACE\n");
	} else {
		p_muxer_data->vpkt_info.write_fail_num++;
		loge("other error\n");
	}

	/*4. put packet to encoder*/
	mm_muxer_put_vpacket(p_muxer_data, &vpkt);

	return MM_ERROR_NONE;
}

static s32 mm_muxer_process_apkt(mm_muxer_data *p_muxer_data)
{
	return MM_ERROR_NONE;
}

static void *mm_muxer_component_thread(void *p_thread_data)
{
	mm_muxer_data *p_muxer_data = (mm_muxer_data *)p_thread_data;
	s32 cmd = MM_COMMAND_UNKNOWN;
	s32 ret = MM_ERROR_NONE;

	p_muxer_data->create_file_flag = 1;

	while (1) {
		/* process cmd and change state */
		cmd = mm_muxer_component_process_cmd(p_muxer_data);
		if (MM_COMMAND_STATE_SET == cmd) {
			continue;
		} else if (MM_COMMAND_STOP == cmd) {
			goto _EXIT;
		}

		if (p_muxer_data->state != MM_STATE_EXECUTING) {
			aic_msg_wait_new_msg(&p_muxer_data->msg, 0);
			continue;
		}

		// process data
		if (p_muxer_data->create_file_flag) {
			if (0 != aic_muxer_create_new_file(p_muxer_data)) {
				goto _EXIT;
			}
			p_muxer_data->create_file_flag = 0;
			p_muxer_data->last_pkt_pts = -1;
		}

		ret = mm_muxer_process_vpkt(p_muxer_data);
		ret |= mm_muxer_process_apkt(p_muxer_data);

		if (ret)
			aic_msg_wait_new_msg(&p_muxer_data->msg, 5000);
	}
_EXIT:
	mm_muxer_state_change_to_invalid(p_muxer_data);
	return (void *)MM_ERROR_NONE;
}
