/*
 * Copyright (C) 2020-2025 ArtInChip Technology Co. Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 *  author: <xiaodong.zhao@artinchip.com>
 *  Desc: mpeg4 global interface
 *
 */

#ifndef _GLOBAL_H
#define _GLOBAL_H

// hardware video engine work mode
#define HW_DISABLE_MODE    0
#define HW_VLD_MODE        1
#define HW_IQIS_MODE       2
#define HW_IDCT_MC_MODE    3

#define SINGLE_STREAM_PROC 0
#define MULT_STREAMS_PROC  1

#define HW_GMC_MODE        1
#define SW_GMC_MODE        0

#define T2_IDCT_MODE       0
#define DIVX_IDCT_MODE     1

#define ERROR_POSSIBLE     0.0001

#define Bin2ASCII(a)       (a) >= 10 ? ((a) + 87) : ((a) + 48)
#define ASCII2Bin(a)       (a) >= 97 ? ((a) - 87) : ((a) - 48)

#ifndef MAX
#define MAX(a, b) (((a) > (b)) ? (a) : (b))
#endif

#ifndef MIN
#define MIN(a, b) (((a) < (b)) ? (a) : (b))
#endif

#define CLIP(a, i, s) (((a) > (s)) ? (s) : MAX(a, i))
#define SIGN(a)       ((a) < 0 ? -1 : 1)

extern int bOpen;
extern int bPlay;
extern int bStop;

extern unsigned long mp4_frame_ctr;

extern int do_idct_mode;

extern int mpeg_test4_init(void);
#endif
