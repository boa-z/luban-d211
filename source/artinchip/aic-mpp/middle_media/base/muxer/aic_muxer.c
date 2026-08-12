/*
 * Copyright (C) 2020-2026 ArtInChip Technology Co. Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 *  author: <che.jiang@artinchip.com>
 *  Desc: aic_muxer
 */

#include <string.h>
#include "aic_muxer.h"
#ifdef MP4_MUXER
#include "aic_mp4_muxer.h"
#endif
#ifdef TS_MUXER
#include "aic_ts_muxer.h"
#endif

s32 aic_muxer_create(unsigned char *uri, struct aic_muxer **muxer, enum aic_muxer_type type)
{
	if (uri == NULL) {
		loge("url is null.");
		return -1;
	}
#ifdef MP4_MUXER
	if (type == AIC_MUXER_TYPE_MP4) {
		return aic_mp4_muxer_create(uri, muxer);
	}
#endif
#ifdef TS_MUXER
	if (type == AIC_MUXER_TYPE_TS) {
		return aic_ts_muxer_create(uri, muxer);
	}
#endif
	loge("unkown muxer for (%s)", uri);
	return -1;
}
