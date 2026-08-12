/*
 * Copyright (C) 2020-2026 ArtInChip Technology Co. Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 *  author: <jianye.liang@artinchip.com>
 *  Desc: aic mpg parser
 */

#ifndef __AIC_MPG_PARSER_H__
#define __AIC_MPG_PARSER_H__

#include "aic_parser.h"
#include "aic_stream.h"

#ifdef __cplusplus
extern "C" {
#endif

s32 aic_mpg_parser_create(unsigned char *uri, struct aic_parser **parser);

#ifdef __cplusplus
}
#endif

#endif
