/*
 * Copyright (c) 2026, ArtInChip Technology Co., Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Authors:  zequan liang <zequan.liang@artinchip.com>
 */

#ifndef _ARTVG_REG_H_
#define _ARTVG_REG_H_

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

#ifndef __KERNEL__
#define GENMASK(h,l) (((~0U) << (l)) & (~0U >>(32 - 1 - (h))))
#define BIT(nr)  (1U << (nr))
#else
#include <linux/bits.h>
#include <linux/io.h>
#include <linux/types.h>
#endif /* __KERNEL__ */

/* Base offsets */
#define VG_BASE               0x000

/* Global control registers */
#define VG_INT_CTRL           (0x000)
#define VG_STATUS             (0x004)
#define VG_START              (0x008)
#define VG_VER_ID             (0xFFC)

/* Source surface registers */
#define SRC_SURFACE_CTRL      (0x010)
#define SRC_INPUT_SIZE        (0x014)
#define SRC_STRIDE            (0x018)
#define SRC_FUNC_SEL          (0x01C)
#define SRC_LOW_ADDR0         (0x020)
#define SRC_LOW_ADDR1         (0x024)
#define SRC_LOW_ADDR2         (0x028)
#define SRC_HIGH_ADDR         (0x02C)
#define SRC_FILL_COLOR        (0x030)
#define SRC_RECOLOR           (0x040)

/* Linear gradient registers */
#define LINEAR_GRAD_OFFSET    (0x034)
#define LINEAR_GRAD_H_STEP    (0x038)
#define LINEAR_GRAD_V_STEP    (0x03C)

/* Transform LUT registers */
#define TRANS_LUT_LOW_ADDR    (0x048)
#define TRANS_LUT_HIGH_ADDR   (0x04C)

/* Mask surface registers */
#define MASK_LOW_ADDR         (0x050)
#define MASK_STRIDE           (0x054) 

/* Affine transform registers */
#define AFFINE_A11            (0x058)
#define AFFINE_A12            (0x05C)
#define AFFINE_A13            (0x060)
#define AFFINE_A21            (0x064)
#define AFFINE_A22            (0x068)
#define AFFINE_A23            (0x06C)

#define CSC0_COEF(n)          (0x070 + 0x4 * (n))

/* Vector drawing registers */
#define VECTOR_CMD_LOW_ADDR   (0x090)
#define VECTOR_CMD_SIZE       (0x094)
#define VECTOR_DRAW_CTRL      (0x0A0)

/* Edge list registers */
#define EDGE_LIST_LOW_ADDR    (0x098)
#define EDGE_LIST_SIZE        (0x09C)

/* Destination surface registers */
#define DST_SURFACE_CTRL      (0x0B0)
#define DST_INPUT_SIZE        (0x0B4)
#define DST_STRIDE            (0x0B8)
#define DST_OFFSET            (0x0BC)
#define DST_LOW_ADDR0         (0x0C0)
#define DST_LOW_ADDR1         (0x0C4)
#define DST_LOW_ADDR2         (0x0C8)
#define DST_HIGH_ADDR         (0x0CC)

/* Blending registers */
#define BLENDING_CTRL         (0x0D0)
#define COLORKEY_MATCH_COLOR  (0x0D4)

/* Output registers */
#define OUTPUT_CTRL           (0x100)
#define OUTPUT_SIZE           (0x104)
#define OUTPUT_STRIDE         (0x108)
#define OUTPUT_OFFSET         (0x10C)
#define OUTPUT_LOW_ADDR0      (0x110)
#define OUTPUT_LOW_ADDR1      (0x114)
#define OUTPUT_LOW_ADDR2      (0x118)
#define OUTPUT_HIGH_ADDR      (0x11C)

/* Dither line buffer registers */
#define DITHER_LINE_BUF_LOW_ADDR (0x120)
#define DITHER_LINE_BUF_HIGH_ADDR (0x124)

/* Command buffer registers */
#define CMD_BUF_START_LOW_ADDR (0x130)
#define CMD_BUF_SIZE          (0x134)
#define CMD_BUF_OFFSET        (0x138)
#define CMD_BUF_VALID_LENGTH  (0x13C)

/* LUT registers */
#define PALETTE_LUT(n)        (0x400 + 0x4 * (n))
#define COLOR_LUT(n)          (0x800 + 0x4 * (n))

/* VG_INT_CTRL register bits */
#define HW_REQ_MEM_IRQ_EN     BIT(2)
#define HW_ERR_IRQ_EN         BIT(1)
#define FINISH_IRQ_EN         BIT(0)

/* VG_STATUS register bits */
#define HW_REQ_MEM_STATUS     BIT(2)
#define HW_ERR_IRQ_STATUS     BIT(1)
#define FINISH_IRQ_STATUS     BIT(0)

/* VG_START register bits */
#define VG_START_EN           BIT(0)

/* SRC_SURFACE_CTRL register bits */
#define SRC_G_ALPHA_MASK      GENMASK(31, 24)
#define SRC_G_ALPHA(x)        (((x) & 0xFF) << 24)
#define SRC_ALPHA_MODE_MASK   GENMASK(23, 22)
#define SRC_ALPHA_MODE(x)     (((x) & 0x3) << 22)
#define SRC_ALPHA_INV         BIT(21)
#define SRC_PRE_MUL_EN        BIT(20)
#define SRC_SCAN_ORDER_MASK   GENMASK(19, 18)
#define SRC_SCAN_ORDER(x)     (((x) & 0x3) << 18)
#define SRC_PRE_MUL_MODE      BIT(15)
#define SRC_INPUT_FORMAT_MASK GENMASK(14, 8)
#define SRC_INPUT_FORMAT(x)   (((x) & 0x7F) << 8)
#define SRC_CSC3_EN               BIT(6)
#define SRC_CSC0_POS              BIT(5)
#define SRC_CSC0_CSC3_COLOR_SPACE_MASK GENMASK(3, 2)
#define SRC_CSC0_CSC3_COLOR_SPACE(x)   (((x) & 0x3) << 2)
#define SRC_CSC0_YCBCR_TO_YUV_DISABLE BIT(2)
#define SRC_CSC0_EN           BIT(1)
#define SRC_EN                BIT(0)

/* SRC_SURFACE_FUNC_SEL register bits */
#define TRANS_LUT_MODE_MASK GENMASK(31, 29)
#define TRANS_LUT_MODE(x)   (((x) & 0x7) << 29)
#define TRANS_LUT_BLOCK_SIZE_MASK GENMASK(28, 27)
#define TRANS_LUT_BLOCK_SIZE(x)   (((x) & 0x3) << 27)
#define BICUBIC_FILTER_V_TABLE_MASK GENMASK(26, 24)
#define BICUBIC_FILTER_V_TABLE(x)   (((x) & 0x7) << 24)
#define BICUBIC_FILTER_H_TABLE_MASK GENMASK(22, 20)
#define BICUBIC_FILTER_H_TABLE(x)   (((x) & 0x7) << 20)
#define TRANSFORM_FILTER     BIT(19)
#define FUNC_SELECT_MASK     GENMASK(18, 16)
#define FUNC_SELECT(x)       (((x) & 0x7) << 16)
#define COLOR_FILL_EN        BIT(14)
#define TRANS_PAD_MODE_EN_MASK GENMASK(13, 12)
#define TRANS_PAD_MODE_EN(x)   (((x) & 0x3) << 12)
#define TRANS_FILL_BG_EN     BIT(11)
#define TRANS_BLEND_EDGE_EN  BIT(10)
#define RECOLOR_EN           BIT(9)
#define MASK_EN              BIT(8)
#define V_FLIP               BIT(7)
#define H_FLIP               BIT(6)
#define ROTATION_CTRL_MASK   GENMASK(5, 4)
#define ROTATION_CTRL(x)     (((x) & 0x3) << 4)
#define FILL_SPREAD_MASK     GENMASK(3, 2)
#define FILL_SPREAD(x)       (((x) & 0x3) << 2)
#define COLOR_FILL_MODE_MASK GENMASK(1, 0)
#define COLOR_FILL_MODE(x)   (((x) & 0x3) << 0)

/* DST_SURFACE_CTRL register bits */
#define DST_G_ALPHA_MASK      GENMASK(31, 24)
#define DST_G_ALPHA(x)        (((x) & 0xFF) << 24)
#define DST_ALPHA_MODE_MASK   GENMASK(23, 22)
#define DST_ALPHA_MODE(x)     (((x) & 0x3) << 22)
#define DST_ALPHA_INV         BIT(21)
#define DST_PRE_MUL_EN        BIT(20)
#define DST_PRE_MUL_MODE      BIT(15)
#define DST_INPUT_FORMAT_MASK GENMASK(14, 8)
#define DST_INPUT_FORMAT(x)   (((x) & 0x7F) << 8)
#define CSC1_COLOR_SPACE_MASK GENMASK(3, 2)
#define CSC1_COLOR_SPACE(x)   (((x) & 0x3) << 2)
#define DST_CSC1_EN           BIT(1)
#define DST_EN                BIT(0)

/* BLENDING_CTRL register bits */
#define RM_PRE_MUL            BIT(16)
#define DST_FACTOR_MODE_MASK  GENMASK(13, 11)
#define DST_FACTOR_MODE(x)    (((x) & 0x7) << 11)
#define SRC_FACTOR_MODE_MASK  GENMASK(10, 8)
#define SRC_FACTOR_MODE(x)    (((x) & 0x7) << 8)
#define BLEND_MODE_MASK       GENMASK(5, 4)
#define BLEND_MODE(x)         (((x) & 0x3) << 4)
#define CK_EN                 BIT(1)
#define ALPHA_BLEND_EN        BIT(0)

/* OUTPUT_CTRL register bits */
#define OUTPUT_FORMAT_MASK    GENMASK(14, 8)
#define OUTPUT_FORMAT(x)      (((x) & 0x7F) << 8)
#define TRANS_OUT_BLK_MODE    BIT(15)
#define RAND_DITHER_EN        BIT(5)
#define DITHER_EN             BIT(4)
#define CSC2_COLOR_SPACE_MASK GENMASK(3, 2)
#define CSC2_COLOR_SPACE(x)   (((x) & 0x3) << 2)
#define OUTPUT_CSC2_EN        BIT(1)

/* VECTOR_DRAW_CTRL register bits */
#define EDGE_LIMIT_SIZE_MASK     GENMASK(27, 24)
#define EDGE_LIMIT_SIZE(x)       (((x) & 0xF) << 24)
#define FILL_RULE_SET(x)         (((x) & 0x1) << 8)
#define CURVE_FLAT_LIMIT_MASK GENMASK(7, 0)
#define CURVE_FLAT_LIMIT(x)   ((x) & 0xFF)

/* Size setting macros */
#define SRC_INPUT_SIZE_SET(w, h) ((((h) & 0x3FFF) << 16) | ((w) & 0x3FFF))
#define DST_INPUT_SIZE_SET(w, h) ((((h) & 0x3FFF) << 16) | ((w) & 0x3FFF))
#define OUTPUT_SIZE_SET(w, h)   ((((h) & 0x3FFF) << 16) | ((w) & 0x3FFF))

/* Stride setting macros */
#define SRC_STRIDE_SET(p0, p1)  ((((p1) & 0xFFFF) << 16) | ((p0) & 0xFFFF))
#define DST_STRIDE_SET(p0, p1)  ((((p1) & 0xFFFF) << 16) | ((p0) & 0xFFFF))
#define OUTPUT_STRIDE_SET(p0, p1) ((((p1) & 0xFFFF) << 16) | ((p0) & 0xFFFF))

/* offset setting macros */
#define DST_OFFSET_SET(x, y) ((((y) & 0x3FFF) << 16) | ((x) & 0x3FFF))
#define OUTPUT_OFFSET_SET(x, y) ((((y) & 0x3FFF) << 16) | ((x) & 0x3FFF))

/* Address setting macros */
#define SRC_HIGH_ADDR_SET(a0, a1, a2) ((((a2) & 0xFF) << 16) | (((a1) & 0xFF) << 8) | ((a0) & 0xFF))
#define DST_HIGH_ADDR_SET(a0, a1, a2) ((((a2) & 0xFF) << 16) | (((a1) & 0xFF) << 8) | ((a0) & 0xFF))
#define OUTPUT_HIGH_ADDR_SET(a0, a1, a2) ((((a2) & 0xFF) << 16) | (((a1) & 0xFF) << 8) | ((a0) & 0xFF))

/* Color setting macros */
#define SRC_FILL_COLOR_SET(a, r, g, b) ((((a) & 0xFF) << 24) | (((r) & 0xFF) << 16) | (((g) & 0xFF) << 8) | ((b) & 0xFF))
#define COLORKEY_SET(r, g, b)        ((((r) & 0xFF) << 16) | (((g) & 0xFF) << 8) | ((b) & 0xFF))

/* CSC coefficient setting macros for different register types */
#define CSC_COEF_SET_PAIR(c0, c1) ((((c1) & 0x3FFF) << 16) | ((c0) & 0x3FFF))
#define CSC_COEF_SET_OFFSET_COEF(offset, coef) ((((offset) & 0x1FF) << 16) | ((coef) & 0x3FFF))
#define CSC_COEF_SET_OFFSET_PAIR(o1, o2) ((((o2) & 0x1FF) << 16) | ((o1) & 0x1FF))
#define CSC_COEF_SET(reg_num, val1, val2) \
    (((reg_num) < 4) ? CSC_COEF_SET_PAIR(val1, val2) : \
     ((reg_num) == 4) ? CSC_COEF_SET_OFFSET_COEF(val1, val2) : \
     CSC_COEF_SET_OFFSET_PAIR(val1, val2))

/* Affine transform coefficient setting macros */
/* Matrix coefficients (26-bit signed, 16-bit fractional) */
#define AFFINE_A11_SET(a11) ((a11) & 0x3FFFFFF)  /* Register 0x058 */
#define AFFINE_A12_SET(a12) ((a12) & 0x3FFFFFF)  /* Register 0x05C */
#define AFFINE_A21_SET(a21) ((a21) & 0x3FFFFFF)  /* Register 0x064 */
#define AFFINE_A22_SET(a22) ((a22) & 0x3FFFFFF)  /* Register 0x068 */

/* Translation offsets (29-bit signed, 16-bit fractional) */
#define AFFINE_A13_SET(a13) ((a13) & 0x1FFFFFFF)  /* Register 0x060 */
#define AFFINE_A23_SET(a23) ((a23) & 0x1FFFFFFF)  /* Register 0x06C */

/* Linear gradient setting macros */
#define LINEAR_GRAD_OFFSET_SET(offset) ((offset) & 0x3FFFFFFF)  /* 30-bit signed with 16-bit fractional */
#define LINEAR_GRAD_H_STEP_SET(step)   ((step) & 0x3FFFFF)    /* 22-bit signed with 16-bit fractional */
#define LINEAR_GRAD_V_STEP_SET(step)   ((step) & 0x3FFFFF)    /* 22-bit signed with 16-bit fractional */

/* Command buffer setting macros */
#define CMD_BUF_HIGH_ADDR_SET(high_addr) (((high_addr) & 0xFF) << 24)
#define CMD_BUF_SIZE_SET(size) ((size) & 0xFFFFFF)  /* 24-bit size in bytes */
#define CMD_BUF_OFFSET_SET(offset) ((offset) & 0xFFFFFF)  /* 24-bit offset in bytes */
#define CMD_BUF_VALID_LENGTH_SET(length) ((length) & 0xFFFFFF)  /* 24-bit length in bytes */

/* Dither line buffer address macros */
#define DITHER_LINE_BUF_LOW_ADDR_SET(addr) ((addr) & 0xFFFFFFFF)
#define DITHER_LINE_BUF_HIGH_ADDR_SET(high_addr) (((high_addr) & 0xFF) << 24)

/* Palette LUT configuration macros */
#define PALETTE_LUT_SET(a, r, g, b) ((((a) & 0xFF) << 24) | (((r) & 0xFF) << 16) | (((g) & 0xFF) << 8) | ((b) & 0xFF))
/* Color LUT configuration macros */
#define COLOR_LUT_SET(a, r, g, b) ((((a) & 0xFF) << 24) | (((r) & 0xFF) << 16) | (((g) & 0xFF) << 8) | ((b) & 0xFF))

/* MASK configuration macros */
#define MASK_STRIDE_SET(stride) ((stride) & 0xFFFF)  /* 16-bit stride width */
#define MASK_HIGH_ADDR_SET(high_addr) (((high_addr) & 0xFF) << 16)  /* 8-bit high address */
#define MASK_LOW_ADDR_SET(addr) ((addr) & 0xFFFFFFFF)  /* 32-bit low address */

/* Perspective transform LUT configuration macros */
#define TRANS_LUT_LOW_ADDR_SET(addr) ((addr) & 0xFFFFFFFF)  /* 32-bit low address */
#define TRANS_LUT_HIGH_ADDR_SET(high_addr) ((high_addr) & 0xFF)  /* 8-bit high address */

/* Vector drawing command buffer configuration macros */
#define VECTOR_CMD_SIZE_SET(size) ((size) & 0xFFFFFF)        /* 24-bit size in bytes */
#define VECTOR_CMD_HIGH_ADDR_SET(high_addr) (((high_addr) & 0xFF) << 24)  /* 8-bit high address */

/* Edge list buffer configuration macros */
#define EDGE_LIST_SIZE_SET(size) ((size) & 0xFFFFFF)         /* 24-bit size in bytes */
#define EDGE_LIST_HIGH_ADDR_SET(high_addr) (((high_addr) & 0xFF) << 24)  /* 8-bit high address */

/* Vector drawing command definitions */
#define VECTOR_CMD_MOVETO     0x0
#define VECTOR_CMD_LINETO     0x1
#define VECTOR_CMD_CONICTO    0x2
#define VECTOR_CMD_CUBICTO    0x3

/* Command field masks */
#define VECTOR_CMD_TYPE_MASK  GENMASK(31, 30)
#define VECTOR_CMD_TYPE(x)    (((x) & 0x3) << 30)

#define VECTOR_CMD_X_MASK     GENMASK(29, 0)
#define VECTOR_CMD_X(x)       ((x) & 0x3FFFFFFF)

#define VECTOR_CMD_Y_MASK     GENMASK(31, 0)
#define VECTOR_CMD_Y(y)       ((y) & 0xFFFFFFFF)

#define VECTOR_CMD_C0_X_MASK  GENMASK(29, 0)
#define VECTOR_CMD_C0_X(x)    ((x) & 0x3FFFFFFF)

#define VECTOR_CMD_C0_Y_MASK  GENMASK(31, 0)
#define VECTOR_CMD_C0_Y(y)    ((y) & 0xFFFFFFFF)

#define VECTOR_CMD_C1_X_MASK  GENMASK(29, 0)
#define VECTOR_CMD_C1_X(x)    ((x) & 0x3FFFFFFF)

#define VECTOR_CMD_C1_Y_MASK  GENMASK(31, 0)
#define VECTOR_CMD_C1_Y(y)    ((y) & 0xFFFFFFFF)

/* Vector drawing command structure macros */
/* MoveTo command: 8 bytes - [31:30]cmd + [29:0]X + [31:0]Y */
#define VECTOR_CMD_MOVETO_DATA(x, y) \
    (VECTOR_CMD_TYPE(VECTOR_CMD_MOVETO) | VECTOR_CMD_X(x)), \
    VECTOR_CMD_Y(y)

/* LineTo command: 8 bytes - [31:30]cmd + [29:0]X + [31:0]Y */
#define VECTOR_CMD_LINETO_DATA(x, y) \
    (VECTOR_CMD_TYPE(VECTOR_CMD_LINETO) | VECTOR_CMD_X(x)), \
    VECTOR_CMD_Y(y)

/* ConicTo command: 16 bytes - [31:30]cmd + [29:0]C0_X + [31:0]C0_Y + [29:0]X + [31:0]Y */
#define VECTOR_CMD_CONICTO_DATA(c0_x, c0_y, x, y) \
    (VECTOR_CMD_TYPE(VECTOR_CMD_CONICTO) | VECTOR_CMD_C0_X(c0_x)), \
    VECTOR_CMD_C0_Y(c0_y), \
    VECTOR_CMD_X(x), \
    VECTOR_CMD_Y(y)

/* CubicTo command: 24 bytes - [31:30]cmd + [29:0]C0_X + [31:0]C0_Y + [29:0]C1_X + [31:0]C1_Y + [29:0]X + [31:0]Y */
#define VECTOR_CMD_CUBICTO_DATA(c0_x, c0_y, c1_x, c1_y, x, y) \
    (VECTOR_CMD_TYPE(VECTOR_CMD_CUBICTO) | VECTOR_CMD_C0_X(c0_x)), \
    VECTOR_CMD_C0_Y(c0_y), \
    VECTOR_CMD_C1_X(c1_x), \
    VECTOR_CMD_C1_Y(c1_y), \
    VECTOR_CMD_X(x), \
    VECTOR_CMD_Y(y)

/* Edge list structure definitions */
#define EDGE_SECTION_SIZE     64  /* bytes per section */
#define EDGE_MAX_PER_SECTION  6   /* max edges per section */

/* Edge data structure masks */
#define EDGE_X_MASK           GENMASK(29, 0)
#define EDGE_X(x)             ((x) & 0x3FFFFFFF)

#define EDGE_Y_MASK           GENMASK(31, 0)
#define EDGE_Y(y)             ((y) & 0xFFFFFFFF)

#define EDGE_DX_MASK          GENMASK(29, 0)
#define EDGE_DX(dx)           ((dx) & 0x3FFFFFFF)

#define EDGE_DY_MASK          GENMASK(31, 0)
#define EDGE_DY(dy)           ((dy) & 0xFFFFFFFF)

/* Edge list section structure */
#define EDGE_SECTION_SIZE           64  /* bytes per section */
#define EDGE_SECTION_HEADER_SIZE    4   /* bytes for section size */
#define EDGE_SECTION_NEXT_ADDR_SIZE 4   /* bytes for next address */
#define EDGE_SECTION_DATA_SIZE      (EDGE_SECTION_SIZE - EDGE_SECTION_HEADER_SIZE - EDGE_SECTION_NEXT_ADDR_SIZE)

/* Edge section structure offsets */
#define EDGE_SECTION_SIZE_OFFSET    0
#define EDGE_SECTION_DATA_OFFSET    4
#define EDGE_SECTION_NEXT_ADDR_OFFSET 60

#endif /* _ARTVG_REG_H_ */

#ifdef __cplusplus
}
#endif /* __cplusplus */
