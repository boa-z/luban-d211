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

#define SPINAND_MFR_FORESEE			0xCD
#define FORESEE_STATUS_ECC_NO_BITFLIPS	    	(0 << 4)
#define FORESEE_STATUS_ECC_1_BITFLIPS	    	(1 << 4)
#define FORESEE_ECC_UNCOR_ERROR10           	(2 << 4)
#define FORESEE_ECC_UNCOR_ERROR11           	(3 << 4)
#define STATUS_ECC_MASK			GENMASK(5, 4)

static SPINAND_OP_VARIANTS(read_cache_variants,
		SPINAND_PAGE_READ_FROM_CACHE_X4_OP(0, 1, NULL, 0),
		SPINAND_PAGE_READ_FROM_CACHE_X2_OP(0, 1, NULL, 0),
		SPINAND_PAGE_READ_FROM_CACHE_OP(true, 0, 1, NULL, 0),
		SPINAND_PAGE_READ_FROM_CACHE_OP(false, 0, 1, NULL, 0));

static SPINAND_OP_VARIANTS(write_cache_variants,
		SPINAND_PROG_LOAD_X4(true, 0, NULL, 0),
		SPINAND_PROG_LOAD(true, 0, NULL, 0));

static SPINAND_OP_VARIANTS(update_cache_variants,
		SPINAND_PROG_LOAD_X4(false, 0, NULL, 0),
		SPINAND_PROG_LOAD(false, 0, NULL, 0));

static int f35sqa_ooblayout_ecc(struct mtd_info *mtd, int section,
				  struct mtd_oob_region *region)
{
	if (section)
		return -ERANGE;

	region->offset = mtd->oobsize;
	region->length = 0;

	return 0;
}

static int f35sqa_ooblayout_free(struct mtd_info *mtd, int section,
				   struct mtd_oob_region *region)
{
	if (section)
		return -ERANGE;

	region->offset = 2;
	region->length = mtd->oobsize - 2;

	return 0;
}

static const struct mtd_ooblayout_ops f35sqa_ooblayout = {
	.ecc = f35sqa_ooblayout_ecc,
	.free = f35sqa_ooblayout_free,
};

static int f35sqa_ecc_get_status(struct spinand_device *spinand,
				      u8 status)
{
	switch (status & STATUS_ECC_MASK) {
	case FORESEE_STATUS_ECC_NO_BITFLIPS:
		return 0;

	case FORESEE_STATUS_ECC_1_BITFLIPS:
		return 1;

	case FORESEE_ECC_UNCOR_ERROR10:
	case FORESEE_ECC_UNCOR_ERROR11:
		return -EBADMSG;

	default:
		break;
	}

	return -EINVAL;
}

static const struct spinand_info foresee_spinand_table[] = {
	SPINAND_INFO("F35SQA002G",
		SPINAND_ID(SPINAND_READID_METHOD_OPCODE_DUMMY, 0x72),
		NAND_MEMORG(1, 2048, 64, 64, 2048, 40, 1, 1, 1),
		NAND_ECCREQ(1, 528),
		SPINAND_INFO_OP_VARIANTS(&read_cache_variants,
			&write_cache_variants,
			&update_cache_variants),
			SPINAND_HAS_QE_BIT,
			SPINAND_ECCINFO(&f35sqa_ooblayout,
			f35sqa_ecc_get_status)),
	SPINAND_INFO("F35SQA001G",
		SPINAND_ID(SPINAND_READID_METHOD_OPCODE_DUMMY, 0x71),
		NAND_MEMORG(1, 2048, 64, 64, 1024, 40, 1, 1, 1),
		NAND_ECCREQ(1, 528),
		SPINAND_INFO_OP_VARIANTS(&read_cache_variants,
			&write_cache_variants,
			&update_cache_variants),
			SPINAND_HAS_QE_BIT,
			SPINAND_ECCINFO(&f35sqa_ooblayout,
			f35sqa_ecc_get_status)),
	SPINAND_INFO("F35SQA512M",
		SPINAND_ID(SPINAND_READID_METHOD_OPCODE_DUMMY, 0x70),
		NAND_MEMORG(1, 2048, 64, 64, 512, 40, 1, 1, 1),
		NAND_ECCREQ(1, 528),
		SPINAND_INFO_OP_VARIANTS(&read_cache_variants,
			&write_cache_variants,
			&update_cache_variants),
			SPINAND_HAS_QE_BIT,
			SPINAND_ECCINFO(&f35sqa_ooblayout,
			f35sqa_ecc_get_status)),
	SPINAND_INFO("F35SQB004G",
		SPINAND_ID(SPINAND_READID_METHOD_OPCODE_DUMMY, 0x53),
		NAND_MEMORG(1, 4096, 128, 64, 2048, 40, 1, 1, 1),
		NAND_ECCREQ(1, 528),
		SPINAND_INFO_OP_VARIANTS(&read_cache_variants,
			&write_cache_variants,
			&update_cache_variants),
			SPINAND_HAS_QE_BIT,
			SPINAND_ECCINFO(&f35sqa_ooblayout,
			f35sqa_ecc_get_status)),
	SPINAND_INFO("FS35ND01G",
		SPINAND_ID(SPINAND_READID_METHOD_OPCODE_DUMMY, 0xEA),
		NAND_MEMORG(1, 2048, 64, 64, 1024, 40, 1, 1, 1),
		NAND_ECCREQ(1, 528),
		SPINAND_INFO_OP_VARIANTS(&read_cache_variants,
			&write_cache_variants,
			&update_cache_variants),
			SPINAND_HAS_QE_BIT,
			SPINAND_ECCINFO(&f35sqa_ooblayout,
			f35sqa_ecc_get_status)),
};

static int foresee_spinand_init(struct spinand_device *spinand)
{
	pr_info("Foresee %s \n", __func__);
	return 0;
}

static void foresee_spinand_cleanup(struct spinand_device *spinand)
{
	pr_info("Foresee %s \n", __func__);
}

static const struct spinand_manufacturer_ops foresee_spinand_manuf_ops = {
	.init = foresee_spinand_init,
	.cleanup = foresee_spinand_cleanup
};

const struct spinand_manufacturer foresee_spinand_manufacturer = {
	.id = SPINAND_MFR_FORESEE,
	.name = "Foresee",
	.chips = foresee_spinand_table,
	.nchips = ARRAY_SIZE(foresee_spinand_table),
	.ops = &foresee_spinand_manuf_ops,
};
