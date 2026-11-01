// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2025 ArtInChip
 *
 * Authors:
 *	jiji.chen <jiji.chen@artinchip.com>
 */

#ifndef __UBOOT__
#include <malloc.h>
#include <linux/device.h>
#include <linux/kernel.h>
#endif
#include <linux/bitops.h>
#include <linux/mtd/spinand.h>

#define SPINAND_MFR_XINCUN		0x8C

#define XINCUN_STATUS_ECC_MASK 		GENMASK(5, 4)
#define XINCUN_STATUS_ECC_NO_BITFLIPS	(0)
#define XINCUN_STATUS_ECC_UNCOR_ERROR	BIT(5)
#define XINCUN_STATUS_ECC_1_4_BITFLIP	BIT(4)

static SPINAND_OP_VARIANTS(read_cache_variants,
		SPINAND_PAGE_READ_FROM_CACHE_QUADIO_OP(0, 2, NULL, 0),
		SPINAND_PAGE_READ_FROM_CACHE_X4_OP(0, 1, NULL, 0),
		SPINAND_PAGE_READ_FROM_CACHE_DUALIO_OP(0, 1, NULL, 0),
		SPINAND_PAGE_READ_FROM_CACHE_X2_OP(0, 1, NULL, 0),
		SPINAND_PAGE_READ_FROM_CACHE_OP(true, 0, 1, NULL, 0),
		SPINAND_PAGE_READ_FROM_CACHE_OP(false, 0, 1, NULL, 0));

static SPINAND_OP_VARIANTS(write_cache_variants,
		SPINAND_PROG_LOAD_X4(true, 0, NULL, 0),
		SPINAND_PROG_LOAD(true, 0, NULL, 0));

static SPINAND_OP_VARIANTS(update_cache_variants,
		SPINAND_PROG_LOAD_X4(false, 0, NULL, 0),
		SPINAND_PROG_LOAD(false, 0, NULL, 0));

static int xcsp1aapk_ooblayout_ecc(struct mtd_info *mtd, int section,
					struct mtd_oob_region *region)
{
	if (section > 1)
		return -ERANGE;

	region->offset = 64;
	region->length = 64;

	return 0;
}

static int xcsp1aapk_ooblayout_free(struct mtd_info *mtd, int section,
					struct mtd_oob_region *region)
{
	if (section > 1)
		return -ERANGE;

	region->offset = 2;
	region->length = 62;

	return 0;
}

static const struct mtd_ooblayout_ops xcsp1aapk_ooblayout = {
	.ecc = xcsp1aapk_ooblayout_ecc,
	.rfree = xcsp1aapk_ooblayout_free,
};

static int xincun_ecc_get_status(struct spinand_device *spinand,
					u8 status)
{
	switch (status & XINCUN_STATUS_ECC_MASK) {
	case XINCUN_STATUS_ECC_NO_BITFLIPS:
		return 0;

	case XINCUN_STATUS_ECC_UNCOR_ERROR:
		return -EBADMSG;

	case XINCUN_STATUS_ECC_1_4_BITFLIP:
		return 4;

	case XINCUN_STATUS_ECC_MASK:
		return 8;

	default:
		break;
	}

	return -EINVAL;
}

static const struct spinand_info xincun_spinand_table[] = {
	SPINAND_INFO("XCSP1AAPK-IT",
		     SPINAND_ID(0x01),
		     NAND_MEMORG(1, 2048, 128, 64, 1024, 1, 1, 1),
		     NAND_ECCREQ(8, 528),
		     SPINAND_INFO_OP_VARIANTS(&read_cache_variants,
					      &write_cache_variants,
					      &update_cache_variants),
			 SPINAND_HAS_QE_BIT,
		     SPINAND_ECCINFO(&xcsp1aapk_ooblayout, xincun_ecc_get_status)),
};

/**
 * xincun_spinand_detect - initialize device related part in spinand_device
 * struct if it is a XINCUN device.
 * @spinand: SPI NAND device structure
 */
static int xincun_spinand_detect(struct spinand_device *spinand)
{
	u8 *id = spinand->id.data;
	int ret;

	/*
	 * XINCUN SPI NAND read ID need a dummy byte,
	 * so the first byte in raw_id is dummy.
	 */
	if (id[1] != SPINAND_MFR_XINCUN)
		return 0;

	ret = spinand_match_and_init(spinand, xincun_spinand_table,
				     ARRAY_SIZE(xincun_spinand_table), &id[2]);
	if (ret)
		return ret;

	return 1;
}

static int xincun_spinand_init(struct spinand_device *spinand)
{
	return 0;
}

static const struct spinand_manufacturer_ops xincun_spinand_manuf_ops = {
	.detect = xincun_spinand_detect,
	.init = xincun_spinand_init,
};

const struct spinand_manufacturer xincun_spinand_manufacturer = {
	.id = SPINAND_MFR_XINCUN,
	.name = "XINCUN",
	.ops = &xincun_spinand_manuf_ops,
};
