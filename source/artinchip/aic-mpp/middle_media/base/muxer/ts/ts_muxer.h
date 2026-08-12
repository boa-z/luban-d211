/*
 * Copyright (C) 2024-2026 ArtInChip Technology Co. Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Desc: aic_ts_muxer — MPEG-TS muxer
 */

#ifndef __TS_MUXER_H__
#define __TS_MUXER_H__

#include "aic_muxer.h"
#include "aic_stream.h"

struct aic_ts_muxer {
	struct aic_muxer base;
	struct aic_stream *stream;

	int video_type;
	int audio_type;
	int has_video;
	int has_audio;

	/* continuity counters */
	u8 cc_pat;
	u8 cc_pmt;
	u8 cc_sdt;
	u8 cc_video;
	u8 cc_audio;

	u32 pcr_base;
	u32 pcr_ext;
	u64 start_pts;
};

s32 ts_init(struct aic_ts_muxer *muxer, struct aic_av_media_info *info);
s32 ts_write_header(struct aic_ts_muxer *muxer);
s32 ts_write_packet(struct aic_ts_muxer *muxer, struct aic_av_packet *packet);
s32 ts_write_trailer(struct aic_ts_muxer *muxer);
s32 ts_close(struct aic_ts_muxer *muxer);

#endif
