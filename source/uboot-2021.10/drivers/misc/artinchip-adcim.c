// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2025 ArtInChip Technology Co.,Ltd
 * Author: Matteo <duanmt@artinchip.com>
 */

#include <common.h>
#include <dm.h>
#include <dm/device.h>
#include <dm/device_compat.h>
#include <misc.h>
#include <clk.h>
#include <reset.h>
#include <linux/io.h>

/* Register of ADCIM */
#define ADCIM_MCSR       0x000
#define ADCIM_CALCSR     0x004

#define ADCIM_CALCSR_CALVAL_UPD		BIT(31)
#define ADCIM_CALCSR_CALVAL_SHIFT	16
#define ADCIM_CALCSR_CALVAL_MASK	GENMASK(27, 16)
#define ADCIM_CALCSR_ADC_ACQ_SHIFT	8
#define ADCIM_CALCSR_ADC_ACQ_MASK	GENMASK(15, 8)
#define ADCIM_CALCSR_DCAL		BIT(1)
#define ADCIM_CALCSR_CAL_ENABLE		BIT(0)

struct aic_adcim_plat {
	void __iomem *regs;
	struct clk clk;
	struct reset_ctl reset;
};

static void adcim_set_dcalmask(struct aic_adcim_plat *adcim)
{
	int val;

	val = readl(adcim->regs + ADCIM_CALCSR);
	val = val | ADCIM_CALCSR_DCAL | ADCIM_CALCSR_ADC_ACQ_MASK;
	writel(val, adcim->regs + ADCIM_CALCSR);
}

static int aic_adcim_of_to_plat(struct udevice *dev)
{
	struct aic_adcim_plat *adcim = dev_get_plat(dev);
#if !defined(CONFIG_SPL_BUILD) || defined(CONFIG_SPL_CLK_ARTINCHIP)
	int ret = 0;

	ret = clk_get_by_index(dev, 0, &adcim->clk);
	if (ret < 0) {
		printf("Failed to get ADCIM clock\n");
		return ret;
	}
	ret = reset_get_by_index(dev, 0, &adcim->reset);
	if (ret && ret != -ENOENT) {
		printf("Failed to get ADCIM reset\n");
		return ret;
	}
#endif
	adcim->regs = (void *)devfdt_get_addr(dev);
	return 0;
}

static int aic_adcim_probe(struct udevice *dev)
{
	struct aic_adcim_plat *adcim = dev_get_plat(dev);
	int ret = 0;

#if !defined(CONFIG_SPL_BUILD) || defined(CONFIG_SPL_CLK_ARTINCHIP)

	ret = clk_enable(&adcim->clk);
	if (ret < 0) {
		printf("Failed to enable ADCIM clock\n");
		return ret;
	}

	ret = reset_deassert(&adcim->reset);
	if (ret < 0) {
		printf("Failed to deassert ADCIM reset\n");
		return ret;
	}
#endif

	adcim_set_dcalmask(adcim);
	dev_dbg(dev, "ArtInChip ADCIM Loaded\n");
	return ret;
}

static int aic_adcim_remove(struct udevice *dev)
{
	struct aic_adcim_plat *adcim = dev_get_plat(dev);

	reset_assert(&adcim->reset);
	clk_disable(&adcim->clk);
	return 0;
}

static const struct udevice_id aic_adcim_ids[] = {
	{ .compatible = "artinchip,aic-adcim-v1.0" },
	{ }
};

U_BOOT_DRIVER(artinchip_adcim) = {
	.name      = "aic-adcim",
	.id        = UCLASS_MISC,
	.of_match  = aic_adcim_ids,
	.of_to_plat = aic_adcim_of_to_plat,
	.probe     = aic_adcim_probe,
	.remove    = aic_adcim_remove,
	.plat_auto = sizeof(struct aic_adcim_plat),
};
