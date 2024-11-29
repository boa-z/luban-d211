/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2020-2022 ArtInChip Technology Co., Ltd.
 * Authors:  matteo <duanmt@artinchip.com>
 */

#ifndef _RGB_REG_H_
#define _RGB_REG_H_

#include <linux/bits.h>
#include <linux/io.h>
#include <linux/types.h>

enum aic_rgb_cko_phase_sel {
	CKO_PHASE_SEL_0 = 0x0,
	CKO_PHASE_SEL_90 = 0x1,
	CKO_PHASE_SEL_180 = 0x2,
	CKO_PHASE_SEL_270 = 0x3
};

#define RGB_LCD_CTL_MODE_MASK		GENMASK(5, 4)
#define RGB_LCD_CTL_MODE(x)		(((x)&0x3) << 4)
#define RGB_LCD_CTL_EN			BIT(0)

#define RGB_CLK_CTL_CKO_PHASE_MASK	GENMASK(1, 0)
#define RGB_CLK_CTL_CKO_PHASE(x)	(((x)&0x3) << 0)

#define RGB_LCD_CTL_PRGB_MODE_MASK	GENMASK(10, 8)
#define RGB_LCD_CTL_PRGB_MODE(x)	(((x)&0x7) << 8)
#define RGB_LCD_CTL_SRGB_MODE		BIT(12)
#define RGB_LCD_CTL_I8080_MODE_MASK	GENMASK(19, 16)
#define RGB_LCD_CTL_I8080_MODE(x)	(((x)&0xF) << 16)
#define RGB_LCD_CTL_SPI_MODE_MASK	GENMASK(21, 20)
#define RGB_LCD_CTL_SPI_MODE(x)		(((x)&0x3)<<20)
#define RGB_LCD_CTL_SPI_FORMAT_MASK	GENMASK(27, 24)
#define RGB_LCD_CTL_SPI_FORMAT(x)	(((x)&0xF)<<24)

#define RGB_DATA_SEL_EVEN_DP2316_MASK	GENMASK(25, 24)
#define RGB_DATA_SEL_EVEN_DP2316(x)	(((x)&0x3) << 24)
#define RGB_DATA_SEL_EVEN_DP1508_MASK	GENMASK(21, 20)
#define RGB_DATA_SEL_EVEN_DP1508(x)	(((x)&0x3) << 20)
#define RGB_DATA_SEL_EVEN_DP0700_MASK	GENMASK(17, 16)
#define RGB_DATA_SEL_EVEN_DP0700(x)	(((x)&0x3) << 16)
#define RGB_DATA_SEL_OOD_DP2316_MASK	GENMASK(9, 8)
#define RGB_DATA_SEL_OOD_DP2316(x)	(((x)&0x3) << 8)
#define RGB_DATA_SEL_OOD_DP1508_MASK	GENMASK(5, 4)
#define RGB_DATA_SEL_OOD_DP1508(x)	(((x)&0x3) << 4)
#define RGB_DATA_SEL_OOD_DP0700_MASK	GENMASK(1, 0)
#define RGB_DATA_SEL_OOD_DP0700(x)	(((x)&0x3) << 0)

#define RGB_DATA_OUT_SEL_MASK		GENMASK(2, 0)
#define RGB_DATA_OUT_SEL(x)		(((x) & 0x7) << 0)

#define CKO_PHASE_SEL_MASK		GENMASK(1, 0)
#define CKO_PHASE_SEL(x)		(((x) & 0x3) << 0)

#define RGB_I8080_TX_FIFO_EMPTY		BIT(1)
#define RGB_I8080_TX_FIFO_EMPTY_SHIFT	1
#define RGB_I8080_RD_FIFO_FLUSH		BIT(21)
#define RGB_I8080_WR_FIFO_FLUSH		BIT(20)
#define RGB_I8080_RD_FIFO_DEPTH_MASK	GENMASK(14, 8)
#define RGB_I8080_RD_FIFO_DEPTH_SHIFT	8
#define RGB_I8080_WR_FIFO_DEPTH_MASK	GENMASK(6, 0)
#define RGB_I8080_WR_FIFO_DEPTH_SHIFT	0

#define RGB_SPI_TX_FIFO_EMPTY		BIT(1)
#define RGB_SPI_TX_FIFO_EMPTY_SHIFT	1
#define RGB_SPI_RD_FIFO_FLUSH		BIT(21)
#define RGB_SPI_WR_FIFO_FLUSH		BIT(20)
#define RGB_SPI_RD_FIFO_DEPTH_MASK	GENMASK(14, 8)
#define RGB_SPI_RD_FIFO_DEPTH_SHIFT	8
#define RGB_SPI_WR_FIFO_DEPTH_MASK	GENMASK(6, 0)
#define RGB_SPI_WR_FIFO_DEPTH_SHIFT	0

#define FIRST_LINE_COMMAND_MASK		GENMASK(15, 8)
#define OTHER_LINE_COMMAND_MASK		GENMASK(31, 24)
#define FIRST_LINE_COMMAND(x)		(((x) & 0xff) << 8)
#define OTHER_LINE_COMMAND(x)		(((x) & 0xff) << 24)
#define FIRST_LINE_COMMAND_CTL		BIT(0)
#define OTHER_LINE_COMMAND_CTL		BIT(16)

#define CODE1_CFG_MASK			GENMASK(23, 16)
#define CODE1_CFG(x)			(((x) & 0xff) << 16)
#define VBP_NUM_MASK			GENMASK(15, 8)
#define VBP_NUM(x)			(((x) & 0xff) << 8)
#define QSPI_MODE_MASK			BIT(0)

#define SCL_CTL				BIT(4)
#define SCL_PHASE_CFG			BIT(1)
#define SCL_POL				BIT(0)

#define RGB_LCD_CTL		0x00
#define RGB_CLK_CTL		0x10
#define RGB_DATA_SEL		0x20
#define RGB_OOD_DATA		0x24
#define RGB_EVEN_DATA		0x28
#define RGB_DATA_SEQ_SEL	0x30
#define RGB_VERSION		0xFC

#define I8080_COMMAND_CTL	0x100
#define I8080_WR_CMD		0x104
#define I8080_WR_DATA		0x108
#define I8080_WR_CTL		0x10C
#define I8080_RD_CTL		0x110
#define I8080_RD_DATA		0x114
#define I8080_FIFO_DEPTH	0x118
#define I8080_INT_ENABLE	0x11C
#define I8080_INT_CLR		0x120
#define I8080_STATUS		0x124
#define I8080_CLK_CTL		0x128

#define SPI_SCL_CFG		0x200
#define QSPI_CODE		0x204
#define SPI_COMMAND_CTL		0x208
#define SPI_WR_CMD		0x20C
#define SPI_WR_DATA		0x210
#define SPI_WR_CTL		0x214
#define SPI_RD_CTL		0x218
#define SPI_RD_DATA		0x21C
#define SPI_FIFO_DEPTH		0x220
#define SPI_INT_ENABLE		0x224
#define SPI_INT_CLR		0x228
#define SPI_STATUS		0x22C
#define QSPI_MODE		0x234

void i8080_cmd_wr(void __iomem *base, u32 code, u32 count, const u8 *data);
void i8080_cmd_ctl(void __iomem *base, u32 first_line, u32 other_line);

void i8080_rd_ctl(void __iomem *base, u32 count, u32 start);
u32 i8080_rd_data(void __iomem *base);

u32 i8080_rd_fifo_depth(void __iomem *base);
u32 i8080_wr_fifo_depth(void __iomem *base);
void i8080_rd_fifo_flush(void __iomem *base);
void i8080_wr_fifo_flush(void __iomem *base);

void spi_cmd_wr(void __iomem *base, u32 code, u32 count, const u8 *data);
void spi_cmd_ctl(void __iomem *base, u32 first_line, u32 other_line);
void spi_scl_cfg(void __iomem *base, u32 phase, u32 pol);

void spi_rd_ctl(void __iomem *base, u32 count, u32 start);
u32 spi_rd_data(void __iomem *base);

u32 spi_rd_fifo_depth(void __iomem *base);
u32 spi_wr_fifo_depth(void __iomem *base);
void spi_rd_fifo_flush(void __iomem *base);
void spi_wr_fifo_flush(void __iomem *base);

void qspi_code_cfg(void __iomem *base, u32 code1, u32 code2, u32 code3);
void qspi_mode_cfg(void __iomem *base, u32 code1_cfg, u32 vbp_num, u32 qspi_mode);

#endif // end of _RGB_REG_H_
