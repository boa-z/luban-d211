/*
 * Copyright (C) 2020-2026 ArtInChip Technology Co. Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 *  author: <xiaodong.zhao@artinchip.com>
 *  Desc: mpeg4 decoder interface
 *
 */

#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "mpeg4_decoder.h"
#include "mpp_log.h"
#include "mpp_mem.h"
#include "ve.h"

struct mpeg4_ctx *g_mpeg4_ctx = NULL;

// unsigned char *mpeg_yuv_buf;


char g_mpeg4_name[256] = {"mpeg4"};
int mpeg4_set_file_path(char *file_path)
{
    int i, j, len;

    if (!file_path) {
        loge("set_filename: NULL file_path");
        return -1;
    }

    len = strlen(file_path);
    printf("file_path:%s\n", file_path);

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
        if (copy_len > 0 && copy_len < sizeof(g_mpeg4_name)) {
            strncpy(g_mpeg4_name, file_path + j + 1, copy_len);
            g_mpeg4_name[copy_len] = '\0';
        } else {
            g_mpeg4_name[0] = '\0';
        }
    } else {
        int copy_len = i + 1;
        if (copy_len > 0 && copy_len < sizeof(g_mpeg4_name)) {
            strncpy(g_mpeg4_name, file_path, copy_len);
            g_mpeg4_name[copy_len] = '\0';
        } else {
            g_mpeg4_name[0] = '\0';
        }
    }

    return 0;
}

void mpeg4_init_debug(struct mpeg4_ctx *s, const char *out_dir)
{
    char file_dir_path[256] = {0};

    strcpy(s->pattern_name, g_mpeg4_name);

    // get the directory path
    snprintf(file_dir_path, sizeof(file_dir_path), "%s/ve_%s", out_dir, s->pattern_name);
    mkdir(file_dir_path, 0755);

#ifdef MPEG4_DUMP_ENABLE
    char file_name[640] = {0};
    snprintf(s->ip_path, sizeof(s->ip_path), "%s/ip", file_dir_path);
    mkdir(s->ip_path, 0755);

    snprintf(s->asic_path, sizeof(s->asic_path), "%s/asic", file_dir_path);
    mkdir(s->asic_path, 0755);

    char tmp_log_dir[512];
    snprintf(tmp_log_dir, sizeof(tmp_log_dir), "%s/tmp_log", s->ip_path);
    mkdir(tmp_log_dir, 0755);

    snprintf(file_name, sizeof(file_name), "%s/mb_coeff.txt", tmp_log_dir);
    s->fp_mb_coeff = fopen(file_name, "wb");
    if (s->fp_mb_coeff == NULL)
        loge("open s->fp_mb_coeff failed, %s", file_name);

    snprintf(file_name, sizeof(file_name), "%s/mb_cfg_data.txt", tmp_log_dir);
    s->fp_mb_cfg_data = fopen(file_name, "wb");

    snprintf(file_name, sizeof(file_name), "%s/tmp_log/mb_info.txt", s->ip_path);
    s->fp_mb_info = fopen(file_name, "wb");
    if (s->fp_mb_info == NULL) {
        loge("Can't open idct out file!");
        return;
    }

    char path[512] = {0};
    snprintf(path, sizeof(path), "%s/aic_cmd.txt", s->ip_path);
    s->fp_reg = fopen(path, "wb");
    if (s->fp_reg == NULL) {
        loge("aic_cmd.txt open fialed");
    }
#endif
    return;
}

int mpeg4_deinit_debug(struct mpeg4_ctx *s)
{
#ifdef MPEG4_DUMP_ENABLE
    if (s->fp_mb_cfg_data)
        fclose(s->fp_mb_cfg_data);

    if (s->fp_mb_coeff)
        fclose(s->fp_mb_coeff);

    if (s->fp_reg) {
        fclose(s->fp_reg);
    }
#endif
    return 0;
}

static void reset_data_cache(struct mpeg4_data_cache *cache)
{
    if (cache->data != NULL) {
        free(cache->data);
        cache->data = NULL;
    }
    cache->len = 0;
}

static int mpeg4_find_start_code(struct mpeg4_ctx *s, unsigned char *data, int len, int *offset, int *frm_len)
{
    int i = 0;
    unsigned int start_code;
    int find_start_code = 0;
    int nstart = 0;

    *frm_len = 0;

    for (i = 0; i < (len - 4); i++) {
        start_code = (data[i] << 24) | (data[i + 1] << 16) | (data[i + 2] << 8) | data[i + 3];
        if (VOP_START_CODE == start_code) {
            find_start_code = 1;
            break;
        }
    }

    if (find_start_code != 1) {
        logw("mpeg4: can not find start code");
        return -1;
    }

    nstart = i;
    if (0 == s->non_first_packet) {
        s->non_first_packet = 1;
        nstart = 0;
    }
    *offset = nstart;

    if (nstart > 0 && (data[i - 1] == 0)) {
        nstart -= 1;
    }

    find_start_code = 0;
    for (i += 4; i < (len - 4); i++) {
        start_code = (data[i] << 24) | (data[i + 1] << 16) | (data[i + 2] << 8) | data[i + 3];
        if (VOP_START_CODE == start_code) {
            find_start_code = 1;
            break;
        }
    }

    if (find_start_code == 1) {
        if (data[i - 1] == 0) {
            *frm_len = i - nstart - 1;
        } else {
            *frm_len = i - nstart;
        }
    } else {
        *frm_len = len - nstart;
    }

    return 0;
}

void mpeg_save_mb_coeff(struct mp4_state *mp4_state, short *block, int block_num)
{
    for (int i = 0; i < 8; i++) {
        for (int j = 0; j < 8; j++) {
#ifdef MPEG4_DUMP_ENABLE
            fprintf(g_mpeg4_ctx->fp_mb_coeff, "%5d ", block[8 * i + j]);
#endif
            mp4_state->mpeg_coef_matrix[block_num][8 * i + j] = block[8 * i + j];
        }
#ifdef MPEG4_DUMP_ENABLE
        fprintf(g_mpeg4_ctx->fp_mb_coeff, "\n");
#endif
    }
}

int __mpeg4_decode_init(struct mpp_decoder *ctx, struct decode_config *config)
{
    struct mpeg4_ctx *s = NULL;
    int ret, size;

    if ((NULL == ctx) || (NULL == config)) {
        loge("invalid parameter[%p, %p]", ctx, config);
        return -1;
    }

    s = (struct mpeg4_ctx *)ctx;

    size = s->dec_init.width * s->dec_init.height * 3 / 2;
    s->dec_frame.bmp = malloc(size);
    if (NULL == s->dec_frame.bmp) {
        loge("malloc dec_frame.bmp failed!");
        return -1;
    }
    s->dec_frame.stride = s->dec_init.width;
    s->dec_frame.render_flag = 1;
    ret = decore(s, DEC_OPT_INIT, &s->dec_init, NULL);
    if (ret != 0) {
        free(s->dec_frame.bmp);
        loge("DEC_OPT_INIT failed\n");
        return -1;
    }

    s->ve_buf_handle = ve_buffer_allocator_create(VE_BUFFER_TYPE_DMA);
    if (NULL == s->ve_buf_handle) {
        free(s->dec_frame.bmp);
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

    return 0;
}

int __mpeg4_decode_destroy(struct mpp_decoder *ctx)
{
    struct mpeg4_ctx *s = (struct mpeg4_ctx *)ctx;

    if (NULL == s) {
        loge("invalid parameter");
        return -1;
    }

    if (0 != decore(s, DEC_OPT_RELEASE, NULL, NULL)) {
        loge("DEC_OPT_RELEASE failed!");
    }

    if (s->dec_frame.bmp) {
        free(s->dec_frame.bmp);
        s->dec_frame.bmp = NULL;
    }

    if (s->decoder.pm) {
        pm_destroy(s->decoder.pm);
        s->decoder.pm = NULL;
    }

    if (s->ve_buf_handle) {
        ve_buffer_allocator_destroy(s->ve_buf_handle);
        s->ve_buf_handle = NULL;
    }

    ve_close_device();

    reset_data_cache(&s->data_cache);

    mpp_free(s);
    g_mpeg4_ctx = NULL;

    mpeg4_deinit_debug(s);

    return 0;
}

int __mpeg4_decode_frame(struct mpp_decoder *ctx)
{
    struct mpeg4_ctx *s = (struct mpeg4_ctx *)ctx;
    int ret = 0, len, frm_len, offset;
    unsigned char *data = NULL;
    unsigned char *combined_data = NULL;
    int need_free_combined = 0;

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
    // s->error = 0; // TODO：set error code
    s->eos = s->curr_packet->flag & PACKET_FLAG_EOS;

    data = s->curr_packet->data;
    len = s->curr_packet->size;

    if (s->data_cache.data != NULL && s->data_cache.len > 0) {
        combined_data = malloc(s->data_cache.len + len);
        if (NULL == combined_data) {
            loge("Failed to allocate memory for combined data!");
            pm_requeue_ready_packet(s->decoder.pm, s->curr_packet);
            return -1;
        } else {
            memcpy(combined_data, s->data_cache.data, s->data_cache.len);
            memcpy(combined_data + s->data_cache.len, data, len);
            data = combined_data;
            len = s->data_cache.len + len;
            reset_data_cache(&s->data_cache);
            need_free_combined = 1;
        }
    }

#ifdef MPEG4_DUMP_ENABLE
    s->fp_es = fopen("/mnt/sdcard/es.bin", "wb");
    if (s->fp_es == NULL) {
        loge("es.bin open fialed");
    } else {
        int wt_len = fwrite(data, 1, len, s->fp_es); // xiaodong just for debug
        if (wt_len != len) {
            loge("write es error, len: %d, wt_len: %d", len, wt_len);
        }
    }
    fclose(s->fp_es);
#endif

    // init_read_bits(&s->gb, data, len * 8, 0);

    do {
        ret = mpeg4_find_start_code(s, data, len, &offset, &frm_len);
        if (ret < 0) {
            reset_data_cache(&s->data_cache);
            s->data_cache.data = malloc(len);
            if (!s->data_cache.data) {
                loge("process start code error");
                break;
            }
            memcpy(s->data_cache.data, data, len);
            s->data_cache.len = len;
            ret = 0;
            break;
        }
        data += offset;
        len -= offset;

        s->dec_frame.bitstream = data;
        s->dec_frame.length = frm_len;
        s->dec_frame.if_flv_h263 = 0;
        s->dec_frame.if_rm_h263 = 0;

        ret = decore(s, DEC_OPT_FRAME, &s->dec_frame, NULL);
        if (ret < 0) {
            loge("decode frame error");
            break;
        }
        s->dec_frame_num++;
        data += frm_len;
        len -= frm_len;
    } while (len > 3);
    pm_enqueue_empty_packet(s->decoder.pm, s->curr_packet);

    if (need_free_combined && combined_data != NULL) {
        free(combined_data);
    }

    logd("__mpeg_decode_frame >>> out");

    return ret;
}

int __mpeg4_decode_control(struct mpp_decoder *ctx, int cmd, void *param)
{
    struct mpeg4_ctx *s = (struct mpeg4_ctx *)ctx;

    if (NULL == s) {
        loge("invalid parameter");
        return -1;
    }

    return 0;
}

int __mpeg4_decode_reset(struct mpp_decoder *ctx)
{
    struct mpeg4_ctx *s = (struct mpeg4_ctx *)ctx;

    if (NULL == s) {
        loge("invalid parameter");
        return -1;
    }

    fm_decoder_reclaim_all_used_frame(s->decoder.fm);

    fm_reset(s->decoder.fm);
    pm_reset(s->decoder.pm);
    reset_data_cache(&s->data_cache);

    return 0;
}

struct dec_ops mpeg4_decoder = {
    .name = "mpeg4",
    .init = __mpeg4_decode_init,
    .destory = __mpeg4_decode_destroy,
    .decode = __mpeg4_decode_frame,
    .control = __mpeg4_decode_control,
    .reset = __mpeg4_decode_reset,
};

struct mpp_decoder *create_mpeg4_decoder(void)
{
    struct mpeg4_ctx *s = (struct mpeg4_ctx *)mpp_alloc(sizeof(struct mpeg4_ctx));
    if (s == NULL) {
        loge("malloc mpeg4_ctx failed");
        return NULL;
    }

    memset(s, 0, sizeof(struct mpeg4_ctx));
    s->data_cache.data = NULL;
    s->data_cache.len = 0;

    s->decoder.ops = &mpeg4_decoder;

    s->dec_init.width = 2048;
    s->dec_init.height = 2048;
    s->dec_init.disp_factor = 1;

    if (ve_open_device() < 0) {
        mpp_free(s);
        return NULL;
    }
    s->regs_base = ve_get_reg_base();
    logd("ve_reg_base: %lx", s->regs_base);

    g_mpeg4_ctx = s;

    mpeg4_init_debug(s, "/mnt/sdcard");

    return &s->decoder;
}

struct mpp_decoder *create_mpeg4_311_decoder()
{
    struct mpeg4_ctx *s = (struct mpeg4_ctx *)mpp_alloc(sizeof(struct mpeg4_ctx));
    if (s == NULL) {
        loge("malloc mpeg4_ctx_311 failed");
        return NULL;
    }

    memset(s, 0, sizeof(struct mpeg4_ctx));

    s->decoder.ops = &mpeg4_decoder;
    if (ve_open_device() < 0) {
        mpp_free(s);
        return NULL;
    }
    s->regs_base = ve_get_reg_base();
    logd("ve_reg_base: %lx", s->regs_base);

    s->dec_init.width = 2048;
    s->dec_init.height = 2048;
    s->dec_init.disp_factor = 1;
    s->dec_init.codec_version = 311;

    g_mpeg4_ctx = s;

    mpeg4_init_debug(s, "/mnt/sdcard");

    return &s->decoder;
}

