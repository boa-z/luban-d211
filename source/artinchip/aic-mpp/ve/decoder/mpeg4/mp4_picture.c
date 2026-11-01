/*
 * Copyright (C) 2020-2026 ArtInChip Technology Co. Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 *  author: <xiaodong.zhao@artinchip.com>
 *  Desc: mpeg4 picture interface
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mp4_dpart.h"
#include "mp4_getbits.h"
#include "mp4_global.h"
#include "mp4_header.h"
#include "mp4_mblock.h"
#include "mp4_mblock_bvop.h"
#include "mp4_vars.h"
#include "mpeg_register.h"
#include "mpeg4_decoder.h"

extern struct mpeg4_ctx *g_mpeg4_ctx;
void make_edge(unsigned char *frame_pic, int width, int height, int stride, int edge);
void make_edge_16x8(unsigned char *frame_pic, int width, int height, int stride, int hor_edge, int ver_edge);
extern void make_edge_311(unsigned char *frame_pic, int width, int height, int stride, int edge);

extern void mp4_set_mb_info(struct mp4_state *_mp4_state);
extern void mp4_set_packet_info(struct mp4_state *_mp4_state);
extern void mp4_save_notcoded_flag(struct mp4_state *_mp4_state);
extern void mp4_backup_mbh_info(struct mp4_state *_mp4_state);
extern void mp4_save_sw_recon_result(reference_t *ref);
extern void mp4_update_info(struct mp4_state *_mp4_state);

static inline int combined_motion_shape_texture(reference_t *ref)
{
    struct mp4_state *mp4_state = ref->mp4_state;
    mp4_stream_t *ld = ref->ld;

    if ((mp4_state->hdr.prediction_type == P_VOP) || (mp4_state->hdr.prediction_type == S_VOP)) {
        if (mp4_state->hdr.prediction_type == S_VOP)
            if (mp4_state->hdr.sprite_usage != GMC_SPRITE) {
                loge("sprite_usage != GMC_SPRITE");
                return 0;
            }

        do {
            memset(mp4_state->mpeg_coef_matrix, 0, 6 * 64 * sizeof(unsigned long));

            for (int j = 0; j < 6; j++) {
                mp4_state->mpeg_coef_matrix_no[j] = 0;
            }

            if (macroblock_p_vop(ref)) {
                // return 0 if derived_mb_type is stuffing
                mp4_set_mb_info(mp4_state);
                mp4_state->hdr.mba++;
            } else {
                loge("stuffing mb, do not deal it");
            }
        } while ((!check_sync_marker(ld)) && (mp4_state->hdr.resync_marker_disable || (nextbits_resync_marker(ld, mp4_state) != 1)) &&
                 (mp4_state->hdr.mba < mp4_state->hdr.mba_size));
    } else if (mp4_state->hdr.prediction_type == I_VOP) {
        do {
            memset(mp4_state->mpeg_coef_matrix, 0, 6 * 64 * sizeof(unsigned long));
            for (int j = 0; j < 6; j++) {
                mp4_state->mpeg_coef_matrix_no[j] = 0;
            }

            if (macroblock_i_vop(ref)) { // return 0 if derived_mb_type is stuffing
                mp4_set_mb_info(mp4_state);

                mp4_state->hdr.mba++;
            } else {
                loge("stuffing mb, do not deal it");
            }
        } while ((nextbits_bytealigned(ld, 23, 0) != 0) && (nextbits_resync_marker(ld, mp4_state) != 1) &&
                 (mp4_state->hdr.mba < mp4_state->hdr.mba_size));
    } else { // B_VOP
        do {
            memset(mp4_state->mpeg_coef_matrix, 0, 6 * 64 * sizeof(unsigned long));
            for (int j = 0; j < 6; j++) {
                mp4_state->mpeg_coef_matrix_no[j] = 0;
            }

            if (macroblock_b_vop(ref)) { // return 0 if derived_mb_type is stuffing
                mp4_set_mb_info(mp4_state);

                mp4_state->hdr.mba++;
            } else {
                loge("stuffing mb, do not deal it");
            }

        } while ( // Include processing NOT CODEC MB at the end of a packet (Berg Xing, Oct 10, 2003)
            ((nextbits_bytealigned(ld, 23, 0) != 0) && (nextbits_resync_marker(ld, mp4_state) != 1) &&
             (mp4_state->hdr.mba < mp4_state->hdr.mba_size)) ||
            ((mp4_state->codedmap[mp4_state->hdr.mb_ypos * mp4_state->codedmap_stride + mp4_state->hdr.mb_xpos] == 1) &&
             (mp4_state->hdr.old_prediction_type != S_VOP) && (mp4_state->hdr.mba < mp4_state->hdr.mba_size)));
    }

    return 1;
}

static inline int datapart_combined_motion_shape_texture(reference_t *ref)
{
    struct mp4_state *mp4_state = ref->mp4_state;

    if (mp4_state->hdr.prediction_type == I_VOP) {
        return data_partitioned_i_vop(ref);
    } else if ((mp4_state->hdr.prediction_type == P_VOP) || (mp4_state->hdr.prediction_type == S_VOP)) {
        return data_partitioned_p_vop(ref);
    } else if (mp4_state->hdr.prediction_type == B_VOP) {
        return combined_motion_shape_texture(ref);
    }

    return 0; // quiet compiler
}


void set_frame_to_display(reference_t *ref)
{
    struct mp4_state *mp4_state = ref->mp4_state;
    if (mp4_state->if_flv_h263) {
    }
    mp4_state->frame_to_decode = (mp4_state->hdr.prediction_type == B_VOP) ? ref->frame_ref : ref->frame_back;

    if (!mp4_state->history_prefixed) {
        switch (mp4_state->hdr.prediction_type) {
        case B_VOP:
            mp4_state->frame_to_display = ref->frame_ref;
            break;
        default:
            mp4_state->frame_to_display = ref->frame_back;
            break;
        }

        // handle special case in which B frames are suddenly encountered: start delaying after that
        // sem_1 - bvop_delay_activated
        // sem_2 - bvop_delay_completed
        if ((mp4_state->hdr.prediction_type == B_VOP) && (mp4_state->bvop_delay_activated == 0)) {
            mp4_state->bvop_delay_activated = 1;
            // display the forward frame until the next P-VOP appears,
            // then switcht to B-VOP display mode [Review] dances!
            mp4_state->frame_to_display = ref->frame_for;
        } else {
            if (mp4_state->bvop_delay_completed == 1) {
                mp4_state->frame_to_display = (mp4_state->hdr.prediction_type == B_VOP) ? (ref->frame_ref) : (ref->frame_for);
            } else {
                if (((mp4_state->hdr.prediction_type == P_VOP) || (mp4_state->hdr.prediction_type == S_VOP)) &&
                    (mp4_state->bvop_delay_activated == 1)) {
                    mp4_state->bvop_delay_completed = 1;
                }
            }
        }
    } else if (mp4_state->flag_smooth_playback) {
        // [smooth]
        switch (mp4_state->hdr.prediction_type) {
        case I_VOP:
            // [Review] This should happen only at the very first frame
            mp4_state->frame_to_display = ref->frame_back;
            break;
        case B_VOP:
            mp4_state->frame_to_display = ref->frame_ref; // decode the B frame right away
            break;
        default:
            mp4_state->frame_to_display = ref->frame_for; // when I receive a P frame, cause the delay, I display the previous frame
            break;
        }
    } else {
        // [prefix]
        switch (mp4_state->hdr.prediction_type) {
        case I_VOP:
            mp4_state->frame_to_display = ref->frame_back;
            break;
        case B_VOP:
            mp4_state->frame_to_display = ref->frame_ref;
            loge("B frame\n");
            break;
        default:
            mp4_state->frame_to_display = ref->frame_for; // do not render it
            break;
        }
    }
}

int do_checksumming_decore = 0;
int last_checksum_y_decore = 0, last_checksum_uv_decore = 0;

// Purpose: render the last VOP present in the frame buffer
int get_notcoded_mp4picture(reference_t *ref, unsigned char *bmp, unsigned int stride, int render_flag)
{
    struct mp4_state *mp4_state = ref->mp4_state;
    if (mp4_state->disp_factor == 1) {
    } else {
        unsigned char *sptr, *dptr;
        int i, j;
        int y_size, c_size;
        y_size = (mp4_state->vertical_size / mp4_state->disp_factor) * (mp4_state->horizontal_size / mp4_state->disp_factor);
        c_size = y_size >> 2;
        dptr = bmp;
        sptr = ref->frame_for[0];
        for (i = 0; i < mp4_state->vertical_size; i += mp4_state->disp_factor) {
            for (j = 0; j < mp4_state->horizontal_size; j += mp4_state->disp_factor) {
                *dptr++ = *(sptr + j);
            }
            sptr += mp4_state->coded_picture_width * mp4_state->disp_factor;
        }
        dptr = bmp + y_size;
        sptr = ref->frame_for[1];
        for (i = 0; i < mp4_state->vertical_size / 2; i += mp4_state->disp_factor) {
            for (j = 0; j < mp4_state->horizontal_size / 2; j += mp4_state->disp_factor) {
                *dptr++ = *(sptr + j);
            }
            sptr += (mp4_state->coded_picture_width >> 1) * mp4_state->disp_factor;
        }
        dptr = bmp + y_size + c_size;
        sptr = ref->frame_for[2];
        for (i = 0; i < mp4_state->vertical_size / 2; i += mp4_state->disp_factor) {
            for (j = 0; j < mp4_state->horizontal_size / 2; j += mp4_state->disp_factor) {
                *dptr++ = *(sptr + j);
            }
            sptr += (mp4_state->coded_picture_width >> 1) * mp4_state->disp_factor;
        }
    }
    return 0;
}

static inline void init_macroblock_params(struct mp4_state *mp4_state)
{
    mp4_state->hdr.mba = 0;
    mp4_state->hdr.macroblock_number = 0;
    mp4_state->hdr.mb_xpos = 0;
    mp4_state->hdr.mb_ypos = 0;
    mp4_state->hdr.packetnum = 0;
    mp4_state->hdr.gob_number = 0;
    mp4_state->hdr.mb_xsize = (mp4_state->hdr.width + 15) >> 4;
    mp4_state->hdr.mb_ysize = (mp4_state->hdr.height + 15) >> 4;
}

static inline void process_combined_motion_shape_texture(reference_t *ref, mp4_stream_t *ld, struct mp4_state *mp4_state)
{
    combined_motion_shape_texture(ref);
    mp4_state->hdr.packetnum++;

    while ((nextbits_resync_marker(ld, mp4_state) == 1) && (mp4_state->hdr.mba < mp4_state->hdr.mba_size)) {
        getpackethdr(ld, mp4_state);
        mark_packet_boundary(mp4_state, mp4_state->hdr.macroblock_number);

        combined_motion_shape_texture(ref);
        mp4_state->hdr.packetnum++;
    }
}

static inline void process_data_partitioned_stream(reference_t *ref, mp4_stream_t *ld, struct mp4_state *mp4_state)
{
    if (!datapart_combined_motion_shape_texture(ref)) {
        // there was an error: resynch!
        while ((nextbits_resync_marker(ld, mp4_state) != 1) &&
               (ld->startptr + ld->length > ld->rdptr)) {
            getbits1(ld);
        }
        mp4_state->error_flag = 1;
        mp4_state->hdr.mb_xpos = 0;
        mp4_state->hdr.mb_ypos = 0;
        mp4_state->hdr.mba = 0;

        // smart recover from the error here
        // ...
    } else {
        mp4_state->error_flag = 0;
    }
    mp4_state->hdr.packetnum++;

    while (nextbits_resync_marker(ld, mp4_state) == 1 && (mp4_state->hdr.mba < mp4_state->hdr.mba_size)) {
        if (getpackethdr(ld, mp4_state) != 0)
            return; // if the header is wrong the error can't be recovered

        // recover from the error, reset mb number references
        if (mp4_state->error_flag == 1) {
            mp4_state->hdr.mba = mp4_state->hdr.macroblock_number;
        } else if (mp4_state->hdr.macroblock_number != mp4_state->hdr.mba) {
            // wrong header? quick fix, hope it will not crash
            mp4_state->hdr.mba = mp4_state->hdr.macroblock_number;
        }
        mark_packet_boundary(mp4_state, mp4_state->hdr.macroblock_number);

        if (!datapart_combined_motion_shape_texture(ref)) {
            // there was an error: resynch!
            while ((nextbits_resync_marker(ld, mp4_state) != 1) && (ld->startptr + ld->length > ld->rdptr)) {
                getbits1(ld);
            }
            mp4_state->error_flag = 1;
            mp4_state->hdr.mb_xpos = 0;
            mp4_state->hdr.mb_ypos = 0;
            mp4_state->hdr.mba = 0;

            // smart recover from the error here
            // ...
        } else {
            mp4_state->error_flag = 0;
        }
        mp4_state->hdr.packetnum++;
    }
}

static inline void process_normal_video_format(reference_t *ref, mp4_stream_t *ld, struct mp4_state *mp4_state)
{
    if (mp4_state->hdr.data_partitioning == 0) {
        process_combined_motion_shape_texture(ref, ld, mp4_state);
    } else {
        process_data_partitioned_stream(ref, ld, mp4_state);
    }
}

static inline void process_short_video_header(reference_t *ref, struct mp4_state *mp4_state)
{
    int i;

    if (!mp4_state->packet_format && mp4_state->hdr.slice_structured) {
        for (i = 0; mp4_state->hdr.mba < mp4_state->hdr.mba_size; i++) {
            if (mp4_state->hdr.num_gobs_in_vop == 1)
                mp4_state->hdr.gob_edge.iabove = mp4_state->mb_width;
        }
    } else {
        for (i = 0; mp4_state->hdr.mba < mp4_state->hdr.mba_size; i++) {
            if (mp4_state->hdr.num_gobs_in_vop == 1)
                mp4_state->hdr.gob_edge.iabove = mp4_state->mb_width;
        }
    }
}

static inline int check_timestamp_error(mp4_stream_t *ld, struct mp4_state *mp4_state)
{
    if (!mp4_state->if_flv_h263) {
        int pos = bitpos(ld);
        pos -= 8 * ld->length;
        if (pos < 0)
            pos = -pos;
        if (pos > 32 && !mp4_state->if_rm_h263) {
            if (mp4_state->test_timeinc >= 0) {
                mp4_state->test_timeinc--;
                mp4_state->hdr.time_increment_resolution = 1 << mp4_state->test_timeinc;
                // initbits(ld, ld->startptr, ld->length);
                return -1;
            } else if (mp4_state->test_timeinc == -1) {
                mp4_state->hdr.time_increment_resolution = 15;
            }
        }
    }
    return 0;
}

static inline void add_edge_to_decoded_frame(reference_t *ref, struct mp4_state *mp4_state)
{
    if (mp4_state->hdr.prediction_type != B_VOP) {
        // patch for the version before 5.0 that was doing the wrong padding
        if ((mp4_state->userdata_codec_version < 500) && (mp4_state->userdata_codec_version != 0) &&
            (mp4_state->userdata_codec_version != 311) && (mp4_state->userdata_codec_version != 263)) {
            make_edge(mp4_state->frame_to_decode[0], mp4_state->horizontal_size, mp4_state->vertical_size, mp4_state->coded_picture_width,
                      16);
            make_edge(mp4_state->frame_to_decode[1], mp4_state->horizontal_size / 2, mp4_state->vertical_size / 2, mp4_state->chrom_width,
                      8);
            make_edge(mp4_state->frame_to_decode[2], mp4_state->horizontal_size / 2, mp4_state->vertical_size / 2, mp4_state->chrom_width,
                      8);
        } else { // divx311, divx5, h263 are the same method.
            if (mp4_state->msmpeg_version >= 5) {
                make_edge_311(mp4_state->frame_to_decode[0], mp4_state->horizontal_size, mp4_state->vertical_size,
                              mp4_state->coded_picture_width, 32);
                make_edge_311(mp4_state->frame_to_decode[1], mp4_state->horizontal_size / 2, mp4_state->vertical_size / 2,
                              mp4_state->chrom_width, 16);
                make_edge_311(mp4_state->frame_to_decode[2], mp4_state->horizontal_size / 2, mp4_state->vertical_size / 2,
                              mp4_state->chrom_width, 16);
            } else {
                make_edge_311(mp4_state->frame_to_decode[0], mp4_state->horizontal_size, mp4_state->vertical_size,
                              mp4_state->coded_picture_width, 16);
                make_edge_311(mp4_state->frame_to_decode[1], mp4_state->horizontal_size / 2, mp4_state->vertical_size / 2,
                              mp4_state->chrom_width, 8);
                make_edge_311(mp4_state->frame_to_decode[2], mp4_state->horizontal_size / 2, mp4_state->vertical_size / 2,
                              mp4_state->chrom_width, 8);
            }
        }
    }
}

static inline void swap_frame_buffers(reference_t *ref, struct mp4_state *mp4_state)
{
    // exchange back and for frames
    // 20080109: to avoid compare error, here we changed the used reference picture.
    // if (mp4_state->hdr.prediction_type != B_VOP && !mp4_state->flag_disposable)
    if (mp4_state->hdr.prediction_type != B_VOP) {
        int i;
        unsigned char *tmp;
        for (i = 0; i < 3; i++) {
            tmp = ref->frame_back[i];
            ref->frame_back[i] = ref->frame_for[i];
            ref->frame_for[i] = tmp;
        }
    }
}

// Purpose: decode and display a Vop
int get_mp4picture(reference_t *ref, unsigned char *bmp, unsigned int stride, int render_flag)
{
    mp4_stream_t *ld = ref->ld;
    struct mp4_state *mp4_state = ref->mp4_state;

    init_macroblock_params(mp4_state);

    set_frame_to_display(ref);

    if ((mp4_state->hdr.prediction_type == B_VOP) && (render_flag == 0))
        return 0;

    memset(&(mp4_state->edge_info), 0, sizeof(mp4_state->edge_info));

    if (!mp4_state->hdr.short_video_header) {
        process_normal_video_format(ref, ld, mp4_state);
    } else {
        process_short_video_header(ref, mp4_state);
    }

#ifdef MPEG4_DUMP_ENABLE
    mp4_save_notcoded_flag(mp4_state);
#endif

    if (mp4_state->hdr.prediction_type != B_VOP)
        mp4_backup_mbh_info(mp4_state);

    if (check_timestamp_error(ld, mp4_state) != 0) {
        return -1;
    }

    ve_mpeg4_decode_slice();
    ref->cur_mb_cfg_ptr = ref->mb_cfg_data;

    add_edge_to_decoded_frame(ref, mp4_state);

    mp4_state->test_timeinc = -2;

    return 0;
}

// Purpose: mark as NOT_VALID the macroblocks that are outside the video packet starting with mbnum
void mark_packet_boundary(struct mp4_state *_mp4_state, int mbnum)
{
    struct mp4_state *mp4_state = _mp4_state;
    int i;

    int mb_xpos = mbnum % mp4_state->hdr.mb_xsize;
    int mb_ypos = mbnum / mp4_state->hdr.mb_xsize;

    // [Ag] 020117 shouldn't be needed, causes a bug in MV prediction (vcon-ge16-L1.bits)
    if (mb_ypos != 0) {
        for (i = 0; i < mp4_state->hdr.mb_xsize + 1; i++) {
            //	mark the macroblock row on top of the video packet as not coded
            //	this will invalidate the previous prediction value
            mp4_state->modemap[(mb_ypos + 1 - 1) * mp4_state->modemap_stride + i] = NOT_VALID;
        }
    }

    if (mb_xpos != 0) {
        for (i = 0; i < mb_xpos + 1; i++) {
            // mark previous macroblocks of current row
            mp4_state->modemap[(mb_ypos + 1) * mp4_state->modemap_stride + i] = NOT_VALID;
        }
    }
    // Add re-sync processing for B-BOP by Robert Yuan, Sept 25,2003
    mp4_state->mv_pfor[0].x = mp4_state->mv_pfor[0].y = 0;
    mp4_state->mv_pfor[1].x = mp4_state->mv_pfor[1].y = 0;
    mp4_state->mv_pback[0].x = mp4_state->mv_pback[0].y = 0;
    mp4_state->mv_pback[1].x = mp4_state->mv_pback[1].y = 0;

    // used in debug mode to know when to draw the packet boundary on the screen
    mp4_state->draw_packet_boundary = 1;
}

static void make_edge_top(unsigned char *frame_pic, int frame_width, int xpos, int chroma)
{
    int stride = frame_width + 64;
    int height = 32;
    int width = 16;
    unsigned char *refptr;
    unsigned char *ptr;
    int i;
    if (chroma) {
        stride /= 2;
        xpos /= 2;
        height /= 2;
        width /= 2;
    }
    refptr = frame_pic + xpos;
    ptr = refptr - stride * height;
    for (i = 0; i < height; i++) {
        memcpy(ptr, refptr, width);
        ptr += stride;
    }
}

static void make_edge_bottom(unsigned char *frame_pic, int frame_start, int frame_width, int frame_height, int xpos, int chroma)
{
    int stride = frame_width + 64;
    int height = 32;
    int width = 16;
    unsigned char *refptr;
    unsigned char *ptr;
    int i;
    if (chroma) {
        stride /= 2;
        xpos /= 2;
        height /= 2;
        width /= 2;
        frame_start /= 2;
        frame_height /= 2;
    }
    refptr = frame_pic + xpos + (frame_start - 1) * stride;
    ptr = frame_pic + xpos + frame_start * stride;
    for (i = 0; i < height; i++) {
        memcpy(ptr, refptr, width);
        ptr += stride;
    }
}

static void make_edge_left(unsigned char *frame_pic, int frame_width, int frame_height, int ypos, int chroma)
{
    int stride = frame_width + 64;
    int width = 32;
    int height = 16;
    unsigned char *refptr;
    unsigned char *ptr;
    int i, j;
    if (chroma) {
        stride /= 2;
        ypos /= 2;
        height /= 2;
        width /= 2;
    }
    refptr = frame_pic + ypos * stride;
    ptr = refptr - width;
    for (i = 0; i < height; i++) {
        int c = refptr[0];
        c |= (c << 8);
        c |= (c << 16);
        for (j = 0; j < width; j += 4)
            *(int *)(&ptr[j]) = c;
        ptr += stride;
        refptr += stride;
    }
}

static void make_edge_right(unsigned char *frame_pic, int frame_start, int frame_width, int frame_height, int ypos, int chroma)
{
    int stride = frame_width + 64;
    int height = 16;
    int width = 32;
    unsigned char *refptr;
    unsigned char *ptr;
    int i, j;
    if (chroma) {
        frame_start /= 2;
        stride /= 2;
        ypos /= 2;
        height /= 2;
        width /= 2;
        frame_width /= 2;
    }
    refptr = frame_pic + ypos * stride + frame_start - 1;
    ptr = refptr + 1;
    for (i = 0; i < height; i++) {
        int c = refptr[0];
        c |= (c << 8);
        c |= (c << 16);
        for (j = 0; j < width; j += 4)
            *(int *)(&ptr[j]) = c;
        ptr += stride;
        refptr += stride;
    }
}

static void fill_corner(unsigned char *frame_pic, int stride, int dim, unsigned char c)
{
    int i, j;
    int c2 = c;
    c2 |= (c2 << 8);
    c2 |= (c2 << 16);

    for (i = 0; i < dim; i++) {
        for (j = 0; j < dim; j += 4)
            *(int *)(frame_pic + j) = c2;
        frame_pic += stride;
    }
}

static void make_edge_lefttop(unsigned char *frame_pic, int frame_width, int frame_height, int chroma)
{
    int stride = frame_width + 64;
    int height = 32;
    if (chroma) {
        stride /= 2;
        height /= 2;
    }
    fill_corner(frame_pic - height - stride * height, stride, height, frame_pic[0]);
}

static void make_edge_righttop(unsigned char *frame_pic, int frame_start, int frame_width, int frame_height, int chroma)
{
    int stride = frame_width + 64;
    int height = 32;
    if (chroma) {
        frame_start /= 2;
        stride /= 2;
        height /= 2;
        frame_width /= 2;
    }
    fill_corner(frame_pic + frame_width - stride * height, stride, height, frame_pic[frame_start - 1]);
}

static void make_edge_leftbottom(unsigned char *frame_pic, int frame_width, int frame_height, int chroma)
{
    int stride = frame_width + 64;
    int height = 32;
    if (chroma) {
        stride /= 2;
        height /= 2;
        frame_height /= 2;
    }
    fill_corner(frame_pic - height + stride * frame_height, stride, height, frame_pic[stride * (frame_height - 1)]);
}

static void make_edge_rightbottom(unsigned char *frame_pic, int frame_xstart, int frame_ystart, int frame_width, int frame_height,
                                  int chroma)
{
    int stride = frame_width + 64;
    int height = 32;
    if (chroma) {
        frame_xstart /= 2;
        frame_ystart /= 2;
        stride /= 2;
        height /= 2;
        frame_width /= 2;
        frame_height /= 2;
    }
    fill_corner(frame_pic + frame_width + stride * frame_height, stride, height, frame_pic[stride * (frame_ystart - 1) + frame_xstart - 1]);
}

static void process_right_edges(reference_t *ref, unsigned char *frame[], int xv, int yv,
                               int xsize, int ysize, int min_ypos, int max_ypos)
{
    mp4_edge_info_t *edge_info = &(ref->mp4_state->edge_info);
    int yentries = (ysize + 15) / 16;
    int i;

    if ((((xv + 2) >> 1) + 16) >= xsize) {
        for (i = min_ypos / 16; i < (max_ypos + 15) / 16; i++) {
            if (i < 0)
                continue;
            if (i > yentries)
                continue;
            if (edge_info->right[i] == 0) {
                make_edge_right(frame[0], xsize, xsize, ysize, i * 16, 0);
                make_edge_right(frame[1], xsize, xsize, ysize, i * 16, 1);
                make_edge_right(frame[2], xsize, xsize, ysize, i * 16, 2);
                edge_info->right[i] = 1;
            }
        }
    }
}

static void process_left_edges(reference_t *ref, unsigned char *frame[], int xv, int yv,
                              int xsize, int ysize, int min_ypos, int max_ypos)
{
    mp4_edge_info_t *edge_info = &(ref->mp4_state->edge_info);
    int yentries = (ysize + 15) / 16;
    int i;

    if ((xv >> 1) < 0) {
        for (i = min_ypos / 16; i < (max_ypos + 15) / 16; i++) {
            if (i < 0)
                continue;
            if (i > yentries)
                continue;
            if (edge_info->left[i] == 0) {
                make_edge_left(frame[0], xsize, ysize, i * 16, 0);
                make_edge_left(frame[1], xsize, ysize, i * 16, 1);
                make_edge_left(frame[2], xsize, ysize, i * 16, 2);
                edge_info->left[i] = 1;
            }
        }
    }
}

static void process_bottom_edges(reference_t *ref, unsigned char *frame[], int xv, int yv,
                                int xsize, int ysize, int min_xpos, int max_xpos)
{
    mp4_edge_info_t *edge_info = &(ref->mp4_state->edge_info);
    int xentries = (xsize + 15) / 16;
    int i;

    if ((((yv + 2) >> 1) + 16) >= ysize) {
        for (i = min_xpos / 16; i < (max_xpos + 15) / 16; i++) {
            if (i < 0)
                continue;
            if (i > xentries)
                continue;
            if (edge_info->bottom[i] == 0) {
                make_edge_bottom(frame[0], ysize, xsize, ysize, i * 16, 0);
                make_edge_bottom(frame[1], ysize, xsize, ysize, i * 16, 1);
                make_edge_bottom(frame[2], ysize, xsize, ysize, i * 16, 2);
                edge_info->bottom[i] = 1;
            }
        }
    }
}

static void process_top_edges(reference_t *ref, unsigned char *frame[], int xv, int yv,
                             int xsize, int ysize, int min_xpos, int max_xpos)
{
    mp4_edge_info_t *edge_info = &(ref->mp4_state->edge_info);
    int xentries = (xsize + 15) / 16;
    int i;

    if ((yv >> 1) < 0) {
        for (i = min_xpos / 16; i < (max_xpos + 15) / 16; i++) {
            if (i < 0)
                continue;
            if (i > xentries)
                continue;
            if (edge_info->top[i] == 0) {
                make_edge_top(frame[0], xsize, i * 16, 0);
                make_edge_top(frame[1], xsize, i * 16, 1);
                make_edge_top(frame[2], xsize, i * 16, 2);
                edge_info->top[i] = 1;
            }
        }
    }
}

static void process_corner_edges(reference_t *ref, unsigned char *frame[],
                                int xsize, int ysize, int min_xpos, int min_ypos, int max_xpos, int max_ypos)
{
    mp4_edge_info_t *edge_info = &(ref->mp4_state->edge_info);

    /**
     * Attention:
     * when image dimensions are not multiple of 16, setting edges in previous
     * steps may overwrite valid data in corners. Because of that, checks
     * for edge_info->corners[x] are disabled.
     */
    if ((min_xpos <= 0) && (min_ypos <= 0))
    //  && (edge_info->corners[0]==0))
    {
        make_edge_lefttop(frame[0], xsize, ysize, 0);
        make_edge_lefttop(frame[1], xsize, ysize, 1);
        make_edge_lefttop(frame[2], xsize, ysize, 2);
        edge_info->corners[0] = 1;
    }
    if ((max_xpos >= xsize) && (min_ypos <= 0))
    {
        make_edge_righttop(frame[0], xsize, xsize, ysize, 0);
        make_edge_righttop(frame[1], xsize, xsize, ysize, 1);
        make_edge_righttop(frame[2], xsize, xsize, ysize, 2);
        edge_info->corners[1] = 1;
    }
    if ((max_ypos >= ysize) && (min_xpos <= 0))
    {
        make_edge_leftbottom(frame[0], xsize, ysize, 0);
        make_edge_leftbottom(frame[1], xsize, ysize, 1);
        make_edge_leftbottom(frame[2], xsize, ysize, 2);
        edge_info->corners[2] = 1;
    }
    if ((max_xpos >= xsize) && (max_ypos >= ysize))
    {
        make_edge_rightbottom(frame[0], xsize, ysize, xsize, ysize, 0);
        make_edge_rightbottom(frame[1], xsize, ysize, xsize, ysize, 1);
        make_edge_rightbottom(frame[2], xsize, ysize, xsize, ysize, 2);
        edge_info->corners[3] = 1;
    }
}

void check_and_set_edges(reference_t *ref, unsigned char *frame[], int xv, int yv)
{
    int ysize = ref->mp4_state->hdr.mb_ysize * 16;
    int xsize = ref->mp4_state->hdr.mb_xsize * 16;
    int max_xpos, max_ypos, min_xpos, min_ypos;

    max_xpos = ((xv + 2) >> 1) + 16;
    min_xpos = xv >> 1;
    max_ypos = ((yv + 2) >> 1) + 16;
    min_ypos = yv >> 1;
    if (ref->mp4_state->msmpeg_version >= 5) {
        min_xpos--;
        min_ypos--;
        max_ypos++;
        max_xpos++;
    }
    if ((min_xpos >= 0) && (max_xpos < xsize) && (min_ypos >= 0) && (max_ypos < ysize)) // true 90% of time
        return;
printf("e\n");
    // Process edges in order: right, left, bottom, top
    process_right_edges(ref, frame, xv, yv, xsize, ysize, min_ypos, max_ypos);
    process_left_edges(ref, frame, xv, yv, xsize, ysize, min_ypos, max_ypos);
    process_bottom_edges(ref, frame, xv, yv, xsize, ysize, min_xpos, max_xpos);
    process_top_edges(ref, frame, xv, yv, xsize, ysize, min_xpos, max_xpos);

    // Process corner edges
    process_corner_edges(ref, frame, xsize, ysize, min_xpos, min_ypos, max_xpos, max_ypos);
}

void make_edge(unsigned char *frame_pic, int width, int height, int stride, int edge)
{
    int j;
    unsigned char *p_border;
    unsigned char *p_border_top, *p_border_bottom;
    unsigned char *p_border_top_ref, *p_border_bottom_ref;

    // left and right edges
    p_border = frame_pic;

    for (j = 0; j < height; j++) {
        unsigned char border_left = p_border[0];
        unsigned char border_right = p_border[width - 1];

        memset((p_border - edge), border_left, edge);
        memset((p_border + width), border_right, edge);
        p_border += stride;
    }

    // top and bottom edges
    p_border_top_ref = frame_pic;
    p_border_bottom_ref = frame_pic + stride * (height - 1);
    p_border_top = p_border_top_ref - edge * stride;
    p_border_bottom = p_border_bottom_ref + stride;

    for (j = 0; j < edge; j++) {
        memcpy(p_border_top, p_border_top_ref, width);
        memcpy(p_border_bottom, p_border_bottom_ref, width);

        p_border_top += stride;
        p_border_bottom += stride;
    }

    // corners
    {
        unsigned char *p_left_corner_top = frame_pic - edge - (edge * stride);
        unsigned char *p_right_corner_top = p_left_corner_top + edge + width;
        unsigned char *p_left_corner_bottom = frame_pic + (stride * height) - edge;
        unsigned char *p_right_corner_bottom = p_left_corner_bottom + edge + width;

        char left_corner_top = frame_pic[0];
        char right_corner_top = frame_pic[width - 1];
        char left_corner_bottom = frame_pic[stride * (height - 1)];
        char right_corner_bottom = frame_pic[stride * (height - 1) + width - 1];

        for (j = 0; j < edge; j++) {
            memset(p_left_corner_top, left_corner_top, edge);
            memset(p_right_corner_top, right_corner_top, edge);
            memset(p_left_corner_bottom, left_corner_bottom, edge);
            memset(p_right_corner_bottom, right_corner_bottom, edge);

            p_left_corner_top += stride;
            p_right_corner_top += stride;
            p_left_corner_bottom += stride;
            p_right_corner_bottom += stride;
        }
    }
}

void make_edge_16x8(unsigned char *frame_pic, int width, int height, int stride, int hor_edge, int ver_edge)
{
    int j;
    unsigned char *p_border;
    unsigned char *p_border_top, *p_border_bottom;
    unsigned char *p_border_top_ref, *p_border_bottom_ref;
    width = (width + hor_edge - 1) & ~(hor_edge - 1);
    height = (height + ver_edge - 1) & ~(ver_edge - 1);

    // left and right edges
    p_border = frame_pic;

    for (j = 0; j < height; j++) {
        unsigned char border_left = p_border[0];
        unsigned char border_right = p_border[width - 1];

        memset((p_border - hor_edge), border_left, hor_edge);
        memset((p_border + width), border_right, hor_edge);
        p_border += stride;
    }

    // top and bottom edges
    p_border_top_ref = frame_pic;
    p_border_bottom_ref = frame_pic + stride * (height - 1);
    p_border_top = p_border_top_ref - ver_edge * stride;
    p_border_bottom = p_border_bottom_ref + stride;

    for (j = 0; j < ver_edge; j++) {
        memcpy(p_border_top, p_border_top_ref, width);
        memcpy(p_border_bottom, p_border_bottom_ref, width);

        p_border_top += stride;
        p_border_bottom += stride;
    }

    // corners
    {
        unsigned char *p_left_corner_top = frame_pic - hor_edge - (ver_edge * stride);
        unsigned char *p_right_corner_top = p_left_corner_top + hor_edge + width;
        unsigned char *p_left_corner_bottom = frame_pic + (stride * height) - hor_edge;
        unsigned char *p_right_corner_bottom = p_left_corner_bottom + hor_edge + width;

        char left_corner_top = frame_pic[0];
        char right_corner_top = frame_pic[width - 1];
        char left_corner_bottom = frame_pic[stride * (height - 1)];
        char right_corner_bottom = frame_pic[stride * (height - 1) + width - 1];

        for (j = 0; j < ver_edge; j++) {
            memset(p_left_corner_top, left_corner_top, hor_edge);
            memset(p_right_corner_top, right_corner_top, hor_edge);
            memset(p_left_corner_bottom, left_corner_bottom, hor_edge);
            memset(p_right_corner_bottom, right_corner_bottom, hor_edge);

            p_left_corner_top += stride;
            p_right_corner_top += stride;
            p_left_corner_bottom += stride;
            p_right_corner_bottom += stride;
        }
    }
}
