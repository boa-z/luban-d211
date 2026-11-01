// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2024, ArtInChip Co., Ltd
 * Author: dwj <weijie.ding@artinchip.com>
 */

#include <linux/init.h>
#include <linux/suspend.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <asm/sbi.h>
#include <linux/clk.h>
#include <linux/clk-provider.h>

#define SBI_HSM_SUSPEND_RET_PLATFORM		0x10000000
#define BUS_CLK_SEL_OFFSET			8
static void __iomem *aic_cmu_base;

static int aic_pm_state_enter(suspend_state_t state)
{
	struct sbiret ret;

	ret = sbi_ecall(SBI_EXT_HSM, SBI_EXT_HSM_HART_SUSPEND,
			SBI_HSM_SUSPEND_RET_PLATFORM, 0, 0, 0, 0, 0);

	return (ret.error) ? sbi_err_map_linux_errno(ret.error) : 0;
}

static int aic_pm_prepare_late(void)
{
	u32 reg_val;

	/* AXI bus */
	reg_val = readl(aic_cmu_base + 0x100);
	reg_val &= ~(1 << BUS_CLK_SEL_OFFSET);
	writel(reg_val, aic_cmu_base + 0x100);

	/* AHB0 bus */
	reg_val = readl(aic_cmu_base + 0x110);
	reg_val &= ~(1 << BUS_CLK_SEL_OFFSET);
	writel(reg_val, aic_cmu_base + 0x110);

	/* APB0 bus */
	reg_val = readl(aic_cmu_base + 0x120);
	reg_val &= ~(1 << BUS_CLK_SEL_OFFSET);
	writel(reg_val, aic_cmu_base + 0x120);

	return 0;
}

static void aic_pm_wake(void)
{
	u32 reg_val;

	/* AXI bus */
	reg_val = readl(aic_cmu_base + 0x100);
	reg_val |= (1 << BUS_CLK_SEL_OFFSET);
	writel(reg_val, aic_cmu_base + 0x100);

	/* AHB0 bus */
	reg_val = readl(aic_cmu_base + 0x110);
	reg_val |= (1 << BUS_CLK_SEL_OFFSET);
	writel(reg_val, aic_cmu_base + 0x110);

	/* APB0 bus */
	reg_val = readl(aic_cmu_base + 0x120);
	reg_val |= (1 << BUS_CLK_SEL_OFFSET);
	writel(reg_val, aic_cmu_base + 0x120);
}

static const struct platform_suspend_ops aic_pm_ops = {
	.valid	= suspend_valid_only_mem,
	.enter	= aic_pm_state_enter,
	.prepare_late = aic_pm_prepare_late,
	.wake = aic_pm_wake,
};

static int __init aic_suspend_init(void)
{
	struct device_node *node;
	struct resource res;
	int ret = 0;

	suspend_set_ops(&aic_pm_ops);

	node = of_find_compatible_node(NULL, NULL, "artinchip,aic-cmu-v1.0");
	if (!node)
		return -ENODEV;

	ret = of_address_to_resource(node, 0, &res);
	if (ret)
		goto __put_node;

	aic_cmu_base = ioremap(res.start, resource_size(&res));
	if (!aic_cmu_base) {
		ret = -ENOMEM;
		goto __put_node;
	}

	return 0;
__put_node:
	return ret;
}

subsys_initcall(aic_suspend_init);

