/*
 * Copyright (C) 2020-2025 ArtInChip Technology Co. Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 *  author: author: <qi.xu@artinchip.com>
 *  Desc: jpeg register define
 */

#ifndef JPEG_REGISTER_H
#define JPEG_REGISTER_H

#include "ve_top_register.h"

#ifdef AIC_VE_DRV_V10
#include "jpeg_register_v1.h"
#else
#include "jpeg_register_v2.h"
#endif

#endif
