/*
 * Copyright (C) 2020-2025 ArtInChip Technology Co. Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 *  author: <xiaodong.zhao@artinchip.com>
 *  Desc: mpeg4 mblock util interface
 *
 */

#ifndef _MP4_MBLOCK_UTIL_H_
#define _MP4_MBLOCK_UTIL_H_

extern int get_mcbpc_i_vop(mp4_stream_t *_ld);
extern int get_mcbpc_p_vop(mp4_stream_t *_ld);
extern int get_cbpy(mp4_stream_t *_ld, int intraFlag);

extern int set_mv(mp4_stream_t *_ld, struct mp4_state *_mp4_state, int mb_xpos, int mb_ypos, int block_num);
extern int get_mv_data(mp4_stream_t *_ld);

extern int set_mv_interlaced(mp4_stream_t *ld, struct mp4_state *mp4_state, int mb_xpos, int mb_ypos, int field);

#endif // _MP4_MBLOCK_UTIL_H_
