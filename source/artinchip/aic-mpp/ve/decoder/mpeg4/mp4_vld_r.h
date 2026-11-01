/*
 * Copyright (C) 2020-2025 ArtInChip Technology Co. Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 *  author: <xiaodong.zhao@artinchip.com>
 *  Desc: mpeg4 vldr interface
 *
 */

#ifndef _VLDR_H_
#define _VLDR_H_

#define ERR (65535) // 0xFFFF
#define ESC (7167)

extern event_t rvld_inter_dct(mp4_stream_t * _ld);
extern event_t rvld_intra_dct(mp4_stream_t * _ld);

#endif // _VLDR_H_
