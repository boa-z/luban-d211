/*
 * Copyright (C) 2020-2026 Artinchip Technology Co. Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 *  author: <xiaodong.zhao@artinchip.com>
 *  Desc: aic_http_stream
 */

#ifndef __AIC_HTTP_STREAM_H__
#define __AIC_HTTP_STREAM_H__

#include "aic_stream.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

s32 http_stream_open(const char *uri, struct aic_stream **s, int flags);

#ifdef __cplusplus
}
#endif /* End of #ifdef __cplusplus */
#endif
