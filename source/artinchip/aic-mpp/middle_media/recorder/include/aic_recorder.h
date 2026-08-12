/*
 * Copyright (C) 2020-2026 ArtInChip Technology Co. Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 *  author: <jun.ma@artinchip.com>
 *  Desc: aic recorder api
 */

#ifndef __AIC_RECORDER_H__
#define __AIC_RECORDER_H__

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

#include "aic_middle_media_common.h"
#include "mpp_dec_type.h"

struct aic_recorder;

#define AIC_RECORDER_PATH_LEN 256

struct aic_recorder_record {
	char file_path[AIC_RECORDER_PATH_LEN];
	char thumbnail_path[AIC_RECORDER_PATH_LEN];
	int64_t file_size;
	int duration;
	int64_t start_time;
	int64_t end_time;
	int locked;
	char user_data[128];
	char created_at[32];
};

struct aic_recorder_picture {
	char file_path[AIC_RECORDER_PATH_LEN];
	char thumbnail_path[AIC_RECORDER_PATH_LEN];
	int64_t file_size;
	int64_t timestamp;
	char created_at[32];
};

/** Unified query struct for recorder control queries.
 *  Set .max (0=all), .date (for date queries), then call control().
 *  On return, .count holds the actual number of results. */
struct aic_recorder_query {
	int max;
	union {
		struct aic_recorder_record  *records;
		struct aic_recorder_picture *pictures;
		void *data;
	};
	int count;
	const char *date;
};

struct video_encoding_config {
	enum mpp_codec_type codec_type;
	s32 out_width;
	s32 out_height;
	s32 out_bit_rate;
	s32 out_frame_rate;
	s32 out_qfactor;
	//now must be  out_width = in_width and out_height= in_height
	//case mjpeg encoder has no scale function
	s32 in_width;
	s32 in_height;
	s32 in_pix_fomat;
};

struct audio_encoding_config {
	enum aic_audio_codec_type codec_type;
	int out_bitrate;
	int out_samplerate;
	int out_channels;
	int out_bits_per_sample;

	int in_samplerate;
	int in_channels;
	int in_bits_per_sample;
};

struct aic_recorder_config {
	int file_duration; //unit:second one file duration
	int file_num; //0-loop, >0 record file_num and then stop recording.
	int file_muxer_type; //only support  mp4
	int qfactor;
	s8 has_video;
	s8 has_audio;
	struct audio_encoding_config audio_config;
	struct video_encoding_config video_config;
};

struct aic_recorder_frame {
	struct mpp_frame mpp_frame;
	void *vaddr[3];
};

struct aic_recorder_osd {
	bool enable;
	int id;
	int x;
	int y;
	char *text;
};

enum aic_recorder_event {
	AIC_RECORDER_EVENT_NEED_NEXT_FILE = 0,
	AIC_RECORDER_EVENT_COMPLETE, // when file_num > 0,record file_num then send this event
	AIC_RECORDER_EVENT_NO_SPACE,
	AIC_RECORDER_EVENT_RELEASE_VIDEO_FRAME, // notify app input_frame has used.
};

enum aic_recorder_command {
	AIC_RECORDER_CMD_SET_DEBUG_INFO,
	AIC_RECORDER_CMD_SET_OSD_TEXT,

	/* lock/unlock current recording (data: int* — 1=lock, 0=unlock) */
	AIC_RECORDER_CMD_SET_LOCK_RECORD,
	AIC_RECORDER_CMD_SET_USER_RECORD_DATA,
	AIC_RECORDER_CMD_SET_RECORD_DURATION,

	/* query: count all records (data: int* output) */
	AIC_RECORDER_CMD_GET_RECORD_COUNT,
	/* query: list records (data: struct aic_recorder_query*) */
	AIC_RECORDER_CMD_GET_RECORD_LIST,
	/* query: count records by date (data: struct aic_recorder_query*, date field required) */
	AIC_RECORDER_CMD_GET_RECORD_COUNT_BY_DATE,
	/* query: list records by date (data: struct aic_recorder_query*, date field required) */
	AIC_RECORDER_CMD_GET_RECORD_BY_DATE,
	/* query: count locked records (data: int* output) */
	AIC_RECORDER_CMD_GET_LOCKED_COUNT,
	/* query: list locked records (data: struct aic_recorder_query*) */
	AIC_RECORDER_CMD_GET_LOCKED_LIST,

	/* query: count all pictures (data: int* output) */
	AIC_RECORDER_CMD_GET_PICTURE_COUNT,
	/* query: list pictures (data: struct aic_recorder_query*) */
	AIC_RECORDER_CMD_GET_PICTURE_LIST,
	/* query: count pictures by date (data: struct aic_recorder_query*, date required) */
	AIC_RECORDER_CMD_GET_PICTURE_COUNT_BY_DATE,
	/* query: list pictures by date (data: struct aic_recorder_query*, date required) */
	AIC_RECORDER_CMD_GET_PICTURE_BY_DATE,
};


typedef s32 (*event_handler)(void *app_data, s32 event, s32 data1, s32 data2);
typedef s32 (*giveback_buffer)(void *app_data, s32 event, void *buffer);

struct aic_recorder *aic_recorder_create(void);

s32 aic_recorder_destroy(struct aic_recorder *recorder);

s32 aic_recorder_set_event_callback(struct aic_recorder *recorder,
				    void *app_data, event_handler event_handle);
s32 aic_recorder_set_buf_callback(struct aic_recorder *recorder,
				  giveback_buffer giveback_buf);

s32 aic_recorder_set_output_path(struct aic_recorder *recorder, char *path);

s32 aic_recorder_init(struct aic_recorder *recorder,
		      struct aic_recorder_config *recorder_config);

s32 aic_recorder_start(struct aic_recorder *recorder);

s32 aic_recorder_stop(struct aic_recorder *recorder);

s32 aic_recorder_send_frame(struct aic_recorder *recorder,
			    struct aic_recorder_frame *frame);

s32 aic_recorder_snapshot(struct aic_recorder *recorder);

s32 aic_recorder_control(struct aic_recorder *recorder,
			 enum aic_recorder_command cmd, void *data);

#ifdef __cplusplus
}
#endif /* End of #ifdef __cplusplus */

#endif
