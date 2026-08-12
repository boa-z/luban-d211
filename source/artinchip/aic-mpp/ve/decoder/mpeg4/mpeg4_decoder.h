/*
 * Copyright (C) 2020-2026 ArtInChip Technology Co. Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 *   author: <xiaodong.zhao@artinchip.com>
 *  Desc: mpeg decoder context
 *
 */

#ifndef _MPEG4_DECODER_H_
#define _MPEG4_DECODER_H_

#include "mpp_codec.h"
#include "mpp_dec_type.h"
#include "read_bits.h"
#include "ve_buffer.h"
#include "mp4_decore.h"
#include "mp4_vars.h"

// #define MPEG4_DUMP_ENABLE 1
// #define MPEG4_COMP_RES  1

extern struct mpeg4_ctx *g_mpeg4_ctx;

struct mpeg4_data_cache {
    int len;
    unsigned char *data;
};

struct mpeg4_ctx {
    struct mpp_decoder decoder;
    unsigned long regs_base;

    struct ve_buffer_allocator *ve_buf_handle;
    struct packet *curr_packet;
    struct frame *curr_frame;
    enum mpp_pixel_format pix_format; // output pixel format
    int extra_frame_num;
    int eos;

    int non_first_packet;
    dec_frame_t dec_frame;
    dec_init_t dec_init;

    int dec_frame_num;
    unsigned char* cmp_yuv_data;
    int cmp_error;

    // save intra DC coeff
    int intra_dc_coeff;

    int packet_num;
    int first_pic;

    int is_h263;
    reference_t ref;
    int pp_en;

    FILE *fp_reg;
    char pattern_name[128];

    struct mpeg4_data_cache data_cache;

#ifdef MPEG4_DUMP_ENABLE
    char pattern_dir_path[384];
    char ip_path[384];
    char asic_path[384];
    FILE *fp_es;
    FILE *fp_mb_coeff;
    FILE *fp_mb_cfg_data;
    FILE *fp_mb_info;
#endif
};

void mpeg_save_mb_coeff(struct mp4_state *mp4_state, short *block, int block_num);

#endif
