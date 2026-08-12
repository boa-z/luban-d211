/*
 * Copyright (C) 2024-2026 ArtInChip Technology Co. Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Desc: aic_ts_muxer wrapper — implements struct aic_muxer vtable
 */

#define LOG_TAG "aic_ts_muxer"

#include <malloc.h>
#include <string.h>
#include <stddef.h>
#include <fcntl.h>
#include "aic_ts_muxer.h"
#include "ts_muxer.h"
#include "aic_stream.h"
#include "mpp_log.h"
#include "mpp_mem.h"

static s32 ts_muxer_destroy(struct aic_muxer *muxer)
{
	struct aic_ts_muxer *s = (struct aic_ts_muxer *)muxer;
	ts_close(s);
	mpp_free(muxer);
	return 0;
}

static s32 ts_muxer_init(struct aic_muxer *muxer,
			 struct aic_av_media_info *info)
{
	struct aic_ts_muxer *s = (struct aic_ts_muxer *)muxer;
	return ts_init(s, info);
}

static s32 ts_muxer_write_header(struct aic_muxer *muxer)
{
	struct aic_ts_muxer *s = (struct aic_ts_muxer *)muxer;
	return ts_write_header(s);
}

static s32 ts_muxer_write_packet(struct aic_muxer *muxer,
				 struct aic_av_packet *packet)
{
	struct aic_ts_muxer *s = (struct aic_ts_muxer *)muxer;
	return ts_write_packet(s, packet);
}

static s32 ts_muxer_write_trailer(struct aic_muxer *muxer)
{
	struct aic_ts_muxer *s = (struct aic_ts_muxer *)muxer;
	return ts_write_trailer(s);
}

s32 aic_ts_muxer_create(unsigned char *uri, struct aic_muxer **muxer)
{
	s32 ret = 0;
	struct aic_ts_muxer *ts_muxer = NULL;

	ts_muxer =
		(struct aic_ts_muxer *)mpp_alloc(sizeof(struct aic_ts_muxer));
	if (ts_muxer == NULL) {
		loge("mpp_alloc aic_ts_muxer failed");
		ret = -1;
		goto exit;
	}
	memset(ts_muxer, 0, sizeof(struct aic_ts_muxer));

	if (aic_stream_open((char *)uri, &ts_muxer->stream, O_RDWR | O_CREAT) <
	    0) {
		loge("stream open %s fail", uri);
		ret = -1;
		goto exit;
	}

	ts_muxer->base.init = ts_muxer_init;
	ts_muxer->base.destroy = ts_muxer_destroy;
	ts_muxer->base.write_header = ts_muxer_write_header;
	ts_muxer->base.write_packet = ts_muxer_write_packet;
	ts_muxer->base.write_trailer = ts_muxer_write_trailer;
	*muxer = &ts_muxer->base;
	return ret;

exit:
	if (ts_muxer) {
		if (ts_muxer->stream)
			aic_stream_close(ts_muxer->stream);
		mpp_free(ts_muxer);
	}
	return ret;
}
