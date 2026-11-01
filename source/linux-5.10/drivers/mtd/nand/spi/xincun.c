// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2022-2025 ArtInChip
 *
 * Authors:
 *	keliang.liu <keliang.liu@artinchip.com>
 */

#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/mtd/spinand.h>

#define SPINAND_MFR_XINCUN			0x8C

#define XINCUN_STATUS_ECC_MASK			GENMASK(5, 4)
#define XINCUN_STATUS_ECC_NO_DETECTED     	(0)
#define XINCUN_STATUS_ECC_STATUS_OFF		BIT(4)
#define XINCUN_STATUS_ECC_UNCOR_ERROR     	BIT(5)

static SPINAND_OP_VARIANTS(read_cache_variants,
		SPINAND_PAGE_READ_FROM_CACHE_QUADIO_OP(0, 1, NULL, 0),
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
	if (section)
		return -ERANGE;

	region->offset = 64;
	region->length = 64;

	return 0;
}

static int xcsp1aapk_ooblayout_free(struct mtd_info *mtd, int section,
					struct mtd_oob_region *region)
{
	if (section)
		return -ERANGE;

	region->offset = 2;
	region->length = 62;

	return 0;
}

static const struct mtd_ooblayout_ops xcsp1aapk_ooblayout = {
	.ecc = xcsp1aapk_ooblayout_ecc,
	.free = xcsp1aapk_ooblayout_free,
};

static int xt26xxxc_ecc_get_status(struct spinand_device *spinand,
					u8 status)
{
	status = status & XINCUN_STATUS_ECC_MASK;

	switch (status) {
	case XINCUN_STATUS_ECC_NO_DETECTED:
		return 0;
	case XINCUN_STATUS_ECC_UNCOR_ERROR:
		return -EBADMSG;
	case XINCUN_STATUS_ECC_STATUS_OFF:
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
		SPINAND_ID(SPINAND_READID_METHOD_OPCODE_ADDR, 0x01),
		NAND_MEMORG(1, 2048, 128, 64, 1024, 40, 1, 1, 1),
		NAND_ECCREQ(8, 528),
		SPINAND_INFO_OP_VARIANTS(&read_cache_variants,
					 &write_cache_variants,
					 &update_cache_variants),
		SPINAND_HAS_QE_BIT,
		SPINAND_ECCINFO(&xcsp1aapk_ooblayout,
				xt26xxxc_ecc_get_status)),
};

static int xincun_spinand_init(struct spinand_device *spinand)
{
	return 0;
}

static void xincun_spinand_cleanup(struct spinand_device *spinand)
{

}

static const struct spinand_manufacturer_ops xincun_spinand_manuf_ops = {
	.init = xincun_spinand_init,
	.cleanup = xincun_spinand_cleanup
};

const struct spinand_manufacturer xincun_spinand_manufacturer = {
	.id = SPINAND_MFR_XINCUN,
	.name = "XINCUN",
	.chips = xincun_spinand_table,
	.nchips = ARRAY_SIZE(xincun_spinand_table),
	.ops = &xincun_spinand_manuf_ops,
};
