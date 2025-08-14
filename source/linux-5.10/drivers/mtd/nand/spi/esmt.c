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

#define SPINAND_MFR_ESMT			0xC8

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

/*
 * OOB spare area map (64 bytes)
 *
 * Bad Block Markers
 * filled by HW and kernel                 Reserved
 *   |                 +-----------------------+-----------------------+
 *   |                 |                       |                       |
 *   |                 |    OOB free data Area |non ECC protected      |
 *   |   +-------------|-----+-----------------|-----+-----------------|-----+
 *   |   |             |     |                 |     |                 |     |
 * +-|---|----------+--|-----|--------------+--|-----|--------------+--|-----|--------------+
 * | |   | section0 |  |     |    section1  |  |     |    section2  |  |     |    section3  |
 * +-v-+-v-+---+----+--v--+--v--+-----+-----+--v--+--v--+-----+-----+--v--+--v--+-----+-----+
 * |   |   |   |    |     |     |     |     |     |     |     |     |     |     |     |     |
 * |0:1|2:3|4:7|8:15|16:17|18:19|20:23|24:31|32:33|34:35|36:39|40:47|48:49|50:51|52:55|56:63|
 * |   |   |   |    |     |     |     |     |     |     |     |     |     |     |     |     |
 * +---+---+-^-+--^-+-----+-----+--^--+--^--+-----+-----+--^--+--^--+-----+-----+--^--+--^--+
 *           |    |                |     |                 |     |                 |     |
 *           |    +----------------|-----+-----------------|-----+-----------------|-----+
 *           |             ECC Area|(Main + Spare) - filled|by ESMT NAND HW        |
 *           |                     |                       |                       |
 *           +---------------------+-----------------------+-----------------------+
 *                         OOB ECC protected Area - not used due to
 *                         partial programming from some filesystems
 *                             (like JFFS2 with cleanmarkers)
 */

static int f50l1g_ooblayout_ecc(struct mtd_info *mtd, int section,
				  struct mtd_oob_region *region)
{
	if (section >= 4)
		return -ERANGE;

	region->offset = section * 16 + 8;
	region->length = 8;

	return 0;
}

static int f50l1g_ooblayout_free(struct mtd_info *mtd, int section,
				   struct mtd_oob_region *region)
{
	if (section >= 4)
		return -ERANGE;

	/*
	 * Reserve space for bad blocks markers (section0) and
	 * reserved bytes (sections 1-3)
	 */
	region->offset = section * 16 + 2;

	/* Use only 2 non-protected ECC bytes per each OOB section */
	region->length = 2;

	return 0;
}

static const struct mtd_ooblayout_ops f50l1g_ooblayout = {
	.ecc = f50l1g_ooblayout_ecc,
	.free = f50l1g_ooblayout_free,
};

static int f50l1g_ecc_get_status(struct spinand_device *spinand,
				      u8 status)
{
	switch (status & STATUS_ECC_MASK) {
	case STATUS_ECC_NO_BITFLIPS:
		return 0;

	case STATUS_ECC_UNCOR_ERROR:
		return -EBADMSG;

	case STATUS_ECC_HAS_BITFLIPS:
		return ((status & STATUS_ECC_MASK) >> 4);

	default:
		break;
	}

	return -EINVAL;
}

static const struct spinand_info esmt_spinand_table[] = {
	SPINAND_INFO("F50L1G",
		SPINAND_ID(SPINAND_READID_METHOD_OPCODE_DUMMY, 0x01),
		NAND_MEMORG(1, 2048, 64, 64, 1024, 20, 1, 1, 1),
		NAND_ECCREQ(1, 512),
		SPINAND_INFO_OP_VARIANTS(&read_cache_variants,
			&write_cache_variants,
			&update_cache_variants),
			SPINAND_HAS_QE_BIT,
			SPINAND_ECCINFO(&f50l1g_ooblayout,
			f50l1g_ecc_get_status)),
};

static int esmt_spinand_init(struct spinand_device *spinand)
{
	pr_info("Esmt %s \n", __func__);
	return 0;
}

static void esmt_spinand_cleanup(struct spinand_device *spinand)
{
	pr_info("Esmt %s \n", __func__);
}

static const struct spinand_manufacturer_ops esmt_spinand_manuf_ops = {
	.init = esmt_spinand_init,
	.cleanup = esmt_spinand_cleanup
};

const struct spinand_manufacturer esmt_spinand_manufacturer = {
	.id = SPINAND_MFR_ESMT,
	.name = "Esmt",
	.chips = esmt_spinand_table,
	.nchips = ARRAY_SIZE(esmt_spinand_table),
	.ops = &esmt_spinand_manuf_ops,
};
