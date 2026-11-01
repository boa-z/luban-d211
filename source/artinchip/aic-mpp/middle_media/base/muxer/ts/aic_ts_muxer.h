/*
 * Copyright (C) 2024-2026 ArtInChip Technology Co. Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Desc: aic_ts_muxer public API
 */

#ifndef __AIC_TS_MUXER_H__
#define __AIC_TS_MUXER_H__

#include "aic_muxer.h"

#ifdef __cplusplus
extern "C" {
#endif

s32 aic_ts_muxer_create(unsigned char *uri, struct aic_muxer **muxer);

#ifdef __cplusplus
}
#endif

#endif
