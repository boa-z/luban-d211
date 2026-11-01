/*
 * Copyright (c) 2026, ArtInChip Technology Co., Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Authors:  zequan liang <zequan.liang@artinchip.com>
 */

#ifndef ARTVG_H
#define ARTVG_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#include <video/mpp_types.h>

#ifdef __cplusplus
extern "C" {
#endif

#define VLC_MAX_GRADIENT_STOPS 16

typedef enum {
    ARTVG_SUCCESS = 0,
    ARTVG_INVALID_PARAM = -1,
    ARTVG_OUT_OF_MEMORY = -2,
    ARTVG_OUT_OF_RESOURCES = -3,
    ARTVG_NOT_SUPPORTED = -4,
    ARTVG_GENERIC_IO = -5,
} artvg_error_t;

/*
 * @artvg_alpha_mode: alpha mode
 *  0: pixel alpha mode(alpha = pixel_alpha)
 *  1: global alpha mode(alpha = global_alpha)
 *  2: mixded alpha mode(alpha = pixel_alpha * global_alpha / 255)
*/
typedef enum artvg_alpha_mode {
    ARTVG_ALPHA_PIXEL,
    ARTVG_ALPHA_GLOBAL,
    ARTVG_ALPHA_MIXED,
} artvg_alpha_mode_t;

/* blend pixel rules */
/* pixel = source * fs + destination * fd */
/* sa = source alpha */
/* da = destination alpha */
/* dst = destination pixel */
/* src = source pixel */
typedef enum artvg_alpha_blend {
    /* Standard compositing modes (Porter-Duff) */
    ARTVG_BLEND_DEFAULT        =  0, /* fs: sa      fd: 1.0-sa (defaults) */
    ARTVG_BLEND_CLEAR          =  1, /* fs: 0.0     fd: 0.0      */
    ARTVG_BLEND_SRC            =  2, /* fs: 1.0     fd: 0.0      */
    ARTVG_BLEND_SRC_OVER       =  3, /* fs: 1.0     fd: 1.0-sa   */
    ARTVG_BLEND_DST_OVER       =  4, /* fs: 1.0-da  fd: 1.0      */
    ARTVG_BLEND_SRC_IN         =  5, /* fs: da      fd: 0.0      */
    ARTVG_BLEND_DST_IN         =  6, /* fs: 0.0     fd: sa       */
    ARTVG_BLEND_SRC_OUT        =  7, /* fs: 1.0-da  fd: 0.0      */
    ARTVG_BLEND_DST_OUT        =  8, /* fs: 0.0     fd: 1.0-sa   */
    ARTVG_BLEND_SRC_ATOP       =  9, /* fs: da      fd: 1.0-sa   */
    ARTVG_BLEND_DST_ATOP       = 10, /* fs: 1.0-da  fd: sa       */
    ARTVG_BLEND_ADD            = 11, /* fs: 1.0     fd: 1.0      */
    ARTVG_BLEND_XOR            = 12, /* fs: 1.0-da  fd: 1.0-sa   */
    ARTVG_BLEND_DST            = 13, /* fs: 0.0     fd: 1.0      */

    /* Arithmetic blend modes */
    ARTVG_BLEND_ARITH_ADD      = 14,   /* min(dst+src, 255) */
    ARTVG_BLEND_ARITH_SUBTRACT = 15,   /* max(dst-src, 0) */
    ARTVG_BLEND_ARITH_MULTIPLY = 16,   /* (dst * src) / 255 */
} artvg_alpha_blend_t;

typedef enum artvg_mirror {
    ARTVG_MIRROR_NONE = 0,          /* No flip (default) */
    ARTVG_MIRROR_HORIZONTAL,        /* Horizontal flip (left-right mirror) */
    ARTVG_MIRROR_VERTICAL,          /* Vertical flip (top-bottom mirror) */
    ARTVG_MIRROR_BOTH,              /* Horizontal + Vertical flip (equivalent to 180-degree rotation) */
} artvg_mirror_t;

typedef enum artvg_rotate
{
    ARTVG_ROTATE_0 = 0,
    ARTVG_ROTATE_90,
    ARTVG_ROTATE_180,
    ARTVG_ROTATE_270,
} artvg_rotate_t;

typedef enum artvg_filter {
    ARTVG_FILTER_BILINEAR = 0x0,
    ARTVG_FILTER_BICUBIC = 0x1,
} artvg_filter_t;

typedef enum artvg_pad_mode {
    ARTVG_PAD_MODE_FIXED_COLOR = 0,
    ARTVG_PAD_MODE_EDGE_REPLICATE,
    ARTVG_PAD_MODE_EDGE_REPLICATE_ALPHA_ONLY,
} artvg_pad_mode_t;

typedef enum artvg_dither {
    ARTVG_DITHER_OFF         = 0,
    ARTVG_DITHER_STANDARD    = 1,
    ARTVG_DITHER_RANDOM      = 2
} artvg_dither_t;

typedef enum artvg_scan_order {
    ARTVG_SCAN_TB_LR = 0x0,
    ARTVG_SCAN_TB_RL = 0x1,
    ARTVG_SCAN_BT_LR = 0x2,
    ARTVG_SCAN_BT_RL = 0x3
} artvg_scan_order_t;

typedef enum artvg_gradient_precision {
    ARTVG_GRADIENT_PRECISION_NONE = 0,
    ARTVG_GRADIENT_PRECISION_256 = 1,
    ARTVG_GRADIENT_PRECISION_64 = 2,
    ARTVG_GRADIENT_PRECISION_16 = 3,
} artvg_gradient_precision_t;

typedef enum artvg_gradient_spread {
    ARTVG_GRADIENT_SPREAD_NONE = 0,
    ARTVG_GRADIENT_SPREAD_PAD = 1,
    ARTVG_GRADIENT_SPREAD_REFLECT = 2,
    ARTVG_GRADIENT_SPREAD_REPEAT = 3,
} artvg_gradient_spread_t;

typedef enum artvg_fill_rule {
    ARTVG_FILL_NON_ZERO = 0,
    ARTVG_FILL_EVEN_ODD = 1,
} artvg_fill_rule_t;

typedef enum artvg_premul_mode {
    /**
     * Pre-multiply using alpha computed according to ALPHA_MODE setting
     * For mixed mode: alpha = pixel_alpha * global_alpha / 255
     * For global mode: alpha = global_alpha
     * For normal mode: alpha = pixel_alpha
     */
    ARTVG_PRE_MUL_BY_COMPUTED_ALPHA = 0,

    /**
     * Pre-multiply using global alpha value directly
     * Alpha = global_alpha regardless of ALPHA_MODE setting
     */
    ARTVG_PRE_MUL_BY_GLOBAL_ALPHA = 1,
} artvg_premul_mode_t;

typedef enum artvg_csc0_pos {
    ARTVG_CSC0_BEFORE_FILL_COLOR,
    ARTVG_CSC0_AFTER_RECOLOR,
} artvg_csc0_pos_t;

typedef enum artvg_projective_lut_mode {
    ARTVG_PROJECTIVE_LUT_MOD_S10_6,
    ARTVG_PROJECTIVE_LUT_MOD_S11_5,
    ARTVG_PROJECTIVE_LUT_MOD_S12_4,
    ARTVG_PROJECTIVE_LUT_MOD_S13_3,
    ARTVG_PROJECTIVE_LUT_MOD_S14_2,
    ARTVG_PROJECTIVE_LUT_MOD_S16_8,
    ARTVG_PROJECTIVE_LUT_MOD_S18_6,
    ARTVG_PROJECTIVE_LUT_MOD_S20_4,
} artvg_projective_lut_mode_t;

typedef enum artvg_projective_lut_block_size {
    ARTVG_PROJECTIVE_LUT_BLOCK_8X8,
    ARTVG_PROJECTIVE_LUT_BLOCK_16X16,
    ARTVG_PROJECTIVE_LUT_BLOCK_32X32,
    ARTVG_PROJECTIVE_LUT_BLOCK_64X64,
} artvg_projective_lut_block_size_t;

typedef enum artvg_projective_out_blk_mode {
    ARTVG_PROJECTIVE_OUT_BLK_MODE_8X8,
    ARTVG_PROJECTIVE_OUT_BLK_MODE_16X16,
} artvg_projective_out_blk_mode_t;

typedef struct artvg_path_buffer {
    struct artvg_path_buffer *prev;
    struct artvg_path_buffer *next;
    struct dma_buf_info buffer;
    void *addr;
    uint32_t size;
    uint32_t used;
    uint32_t flag;
} artvg_path_buffer_t;

typedef struct {
    float m[3][3];
} artvg_matrix_t;


typedef struct {
    int32_t x;
    int32_t y;
} artvg_point_t;

typedef artvg_point_t artvg_point4_t[4];

typedef struct artvg_ctl {
    uint32_t                    color_key_value;       /* Color key match value (ARGB) */
    uint32_t                    src_recolor_value;     /* Source recolor value (ARGB) */
    uint8_t                     src_global_alpha;      /* Source global alpha: 0-255 */
    uint8_t                     dst_global_alpha;      /* Dest global alpha: 0-255 */
    uint8_t                     alpha_en : 1;          /* Alpha blending enable */
    uint8_t                     output_unpremul_en : 1; /* Output unpremultiply enable (remove premultiply after blend) */
    uint8_t                     color_key_en : 1;      /* Color key enable */
    uint8_t                     src_recolor_en : 1;    /* Source recolor enable */
    artvg_alpha_blend_t         alpha_rules;           /* Blend mode (artvg_alpha_blend_t) */
    artvg_alpha_mode_t          src_alpha_mode;        /* Source alpha mode (artvg_alpha_mode_t) */
    artvg_alpha_mode_t          dst_alpha_mode;        /* Dest alpha mode (artvg_alpha_mode_t) */
    artvg_premul_mode_t         src_premul_mode;       /* Src premultiply mode (artvg_premul_mode_t) */
    artvg_premul_mode_t         dst_premul_mode;       /* Dst premultiply mode (artvg_premul_mode_t) */
    artvg_csc0_pos_t            csc0_pos;              /* CSC0 position (artvg_csc0_pos_t) */
    artvg_dither_t              output_dither;         /* Dither mode (artvg_dither_t) */
    artvg_scan_order_t          src_scan_order;        /* Scan order (artvg_scan_order_t) */
} artvg_ctrl_t;

typedef struct artvg_base_ctl {
    artvg_rotate_t rotate;
    artvg_mirror_t mirror;
} artvg_blit_ctl_t;

typedef struct artvg_transform_edge {
    uint32_t fill_color;        /* Fill color (ARGB) */
    artvg_pad_mode_t pad_mode;
    uint8_t fill_en;
    uint8_t blend_en;
} artvg_transform_edge_t;

typedef struct artvg_projective {
    artvg_transform_edge_t transform_edge;
    artvg_matrix_t matrix;
    artvg_projective_lut_mode_t projective_lut_mode;        /* 0-7 */
    artvg_projective_lut_block_size_t projective_lut_block_size;  /* 0-3 */
    artvg_projective_out_blk_mode_t projective_out_blk_mode;      /* 0-1 */

    /* Internal LUT buffer */
    struct dma_buf_info lut_buffer;
    void *lut_point;
    uint32_t size;
} artvg_projective_t;

// The fill direction is perpendicular to the midpoint of the two points.
typedef struct artvg_gradient {
    artvg_point_t start;
    artvg_point_t end;
    uint32_t colors[VLC_MAX_GRADIENT_STOPS];
    uint32_t count;
    uint8_t stops[VLC_MAX_GRADIENT_STOPS]; /* 0~255 */
    artvg_gradient_precision_t precision;
    artvg_gradient_spread_t spread;

    void *user_color_lut;
    uint32_t user_color_lut_size;
} artvg_gradient_t; /* Linear gradient */

typedef struct artvg_vector_ctl {
    artvg_fill_rule_t fill_rule;
    uint8_t curve_flat_limit;
} artvg_vector_ctl_t;


typedef struct artvg_path {
    artvg_path_buffer_t vector_list;
    artvg_path_buffer_t *active_vector;
} artvg_path_t;

typedef struct artvg artvg_t;

artvg_t *artvg_create();

artvg_error_t artvg_destroy(artvg_t *vg);

artvg_error_t artvg_wait_finish(artvg_t *vg);

artvg_error_t artvg_flush(artvg_t *vg);

struct mpp_buf *artvg_allocate(artvg_t *vg, int width, int height, enum mpp_pixel_format format);

artvg_error_t artvg_free(artvg_t *vg, struct mpp_buf *buffer);

artvg_error_t artvg_add_dma_fd(artvg_t *vg, int fd);

artvg_error_t artvg_remove_dma_fd(artvg_t *vg, int fd);

artvg_error_t artvg_clear(artvg_t *vg, struct mpp_buf *dst, uint32_t color);

artvg_error_t artvg_fill_gradient(artvg_t *vg,
                                  struct mpp_buf *dst,
                                  artvg_ctrl_t *ctrl,
                                  artvg_gradient_t *gradient);

artvg_error_t artvg_blit(artvg_t *vg,
                         struct mpp_buf *src,
                         struct mpp_buf *dst,
                         struct mpp_buf *mask,
                         artvg_ctrl_t *ctrl,
                         artvg_blit_ctl_t *blit_ctl);

artvg_error_t artvg_affine(artvg_t *vg,
                           struct mpp_buf *src,
                           struct mpp_buf *dst,
                           struct mpp_buf *mask,
                           artvg_matrix_t *matrix,
                           artvg_ctrl_t *ctl,
                           artvg_transform_edge_t *transform_edge);

artvg_path_t *artvg_path_allocate(artvg_t *vg);

artvg_error_t artvg_path_move_to(artvg_t *vg, artvg_path_t *path, float x, float y);

artvg_error_t artvg_path_line_to(artvg_t *vg, artvg_path_t *path, float x, float y);

artvg_error_t artvg_path_quad_to(artvg_t *vg, artvg_path_t *path, float cx, float cy, float x, float y);

artvg_error_t artvg_path_cubic_to(artvg_t *vg, artvg_path_t *path,
                                  float c0x, float c0y,
                                  float c1x, float c1y,
                                  float x, float y);

artvg_error_t artvg_path_finish(artvg_t *vg, artvg_path_t *path);

artvg_error_t artvg_path_free(artvg_t *vg, artvg_path_t *path);

artvg_error_t artvg_draw_path(artvg_t *vg,
                              struct mpp_buf *dst,
                              artvg_path_t *path,
                              artvg_matrix_t *matrix,
                              artvg_ctrl_t *ctl,
                              artvg_gradient_t *gradient,
                              artvg_vector_ctl_t *vector);

artvg_error_t artvg_projective(artvg_t *vg,
                                struct mpp_buf *src,
                                struct mpp_buf *dst,
                                artvg_ctrl_t *ctl,
                                artvg_projective_t *projective);

artvg_projective_t *artvg_projective_allocate(artvg_t *vg,
                                              artvg_matrix_t *matrix,
                                              artvg_transform_edge_t *transform_edge,
                                              artvg_projective_lut_mode_t projective_lut_mode,
                                              artvg_projective_lut_block_size_t projective_lut_block_size,
                                              artvg_projective_out_blk_mode_t projective_out_blk_mode,
                                              int dst_w, int dst_h);

artvg_error_t artvg_projective_free(artvg_t *vg,
                                    artvg_projective_t *projective);

void artvg_matrix_identity(artvg_matrix_t *matrix);

void artvg_matrix_multiply(artvg_matrix_t *result, const artvg_matrix_t *a, const artvg_matrix_t *b);

void artvg_matrix_translate(float tx, float ty, artvg_matrix_t *matrix);

void artvg_matrix_scale(float sx, float sy, artvg_matrix_t *matrix);

void artvg_matrix_rotate(float angle, artvg_matrix_t *matrix);

void artvg_matrix_rotate_point(artvg_point_t *point, float angle, artvg_matrix_t *matrix);

void artvg_matrix_skew(float skew_x, float skew_y, artvg_matrix_t *matrix);

void artvg_matrix_perspective(float px, float py, artvg_matrix_t *matrix);

bool artvg_matrix_is_identity(const artvg_matrix_t *matrix);

bool artvg_matrix_inverse(artvg_matrix_t *result, const artvg_matrix_t *m);

artvg_error_t artvg_fill_gradient2(artvg_t *vg,
                                   struct mpp_buf *dst,
                                   struct mpp_buf *output,
                                   artvg_ctrl_t *ctrl,
                                   artvg_gradient_t *gradient);

artvg_error_t artvg_blit2(artvg_t *vg,
                          struct mpp_buf *src,
                          struct mpp_buf *dst,
                          struct mpp_buf *output,
                          struct mpp_buf *mask,
                          artvg_ctrl_t *ctl,
                          artvg_blit_ctl_t *blit_ctl);

artvg_error_t artvg_affine2(artvg_t *vg,
                            struct mpp_buf *src,
                            struct mpp_buf *dst,
                            struct mpp_buf *output,
                            struct mpp_buf *mask,
                            artvg_matrix_t *matrix,
                            artvg_ctrl_t *ctl,
                            artvg_transform_edge_t *transform_edge);

artvg_error_t artvg_draw_path2(artvg_t *vg,
                               struct mpp_buf *dst,
                               struct mpp_buf *output,
                               artvg_path_t *path,
                               artvg_matrix_t *matrix,
                               artvg_ctrl_t *ctl,
                               artvg_gradient_t *gradient,
                               artvg_vector_ctl_t *vector);


artvg_error_t artvg_projective2(artvg_t *vg,
                                struct mpp_buf *src,
                                struct mpp_buf *dst,
                                struct mpp_buf *output,
                                artvg_ctrl_t *ctl,
                                artvg_projective_t *projective);

artvg_error_t artvg_get_version(char* version, uint32_t len);

artvg_error_t artvg_dumping_cmd(artvg_t *vg);

artvg_error_t artvg_enable_edge_buffer_pool(artvg_t *vg, uint32_t size);

artvg_error_t artvg_disable_edge_buffer_pool(artvg_t *vg);

artvg_error_t artvg_enable_vector_buffer_pool(artvg_t *vg, uint32_t block_count, uint32_t block_size);

artvg_error_t artvg_disable_vector_buffer_pool(artvg_t *vg);

artvg_error_t artvg_get_flush_batch(artvg_t *vg, uint32_t *batch_id);

artvg_error_t artvg_wait_batch_finish(artvg_t *vg, uint32_t batch_id);

artvg_error_t artvg_get_completed_batch(artvg_t *vg, uint32_t *batch_id);

artvg_error_t artvg_batch_completed(artvg_t *vg, uint32_t flush_bacth_id,
                                    uint32_t completed_batch_id, bool *completed);

artvg_error_t artvg_get_transform_matrix(artvg_point4_t src, artvg_point4_t dst, artvg_matrix_t *mat);

artvg_error_t artvg_transform_bounding_box(struct mpp_rect *in_bbx,
                                           artvg_matrix_t *matrix,
                                           struct mpp_rect *clip,
                                           struct mpp_rect *out_bbx);

artvg_error_t artvg_transform_gradient_points(artvg_gradient_t *gradient, artvg_matrix_t *matrix);

artvg_error_t artvg_gradient_fill_color_lut(artvg_gradient_t *gradient, void *color_lut, uint32_t size);

#ifdef __cplusplus
}
#endif

#endif /* ARTVG_H */
