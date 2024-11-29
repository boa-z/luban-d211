// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) 2021 ArtInChip Technology Co., Ltd
 */
#include <common.h>
#include <artinchip/aicupg.h>
#include "upg_internal.h"
#include <mmc.h>
#include <env.h>

#if 0
#undef debug
#define debug printf
#endif

#define MMC_BLOCK_SIZE   512

struct aicupg_mmc_priv {
	struct mmc *mmc;
	struct disk_partition part_info;
};

static struct mmc *init_mmc(int dev)
{
	struct mmc *mmc;

	mmc = find_mmc_device(dev);
	if (!mmc) {
		printf("no mmc device at slot %x\n", dev);
		return NULL;
	}

	if (mmc_init(mmc))
		return NULL;

#ifdef CONFIG_SUPPORT_EMMC_BOOT
	if (!IS_SD(mmc)) {
		/*
		 * For eMMC:
		 * Set RST_n_FUNCTION to 1: enable it
		 */
		if (mmc->ext_csd[EXT_CSD_RST_N_FUNCTION] == 0)
			mmc_set_rst_n_function(mmc, 1);
	}
#endif

#ifdef CONFIG_BLOCK_CACHE
	struct blk_desc *bd = mmc_get_blk_desc(mmc);
	blkcache_invalidate(bd->if_type, bd->devnum);
#endif
	return mmc;
}

s32 mmc_fwc_prepare(struct fwc_info *fwc, u32 mmc_id)
{
	int ret = 0;

	/*Set GPT partition*/
	ret = aicupg_mmc_create_gpt_part(mmc_id, false);
	if (ret < 0) {
		pr_err("Create GPT partitions failed\n");
		return ret;
	}
	return ret;
}

s32 mmc_is_exist(struct fwc_info *fwc, u32 mmc_id)
{
	if (mmc_id >= get_mmc_num()) {
		pr_err("Invalid mmc dev %d\n", mmc_id);
		return -ENODEV;
	}

	return 0;
}

void mmc_fwc_start(struct fwc_info *fwc)
{
	struct aicupg_mmc_priv *priv;
	struct blk_desc *desc;
	int dev, ret = 0;

	pr_debug("%s, FWC name %s\n", __func__, fwc->meta.name);
	fwc->block_size = MMC_BLOCK_SIZE;
	fwc->burn_result = 0;
	fwc->run_result = 0;
	priv = malloc(sizeof(struct aicupg_mmc_priv));
	if (!priv)
		goto err;

	dev = get_current_device_id();
	priv->mmc = init_mmc(dev);
	if (!priv->mmc) {
		pr_err("init mmc failed.\n");
		goto err;
	}
	desc = mmc_get_blk_desc(priv->mmc);
	ret = part_get_info_by_name(desc, fwc->meta.partition,
				    &priv->part_info);
	if (ret == -1) {
		pr_err("Get partition information failed.\n");
		goto err;
	}
	fwc->priv = priv;
	return;
err:
	if (priv)
		free(priv);
}

s32 mmc_fwc_data_write(struct fwc_info *fwc, u8 *buf, s32 len)
{
	struct aicupg_mmc_priv *priv;
	lbaint_t blkstart, blkcnt;
	struct blk_desc *desc;
	u8 *rdbuf;
	s32 clen = 0, calc_len;
	long n;

	rdbuf = malloc(len);
	priv = (struct aicupg_mmc_priv *)fwc->priv;
	if (!priv) {
		pr_err("MMC FWC get priv failed.\n");
		goto out;
	}

	if (mmc_getwp(priv->mmc) == 1) {
		pr_err("Error: card is write protected!\n");
		goto out;
	}

	blkstart = fwc->trans_size / MMC_BLOCK_SIZE;
	blkcnt = len / MMC_BLOCK_SIZE;
	if (len % MMC_BLOCK_SIZE)
		blkcnt++;

	if ((blkstart + blkcnt) > priv->part_info.size) {
		pr_err("Data size exceed the partition size.\n");
		goto out;
	}

	desc = mmc_get_blk_desc(priv->mmc);
	n = blk_dwrite(desc, priv->part_info.start + blkstart, blkcnt, buf);
	clen = len;
	if (n != blkcnt) {
		pr_err("Error, write to partition %s failed.\n",
		      fwc->meta.partition);
		fwc->burn_result += 1;
		clen = n * MMC_BLOCK_SIZE;
		fwc->trans_size += clen;
	}
	// Read data to calc crc
	n = blk_dread(desc, priv->part_info.start + blkstart, blkcnt, rdbuf);
	if (n != blkcnt) {
		pr_err("Error, read from partition %s failed.\n",
		      fwc->meta.partition);
		fwc->burn_result += 1;
		clen = n * MMC_BLOCK_SIZE;
		fwc->trans_size += clen;
	}

	if ((fwc->meta.size - fwc->trans_size) < len)
		calc_len = fwc->meta.size % DEFAULT_BLOCK_ALIGNMENT_SIZE;
	else
		calc_len = len;

	fwc->calc_partition_crc = crc32(fwc->calc_partition_crc,
						rdbuf, calc_len);
#ifdef CONFIG_AICUPG_SINGLE_TRANS_BURN_CRC32_VERIFY
	fwc->calc_trans_crc = crc32(fwc->calc_trans_crc, buf, calc_len);
	if (fwc->calc_trans_crc != fwc->calc_partition_crc) {
		pr_err("calc_len:%d\n", calc_len);
		pr_err("crc err at trans len %u\n", fwc->trans_size);
		pr_err("trans crc:0x%x, partition crc:0x%x\n",
				fwc->calc_trans_crc, fwc->calc_partition_crc);
	}
#endif

	fwc->trans_size += clen;

	debug("%s, data len %d, trans len %d, calc len %d\n", __func__, len,
	      fwc->trans_size, calc_len);

out:
	free(rdbuf);
	return clen;
}

s32 mmc_fwc_data_read(struct fwc_info *fwc, u8 *buf, s32 len)
{
	struct aicupg_mmc_priv *priv;
	lbaint_t blkstart, blkcnt, start, trans_size;
	struct blk_desc *desc;
	s32 clen = 0;
	long n;

	priv = (struct aicupg_mmc_priv *)fwc->priv;
	if (!priv) {
		pr_err("MMC FWC get priv failed.\n");
		return 0;
	}

	start = fwc->mpart.part.start / MMC_BLOCK_SIZE;
	trans_size = fwc->trans_size;
	blkstart = trans_size / MMC_BLOCK_SIZE;
	blkcnt = len / MMC_BLOCK_SIZE;
	if (len % MMC_BLOCK_SIZE)
		blkcnt++;

	if ((blkstart + blkcnt) > fwc->mpart.part.size) {
		pr_err("Data size exceed the partition size.\n");
		return 0;
	}

	desc = mmc_get_blk_desc(priv->mmc);
	n = blk_dread(desc, start + blkstart, blkcnt, buf);
	clen = len;
	if (n != blkcnt) {
		pr_err("Error, Read from partition %s failed.\n",
		      fwc->meta.partition);
		fwc->burn_result += 1;
		clen = n * MMC_BLOCK_SIZE;
		fwc->trans_size += clen;
	}

	fwc->trans_size += clen;
	fwc->calc_partition_crc = fwc->meta.crc;

	debug("%s, data len %d, trans len %d\n", __func__, len,
	      fwc->trans_size);
	return clen;
}

void mmc_fwc_data_end(struct fwc_info *fwc)
{
	struct aicupg_mmc_priv *priv;
	priv = (struct aicupg_mmc_priv *)fwc->priv;
	if (fwc->priv) {
		free(priv);
		fwc->priv = 0;
	}
}
