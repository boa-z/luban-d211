/*
 * Copyright (C) 2020-2026 ArtInChip Technology Co. Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 *  author: <xiaodong.zhao@artinchip.com>
 *  Desc: mpeg4 decore interface
 *
 */

#ifndef _MP4_DECORE_H_
#define _MP4_DECORE_H_

#ifdef __cplusplus
extern "C" {
#endif

struct mpeg4_ctx;

// decore options
#define DEC_OPT_INIT           1
#define DEC_OPT_RELEASE        2
#define DEC_OPT_SETOUT         3
#define DEC_OPT_ADJUST         4
#define DEC_OPT_FRAME          5
#define DEC_OPT_INIT_VOL       6
#define DEC_OPT_FLUSH          7
#define DEC_OPT_VERSION        8
#define DEC_OPT_SETDEBUG       9
#define DEC_OPT_CONVERTYUV     10
#define DEC_OPT_CONVERTYV12    11
#define DEC_OPT_FLV_FRAME      12
#define DEC_OPT_RM_FRAME       13
#define DEC_OPT_WMV_FRAME      14

// decore return values
#define DEC_MEMORY             1
#define DEC_BAD_FORMAT         2
#define DEC_INVALID_ARGUMENT   3
#define DEC_NOT_IMPLEMENTED    4

#define DECORE_VERSION         20021112

#define DEC_ADJ_POSTPROCESSING 0
#define DEC_ADJ_BRIGHTNESS     1
#define DEC_ADJ_CONTRAST       2
#define DEC_ADJ_SATURATION     3
#define DEC_ADJ_WARMTH         4
#define DEC_ADJ_SET            0
#define DEC_ADJ_RETRIEVE       0x80000000

// fixme: add watermarking to the API doc
#include "mp4_portab.h"
#include <stddef.h>


typedef struct {
    // codec_version:
    //   = 311, DIVX311
    //   = 62, VP62
    //   = 12, TSCC
    //   = 412, DIVX 4.x
    //   = 500, DIVX 5.x
    uint32_t codec_version;
    uint32_t smooth_playback;
    uint32_t width;
    uint32_t height;
    uint32_t disp_factor;
    void *(*alloc)(size_t);
    void (*free)(void *);
} dec_init_t;

typedef struct {
    void *bmp;             // decoded bitmap
    const void *bitstream; // decoder buffer
    uint32_t length;       // length of the decoder stream
    uint32_t render_flag;  // 1: the frame is going to be rendered
    uint32_t stride;       // decoded bitmap stride, in pixels ( not bytes! is it good? )
    // rv_backend_init_params pInitParams;
    uint32_t skip_decoding;
    uint32_t if_flv_h263;
    uint32_t if_rm_h263;
    uint32_t rm_codec_id;
    uint32_t msmpeg_version;
    int extra_size;
    int rv_version;
    int rm_low_delay;
    int *data_offset;
    short *data_valid;
    short packet_num;
    int width;
    int height;
    int format_plus;
    int h263_aic;
    int slice_structured;
    int modified_qantization;
    int umv;
    int h263_ap;
    int deblocking;
    int rps;
    int isd;
    int aiv;
    unsigned int spo_extra;
    unsigned int stream_version;
    unsigned int majors_tream_version;
    unsigned int minor_stream_version;
    unsigned int num_resampled_image_sizes;
    unsigned int encode_size;
    unsigned int largest_pels;
    unsigned int largest_lines;
    int num_rpr_sizes;
    int fid;
    int is_rv8;
    double m_picture_clock_frequency;
    int tr_wrap;
    int packet_format;
    int multi_frame;
} dec_frame_t;

typedef struct {
    const char *quant_store;
    uint32_t quant_stride;
    uint32_t prediction_type;
    uint32_t frame_length;
    uint32_t frame_num;
    uint32_t vop_coded;

    void *y;
    void *u;
    void *v;
    uint32_t stride_y;
    uint32_t stride_uv;
} dec_frame_info_t;

typedef struct {
    uint32_t x_dim;
    uint32_t y_dim;
    uint32_t time_incr;
    uint32_t codec_version;
    uint32_t build_number;
    uint32_t prefixed;
} dec_vol_info_t;

enum PIC_STRUCTURE {
    TOP_FIELD = 1,
    BOTTOM_FIELD,
    FRAME,
};

int decore(struct mpeg4_ctx* s, int dec_opt, void *param1, void *param2);

#ifdef __cplusplus
}
#endif

#endif // _DECORE_H_
