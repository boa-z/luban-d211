// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) 2022-2025 ArtInChip Technology Co., Ltd
 */

#include <common.h>
#include <command.h>
#include <console.h>
#include <malloc.h>
#include <dm.h>
#include <misc.h>
#include <asm/io.h>
#include <linux/delay.h>
#include "spi_aes_key.h"

#ifndef CONFIG_SPL_BUILD

static struct udevice *efuse_dev;
static struct udevice *get_efuse_device(void)
{
	struct udevice *dev = NULL;
	int ret;

	if (efuse_dev)
		return efuse_dev;

	ret = uclass_first_device_err(UCLASS_MISC, &dev);
	if (ret) {
		pr_err("Get UCLASS_MISC device failed.\n");
		return NULL;
	}

	do {
		if (device_is_compatible(dev, "artinchip,aic-sid-v1.0"))
			break;
		ret = uclass_next_device_err(&dev);
	} while (dev);

	efuse_dev = dev;
	return efuse_dev;
}

static void hexdump(const u8 *buf, uint len)
{
	int i;

	for (i = 0; i < len; i++) {
		if (i && (i % 16) == 0)
			printf("\n");
		if ((i % 16) == 0)
			printf("0x%08lx : ", (unsigned long)&buf[i]);
		printf("%02x ", buf[i]);
	}
	printf("\n");
}

static int write_efuse(char *msg, u32 offset, const void *val, u32 size)
{
#if CONFIG_ARTINCHIP_SID_BURN_SIMULATED
	printf("eFuse %s:\n", msg);
	hexdump((unsigned char *)val, size);
	return size;
#else
	struct udevice *dev = NULL;
	int wrcnt;

	dev = get_efuse_device();
	if (!dev) {
		pr_err("Failed to get efuse device.\n");
		return -1;
	}

	wrcnt = misc_write(dev, offset, val, size);
	if (wrcnt != size) {
		pr_err("Failed to write eFuse.\n");
		return -1;
	}

	return size;
#endif
}

static int read_efuse(u32 offset, void *val, u32 size)
{
	struct udevice *dev = NULL;
	int rdcnt;

	dev = get_efuse_device();
	if (!dev) {
		pr_err("Failed to get efuse device.\n");
		return -1;
	}

	rdcnt = misc_read(dev, offset, val, size);
	if (rdcnt != size) {
		pr_err("Failed to read eFuse.\n");
		return -1;
	}

	return size;
}

static int burn_brom_spienc_bit(void)
{
	u32 offset = 0xFFFF, val;
	int ret;

	offset = 0x38;
	val = 0;
	val |= (1 << 16); // Secure boot bit for brom
	val |= (1 << 19); // SPIENC boot bit for brom

	ret = write_efuse("brom enable spienc secure bit", offset, (const void *)&val, 4);
	if (ret <= 0) {
		printf("Write BROM SPIENC bit error\n");
		return -1;
	}

	return 0;
}

static int check_brom_spienc_bit(void)
{
	u32 offset = 0xFFFF, val, mskval = 0;
	int ret;

	offset = 0x38;
	mskval = 0;
	mskval |= (1 << 16); // Secure boot bit for brom
	mskval |= (1 << 19); // SPIENC boot bit for brom

	ret = read_efuse(offset, (void *)&val, 4);
	if (ret <= 0) {
		printf("Read secure bit efuse error.\n");
		return -1;
	}

	if ((val & mskval) == mskval) {
		printf("BROM SPIENC is ENABLED\n");
	} else {
		printf("BROM SPIENC is NOT enabled\n");
	}

	return 0;
}

static int burn_jtag_lock_bit(void)
{
	u32 offset = 0xFFFF, val;
	int ret;

	offset = 0x38;
	val = 0;
	val |= (1 << 0); // JTAG LOCK

	ret = write_efuse("jtag lock bit", offset, (const void *)&val, 4);
	if (ret <= 0) {
		printf("Write JTAG LOCK bit error\n");
		return -1;
	}

	return 0;
}

static int check_jtag_lock_bit(void)
{
	u32 offset = 0xFFFF, val, mskval = 0;
	int ret;

	offset = 0x38;
	mskval = 0;
	mskval |= (1 << 0); // JTAG LOCK

	ret = read_efuse(offset, (void *)&val, 4);
	if (ret <= 0) {
		printf("Read secure bit efuse error.\n");
		return -1;
	}

	if ((val & mskval) == mskval) {
		printf("JTAG LOCK   is ENABLED\n");
	} else {
		printf("JTAG LOCK   is NOT enabled\n");
	}

	return 0;
}

static int burn_spienc_key(void)
{
	u32 offset = 0xFFFF;
	int ret;

	offset = 0xA0;

	ret = write_efuse("spi_aes.key", offset, (const void *)spi_aes_key, spi_aes_key_len);
	if (ret <= 0) {
		printf("Write SPI ENC AES key error.\n");
		return -1;
	}

	return 0;
}

static int check_spienc_key(void)
{
	u32 offset = 0xFFFF;
	u8 data[256];
	int ret;

	offset = 0xA0;

	ret = read_efuse(offset, (void *)data, 16);
	if (ret <= 0) {
		printf("Read efuse error.\n");
		return -1;
	}

	printf("SPI ENC KEY:\n");
	hexdump(data, 16);

	return 0;
}

static int burn_spienc_nonce(void)
{
	u32 offset;
	int ret;

	offset = 0xB0;
	ret = write_efuse("spi_nonce.key", offset, (const void *)spi_nonce_key,
			  spi_nonce_key_len);
	if (ret <= 0) {
		printf("Write SPI ENC NONCE key error.\n");
		return -1;
	}

	return 0;
}

static int check_spienc_nonce(void)
{
	u32 offset;
	u8 data[256];
	int ret;

	offset = 0xB0;
	ret = read_efuse(offset, (void *)data, 8);
	if (ret <= 0) {
		printf("Read efuse error.\n");
		return -1;
	}

	printf("SPI ENC NONCE:\n");
	hexdump(data, 8);

	return 0;
}

static int burn_spienc_rotpk(void)
{
	u32 offset = 0xFFFF;
	int ret;

	offset = 0x40;
	ret = write_efuse("rotpk.bin", offset, (const void *)rotpk_bin,
			  rotpk_bin_len);
	if (ret <= 0) {
		printf("Write SPI ENC ROTPK error.\n");
		return -1;
	}

	return 0;
}

static int check_spienc_rotpk(void)
{
	u32 offset = 0xFFFF;
	u8 data[256];
	int ret;

	offset = 0x40;
	ret = read_efuse(offset, (void *)data, 16);
	if (ret <= 0) {
		printf("Read efuse error.\n");
		return -1;
	}

	printf("ROTPK:\n");
	hexdump(data, 16);

	return 0;
}

static int burn_spienc_key_read_write_disable_bits(void)
{
	u32 offset, val;
	int ret;

	// SPIENC KEY and NONCE
	offset = 0x4;
	val = 0;
	val = 0x00003F00; // SPIENC Key and Nonce Read disable
	ret = write_efuse("spienc key/nonce r dis", offset, (const void *)&val,
			  4);
	if (ret <= 0) {
		printf("Write r/w disable bit efuse error.\n");
		return -1;
	}

	//  ROTPK
	offset = 0x8;
	val = 0;
	val = 0x000F0000; // ROTPK Write disable
	ret = write_efuse("rotpk w dis", offset, (const void *)&val, 4);
	if (ret <= 0) {
		printf("Write r/w disable bit efuse error.\n");
		return -1;
	}
	// SPIENC KEY and NONCE
	offset = 0xC;
	val = 0;
	val = 0x00003F00; // SPIENC Key Write disable
	ret = write_efuse("spienc key/nonce w dis", offset, (const void *)&val,
			  4);
	if (ret <= 0) {
		printf("Write r/w disable bit efuse error.\n");
		return -1;
	}

	return 0;
}

static int check_spienc_key_read_write_disable_bits(void)
{
	u32 offset, val, mskval;
	int ret;

	offset = 0x4;
	mskval = 0x00003F00;
	ret = read_efuse(offset, (void *)&val, 4);
	if (ret <= 0) {
		printf("Read read disable bit efuse error.\n");
		return -1;
	}

	if ((val & mskval) == mskval)
		printf("SPI ENC Key is read DISABLED\n");
	else
		printf("SPI ENC Key is NOT read disabled\n");

	offset = 0x8;
	mskval = 0x000F0000;
	ret = read_efuse(offset, (void *)&val, 4);
	if (ret <= 0) {
		printf("Read write disable bit efuse error.\n");
		return -1;
	}

	if ((val & mskval) == mskval)
		printf("SPI ENC ROTPK is write DISABLED\n");
	else
		printf("SPI ENC ROTPK is NOT write disabled\n");

	offset = 0xC;
	mskval = 0x00003F00;
	ret = read_efuse(offset, (void *)&val, 4);
	if (ret <= 0) {
		printf("Read write disable bit efuse error.\n");
		return -1;
	}

	if ((val & mskval) == mskval)
		printf("SPI ENC Key is write DISABLED\n");
	else
		printf("SPI ENC Key is NOT write disabled\n");

	return 0;
}

int cmd_efuse_do_spienc(struct cmd_tbl *cmdtp, int flag, int argc, char *const argv[])
{
	int ret;

	ret = burn_brom_spienc_bit();
	if (ret) {
		printf("Error\n");
		return -1;
	}

	ret = burn_spienc_key();
	if (ret) {
		printf("Error\n");
		return -1;
	}

	ret = burn_spienc_nonce();
	if (ret) {
		printf("Error\n");
		return -1;
	}

	ret = burn_spienc_rotpk();
	if (ret) {
		printf("Error\n");
		return -1;
	}

	ret = burn_spienc_key_read_write_disable_bits();
	if (ret) {
		printf("Error\n");
		return -1;
	}

	ret = burn_jtag_lock_bit();
	if (ret) {
		printf("Error\n");
		return -1;
	}

	ret = check_brom_spienc_bit();
	if (ret) {
		printf("Error\n");
		return -1;
	}

	ret = check_jtag_lock_bit();
	if (ret) {
		printf("Error\n");
		return -1;
	}
	ret = check_spienc_key();
	if (ret) {
		printf("Error\n");
		return -1;
	}

	ret = check_spienc_nonce();
	if (ret) {
		printf("Error\n");
		return -1;
	}

	ret = check_spienc_rotpk();
	if (ret) {
		printf("Error\n");
		return -1;
	}

	ret = check_spienc_key_read_write_disable_bits();
	if (ret) {
		printf("Error\n");
		return -1;
	}

	printf("\n");
	printf("Write SPI ENC eFuse done.\n");
#if CONFIG_ARTINCHIP_SID_BURN_SIMULATED
	printf("WARNING: This is a dry run to check the eFuse content, key is not burn to eFuse yet.\n");
#endif
#if !CONFIG_ARTINCHIP_SID_CONTINUE_BOOT_BURN_AFTER
	while (1)
		continue;
#endif
	return 0;
}

U_BOOT_CMD(efuse_spienc, 1, 1, cmd_efuse_do_spienc,
	   "ArtInChip eFuse burn spienc command", "");

#endif
