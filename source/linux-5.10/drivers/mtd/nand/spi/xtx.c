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

#define SPINAND_MFR_XTX			0x0B

#define XT26XXXC_STATUS_ECC_MASK			GENMASK(7, 4)
#define XT26XXXC_STATUS_ECC_NO_DETECTED     (0)
#define XT26XXXC_STATUS_ECC_STATUS_OFF		(4)
#define XT26XXXC_STATUS_ECC_UNCOR_ERROR     GENMASK(7, 4)

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

static int xt26xxxc_ooblayout_ecc(struct mtd_info *mtd, int section,
					struct mtd_oob_region *region)
{
	if (section)
		return -ERANGE;

	region->offset = 64;
	region->length = 52;

	return 0;
}

static int xt26xxxc_ooblayout_free(struct mtd_info *mtd, int section,
					struct mtd_oob_region *region)
{
	if (section)
		return -ERANGE;

	region->offset = 2;
	region->length = 62;

	return 0;
}

static const struct mtd_ooblayout_ops xt26xxxc_ooblayout = {
	.ecc = xt26xxxc_ooblayout_ecc,
	.free = xt26xxxc_ooblayout_free,
};

static int xt26xxxc_ecc_get_status(struct spinand_device *spinand,
					u8 status)
{
	status = status & XT26XXXC_STATUS_ECC_MASK;

	switch (status) {
	case XT26XXXC_STATUS_ECC_NO_DETECTED:
		return 0;
	case XT26XXXC_STATUS_ECC_UNCOR_ERROR:
		return -EBADMSG;

	default:
		break;
	}

	/* At this point values greater than (2 << 4) are invalid  */
	if (status > XT26XXXC_STATUS_ECC_UNCOR_ERROR)
		return -EINVAL;

	/* (1 << 4) through (7 << 4) are 1-7 corrected errors */
	return status >> XT26XXXC_STATUS_ECC_STATUS_OFF;
}

static const struct spinand_info xtx_spinand_table[] = {
	SPINAND_INFO("XT26G01C",
		SPINAND_ID(SPINAND_READID_METHOD_OPCODE_DUMMY, 0x11),
		NAND_MEMORG(1, 2048, 64, 64, 1024, 40, 1, 1, 1),
		NAND_ECCREQ(8, 528),
		SPINAND_INFO_OP_VARIANTS(&read_cache_variants,
					 &write_cache_variants,
					 &update_cache_variants),
		SPINAND_HAS_QE_BIT,
		SPINAND_ECCINFO(&xt26xxxc_ooblayout,
				xt26xxxc_ecc_get_status)),
};

static int xtx_spinand_init(struct spinand_device *spinand)
{
	return 0;
}

static void xtx_spinand_cleanup(struct spinand_device *spinand)
{

}

static const struct spinand_manufacturer_ops xtx_spinand_manuf_ops = {
	.init = xtx_spinand_init,
	.cleanup = xtx_spinand_cleanup
};

const struct spinand_manufacturer xtx_spinand_manufacturer = {
	.id = SPINAND_MFR_XTX,
	.name = "xtx",
	.chips = xtx_spinand_table,
	.nchips = ARRAY_SIZE(xtx_spinand_table),
	.ops = &xtx_spinand_manuf_ops,
};
