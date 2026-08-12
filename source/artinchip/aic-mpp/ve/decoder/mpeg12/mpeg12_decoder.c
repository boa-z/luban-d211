/*
 * Copyright (C) 2020-2026 ArtInChip Technology Co. Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 *  author: <xiaodong.zhao@artinchip.com>
 *  Desc: mpeg1/2 decoder interface
 *
 */

#define LOG_TAG "mpeg_decoder"
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include <time.h>

#include "mpeg12_decoder.h"
#include "mpp_log.h"
#include "mpp_mem.h"
#include "ve.h"
#include "vlc.h"
#include "rotation_config.h"

#define EXTENSION_START_CODE                   0x000001B5
#define SEQUENCE_START_CODE                    0x000001B3
#define GROUP_START_CODE                       0x000001B8
#define PICTURE_START_CODE                     0x00000100
#define SEQUENCE_END_CODE                      0x000001B7
#define SEQUENCE_ERROR_CODE                    0x000001B4
#define USER_DATA_START_CODE                   0x000001B2

#define SEQUENCE_EXTENSION_ID                  1
#define SEQUENCE_DISPLAY_EXTENSION_ID          2
#define QUANT_MATRIX_EXTENSION_ID              3
#define COPYRIGHT_EXTENSION_ID                 4
#define SEQUENCE_SCALABLE_EXTENSION_ID         5
#define PICTURE_DISPLAY_EXTENSION_ID           7
#define PICTURE_CODING_EXTENSION_ID            8
#define PICTURE_SPATIAL_SCALABLE_EXTENSION_ID  9
#define PICTURE_TEMPORAL_SCALABLE_EXTENSION_ID 10

const uint8_t mpeg2_non_linear_qscale[32] = {
    0, 1, 2, 3, 4, 5, 6, 7, 8, 10, 12, 14, 16, 18, 20, 22, 24,
    28, 32, 36, 40, 44, 48, 52, 56, 64, 72, 80, 88, 96, 104, 112,
};

/**
 * Calculate rotation/mirror offsets for output frame
 *
 * The calculated values are used to:
 * 1. Configure hardware post-processing registers
 * 2. Set proper crop window for output display
 * 3. Adjust buffer stride for rotated frames
 */
void get_rotmir_offset(struct mpeg12_dec_ctx *s)
{
    int rotate = MPP_ROTATION_GET(s->decoder.rotmir_flag);
    int flip_h = MPP_FLIP_H_GET(s->decoder.rotmir_flag);
    int flip_v = MPP_FLIP_V_GET(s->decoder.rotmir_flag);

    s->h_offset = 0;
    s->v_offset = 0;
    s->rotmir_h_stride = s->mb_width * 16;
    s->rotmir_v_stride = s->mb_height * 16;
    s->rotmir_h_real_size = s->seq_header.horizontal_size;
    s->rotmir_v_real_size = s->seq_header.vertical_size;

    const rotation_config *config = find_rotation_config(rotate, flip_h, flip_v);
    if (config != NULL) {
         if (config->set_h_offset) {
             s->h_offset = s->rotmir_h_stride - s->rotmir_h_real_size;
         }

         if (config->set_v_offset) {
             s->v_offset = s->rotmir_v_stride - s->rotmir_v_real_size;
         }

         if (config->h_v_switch) {
             swap_val(&s->h_offset, &s->v_offset);
             swap_val(&s->rotmir_h_real_size, &s->rotmir_v_real_size);
             swap_val(&s->rotmir_h_stride, &s->rotmir_v_stride);
         }

         logi("Found matching : rotate=%d, flip_h=%d, flip_v=%d",
              config->rotate, config->flip_h, config->flip_v);
    }
}

int64_t get_time()
{
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return (int64_t)ts.tv_sec * 1000 * 1000 + ts.tv_nsec / 1000;
}

static int search_start_code(struct read_bit_context *gb)
{
    if (read_bits_count(gb) & 7) {
        skip_bits(gb, 8 - (read_bits_count(gb) & 7));
    }

    while (read_bits_left(gb) > 24) {
        if (show_bits(gb, 24) == 0x000001)
            return 0;
        skip_bits(gb, 8);
    }

    return -1;
}

static int mpeg_get_qscale(struct mpeg12_dec_ctx *s)
{
    int qscale = read_bits(&s->gb, 5);
    s->mb_info.quantiser_scale_code = qscale;

    if (s->pic_code_extension.q_scale_type)
        return mpeg2_non_linear_qscale[qscale];
    else
        return qscale << 1;
}

static void update_rotmir_buffer_idx(struct mpeg12_dec_ctx *s)
{
    if (s->decoder.rotmir_flag) {
        if(s->last_rotmir_idx >= 0) {
            fm_decoder_put_frame(s->decoder.fm, s->frame_buf[s->last_rotmir_idx].frame);
            s->frame_buf[s->last_rotmir_idx].is_ref = 0;
        }
        s->last_rotmir_idx = s->cur_rotmir_idx;
        s->frame_buf[s->last_rotmir_idx].is_ref = 1;
    }
}

static int update_frame_buffer_idx(struct mpeg12_dec_ctx *s)
{
    if (s->pic.picture_coding_type == MP2_B_PICTURE) {
        return 0;
    } else if (s->pic.picture_coding_type == MP2_I_PICTURE) {
        if (s->last_pic_idx >= 0) {
            fm_decoder_put_frame(s->decoder.fm, s->frame_buf[s->last_pic_idx].frame);
            s->frame_buf[s->last_pic_idx].is_ref = 0;
        }
        if (s->next_pic_idx >= 0) {
            fm_decoder_put_frame(s->decoder.fm, s->frame_buf[s->next_pic_idx].frame);
            s->frame_buf[s->next_pic_idx].is_ref = 0;
            s->next_pic_idx = -1;
        }
    } else if (s->pic.picture_coding_type == MP2_P_PICTURE) {
        if (s->last_pic_idx >= 0) {
            fm_decoder_put_frame(s->decoder.fm, s->frame_buf[s->last_pic_idx].frame);
            s->frame_buf[s->last_pic_idx].is_ref = 0;
        }
    }
    s->last_pic_idx = s->next_pic_idx;
    s->next_pic_idx = s->cur_pic_idx;
    s->frame_buf[s->next_pic_idx].is_ref = 1;

    update_rotmir_buffer_idx(s);
    return 0;
}

static int sequence_extension(struct mpeg12_dec_ctx *s)
{
    logi("=======> mpeg2");
    s->is_mpeg2 = 1;
    s->seq_extension.extension_start_code_identifier = read_bits(&s->gb, 4);
    s->seq_extension.profile_and_level_indication = read_bits(&s->gb, 8);
    s->seq_extension.progressive_sequence = read_bits(&s->gb, 1);
    s->seq_extension.chroma_format = read_bits(&s->gb, 2);
    s->seq_extension.horizontal_size_extension = read_bits(&s->gb, 2);
    s->seq_extension.vertical_size_extension = read_bits(&s->gb, 2);
    s->seq_extension.bit_rate_extension = read_bits(&s->gb, 2);
    skip_bits(&s->gb, 1); // skip_bits(1) marker_bit
    s->seq_extension.vbv_buffer_size_extension = read_bits(&s->gb, 8);
    s->seq_extension.low_delay = read_bits(&s->gb, 1);
    s->seq_extension.frame_rate_extension_n = read_bits(&s->gb, 2);
    s->seq_extension.frame_rate_extension_d = read_bits(&s->gb, 5);

    if (s->seq_extension.chroma_format == 2 || s->seq_extension.chroma_format == 3) {
        logw("donot support this format(%ld)", s->seq_extension.chroma_format);
    }

    return 0;
}

static int picture_coding_extension(struct mpeg12_dec_ctx *s)
{
    static char str_pic_structure[4][64] = {"unknow", "top_field", "bottom_field", "frame"};
    struct pic_coding_extension_info *ext = &s->pic_code_extension;

    ext->extension_start_code_identifier = read_bits(&s->gb, 4);
    ext->f_code[0][0] = read_bits(&s->gb, 4);
    ext->f_code[0][1] = read_bits(&s->gb, 4);
    ext->f_code[1][0] = read_bits(&s->gb, 4);
    ext->f_code[1][1] = read_bits(&s->gb, 4);
    ext->intra_dc_precision = read_bits(&s->gb, 2);
    ext->picture_structure = read_bits(&s->gb, 2);
    ext->top_field_first = read_bits(&s->gb, 1);
    ext->frame_pred_frame_dct = read_bits(&s->gb, 1);
    ext->concealment_motion_vectors = read_bits(&s->gb, 1);
    ext->q_scale_type = read_bits(&s->gb, 1);
    ext->intra_vlc_format = read_bits(&s->gb, 1);

    ext->alternate_scan = read_bits(&s->gb, 1);
    ext->repeat_first_field = read_bits(&s->gb, 1);
    ext->chroma_420_type = read_bits(&s->gb, 1);
    ext->progressive_frame = read_bits(&s->gb, 1);
    ext->composite_display_flag = read_bits(&s->gb, 1);
    if (ext->composite_display_flag) {
        ext->v_axis = read_bits(&s->gb, 1);
        ext->field_sequence = read_bits(&s->gb, 3);
        ext->sub_carrier = read_bits(&s->gb, 1);
        ext->burst_amplitude = read_bits(&s->gb, 7);
        ext->sub_carrier_phase = read_bits(&s->gb, 8);
    }

    if (ext->alternate_scan)
        s->scan_table = ff_alternate_vertical_scan;
    else
        s->scan_table = ff_zigzag_direct;

    logd("================= picture coding extension =======================");
    logd("f_code: %ld %ld %ld %ld", ext->f_code[0][0], ext->f_code[0][1], ext->f_code[1][0], ext->f_code[1][1]);
    logd("intra_dc_precision: %ld", ext->intra_dc_precision);
    logd("picture_structure: %s", str_pic_structure[ext->picture_structure]);
    logd("top_field_first: %ld", ext->top_field_first);
    logd("frame_pred_frame_dct: %ld", ext->frame_pred_frame_dct);
    logd("q_scale_type: %ld", ext->q_scale_type);
    logd("intra_vlc_format: %ld", ext->intra_vlc_format);
    logd("progressive_frame: %ld", ext->progressive_frame);
    logd("ext->alternate_scan: %ld", ext->alternate_scan);
    logd("===================================================================");

    return 0;
}

static int quant_matrix_extension(struct mpeg12_dec_ctx *s)
{
    int i;
    if (read_bits(&s->gb, 1)) {
        for (i = 0; i < 64; i++) {
            s->seq_header.intra_quantiser_matrix[ff_zigzag_direct[i]] = read_bits(&s->gb, 8);
        }
    }

    if (read_bits(&s->gb, 1)) {
        for (i = 0; i < 64; i++) {
            s->seq_header.non_intra_quantiser_matrix[ff_zigzag_direct[i]] = read_bits(&s->gb, 8);
        }
    }

    logd("===================== quant matrix extension =========================");
    for (i = 0; i < 8; i++) {
        for (int j = 0; j < 8; j++) {
            logd("%3d ", s->seq_header.non_intra_quantiser_matrix[8 * i + j]);
        }
        logd("\n");
    }

    return 0;
}

static int extension_and_user_data(struct mpeg12_dec_ctx *s)
{
    uint32_t ext_id;
    uint32_t nextbit32;

    nextbit32 = show_bits(&s->gb, 32);
    // extension start code or user data start code
    while ((nextbit32 = show_bits(&s->gb, 32)) == EXTENSION_START_CODE || nextbit32 == USER_DATA_START_CODE) {
        if (nextbit32 == EXTENSION_START_CODE) {
            skip_bits(&s->gb, 32);
            ext_id = show_bits(&s->gb, 4); // read_bits(4)
            switch (ext_id) {
            case SEQUENCE_EXTENSION_ID:
                sequence_extension(s);
                break;
            case SEQUENCE_DISPLAY_EXTENSION_ID:
                break;
            case QUANT_MATRIX_EXTENSION_ID:
                read_bits(&s->gb, 4); // ext_id
                quant_matrix_extension(s);
                break;
            case SEQUENCE_SCALABLE_EXTENSION_ID:
                break;
            case PICTURE_DISPLAY_EXTENSION_ID:
                break;
            case PICTURE_CODING_EXTENSION_ID:
                if (s->is_mpeg2) {
                    picture_coding_extension(s);
                }
                break;
            case PICTURE_SPATIAL_SCALABLE_EXTENSION_ID:
                break;
            case PICTURE_TEMPORAL_SCALABLE_EXTENSION_ID:
                break;
            case COPYRIGHT_EXTENSION_ID:
                break;
            default:
                logd("reserved extension start code ID %d\n", ext_id);
                break;
            }
            search_start_code(&s->gb);
        } else {
            // skip user data
            skip_bits(&s->gb, 32);
            search_start_code(&s->gb);
        }
    }

    return 0;
}

static int mpeg_free_frame_buffer(struct mpeg12_dec_ctx *s)
{
    if (s->mb_cfg_phy_addr) {
        ve_buffer_free(s->ve_buf_handle, s->mb_cfg_phy_addr);
        s->mb_cfg_phy_addr = NULL;
    }
    s->mb_cfg_data = NULL; // mb_cfg_data is mb_cfg_phy_addr virtual address

    if (s->mb_cfg_info_list) {
        free(s->mb_cfg_info_list);
        s->mb_cfg_info_list = NULL;
    }

    return 0;
}

static int decode_sequence_header(struct mpeg12_dec_ctx *s)
{
    struct sequence_header *seq = &s->seq_header;
    int i = 0;

    seq->horizontal_size = read_bits(&s->gb, 12); // 12bit
    seq->vertical_size = read_bits(&s->gb, 12);   // 12bit
    if (seq->horizontal_size == 0 || seq->vertical_size == 0) {
        loge("Invalid video resolution: %dx%d", seq->horizontal_size, seq->vertical_size);
        return -1;
    }

    seq->aspect_ratio = read_bits(&s->gb, 4);
    seq->frame_rate = read_bits(&s->gb, 4);
    if (seq->frame_rate > 8) { // MPEG-1/2 frame_rate 1-8
        loge("Invalid frame rate index: %d", seq->frame_rate);
        return -1;
    }

    seq->bitrate = read_bits(&s->gb, 18);
    skip_bits(&s->gb, 1);
    seq->vbv_buffer_size = read_bits(&s->gb, 10);

    seq->constrained_param = read_bits(&s->gb, 1);
    seq->load_intra_quantiser_matrix = read_bits(&s->gb, 1);

    if (seq->load_intra_quantiser_matrix) {
        for (i = 0; i < 64; i++) {
            seq->intra_quantiser_matrix[ff_zigzag_direct[i]] = read_bits(&s->gb, 8);
        }
    } else {
        for (i = 0; i < 64; i++) {
            seq->intra_quantiser_matrix[ff_zigzag_direct[i]] = mpeg2_intra_q[ff_zigzag_direct[i]];
        }
    }

    seq->load_non_intra_quantiser_matrix = read_bits(&s->gb, 1);
    if (seq->load_non_intra_quantiser_matrix) {
        for (i = 0; i < 64; i++) {
            seq->non_intra_quantiser_matrix[ff_zigzag_direct[i]] = read_bits(&s->gb, 8);
        }
    } else {
        for (i = 0; i < 64; i++) {
            seq->non_intra_quantiser_matrix[i] = 16;
        }
    }

    logd("=========== sequence header ================= \n");
    logd("horizontal_size: %d \n", seq->horizontal_size);
    logd("vertical_size: %d \n", seq->vertical_size);
    logd("aspect_ratio: %d \n", seq->aspect_ratio);
    logd("frame_rate: %d \n", seq->frame_rate);
    logd("load_intra_quantiser_matrix: %d \n", seq->load_intra_quantiser_matrix);
    logd("load_non_intra_quantiser_matrix: %d \n", seq->load_non_intra_quantiser_matrix);
    logd("================================================= \n");

    search_start_code(&s->gb);
    extension_and_user_data(s);

    return 0;
}

static int process_sequence_header(struct mpeg12_dec_ctx *s)
{
    int mb_cfg_size, mb_cfg_list_size, ret;
    struct ve_buffer_allocator *ctx = NULL;

    if (NULL == s) {
        loge("invalid parameter");
        return -1;
    }

    s->scan_table = ff_zigzag_direct;
    ret = decode_sequence_header(s);
    if (ret < 0) {
        loge("decode sequence header failed");
        return -1;
    }
    s->mb_width = (s->seq_header.horizontal_size + 15) / 16;
    s->mb_height = (s->seq_header.vertical_size + 15) / 16;

    if (s->decoder.rotmir_flag) {
        get_rotmir_offset(s);
    }

    ctx = s->ve_buf_handle;
    mb_cfg_size = s->mb_width * s->mb_height * MB_INFO_SIZE * sizeof(uint32_t);

    if (s->mb_cfg_phy_addr == NULL) {
        s->mb_cfg_phy_addr = ve_buffer_alloc(ctx, mb_cfg_size, ALLOC_NEED_VIR_ADDR);
        if (NULL == s->mb_cfg_phy_addr) {
            loge("ve_buffer_alloc mb_cfg_phy_addr failed.");
            return -1;
        }
        s->mb_cfg_data = (uint32_t *)s->mb_cfg_phy_addr->vir_addr;
        memset(s->mb_cfg_data, 0, mb_cfg_size);
        s->cur_mb_cfg_ptr = s->mb_cfg_data;
    }

    mb_cfg_list_size = s->mb_width * s->mb_height * sizeof(struct mb_config_info);
    if (s->mb_cfg_info_list == NULL) {
        s->mb_cfg_info_list = (struct mb_config_info *)malloc(mb_cfg_list_size);
        if (NULL == s->mb_cfg_info_list) {
            loge("malloc mb_cfg_info_list failed");
            ve_buffer_free(ctx, s->mb_cfg_phy_addr);
            return -1;
        }
        memset(s->mb_cfg_info_list, 0, mb_cfg_list_size);
    }

    return 0;
}

static int process_start_code(struct mpeg12_dec_ctx *s)
{
    int ret;

    if (NULL == s) {
        loge("invalid parameter");
        return -1;
    }

    ret = search_start_code(&s->gb);
    if (ret < 0) {
        if (read_bits_left(&s->gb) < 32) {
            loge("file EOS!");
            return -2;
        }
        loge("cannot find start code, %d", read_bits_left(&s->gb));
        return -1;
    }

    if (read_bits_left(&s->gb) < 32) {
        loge("not enough data");
        return -1;
    }

    skip_bits(&s->gb, 24);
    ret = read_bits(&s->gb, 8);

    return ret;
}

static int decode_group_of_picture(struct mpeg12_dec_ctx *s)
{
    struct gop *g = &s->group_of_pic;

    g->time_code = read_bits(&s->gb, 25);
    g->close_gop = read_bits(&s->gb, 1);
    g->broken_link = read_bits(&s->gb, 1);

    logd("=========== gop header ================= \n");
    logd("time_code: %d \n", g->time_code);
    logd("close_gop: %d \n", g->close_gop);
    logd("broken_link: %d \n", g->broken_link);
    logd("======================================== \n");

    return 0;
}

static int decode_picture_header(struct mpeg12_dec_ctx *s, struct picture *pic)
{
    s->cur_mblk_addr = -1;
    s->prev_mblk_addr = -1;
    s->picture_end = 0;

    pic->temporal_reference = read_bits(&s->gb, 10); // 10bit
    pic->picture_coding_type = read_bits(&s->gb, 3); // 3bit
    pic->vbv_delay = read_bits(&s->gb, 16);          // 16bit

    if (s->decoder.fm == NULL) {
        struct frame_manager_init_cfg cfg;
        cfg.frame_count = s->extra_frame_num + 3;
        if (s->decoder.rotmir_flag && cfg.frame_count < 5) {
            cfg.frame_count += 1;
        }
        cfg.height = s->mb_height * 16;
        cfg.width = s->mb_width * 16;
        cfg.height_align = s->mb_height * 16;
        cfg.stride = s->mb_width * 16;
        cfg.pixel_format = MPP_FMT_YUV420P;
        cfg.allocator = s->decoder.allocator;
        s->decoder.fm = fm_create(&cfg);
        if (NULL == s->decoder.fm) {
            loge("frame manager create failed !");
            return -1;
        }
    }

    if (s->need_get_frame) {
        struct frame *f = fm_decoder_get_frame(s->decoder.fm);
        if (f == NULL) {
            loge("fm_decoder_get_frame failed!");
            pm_reclaim_ready_packet(s->decoder.pm, s->curr_packet);
            return DEC_NO_EMPTY_FRAME;
        }
        s->frame_buf[f->mpp_frame.id].frame = f;
        s->cur_pic_idx = f->mpp_frame.id;

        if (s->decoder.rotmir_flag) {
            struct frame *rotmir_f = fm_decoder_get_frame(s->decoder.fm);
            if (rotmir_f == NULL) {
                loge("fm_decoder_get_frame failed for rotmir_f!");
                pm_reclaim_ready_packet(s->decoder.pm, s->curr_packet);
                return DEC_NO_EMPTY_FRAME;
            }
            s->frame_buf[rotmir_f->mpp_frame.id].frame = rotmir_f;
            s->cur_rotmir_idx = rotmir_f->mpp_frame.id;
        }
    }

    if (s->cur_pic_idx < 0 || s->cur_pic_idx >= FRAME_BUFFER_NUM
        || s->frame_buf[s->cur_pic_idx].frame == NULL) {
        loge("Invalid cur_pic_idx : %d ! need_get_frame: %d", s->cur_pic_idx, s->need_get_frame);
        return -1;
    }

    if (s->pic_code_extension.picture_structure == FRAME || (pic->temporal_reference != s->last_temporal_reference)) {
        s->update_pic = 1;
        update_frame_buffer_idx(s);
    } else {
        s->update_pic = 0;
    }

    s->last_temporal_reference = pic->temporal_reference;
    if (s->decoder.rotmir_flag) {
        s->frame_buf[s->cur_rotmir_idx].display_count = s->display_num_base + pic->temporal_reference;
    } else {
        s->frame_buf[s->cur_pic_idx].display_count = s->display_num_base + pic->temporal_reference;
    }

    if (pic->picture_coding_type == 2 || pic->picture_coding_type == 3) {

        pic->full_pel_forward_vector = read_bits(&s->gb, 1);
        pic->forward_f_code = read_bits(&s->gb, 3);
    }
    if (pic->picture_coding_type == 3) {
        pic->full_pel_backward_vector = read_bits(&s->gb, 1); // 1bit
        pic->backward_f_code = read_bits(&s->gb, 3);          // 3bit
    }

    unsigned char name[4][32] = {"unkown", "I", "P", "B"};
    logi("picture type: %s, cur_pic_idx: %d, next_pic_idx: %d, last_pic_idx: %d\n",
            name[pic->picture_coding_type], s->cur_pic_idx, s->next_pic_idx, s->last_pic_idx);

    logd("=========== picture header ================= \n");
    logd("temporal_reference: %d \n", pic->temporal_reference);
    logd("picture_coding_type: %d \n", pic->picture_coding_type);
    logd("vbv_delay: %d \n", pic->vbv_delay);
    logd("full_pel_forward_vector: %d \n", pic->full_pel_forward_vector);
    logd("forward_f_code: %d \n", pic->forward_f_code);
    logd("full_pel_backward_vector: %d \n", pic->full_pel_backward_vector);
    logd("backward_f_code: %d \n", pic->backward_f_code);
    logd("picture type: %s, cur_pic_idx: %d\n", name[pic->picture_coding_type], s->cur_pic_idx);
    logd("============================================== \n");

    search_start_code(&s->gb);
    // picture coding extension
    extension_and_user_data(s);

    if (s->pic_code_extension.picture_structure == TOP_FIELD || s->pic_code_extension.picture_structure == BOTTOM_FIELD) {
        s->first_field ^= 1;
    } else {
        s->first_field = 0;
    }

    return 0;
}

static int decode_dc(struct mpeg12_dec_ctx *s, int block_num, short *dc)
{
    int component = (block_num < 4) ? 0 : block_num - 4 + 1;
    int size, dct_dc_differential = 0;
    struct vlc_tab1 *dds_ptr;
    int level;

    //  Y componengt DC
    if (component == 0) {
        // show 7 bits
        dds_ptr = &dct_dc_size_luminance_table[show_bits(&s->gb, 9)];
        size = dds_ptr->value;
        // skip the real bit len of dct_dc_size_luma
        skip_bits(&s->gb, dds_ptr->len);
    } else { // i>4
        // show 8 bits
        dds_ptr = &dct_dc_size_chrominance_table[show_bits(&s->gb, 10)];
        size = dds_ptr->value;
        skip_bits(&s->gb, dds_ptr->len);
    }

    if (size) {
        dct_dc_differential = read_bits(&s->gb, size);
        if (dct_dc_differential & (1 << (size - 1)))
            s->last_dc[component] += dct_dc_differential;
        else
            s->last_dc[component] += (-1 << size) | (dct_dc_differential + 1);
    }

    logi("size: %d, dct_dc_differential: %d, last_dc: %d %d", size, dct_dc_differential, s->last_dc[component],
         (-1 << size) | (dct_dc_differential + 1));

    level = s->last_dc[component];
    if (level < 0)
        level = 0;
    else if (level > ((1 << (8 + s->pic_code_extension.intra_dc_precision)) - 1))
        level = (1 << (8 + s->pic_code_extension.intra_dc_precision)) - 1;

    s->last_dc[component] = level;

    *dc = level;

    return 0;
}

static int decode_ac(struct mpeg12_dec_ctx *s, int block_num)
{
    short *reconp = s->mb_info.recon[block_num];
    int pos = (s->mb_info.mb_intra) ? 0 : -1;
    unsigned long code, sign, tab;
    int run, level, sign_level;

    logd("---- decode ac block num: %d ----\n", block_num);

    for (;;) {
        code = show_bits(&s->gb, 17);

        logd("code: %ld next 32 bit:%x index:%d left_bits:%d\n", code, show_bits(&s->gb, 32), read_bits_count(&s->gb),
             read_bits_left(&s->gb));

        if (!s->pic_code_extension.intra_vlc_format && code >= 32768) {
            if (pos == -1)
                tab = DCTtabfirst[code >> 12];
            else
                tab = DCTtabnext[code >> 12];
        } else if (code >= 2048) {
            tab = s->pic_code_extension.intra_vlc_format ? DCTtab0a[code >> 8] : DCTtab0[code >> 8];
        } else if (code >= 1024) {
            tab = s->pic_code_extension.intra_vlc_format ? DCTtab1a[(code >> 6)] : DCTtab1[(code >> 6)];
        } else if (code >= 512) {
            tab = DCTtab2[(code >> 4)];
        } else if (code >= 256) {
            tab = DCTtab3[(code >> 3)];
        } else if (code >= 128) {
            tab = DCTtab4[(code >> 2)];
        } else if (code >= 64) {
            tab = DCTtab5[(code >> 1)];
        } else if (code >= 32) {
            tab = DCTtab6[code];
        } else {
            loge("intra_block ac coefficient decoding error code(%ld). offset(%d)\n", code, read_bits_count(&s->gb));
            // todo: need return error or not
        }

        skip_bits(&s->gb, tab & 0xff);
        run = tab >> 24;

        logd("ac tab: %lx, len: %ld, run: %d index:%d\n", tab, tab & 0xff, run, read_bits_count(&s->gb));

        if (run < 64) { // nonescape //
            level = (tab >> 16) & 0xff;
            sign = tab & 0xff00;
        } else if (run == 64) { // EOB//
            break;
        } else {                        // ESCAPE//
            run = read_bits(&s->gb, 6); // GetBits(6);
            if (s->is_mpeg2) {          // mpeg2
                level = read_bits(&s->gb, 12);
                if ((sign = (level >= 2048)))
                    level = 4096 - level;
            } else {
                level = read_bits(&s->gb, 8); // GetBits(8);
                if (level & 0x7f) {
                    if ((sign = (level > 128)))
                        level = 256 - level;
                } else {
                    level = (level << 8) | read_bits(&s->gb, 8);
                    if ((sign = (level > 32768)))
                        level = (32768 + 256) - level;
                }
            }
        }

        sign_level = sign ? -level : level;
        pos += run + 1;
        if (pos > 63)
            break;

        reconp[s->scan_table[pos]] = sign_level;

        logd("ac coeff   pos: %d, scan_pos: %d, run: %d, level: %d, get_index: %d, qscale: %d\n", pos, s->scan_table[pos], run, sign_level,
             read_bits_count(&s->gb), s->mb_info.quantiser_scale);
    }

    return 0;
}

static int mpeg2_inter_decode_ac(struct mpeg12_dec_ctx *s, int block_num)
{
    short *reconp = s->mb_info.recon[block_num];
    int pos = (s->mb_info.mb_intra) ? 0 : -1;
    unsigned long code, sign, tab;
    int run, level, sign_level;

    for (;;) {
        code = show_bits(&s->gb, 17);
        if (code >= 32768) {
            // For intra MB, need to process DC separately; for non-intra MB, use DCTtabfirst
            if (pos == -1)
                tab = DCTtabfirst[code >> 12];
            else
                tab = DCTtabnext[code >> 12];
        } else if (code >= 2048) {
            tab = DCTtab0[code >> 8];
        } else {
            if (code >= 1024) {
                tab = DCTtab1[(code >> 6)];
            } else if (code >= 512) {
                tab = DCTtab2[(code >> 4)];
            } else if (code >= 256) {
                tab = DCTtab3[(code >> 3)];
            } else if (code >= 128) {
                tab = DCTtab4[(code >> 2)];
            } else if (code >= 64) {
                tab = DCTtab5[(code >> 1)];
            } else if (code >= 32) {
                tab = DCTtab6[code];
            } else {
                loge("inter_block ac coefficient decoding error code(%ld). offset(%d)\n", code, read_bits_count(&s->gb));
            }
        }

        skip_bits(&s->gb, tab & 0xff);
        run = tab >> 24;

        if (run < 64) { // nonescape
            level = (tab >> 16) & 0xff;
            sign = tab & 0xff00;
        } else if (run == 64) { // EOB
            break;
        } else {                        // ESCAPE
            run = read_bits(&s->gb, 6); // GetBits(6);

            if (s->is_mpeg2) {

                level = read_bits(&s->gb, 12);
                if ((sign = (level >= 2048)))
                    level = 4096 - level;
            } else {
                level = read_bits(&s->gb, 8); // GetBits(8);
                if (level & 0x7f) {
                    if ((sign = (level > 128)))
                        level = 256 - level;
                } else {
                    level = (level << 8) | read_bits(&s->gb, 8); // GetBits(8);
                    if ((sign = (level > 32768)))
                        level = (32768 + 256) - level;
                }
            }
        }

        sign_level = sign ? -level : level;
        pos += run + 1;
        if (pos > 63)
            break;

        reconp[s->scan_table[pos]] = sign_level;

        logi("ac coeff   pos: %d, scan_pos: %d, run: %d, level: %d, get_index: %d, qscale: %d", pos, s->scan_table[pos], run, sign_level,
             read_bits_count(&s->gb), s->mb_info.quantiser_scale);
    }

    return 0;
}

static void save_mb_coeffs(struct mpeg12_dec_ctx *s, int block_num)
{
#ifdef MPEG12_DUMP_ENABLE
    short *reconp = s->mb_info.recon[block_num];

    fprintf(s->fp_mb_coeff, "block num(%d)\n", block_num);
    int ii = 0, jj = 0;
    for (ii = 0; ii < 8; ii++) {
        for (jj = 0; jj < 8; jj++) {
            fprintf(s->fp_mb_coeff, "%5d ", reconp[8 * ii + jj]);
        }
        fprintf(s->fp_mb_coeff, "\n");
    }
#endif

    config_mpeg12_mb_coeff(s, block_num);
}

int mpeg2_intra_6block(struct mpeg12_dec_ctx *s)
{
    short *reconp;

    s->mb_info.mb_intra = 1;

    for (int i = 0; i < 6; i++) {
        reconp = s->mb_info.recon[i];
        memset(reconp, 0, 64 * sizeof(short));

        // 1. parse DC coeff
        decode_dc(s, i, &reconp[0]);

        logi("DC coeff: %d, i: %d, mb_intra: %ld, showbits: %x, offset: %d", reconp[0], i, s->mb_info.mb_intra, show_bits(&s->gb, 32),
             read_bits_count(&s->gb));

        // 2. AC coeff
        decode_ac(s, i);

        save_mb_coeffs(s, i);
    }

    return 0;
}

static int mpeg2_inter_6block(struct mpeg12_dec_ctx *s)
{
    int i;
    short *reconp;

    for (i = 0; i < 6; i++) {
        reconp = s->mb_info.recon[i];
        memset(reconp, 0, 64 * sizeof(short));
        if (s->mb_info.cbp & (0x20 >> i)) {
            reconp = s->mb_info.recon[i];
            // 5.1. parse non-intra block DCT coeff
            // mpeg2_nonotra_block
            mpeg2_inter_decode_ac(s, i);

            save_mb_coeffs(s, i);
        }
    }

    return 0;
}

int mpeg1_intra_6block(struct mpeg12_dec_ctx *s)
{
    short *reconp;

    s->mb_info.mb_intra = 1;

    for (int i = 0; i < 6; i++) {
        reconp = s->mb_info.recon[i];
        memset(reconp, 0, 64 * sizeof(short));

        // 1. parse DC coeff
        decode_dc(s, i, &reconp[0]);

        logi("DC coeff: %d, i: %d, mb_intra: %ld, showbits: %x", reconp[0], i, s->mb_info.mb_intra, show_bits(&s->gb, 32));

        int bits = read_bits_left(&s->gb);
        // 2. AC coeff
        if (bits >= 2) {
            if ((int)show_bits(&s->gb, 2) == (int)0x2) {
                skip_bits(&s->gb, 2);
            } else if (s->pic.picture_coding_type != MP2_D_PICTURE) {
                decode_ac(s, i);
            }
        }

        save_mb_coeffs(s, i);
    }

    return 0;
}

static int mpeg1_inter_6block(struct mpeg12_dec_ctx *s)
{
    int i;
    short *reconp;

    for (i = 0; i < 6; i++) {
        reconp = s->mb_info.recon[i];
        memset(reconp, 0, 64 * sizeof(short));

        logd("cbp:%lx i:%d ", s->mb_info.cbp, i);

        if (s->mb_info.cbp & (0x20 >> i)) {
            reconp = s->mb_info.recon[i];
            // 5.1. parse non-intra block DCT coeff
            // mpeg1_nonotra_block
            decode_ac(s, i);

            save_mb_coeffs(s, i);
        }
    }

    return 0;
}

static int mpeg2_parse_mb_type(struct mpeg12_dec_ctx *s)
{
    struct vlc_tab1 *tab;
    int mb_type = 0;

    // Parse mb_type according to picture_type
    if (s->pic.picture_coding_type == MP2_I_PICTURE) {
        if (read_bits(&s->gb, 1)) {
            s->mb_info.mb_quant = 0;
        } else {
            skip_bits(&s->gb, 1);
            s->mb_info.mb_quant = 1;
        }
        s->mb_info.mb_intra = 1;
        s->mb_info.mb_pattern = 0;
        s->mb_info.motion_backward = 0;
        s->mb_info.motion_forward = 0;
        s->mb_info.cbp = 0x3f;
        s->mb_info.spatial_temporal_weight_code_flag = 0;
        return 0;
    } else if (s->pic.picture_coding_type == MP2_P_PICTURE) {
        tab = &macroblock_type_p_table[show_bits(&s->gb, 6)];
        if (tab->len) {
            skip_bits(&s->gb, tab->len);
            mb_type = tab->value;
        } else {
            loge("macroblock_type error in P picture!\n");
            return -1;
        }
    } else { // B picture
        tab = &macroblock_type_b_table[show_bits(&s->gb, 6)];
        if (tab->len) {
            skip_bits(&s->gb, tab->len);
            mb_type = tab->value;
        } else {
            loge("macroblock_type error in P picture!\n");
            return -1;
        }
    }

    s->mb_info.mb_intra = mb_type & 0x10;
    s->mb_info.mb_pattern = mb_type & 0x08;
    s->mb_info.motion_backward = mb_type & 0x04;
    s->mb_info.motion_forward = mb_type & 0x02;
    s->mb_info.mb_quant = mb_type & 0x01;

    return 0;
}

static int mpeg2_handle_intra_mb(struct mpeg12_dec_ctx *s)
{
    // Parse intra-mb info
    if (s->pic_code_extension.picture_structure == FRAME && s->pic_code_extension.frame_pred_frame_dct == 0) {
        s->mb_info.dct_type = read_bits(&s->gb, 1);
        logd("=====> dct_type: %ld", s->mb_info.dct_type);
    } else {
        s->mb_info.dct_type = 0;
    }

    if (s->mb_info.mb_quant) {
        s->mb_info.quantiser_scale = mpeg_get_qscale(s);
    }

    if (s->pic_code_extension.concealment_motion_vectors) {
        if (s->pic_code_extension.picture_structure == FRAME) {
            s->mb_info.motion_type = FRAME_BASED;
            s->mb_info.mv_format = MV_FORMAT_FRAME;
        } else {
            s->mb_info.motion_type = FIELD_BASED;
            s->mb_info.mv_format = MV_FORMAT_FIELD;
        }

        s->mb_info.motion_vector_count = 1;
        s->mb_info.dmv = 0;
    }

    if (s->pic_code_extension.concealment_motion_vectors) {
        if (mpeg2_motion_vectors(s, 0)) {
            return -1;
        }
        skip_bits(&s->gb, 1);
    } else {
        // reset mv predictions
        s->mb_info.pmv[0][0][0] = s->mb_info.pmv[0][0][1] = 0;
        s->mb_info.pmv[0][1][0] = s->mb_info.pmv[0][1][1] = 0;
        s->mb_info.pmv[1][0][0] = s->mb_info.pmv[1][0][1] = 0;
        s->mb_info.pmv[1][1][0] = s->mb_info.pmv[1][1][1] = 0;
    }

    return 0;
}

static int mpeg2_handle_zero_mv_mb(struct mpeg12_dec_ctx *s)
{
    if (!s->mb_info.mb_pattern) {
        loge("====> mb error, zero mv but mb_pattern is 0");
        return -1;
    }

    // init dct type here
    s->mb_info.dct_type = 0;
    // Refer to ffmpeg mpeg12dec.c line 834
    if (s->pic_code_extension.picture_structure == FRAME) {
        if (!s->pic_code_extension.frame_pred_frame_dct)
            s->mb_info.dct_type = read_bits(&s->gb, 1);
        s->mb_info.motion_type = FRAME_BASED;
    } else {
        s->mb_info.motion_type = FIELD_BASED;
        s->mb_info.motion_vertical_field_select[0][0] = s->pic_code_extension.picture_structure - 1;
    }

    if (s->mb_info.mb_quant)
        s->mb_info.quantiser_scale = mpeg_get_qscale(s);

    // For zero MV blocks, use previous prediction values
    s->mb_info.motion_forward = 1;
    s->mb_info.motion_backward = 0;
    s->mb_info.mv[0][0][0] = 0;
    s->mb_info.mv[0][0][1] = 0;

    s->mb_info.pmv[0][0][0] = 0;
    s->mb_info.pmv[0][0][1] = 0;
    s->mb_info.pmv[1][0][0] = 0;
    s->mb_info.pmv[1][0][1] = 0;

    return 0;
}

static int mpeg2_handle_nonzero_mv_mb(struct mpeg12_dec_ctx *s)
{
    s->mb_info.motion_type = MOTION_TYPE_NOT_EXIST;
    if (s->pic_code_extension.picture_structure == FRAME) {
        if (s->pic_code_extension.frame_pred_frame_dct == 0) {
            s->mb_info.motion_type = read_bits(&s->gb, 2); // getbits(2);
        } else {
            s->mb_info.motion_type = FRAME_BASED;
        }

        if (s->mb_info.motion_type == FIELD_BASED) {
            s->mb_info.dmv = 0;
            s->mb_info.mv_format = MV_FORMAT_FIELD;
            s->mb_info.motion_vector_count = 2; //
        } else if (s->mb_info.motion_type == FRAME_BASED) {
            s->mb_info.dmv = 0;
            s->mb_info.mv_format = MV_FORMAT_FRAME;
            s->mb_info.motion_vector_count = 1;
        } else if (s->mb_info.motion_type == DUAL_PRIME) {
            s->mb_info.dmv = 1;
            s->mb_info.mv_format = MV_FORMAT_FIELD;
            s->mb_info.motion_vector_count = 1;
        } else {
            return -1;
        }
    } else { // field picture
        s->mb_info.mv_format = MV_FORMAT_FIELD;
        s->mb_info.motion_type = read_bits(&s->gb, 2); // getbits(2);
        if (s->mb_info.motion_type == FIELD_BASED) {
            s->mb_info.dmv = 0;
            s->mb_info.motion_vector_count = 1;
        } else if (s->mb_info.motion_type == SIXTEEN_BY_EIGHT_MC) {
            s->mb_info.dmv = 0;
            s->mb_info.motion_vector_count = 2;
        } else if (s->mb_info.motion_type == DUAL_PRIME) {
            s->mb_info.dmv = 1;
            s->mb_info.motion_vector_count = 1;
        } else {
            loge("field_motion_type error!\n");
            return -1;
        }
    }

    if (s->pic_code_extension.picture_structure == FRAME && !s->pic_code_extension.frame_pred_frame_dct && s->mb_info.mb_pattern)
        s->mb_info.dct_type = read_bits(&s->gb, 1);
    else
        s->mb_info.dct_type = 0;

    if (s->mb_info.mb_quant)
        s->mb_info.quantiser_scale = mpeg_get_qscale(s);

    logd("=== dct_type: %ld, quantiser_scale: %d", s->mb_info.dct_type, s->mb_info.quantiser_scale);

    if (s->mb_info.motion_forward)
        mpeg2_motion_vectors(s, 0);

    if (s->mb_info.motion_backward)
        mpeg2_motion_vectors(s, 1);

    if (s->mb_info.motion_type == DUAL_PRIME && s->pic_code_extension.picture_structure == FRAME) {
        // For dual-prime prediction, second MV is same as first MV
        // Refer to spec (ISO/IEC 13818-2) Table 7-14
        s->mb_info.mv[1][0][0] = s->mb_info.mv[0][0][0];
        s->mb_info.mv[1][0][1] = s->mb_info.mv[0][0][1];
        s->mb_info.mv[1][1][0] = s->mb_info.mv[0][1][0];
        s->mb_info.mv[1][1][1] = s->mb_info.mv[0][1][1];
    }

    logd("mv: forward: %d %d", s->mb_info.mv[0][0][0], s->mb_info.mv[0][0][1]);

    return 0;
}

static int mpeg2_parse_cbp(struct mpeg12_dec_ctx *s)
{
    struct vlc_tab1 *tab;

    // * parse cbp
    if (s->mb_info.mb_pattern) {
        tab = &coded_block_pattern_table[show_bits(&s->gb, 9)];
        if (tab->len) {
            skip_bits(&s->gb, tab->len);
            s->mb_info.cbp = tab->value;
        } else {
            loge("coded block pattern error!\n");
            return -1;
        }
    } else if (s->mb_info.mb_intra) {
        s->mb_info.cbp = 0x3f;
    } else {
        s->mb_info.cbp = 0;
    }

    return 0;
}

static int mpeg2_mb_modes(struct mpeg12_dec_ctx *s)
{
    // 1. Parse mb_type according to picture_type
    if (mpeg2_parse_mb_type(s) < 0) {
        return -1;
    }

    // 2. Parse macroblock_modes
    if (s->mb_info.mb_intra) {
        // 2.1  Parse intra-mb info
        if (mpeg2_handle_intra_mb(s) < 0) {
            return -1;
        }
    } else { // non-intra mb
        // 2.2 Handle zero MV macroblocks
        if (!s->mb_info.motion_forward && !s->mb_info.motion_backward) {
            if (mpeg2_handle_zero_mv_mb(s) < 0) {
                return -1;
            }
        } else {
            // 2.3 Parse non-zero MV non-intra macroblocks
            if (mpeg2_handle_nonzero_mv_mb(s) < 0) {
                return -1;
            }
        }
    }

    // * parse cbp
    if (mpeg2_parse_cbp(s) < 0) {
        return -1;
    }

    return 0;
}

static void mpeg1_motion_vector_back(struct mpeg12_dec_ctx *s)
{
    int complement_horizontal_backward_r;
    int complement_vertical_backward_r;
    int backward_f, back_max, back_min;
    int right_big, right_little;
    int down_big, down_little;
    int new_vector;
    int tmp;

    backward_f = 1 << (s->pic.backward_f_code - 1);
    back_max = (backward_f << 4) - 1;
    back_min = -(backward_f << 4);

    if (backward_f == 1 || s->mb_info.motion_horizontal_backward_code == 0)
        complement_horizontal_backward_r = 0;
    else
        complement_horizontal_backward_r = backward_f - 1 - s->mb_info.motion_horizontal_backward_r;

    if (backward_f == 1 || s->mb_info.motion_vertical_backward_code == 0)
        complement_vertical_backward_r = 0;
    else
        complement_vertical_backward_r = backward_f - 1 - s->mb_info.motion_vertical_backward_r;

    // gPicHeaderInfo.backward_f;
    right_little = s->mb_info.motion_horizontal_backward_code << (s->pic.backward_f_code - 1);
    if (right_little == 0) {
        right_big = 0;
    } else {
        if (right_little > 0) {
            right_little = right_little - complement_horizontal_backward_r;
            right_big = right_little - (backward_f << 5);
        } else {
            right_little = right_little + complement_horizontal_backward_r;
            right_big = right_little + (backward_f << 5);
        }
    }

    // gPicHeaderInfo.backward_f;
    down_little = s->mb_info.motion_vertical_backward_code << (s->pic.backward_f_code - 1);
    if (down_little == 0) {
        down_big = 0;
    } else {
        if (down_little > 0) {
            down_little = down_little - complement_vertical_backward_r;
            down_big = down_little - (backward_f << 5);
        } else {
            down_little = down_little + complement_vertical_backward_r;
            down_big = down_little + (backward_f << 5);
        }
    }

    new_vector = s->mb_info.mv_x_bwd_prev + right_little;
    if ((new_vector >= back_min) && (new_vector <= back_max))
        s->mb_info.mv_x_bwd = s->mb_info.mv_x_bwd_prev + right_little;
    else
        s->mb_info.mv_x_bwd = s->mb_info.mv_x_bwd_prev + right_big;

    s->mb_info.mv_x_bwd_prev = s->mb_info.mv_x_bwd;
    if (s->pic.full_pel_backward_vector)
        s->mb_info.mv_x_bwd <<= 1;

    new_vector = s->mb_info.mv_y_bwd_prev + down_little;
    if ((new_vector >= back_min) && (new_vector <= back_max))
        s->mb_info.mv_y_bwd = s->mb_info.mv_y_bwd_prev + down_little;
    else
        s->mb_info.mv_y_bwd = s->mb_info.mv_y_bwd_prev + down_big;

    s->mb_info.mv_y_bwd_prev = s->mb_info.mv_y_bwd;
    if (s->pic.full_pel_backward_vector)
        s->mb_info.mv_y_bwd <<= 1;

    s->mb_info.right_back_y = s->mb_info.mv_x_bwd >> 1;
    s->mb_info.down_back_y = s->mb_info.mv_y_bwd >> 1;
    s->mb_info.right_back_c = (s->mb_info.mv_x_bwd / 2) >> 1;
    s->mb_info.down_back_c = (s->mb_info.mv_y_bwd / 2) >> 1;

    s->mb_info.right_half_back_y = s->mb_info.mv_x_bwd & 0x01;
    s->mb_info.down_half_back_y = s->mb_info.mv_y_bwd & 0x01;
    tmp = s->mb_info.mv_x_bwd / 2;
    s->mb_info.right_half_back_c = tmp & 0x01;
    tmp = s->mb_info.mv_y_bwd / 2;
    s->mb_info.down_half_back_c = tmp & 0x01;
}

static void mpeg1_motion_vector_for(struct mpeg12_dec_ctx *s)
{
    int complement_horizontal_forward_r;
    int complement_vertical_forward_r;
    int right_big, right_little;
    int down_big, down_little;
    int new_vector;
    int tmp;

    int forward_f = 1 << (s->pic.forward_f_code - 1);
    int for_max = (forward_f << 4) - 1;
    int for_min = -(forward_f << 4);

    if (forward_f == 1 || s->mb_info.motion_horizontal_forward_code == 0)
        complement_horizontal_forward_r = 0;
    else
        complement_horizontal_forward_r = forward_f - 1 - s->mb_info.motion_horizontal_forward_r;

    if (forward_f == 1 || s->mb_info.motion_vertical_forward_code == 0)
        complement_vertical_forward_r = 0;
    else
        complement_vertical_forward_r = forward_f - 1 - s->mb_info.motion_vertical_forward_r;

    // gPicHeaderInfo.forward_f;
    right_little = s->mb_info.motion_horizontal_forward_code << (s->pic.forward_f_code - 1);
    if (right_little == 0) {
        right_big = 0;
    } else {
        if (right_little > 0) {
            right_little = right_little - complement_horizontal_forward_r;
            right_big = right_little - (forward_f << 5);
        } else {
            right_little = right_little + complement_horizontal_forward_r;
            right_big = right_little + (forward_f << 5);
        }
    }

    down_little = s->mb_info.motion_vertical_forward_code << (s->pic.forward_f_code - 1); // gPicHeaderInfo.forward_f;
    if (down_little == 0) {
        down_big = 0;
    } else {
        if (down_little > 0) {
            down_little = down_little - complement_vertical_forward_r;
            down_big = down_little - (forward_f << 5);
        } else {
            down_little = down_little + complement_vertical_forward_r;
            down_big = down_little + (forward_f << 5);
        }
    }

    new_vector = s->mb_info.mv_x_for_prev + right_little;
    if ((new_vector >= for_min) && (new_vector <= for_max))
        s->mb_info.mv_x_for = s->mb_info.mv_x_for_prev + right_little;
    else
        s->mb_info.mv_x_for = s->mb_info.mv_x_for_prev + right_big;

    s->mb_info.mv_x_for_prev = s->mb_info.mv_x_for;
    if (s->pic.full_pel_forward_vector)
        s->mb_info.mv_x_for <<= 1;

    new_vector = s->mb_info.mv_y_for_prev + down_little;
    if ((new_vector >= for_min) && (new_vector <= for_max))
        s->mb_info.mv_y_for = s->mb_info.mv_y_for_prev + down_little;
    else
        s->mb_info.mv_y_for = s->mb_info.mv_y_for_prev + down_big;

    s->mb_info.mv_y_for_prev = s->mb_info.mv_y_for;
    if (s->pic.full_pel_forward_vector)
        s->mb_info.mv_y_for <<= 1;

    s->mb_info.right_for_y = s->mb_info.mv_x_for >> 1;
    s->mb_info.down_for_y = s->mb_info.mv_y_for >> 1;
    s->mb_info.right_for_c = (s->mb_info.mv_x_for / 2) >> 1;
    s->mb_info.down_for_c = (s->mb_info.mv_y_for / 2) >> 1;

    s->mb_info.right_half_for_y = s->mb_info.mv_x_for & 0x01;
    s->mb_info.down_half_for_y = s->mb_info.mv_y_for & 0x01;
    tmp = s->mb_info.mv_x_for / 2;
    s->mb_info.right_half_for_c = tmp & 0x01;
    tmp = s->mb_info.mv_y_for / 2;
    s->mb_info.down_half_for_c = tmp & 0x01;
}

int mpeg1_calc_mv(struct mpeg12_dec_ctx *s)
{
    struct vlc_tab1 *tab;

    // 1. forward mv
    if (s->mb_info.motion_forward) {
        tab = &motion_code_table[show_bits(&s->gb, 11)];
        if (tab->len) {
            skip_bits(&s->gb, tab->len);
            s->mb_info.motion_horizontal_forward_code = tab->value;
            s->mb_info.motion_code[0][0][0] = s->mb_info.motion_horizontal_forward_code;
        } else {
            loge("motion_code horizointal error");
            return -1;
        }

        if (s->pic.forward_f_code != 1 && s->mb_info.motion_horizontal_forward_code) {
            s->mb_info.motion_horizontal_forward_r = read_bits(&s->gb, s->pic.forward_f_code - 1);
            s->mb_info.motion_residual[0][0][0] = s->mb_info.motion_horizontal_forward_r;
        }

        tab = &motion_code_table[show_bits(&s->gb, 11)];
        if (tab->len) {
            skip_bits(&s->gb, tab->len);
            s->mb_info.motion_vertical_forward_code = tab->value;
            s->mb_info.motion_code[0][0][1] = s->mb_info.motion_vertical_forward_code;
        } else {
            loge("motion_code vertical error");
            return -1;
        }

        if (s->pic.forward_f_code != 1 && s->mb_info.motion_vertical_forward_code != 0) {
            s->mb_info.motion_vertical_forward_r = read_bits(&s->gb, s->pic.forward_f_code - 1);
            s->mb_info.motion_residual[0][0][1] = s->mb_info.motion_vertical_forward_r;
        }

        // calc forward motion vector
        mpeg1_motion_vector_for(s);
    } else {
        // If the current P macroblock does not encode mv, set the current macroblock mv information to 0;
        // If it is a B frame, use the mv information from the previous macroblock
        if (s->pic.picture_coding_type == MP2_P_PICTURE) {
            s->mb_info.mv_x_for = 0;
            s->mb_info.mv_y_for = 0;
            s->mb_info.mv_x_for_prev = 0;
            s->mb_info.mv_y_for_prev = 0;
            s->mb_info.right_half_for_y = 0;
            s->mb_info.down_half_for_y = 0;
            s->mb_info.right_half_for_c = 0;
            s->mb_info.down_half_for_c = 0;
            s->mb_info.pmv[0][0][0] = 0;
            s->mb_info.pmv[0][0][1] = 0;
            s->mb_info.pmv[1][0][0] = 0;
            s->mb_info.pmv[1][0][1] = 0;
        } else {    // B picture
            // use last mb info
            s->mb_info.mv_x_for = s->mb_info.mv_x_for_prev;
            s->mb_info.mv_y_for = s->mb_info.mv_y_for_prev;
        }
    }

    // 2. backward mv
    if (s->mb_info.motion_backward) {
        tab = &motion_code_table[show_bits(&s->gb, 11)];
        if (tab->len) {
            skip_bits(&s->gb, tab->len);
            s->mb_info.motion_horizontal_backward_code = tab->value;
            s->mb_info.motion_code[0][1][0] = s->mb_info.motion_horizontal_backward_code;
        } else {
            loge("motion_code horizointal error");
            return -1;
        }

        if (s->pic.backward_f_code != 1 && s->mb_info.motion_horizontal_backward_code) {
            s->mb_info.motion_horizontal_backward_r = read_bits(&s->gb, s->pic.backward_f_code - 1);
            s->mb_info.motion_residual[0][1][0] = s->mb_info.motion_horizontal_backward_r;
        }

        tab = &motion_code_table[show_bits(&s->gb, 11)];
        if (tab->len) {
            skip_bits(&s->gb, tab->len);
            s->mb_info.motion_vertical_backward_code = tab->value;
            s->mb_info.motion_code[0][1][1] = s->mb_info.motion_vertical_backward_code;
        } else {
            loge("motion_code vertical error");
            return -1;
        }

        if (s->pic.backward_f_code != 1 && s->mb_info.motion_vertical_backward_code != 0) {
            s->mb_info.motion_vertical_backward_r = read_bits(&s->gb, s->pic.backward_f_code - 1);
            s->mb_info.motion_residual[0][1][1] = s->mb_info.motion_vertical_backward_r;
        }

        // calc backward motion vector
        mpeg1_motion_vector_back(s);
    } else {
        s->mb_info.mv_x_bwd = s->mb_info.mv_x_bwd_prev;
        s->mb_info.mv_y_bwd = s->mb_info.mv_y_bwd_prev;
    }

    s->mb_info.mv[0][0][0] = s->mb_info.mv_x_for;
    s->mb_info.mv[0][0][1] = s->mb_info.mv_y_for;
    s->mb_info.mv[0][1][0] = s->mb_info.mv_x_bwd;
    s->mb_info.mv[0][1][1] = s->mb_info.mv_y_bwd;

    return 0;
}

void mpeg1_decode_skipped_mb(struct mpeg12_dec_ctx *s, int mb_idx)
{
    s->mb_info.mb_intra = 0;
    s->mb_info.cbp = 0;
    if (s->pic.picture_coding_type == MP2_P_PICTURE) {
        // For skipped macroblocks in P frames, mv is 0, and reconstructed
        // macroblocks directly get data from reference frames
        s->mb_info.motion_backward = 0;
        s->mb_info.motion_forward = 1;
        s->mb_info.mv[0][0][0] = s->mb_info.mv[0][0][1] = 0;
        s->mb_info.pmv[0][0][0] = s->mb_info.pmv[0][0][1] = 0;
        s->mb_info.pmv[1][0][0] = s->mb_info.pmv[1][0][1] = 0;
        s->mb_info.motion_vertical_field_select[0][0] = (s->pic_code_extension.picture_structure - 1) & 1;
    } else {
        // For skipped macroblocks in B frames, mb_type and mv inherit from the
        // previous macroblock, motion compensation is needed
        // motion_backward/motion_forward use the param of last mb, donot set here
        s->mb_info.mv[0][0][0] = s->mb_info.pmv[0][0][0];
        s->mb_info.mv[0][0][1] = s->mb_info.pmv[0][0][1];
        s->mb_info.mv[0][1][0] = s->mb_info.pmv[0][1][0];
        s->mb_info.mv[0][1][1] = s->mb_info.pmv[0][1][1];
        s->mb_info.motion_vertical_field_select[0][0] = (s->pic_code_extension.picture_structure - 1) & 1;
        s->mb_info.motion_vertical_field_select[0][1] = (s->pic_code_extension.picture_structure - 1) & 1;
    }
}
static int mpeg1_macroblock(struct mpeg12_dec_ctx *s)
{
    struct vlc_tab1 *tab;
    uint32_t nextbit11;
    int mb_addr_inc;
    int n = 0;
    int i = 0;

    while ((nextbit11 = show_bits(&s->gb, 11)) == 0x0f)     // macroblock_stuffing
        skip_bits(&s->gb, 11);

    while ((nextbit11 = show_bits(&s->gb, 11)) == 0x08) {   // macroblock_escape
        skip_bits(&s->gb, 11);
        n += 33;
    }

    tab = &macroblock_address_increment_table[show_bits(&s->gb, 11)];
    if (tab->len) {
        skip_bits(&s->gb, tab->len);
        mb_addr_inc = tab->value;
    } else {
        loge("macroblock_address_increment error!");
        loge("showbits: %x", show_bits(&s->gb, 11));
        loge("left bits: %d", read_bits_left(&s->gb));
        return -1;
    }

    n += mb_addr_inc;
    s->cur_mblk_addr = s->prev_mblk_addr + n;
    s->mb_info.macroblock_address_increment = n;
    s->mb_x = s->cur_mblk_addr % s->mb_width;
    s->mb_y = s->cur_mblk_addr / s->mb_width;
    s->rsim_mb_y = s->mb_y;
    s->mb_info.motion_type = FRAME_BASED;
    s->pic_code_extension.picture_structure = FRAME;

    if (s->cur_mblk_addr > s->mb_height * s->mb_width) {
        loge("cur_mb_addr(%d) > total mb num:%d %d", s->cur_mblk_addr, s->mb_height, s->mb_width);
        // return -1;
    }

    logd("mb (%d %d) n:%d cur_mblk_addr:%d mb_width:%d", s->mb_x, s->rsim_mb_y, n, s->cur_mblk_addr, s->mb_width);
    // 1. decode skipped macroblock in P/B picture
    if (n > 1 && s->pic.picture_coding_type != MP2_I_PICTURE) {
        int cur_mb_addr = s->cur_mblk_addr;
        int mb_x = s->mb_x;
        int mb_y = s->mb_y;
        for (i = s->prev_mblk_addr + 1; i < cur_mb_addr; i++) {
            s->mb_x = i % s->mb_width;
            s->mb_y = i / s->mb_width;
            s->rsim_mb_y = s->mb_y;
            s->cur_mblk_addr = s->mb_x + s->mb_y * s->mb_width;
            s->mb_cfg_info_list[s->cur_mblk_addr].ptr = s->cur_mb_cfg_ptr;
            s->mb_cfg_info_list[s->cur_mblk_addr].len = 1;

            logd("mb_pos: (x:%d, y:%d)\n", s->mb_x, s->rsim_mb_y);

            s->mb_info.motion_type = FRAME_BASED;
            mpeg1_decode_skipped_mb(s, i);
            config_mpeg12_mb_info(s);
            write_mb_info_header(s);
        }
        s->mb_x = mb_x;
        s->mb_y = mb_y;
        s->rsim_mb_y = s->mb_y;
        s->cur_mblk_addr = cur_mb_addr;
        s->last_dc[0] = s->last_dc[1] = s->last_dc[2] = 128;
    }

#ifdef MPEG12_DUMP_ENABLE
    fprintf(s->fp_mb_coeff, "mb (%d %d)\n", s->mb_x, s->rsim_mb_y);
#endif

    s->mb_cfg_info_list[s->cur_mblk_addr].ptr = s->cur_mb_cfg_ptr;
    s->mb_cfg_info_list[s->cur_mblk_addr].len = 1;

    if (s->pic.picture_coding_type == MP2_I_PICTURE) {
        if (read_bits(&s->gb, 1)) {
            s->mb_info.mb_quant = 0;
        } else {
            // mb_type vlc code is 01
            skip_bits(&s->gb, 1);
            s->mb_info.mb_quant = 1;
            s->mb_info.quantiser_scale = mpeg_get_qscale(s); // read_bits(&s->gb, 5);
        }

        // cbp is 0x3f for intra macroblock
        s->mb_info.cbp = 0x3f;
        s->mb_info.mb_intra = 1;

        config_mpeg12_mb_info(s); // todo

        mpeg1_intra_6block(s);
    } else if (s->pic.picture_coding_type == MP2_P_PICTURE || s->pic.picture_coding_type == MP2_B_PICTURE) {
        // 1. parse macroblock type
        if (s->pic.picture_coding_type == MP2_P_PICTURE)
            tab = &macroblock_type_p_table[show_bits(&s->gb, 6)];
        else
            tab = &macroblock_type_b_table[show_bits(&s->gb, 6)];

        if (tab->len) {
            skip_bits(&s->gb, tab->len);
            s->mb_info.mb_type = tab->value;
        } else {
            loge("macroblock type error");
            return -1;
        }

        // mb_type = mb_intra | mb_pattern | motion_backward | motion_forward | mb_quant
        s->mb_info.mb_intra = s->mb_info.mb_type & 0x10;
        s->mb_info.mb_pattern = s->mb_info.mb_type & 0x08;
        if (s->pic.picture_coding_type == MP2_P_PICTURE)
            s->mb_info.motion_backward = 0;
        else
            s->mb_info.motion_backward = s->mb_info.mb_type & 0x04;

        s->mb_info.motion_forward = s->mb_info.mb_type & 0x02;
        s->mb_info.mb_quant = s->mb_info.mb_type & 0x01;

        logd("mb_type: %lx, mb_intra: %lx forward: %lx, backward: %lx, pattern: %lx\n", s->mb_info.mb_type, s->mb_info.mb_intra,
             s->mb_info.motion_forward, s->mb_info.motion_backward, s->mb_info.mb_pattern);

        if (n > 1) {
            if (s->pic.picture_coding_type == MP2_P_PICTURE) {
                s->mb_info.mv_x_for_prev = 0;
                s->mb_info.mv_y_for_prev = 0;
                s->mb_info.pmv[0][0][0] = 0;
                s->mb_info.pmv[0][0][1] = 0;
                s->mb_info.pmv[1][0][0] = 0;
                s->mb_info.pmv[1][0][1] = 0;
            }
            s->last_dc[0] = 128;
            s->last_dc[1] = 128;
            s->last_dc[2] = 128;
        }

        if (s->mb_info.mb_intra) {
            s->mb_info.mv_x_for_prev = 0;
            s->mb_info.mv_y_for_prev = 0;
            s->mb_info.mv_x_bwd_prev = 0;
            s->mb_info.mv_y_bwd_prev = 0;
            s->mb_info.pmv[0][0][0] = 0;
            s->mb_info.pmv[0][0][1] = 0;
            s->mb_info.pmv[1][0][0] = 0;
            s->mb_info.pmv[1][0][1] = 0;
        } else {
            // DC prediction values need to be reset to 128 for:
            // 1.skipped mb
            // 2. non-intra mb
            s->last_dc[0] = s->last_dc[1] = s->last_dc[2] = 128;
        }

        // 2. update q_scale
        if (s->mb_info.mb_quant) {
            s->mb_info.quantiser_scale = mpeg_get_qscale(s);
        }

        // 3. calc forward motion vector
        mpeg1_calc_mv(s);

        // 4. get cbp
        if (s->mb_info.mb_pattern) {
            tab = &coded_block_pattern_table[show_bits(&s->gb, 9)];

            if (tab->len) {
                skip_bits(&s->gb, tab->len);
                s->mb_info.cbp = tab->value;
            } else {
                loge("coded_block_pattern code error!\n");
                return -1;
            }
        } else if (s->mb_info.mb_intra) {
            s->mb_info.cbp = 0x3f;
        } else {
            s->mb_info.cbp = 0;
        }

        if (!s->mb_info.motion_forward && !s->mb_info.motion_backward) {
            s->mb_info.motion_forward = 1;
            s->mb_info.motion_backward = 0;
            s->mb_info.mv[0][0][0] = 0;
            s->mb_info.mv[0][0][1] = 0;
            s->mb_info.mv_x_bwd_prev = s->mb_info.mv_x_bwd = 0;
            s->mb_info.mv_y_bwd_prev = s->mb_info.mv_y_bwd = 0;
            s->mb_info.mv_x_for_prev = s->mb_info.mv_x_for = 0;
            s->mb_info.mv_y_for_prev = s->mb_info.mv_y_for = 0;

            s->mb_info.pmv[0][0][0] = 0;
            s->mb_info.pmv[0][0][1] = 0;
            s->mb_info.pmv[1][0][0] = 0;
            s->mb_info.pmv[1][0][1] = 0;
        }

        config_mpeg12_mb_info(s);

        logd("<%s:%d> mb_pos: (x:%02x, y:%02x)\n", __func__, __LINE__, s->mb_x, s->rsim_mb_y);
        logd("cbp: %lx, intra: %ld, quant: %ld, forward: %ld, backward: %ld, mb_pattern: %ld\n", s->mb_info.cbp, s->mb_info.mb_intra,
             s->mb_info.mb_quant, s->mb_info.motion_forward, s->mb_info.motion_backward, s->mb_info.mb_pattern);

        // 5. decode the macroblock
        memset(s->mb_info.recon[0], 0, 768); // 64*sizeof(short)*6
        if (!s->mb_info.mb_intra) {
            if (s->mb_info.mb_pattern)
                mpeg1_inter_6block(s);

            // 6. do mc
            // mb_do_mc_mpeg2(s, s->mb_x, s->mb_y);
        } else {
            mpeg1_intra_6block(s);
        }
    }

    write_mb_info_header(s);
    s->prev_mblk_addr = s->cur_mblk_addr;

    return 0;
}

void mpeg2_decode_skipped_mb(struct mpeg12_dec_ctx *s, int mb_idx)
{
    int field_pic = s->pic_code_extension.picture_structure != FRAME;

    s->mb_x++;
    while (s->mb_x >= s->mb_width) {
        s->mb_x -= s->mb_width;
        s->mb_y += (1 << field_pic);
        s->rsim_mb_y++;
    }

    if (s->pic_code_extension.picture_structure == FRAME)
        s->mb_info.motion_type = FRAME_BASED;
    else
        s->mb_info.motion_type = FIELD_BASED;

    s->mb_info.mb_intra = 0;
    s->mb_info.cbp = 0;

    if (s->pic.picture_coding_type == MP2_P_PICTURE) {
        // For skipped macroblocks in P frames, mv is 0, and reconstructed
        // macroblocks directly get data from reference frames
        s->mb_info.motion_backward = 0;
        s->mb_info.motion_forward = 1;
        s->mb_info.mv[0][0][0] = s->mb_info.mv[0][0][1] = 0;
        s->mb_info.pmv[0][0][0] = s->mb_info.pmv[0][0][1] = 0;
        s->mb_info.pmv[1][0][0] = s->mb_info.pmv[1][0][1] = 0;
        s->mb_info.motion_vertical_field_select[0][0] = (s->pic_code_extension.picture_structure - 1) & 1;
    } else {
        // For skipped macroblocks in B frames, mb_type and mv inherit from the
        // previous macroblock, motion compensation is needed
        s->mb_info.mv[0][0][0] = s->mb_info.pmv[0][0][0];
        s->mb_info.mv[0][0][1] = s->mb_info.pmv[0][0][1];
        s->mb_info.mv[0][1][0] = s->mb_info.pmv[0][1][0];
        s->mb_info.mv[0][1][1] = s->mb_info.pmv[0][1][1];
        s->mb_info.motion_vertical_field_select[0][0] = (s->pic_code_extension.picture_structure - 1) & 1;
        s->mb_info.motion_vertical_field_select[0][1] = (s->pic_code_extension.picture_structure - 1) & 1;
    }
    memset(s->mb_info.recon[0], 0, sizeof(short) * 384);
}

static int mpeg2_macroblock(struct mpeg12_dec_ctx *s)
{
    int n = 0, i = 0, mb_addr_inc, field_pic, cur_mblk_addr;
    struct vlc_tab1 *tab;
    uint32_t nextbit11;

    cur_mblk_addr = s->cur_mblk_addr;
    field_pic = s->pic_code_extension.picture_structure != FRAME;

    while ((nextbit11 = show_bits(&s->gb, 11)) == 0x08) {   // escape
        skip_bits(&s->gb, 11);
        n += 33;
    }

    tab = &macroblock_address_increment_table[show_bits(&s->gb, 11)];
    if (tab->len) {
        skip_bits(&s->gb, tab->len);
        mb_addr_inc = tab->value;
    } else {
        loge("macroblock_address_increment error!");
        return -1;
    }
    n += mb_addr_inc;
    cur_mblk_addr += n;
    s->mb_info.macroblock_address_increment = n;

    int mb_x_tmp = s->mb_x + n;
    // 1. decode skipped macroblock in P/B picture
    if (!s->first_mb_slice && (cur_mblk_addr - s->prev_mblk_addr > 1) && (s->pic.picture_coding_type != MP2_I_PICTURE)) {
        for (i = s->prev_mblk_addr + 1; i < cur_mblk_addr; i++) {
            mpeg2_decode_skipped_mb(s, i);

            s->cur_mblk_addr++;

            s->mb_cfg_info_list[s->cur_mblk_addr].ptr = s->cur_mb_cfg_ptr;
            s->mb_cfg_info_list[s->cur_mblk_addr].len = 1;
            config_mpeg12_mb_info(s);
            write_mb_info_header(s);
        }

        s->last_dc[0] = s->last_dc[1] = s->last_dc[2] = 1 << (7 + s->pic_code_extension.intra_dc_precision);
    }
    s->first_mb_slice = 0;
    s->cur_mblk_addr = cur_mblk_addr;
    s->mb_x = mb_x_tmp;
    while (s->mb_x >= s->mb_width) {
        s->mb_x -= s->mb_width;
        s->mb_y += (1 << field_pic);
        s->rsim_mb_y += 1;
    }

    s->cur_mblk_addr = s->rsim_mb_y * s->mb_width + s->mb_x;
    s->mb_cfg_info_list[s->cur_mblk_addr].ptr = s->cur_mb_cfg_ptr;
    s->mb_cfg_info_list[s->cur_mblk_addr].len = 1;

#ifdef MPEG12_DUMP_ENABLE
    fprintf(s->fp_mb_coeff, "mb (%d %d)\n", s->mb_x, s->rsim_mb_y);
#endif

    // 2. parse the mb info
    mpeg2_mb_modes(s);

    config_mpeg12_mb_info(s); // todo

    // 3. recon the mb pixels
    if (s->mb_info.mb_intra) {
        mpeg2_intra_6block(s);
    } else {    // non-intra mb
        mpeg2_inter_6block(s);

        // reset the DC pred value
        s->last_dc[0] = s->last_dc[1] = s->last_dc[2] = 1 << (7 + s->pic_code_extension.intra_dc_precision);
    }

    write_mb_info_header(s);
    s->prev_mblk_addr = s->cur_mblk_addr;

    return 0;
}

static int mpeg1_slice_header(struct mpeg12_dec_ctx *s)
{
    s->last_dc[0] = 128 << s->pic_code_extension.intra_dc_precision;
    s->last_dc[1] = 128 << s->pic_code_extension.intra_dc_precision;
    s->last_dc[2] = 128 << s->pic_code_extension.intra_dc_precision;

    logd("<%s:%d> last_dc: %d, %ld\n", __func__, __LINE__, s->last_dc[0], s->pic_code_extension.intra_dc_precision);

    if (s->is_mpeg2 && s->mb_height > 2800 / 16) {
        loge("======> slice_vertical_position_extension");
        skip_bits(&s->gb, 3); // slice_vertical_position_extension
    }

    s->first_mb_slice = 1;
    s->mb_info.quantiser_scale = mpeg_get_qscale(s); // s->sh.quantiser_scale;
    logd("<%s:%d> ===== qscale: %d, offset: %d\n", __func__, __LINE__, s->mb_info.quantiser_scale, read_bits_count(&s->gb));

    // reset the prev mv when first mb in the slice
    s->mb_info.mv_x_bwd_prev = 0;
    s->mb_info.mv_y_bwd_prev = 0;
    s->mb_info.mv_x_for_prev = 0;
    s->mb_info.mv_y_for_prev = 0;
    s->mb_info.pmv[0][0][0] = 0;
    s->mb_info.pmv[0][0][1] = 0;
    s->mb_info.pmv[1][0][0] = 0;
    s->mb_info.pmv[1][0][1] = 0;
    memset(s->mb_info.pmv, 0, 16 * sizeof(int));

    /* extra slice info */
    while (read_bits(&s->gb, 1)) {
        skip_bits(&s->gb, 8);
    }

    do {
        // decode mb
        if (s->is_mpeg2) {
            if (mpeg2_macroblock(s)) {
                loge("mpeg2_macroblock decode error");
                return -1;
            }
        } else {
            if (mpeg1_macroblock(s))
                return -1;
        }

#ifdef MPEG12_DUMP_ENABLE
        char motion_type_name[4][16] = {"8x8", "field_base", "frame_base", "dual-prime"};
        fprintf(s->fp_mb_coeff, "intra: %lu, fwd-pred: %lu, bwd-pred: %lu\n", s->mb_info.mb_intra, s->mb_info.motion_forward,
                s->mb_info.motion_backward);
        if (s->mb_info.motion_forward || s->mb_info.motion_backward) {
            fprintf(s->fp_mb_coeff, "mv: %d, %d\n", s->mb_info.mv[0][0][0], s->mb_info.mv[0][0][1]);
        }
        fprintf(s->fp_mb_coeff, "cbp: %lu, dct_type: %lu\n", s->mb_info.cbp, s->mb_info.dct_type);
        fprintf(s->fp_mb_coeff, "pic_struct: %lu, motion_type: %s\n", s->pic_code_extension.picture_structure,
                motion_type_name[s->mb_info.motion_type]);
        fprintf(s->fp_mb_coeff, "mb_cfg_info offset: %ld, len: %d\n", s->mb_cfg_info_list[s->cur_mblk_addr].ptr - s->mb_cfg_data,
                s->mb_cfg_info_list[s->cur_mblk_addr].len);
#endif

        int left_bits = read_bits_left(&s->gb);
        if ((0 == left_bits) || (left_bits < 23 && show_bits(&s->gb, left_bits) == 0)) {
            logd("slice finish, left bits:%d\n", left_bits);
            break;
        }
    } while (show_bits(&s->gb, 23) != 0);

    return 0;
}

static int decode_slice(struct mpeg12_dec_ctx *s, int start_code)
{
    int picture_end, field_pic;
    s->mb_x = -1;

    field_pic = (s->pic_code_extension.picture_structure != FRAME);

    if (read_bits_left(&s->gb) < 8) {
        loge("data is not enough, eos");
        return -1; // todo EOS
    }

    s->sh.vertical_position = start_code;
    s->mb_y = (s->sh.vertical_position - 1) << field_pic;
    if (s->pic_code_extension.picture_structure == BOTTOM_FIELD) {
        s->mb_y++;
    }
    s->rsim_mb_y = s->sh.vertical_position - 1;
    s->mb_x = -1;

    // decode slice
    if (mpeg1_slice_header(s)) {
        return -1;
    }

    if (s->pic_code_extension.picture_structure == FRAME)
        picture_end = (s->mb_x == (s->mb_width - 1) && s->mb_y == (s->mb_height - 1));
    else
        picture_end = (s->mb_x == (s->mb_width - 1) && s->rsim_mb_y == (s->mb_height / 2 - 1));

    // frame or field parse finish
    if (picture_end) {

        ve_decode_slice(s);
        s->pic_num++;

        int mb_cfg_list_size = s->mb_width * s->mb_height * sizeof(struct mb_config_info);
        memset(s->mb_cfg_info_list, 0, mb_cfg_list_size);

        int mb_cfg_size = s->mb_width * s->mb_height * MB_INFO_SIZE * sizeof(uint32_t);
        memset(s->mb_cfg_data, 0, mb_cfg_size);
        s->cur_mb_cfg_ptr = s->mb_cfg_data;

        s->mb_cfg_addr = 0;
        s->picture_end = 1;
    }

    return 0;
}

static int output_yuv(struct mpeg12_dec_ctx *s)
{
    struct mpp_buf *video = NULL;
    int data_size[3] = {0};
    int idx = 0;

    while (1) {
        for (idx = 0; idx < FRAME_BUFFER_NUM; idx++) {
            if (s->frame_buf[idx].display_count == s->display_pic_num) {
                logd("find a display buffer, idx(%d), disp_cnt: %d, display_num(%d)", idx, s->frame_buf[idx].display_count,
                     s->display_pic_num);
                break;
            }
        }

        if (idx == FRAME_BUFFER_NUM) {
            break;
        }

        video = &s->frame_buf[idx].frame->mpp_frame.buf;
        if (NULL == video) {
            loge("video is null, idx:%d frame:%p", idx, s->frame_buf[idx].frame);
            return -1;
        }
        data_size[0] = video->size.height * video->stride[0];
        data_size[1] = data_size[2] = data_size[0] / 4;

#ifdef MPEG12_DUMP_ENABLE
        unsigned char *hw_data[3] = {0};
        char path[512] = {0};
        FILE *fp = NULL;
        for (int i = 0; i < 3; i++) {
            hw_data[i] = mmap(NULL, data_size[i], PROT_READ, MAP_SHARED, video->fd[i], 0);
            if (hw_data[i] == MAP_FAILED) {
                loge("dmabuf alloc mmap failed!");
                break;
            }

            snprintf(path, sizeof(path), "%s/comp%d%d.bin", s->asic_path, s->display_pic_num, i);
            fp = fopen(path, "wb");
            if (fp) {
                fwrite(hw_data[i], 1, data_size[i], fp);
                fclose(fp);
            } else {
                loge("fopen file failed!");
            }

            munmap(hw_data[i], data_size[i]);
        }
#endif
        if (s->decoder.rotmir_flag) {
            if (s->cur_rotmir_idx < 0) {
                loge("cur_rotmir_idx < 0!");
                continue;
            }

            fm_decoder_frame_to_render(s->decoder.fm, s->frame_buf[s->cur_rotmir_idx].frame, 1);
            fm_decoder_frame_to_render(s->decoder.fm, s->frame_buf[s->cur_pic_idx].frame, 0);
        } else {
            fm_decoder_frame_to_render(s->decoder.fm, s->frame_buf[s->cur_pic_idx].frame, 1);
        }

        if (s->pic.picture_coding_type == MP2_B_PICTURE) {
            fm_decoder_put_frame(s->decoder.fm, s->frame_buf[s->cur_pic_idx].frame);
            s->frame_buf[s->cur_pic_idx].is_ref = 0;
        }
        s->display_pic_num++;
    }

    return 0;
}

#ifdef MPEG12_DUMP_ENABLE
char g_mpeg12_name[256] = {"mpeg12"};
int mpeg12_set_file_path(char *file_path)
{
    int i, j, len;

    if (!file_path) {
        loge("set_filename: NULL file_path");
        return -1;
    }

    len = strlen(file_path);
    loge("file_path: %s", file_path);

    for (i = len - 1; i >= 0; i--) {
        if (file_path[i] == '.') {
            i--;
            break;
        }
    }

    if (i < 0)
        i = len - 1;

    for (j = len - 1; j >= 0; j--) {
        if (file_path[j] == '/') {
            break;
        }
    }

    if (j >= 0) {
        int copy_len = i - j;
        if (copy_len > 0 && copy_len < sizeof(g_mpeg12_name)) {
            strncpy(g_mpeg12_name, file_path + j + 1, copy_len);
            g_mpeg12_name[copy_len] = '\0';
        } else {
            g_mpeg12_name[0] = '\0';
        }
    } else {
        int copy_len = i + 1;
        if (copy_len > 0 && copy_len < sizeof(g_mpeg12_name)) {
            strncpy(g_mpeg12_name, file_path, copy_len);
            g_mpeg12_name[copy_len] = '\0';
        } else {
            g_mpeg12_name[0] = '\0';
        }
    }

    return 0;
}

void *init_debug(struct mpeg12_dec_ctx *s, const char *out_dir)
{
    char file_name[1024] = {0};
    char pattern_name[64] = {0};
    char file_dir_path[100] = {0};

    strcpy(pattern_name, g_mpeg12_name);

    // get the directory path
    snprintf(file_dir_path, sizeof(file_dir_path), "%s/ve_%s/", out_dir, pattern_name);
    mkdir(file_dir_path, 0755);

    loge("file_dir_path %s", file_dir_path);

    snprintf(s->ip_path, sizeof(s->ip_path), "%s/ip", file_dir_path);
    mkdir(s->ip_path, 0755);

    snprintf(s->asic_path, sizeof(s->asic_path), "%s/asic", file_dir_path);
    mkdir(s->asic_path, 0755);

    char tmp_log_dir[256];
    snprintf(tmp_log_dir, sizeof(tmp_log_dir),"%s/tmp_log", s->ip_path);
    mkdir(tmp_log_dir, 0755);

    snprintf(file_name, sizeof(file_name), "%s/mb_coeff.txt", tmp_log_dir);
    s->fp_mb_coeff = fopen(file_name, "wb");
    if (s->fp_mb_coeff == NULL)
        loge("open s->fp_mb_coeff failed, %s", file_name);

    snprintf(file_name, sizeof(file_name), "%s/mb_cfg_data.txt", tmp_log_dir);
    s->fp_mb_cfg_data = fopen(file_name, "wb");

    char path[512] = {0};
    snprintf(path, sizeof(path), "%s/aic_cmd.txt", s->ip_path);
    s->fp_reg = fopen(path, "wb");
    if (s->fp_reg == NULL) {
        loge("aic_cmd.txt open fialed");
    }

    return s;
}

int deinit_debug(struct mpeg12_dec_ctx *s)
{
    if (s->fp_mb_cfg_data)
        fclose(s->fp_mb_cfg_data);

    if (s->fp_mb_coeff)
        fclose(s->fp_mb_coeff);

    if (s->fp_reg) {
        fclose(s->fp_reg);
    }
    return 0;
}
#endif

int __mpeg12_decode_init(struct mpp_decoder *ctx, struct decode_config *config)
{
    struct mpeg12_dec_ctx *s = NULL;

    if ((NULL == ctx) || (NULL == config)) {
        loge("invalid parameter[%p, %p]", ctx, config);
        return -1;
    }

    s = (struct mpeg12_dec_ctx *)ctx;
    s->ve_buf_handle = ve_buffer_allocator_create(VE_BUFFER_TYPE_DMA);
    if (NULL == s->ve_buf_handle) {
        loge("ve_buffer_allocator_create failed");
        return -1;
    }

    s->extra_frame_num = config->extra_frame_num;

    struct packet_manager_init_cfg cfg;
    cfg.buffer_size = config->bitstream_buffer_size;
    cfg.ve_buf_handle = s->ve_buf_handle;
    cfg.packet_count = config->packet_count;
    s->decoder.pm = pm_create(&cfg);

    s->pix_format = config->pix_fmt;
    if (config->pix_fmt != MPP_FMT_YUV420P && config->pix_fmt != MPP_FMT_NV12 && config->pix_fmt != MPP_FMT_NV21) {
        logw("unsupport pix format, force to yuv420p");
        s->pix_format = MPP_FMT_YUV420P;
    }

    s->pic_code_extension.frame_pred_frame_dct = 1;
    s->pic_code_extension.chroma_420_type = 1;
    s->pic_code_extension.picture_structure = FRAME;
    s->pic_code_extension.q_scale_type = 0;

    return 0;
}

int __mpeg12_decode_destroy(struct mpp_decoder *ctx)
{
    struct mpeg12_dec_ctx *s = (struct mpeg12_dec_ctx *)ctx;

    if (NULL == s) {
        loge("invalid parameter");
        return -1;
    }

    mpeg_free_frame_buffer(s);

    if (s->decoder.pm) {
        pm_destroy(s->decoder.pm);
        s->decoder.pm = NULL;
    }

    if (s->decoder.fm) {
        fm_destory(s->decoder.fm);
        s->decoder.fm = NULL;
    }

    if (s->ve_buf_handle) {
        ve_buffer_allocator_destroy(s->ve_buf_handle);
        s->ve_buf_handle = NULL;
    }

    ve_close_device();

    mpp_free(s);

#ifdef MPEG12_DUMP_ENABLE
    deinit_debug(s);
#endif

    return 0;
}

int __mpeg12_decode_frame(struct mpp_decoder *ctx)
{
    struct mpeg12_dec_ctx *s = (struct mpeg12_dec_ctx *)ctx;
    int ret = 0, start_code, len, save_frame;
    unsigned char *data = NULL;
    int find_top_field = 0;
    int find_bot_field = 0;

    if (NULL == s) {
        loge("invalid parameter");
        return -1;
    }

    // 1. get a packet data
    s->curr_packet = pm_dequeue_ready_packet(s->decoder.pm);
    if (s->curr_packet == NULL) {
        loge("pm_dequeue_ready_packet error, ready_packet num: %d", pm_get_ready_packet_num(s->decoder.pm));
        return DEC_NO_READY_PACKET;
    }
    s->eos = s->curr_packet->flag & PACKET_FLAG_EOS;

    data = s->curr_packet->data;
    len = s->curr_packet->size;

#ifdef MPEG12_DUMP_ENABLE
    s->fp_es = fopen("/mnt/sdcard/es.bin", "wb");
    if (s->fp_es == NULL) {
        loge("es.bin open fialed");
    } else {
        int wt_len = fwrite(data, 1, len, s->fp_es);
        if (wt_len != len) {
            loge("write es error, len: %d, wt_len: %d", len, wt_len);
        }
    }
    fclose(s->fp_es);
    loge("<%s:%d> data len:%d\n", __func__, __LINE__, len);
#endif

    init_read_bits(&s->gb, data, len * 8, 0);

    do {
        start_code = process_start_code(s);
        if (start_code < 0) {
            pm_enqueue_empty_packet(s->decoder.pm, s->curr_packet);
            return -1;
        }

        logd("<%s:%d> start code: %x\n", __func__, __LINE__, start_code);
        if (0xb3 == start_code) { // sequence header
            process_sequence_header(s);
        } else if (0xb8 == start_code) { // gop
            s->display_num_base += s->gop_pic_num;
            s->gop_pic_num = 0;
            decode_group_of_picture(s);
        } else if (0x00 == start_code) { // picture
            ret = decode_picture_header(s, &s->pic);
            if (ret < 0) {
                loge("frame manager create failed, ret= %d", ret);
                return -1;
            } else if(ret == DEC_NO_EMPTY_FRAME) {
                loge("Decoder has no empty frame!");
                return ret;
            }
        } else if (0x01 <= start_code && start_code <= 0xaf) { // slice
            ret = decode_slice(s, start_code);

            if (0 != ret) {
                pm_enqueue_empty_packet(s->decoder.pm, s->curr_packet);
                return ret;
            }

            if (s->picture_end) {
                save_frame = 0;
                if (s->pic_code_extension.picture_structure == TOP_FIELD) {
                    if (find_bot_field) {
                        save_frame = 1;
                        find_bot_field = 0;
                    } else {
                        find_top_field = 1;
                    }
                }

                if (s->pic_code_extension.picture_structure == BOTTOM_FIELD) {
                    if (find_top_field) {
                        save_frame = 1;
                        find_top_field = 0;
                    } else {
                        find_bot_field = 1;
                    }
                }

                if (s->pic_code_extension.picture_structure == FRAME || !s->is_mpeg2) {
                    save_frame = 1;
                }

                logd("picture_structure:%ld save_frame:%d\n", s->pic_code_extension.picture_structure, save_frame);
                if (save_frame) {
                    s->need_get_frame = 1;
                    s->frame_buf[s->cur_pic_idx].decoded_count = s->decoded_pic_num;
                    output_yuv(s);

                    s->decoded_pic_num++;
                    s->gop_pic_num++;
                    // get an unused buffer
                    for (int i = 0; i < FRAME_BUFFER_NUM; i++) {
                        if (s->frame_buf[i].is_ref == 0) {
                            s->cur_pic_idx = i;
                            logd("cur_idx:%d", s->cur_pic_idx);
                            break;
                        }
                    }
                }
            }
        }
    } while (read_bits_left(&s->gb) > 32);

    pm_enqueue_empty_packet(s->decoder.pm, s->curr_packet);

    logd("__mpeg_decode_frame >>> out");

    return ret;
}

int __mpeg12_decode_control(struct mpp_decoder *ctx, int cmd, void *param)
{
    struct mpeg12_dec_ctx *s = (struct mpeg12_dec_ctx *)ctx;
    struct mpp_size *max_resolution = NULL;

    if (NULL == s) {
        loge("invalid parameter");
        return -1;
    }

    switch (cmd) {
    case MPP_DEC_INIT_CMD_SET_ROT_FLIP_FLAG:
        s->decoder.rotmir_flag = *(int *)param;
        return 0;
    case MPP_DEC_SET_MAX_RESOLUTION:
        max_resolution = (struct mpp_size *)param;
        s->max_width = max_resolution->width;
        s->max_height = max_resolution->height;
        return 0;
    default:
        break;
    }

    return 0;
}

int __mpeg12_decode_reset(struct mpp_decoder *ctx)
{
    struct mpeg12_dec_ctx *s = (struct mpeg12_dec_ctx *)ctx;

    if (NULL == s) {
        loge("invalid parameter");
        return -1;
    }

    fm_decoder_reclaim_all_used_frame(s->decoder.fm);

    fm_reset(s->decoder.fm);
    pm_reset(s->decoder.pm);

    return 0;
}

struct dec_ops mpeg12_decoder = {
    .name = "mpeg",
    .init = __mpeg12_decode_init,
    .destory = __mpeg12_decode_destroy,
    .decode = __mpeg12_decode_frame,
    .control = __mpeg12_decode_control,
    .reset = __mpeg12_decode_reset,
};

struct mpp_decoder *create_mpeg12_decoder()
{
    struct mpeg12_dec_ctx *s = (struct mpeg12_dec_ctx *)mpp_alloc(sizeof(struct mpeg12_dec_ctx));
    if (s == NULL) {
        return NULL;
    }

    init_vlcs();
    memset(s, 0, sizeof(struct mpeg12_dec_ctx));
    for (int i = 0; i < FRAME_BUFFER_NUM; i++) {
        s->frame_buf[i].decoded_count = -1;
        s->frame_buf[i].display_count = -1;
    }
    s->display_pic_num = 0;
    s->last_temporal_reference = -1;
    s->cur_pic_idx = -1;
    s->last_pic_idx = -1;
    s->next_pic_idx = -1;
    s->need_get_frame = 1;
    s->cur_rotmir_idx = -1;
    s->last_rotmir_idx = -1;
    s->h_offset = 0;
    s->v_offset = 0;
    s->rotmir_h_stride = 0;
    s->rotmir_v_stride = 0;
    s->rotmir_h_real_size = 0;
    s->rotmir_v_real_size = 0;

    s->decoder.ops = &mpeg12_decoder;
    if (ve_open_device() < 0) {
        mpp_free(s);
        return NULL;
    }
    s->regs_base = ve_get_reg_base();
    logd("ve_reg_base: %lx", s->regs_base);

    for (int i = 0; i < FRAME_BUFFER_NUM; i++) {
        s->frame_buf[i].decoded_count = -1;
        s->frame_buf[i].display_count = -1;
    }
    s->display_pic_num = 0;
    s->last_temporal_reference = -1;

#ifdef MPEG12_DUMP_ENABLE
    init_debug(s, "/mnt/sdcard");
#endif

    return &s->decoder;
}
