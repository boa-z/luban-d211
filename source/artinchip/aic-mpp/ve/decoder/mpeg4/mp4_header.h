/*
 * Copyright (C) 2020-2025 ArtInChip Technology Co. Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 *  author: <xiaodong.zhao@artinchip.com>
 *  Desc: mpeg4 header interface
 *
 */

#ifndef _MP4_HEADER_H_
#define _MP4_HEADER_H_

#include "mp4_vars.h"

extern int log2ceil(int arg);

/* All get*hdr functions return 0 if successful, -1 if header is not found, positive error code if an error happens */
extern int getvoshdr(mp4_stream_t *_ld, struct mp4_state *_mp4_state, int just_vol_init);
extern int getvsohdr(mp4_stream_t *_ld, struct mp4_state *_mp4_state);
extern int getvolhdr(mp4_stream_t *_ld, struct mp4_state *_mp4_state, int just_vol_init);
extern int getshvhdr(mp4_stream_t *_ld, struct mp4_state *_mp4_state);
extern int get_flv_pic_hdr(mp4_stream_t *_ld, struct mp4_state *_mp4_state);
extern int getgophdr(mp4_stream_t *_ld, struct mp4_state *_mp4_state);
extern int getvophdr(mp4_stream_t *_ld, struct mp4_state *_mp4_state);
extern int getvophdr_311(mp4_stream_t *_ld, struct mp4_state *_mp4_state);
extern int getpackethdr(mp4_stream_t *_ld, struct mp4_state *_mp4_state);

extern int get_use_intra_dc_vlc(int quantizer, int intra_dc_vlc_thr);

extern int nextbits(mp4_stream_t *_ld, int nbits);
extern int bytealign(mp4_stream_t *_ld);
extern int bytealigned(mp4_stream_t *_ld, int nbits);
extern void next_start_code(mp4_stream_t *_ld, struct mp4_state *_mp4_state);
extern void next_resync_marker(mp4_stream_t *ld, struct mp4_state *mp4_state);
extern int nextbits_bytealigned(mp4_stream_t *_ld, int nbit, int short_video_header);

extern int nextbits_resync_marker(mp4_stream_t *ld, struct mp4_state *mp4_state);
extern int get_resync_marker(mp4_stream_t *_ld, struct mp4_state *_mp4_state);
extern int check_sync_marker(mp4_stream_t *ld);

#endif // _MP4_HEADER_H_
