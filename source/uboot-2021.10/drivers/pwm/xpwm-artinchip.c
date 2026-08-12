// SPDX-License-Identifier: GPL-2.0-only
/*
 * XPWM driver of ArtInChip SoC
 *
 * Copyright (C) 2022-2026 ArtInChip Technology Co., Ltd.
 * Authors:  zrq <ruiqi.zheng@artinchip.com>
 */

#include <common.h>
#include <log.h>
#include <pwm.h>
#include <asm/io.h>
#include <dm/device_compat.h>
#include <clk.h>
#include <reset.h>
#include <dm.h>
#include <div64.h>

#define AIC_XPWM_NAME			"aic-xpwm"

/* Register definition */
#define PWM_CONF			0x000
#define PWM_CH_CONF			0x004
#define XPWM_STS_FLAG			0x008
#define XPWM_INT_EN			0x00C
#define XPWM_FIFO_FLUSH			0x010
#define XPWM_RESUME			0x014
#define PUL_THR_RS			0x018
#define XPWM_FIFO			0x020
#define XPWM_PUL_STA			0x024
#define XPWM_CNT_STA			0x028
#define XPWM_PRDV			0x050
#define PUL_CNT				0x054
#define PUL_THR				0x058
#define XPWM_CMPV			0x060
#define XPWM_VER			0x0FC

/* PWM_CONF */
#define PWM_CONF_CLKDIV_MAX		0x3FF
#define PWM_CONF_CLK_DIV_SHIFT		16
#define XPWM_CNT_EN			BIT(0)
#define XPWM_MOD			BIT(1)
#define XPWM_RESUME_EN			BIT(2)
#define XPWM_FIFO_EN			BIT(4)
#define PUL_STA_EN			BIT(5)
#define XPWM_DMA_EN			BIT(6)
#define PUL_LIMIT_EN			BIT(7)
#define PWM_CONF_XPWM_FIFO_TH_SHIFT	8
#define XPWM_FIFO_TH_DEFAULT_VALUE	0xC

/* PWM_CH_CONF */
#define XPWM_INV_EN			BIT(1)
#define XPWM_IDLE			BIT(2)

#define XPWM_PRD_MAX			0xFFFFFFFF
#define XPWM_DEFAULT_TB_CLK_RATE	24000000
#define NSEC_PER_SEC			1000000000L

struct aic_xpwm_chip {
	u8 __iomem *regs;
	unsigned long clk_rate;
	u32 tb_clk_rate;
	bool xpwm_mode;
	bool def_level;
	bool polarity;
	struct clk clk;
	struct clk set_rate;
	struct reset_ctl rst;
};

static void xpwm_reg_enable(u8 __iomem *base, int offset, int bit, int enable)
{
	int tmp;

	tmp = readl(base + offset);
	tmp &= ~bit;
	if (enable)
		tmp |= bit;

	writel(tmp, base + offset);
}

static int aic_xpwm_set_config(struct udevice *dev, uint channel,
				uint period_ns, uint duty_ns)
{
	struct aic_xpwm_chip *xpwm = dev_get_priv(dev);
	u32 prd;
	u64 duty;
	u32 freq;

	if (period_ns < 1 || period_ns > NSEC_PER_SEC) {
		dev_err(dev, "Invalid period %u\n", period_ns);
		return -ERANGE;
	}

	freq = NSEC_PER_SEC / period_ns;
	prd = xpwm->tb_clk_rate / freq;

	if (prd > XPWM_PRD_MAX) {
		dev_err(dev, "period %u is too big\n", prd);
		return -ERANGE;
	}

	duty = (u64)duty_ns * (u64)prd;
	do_div(duty, period_ns);
	if (duty == prd)
		duty--;

	writel(prd, xpwm->regs + XPWM_PRDV);
	writel((u32)duty, xpwm->regs + XPWM_CMPV);

	return 0;
}

static int aic_xpwm_set_invert(struct udevice *dev, uint channel, bool polarity)
{
	struct aic_xpwm_chip *xpwm = dev_get_priv(dev);

	xpwm->polarity = polarity;
	xpwm_reg_enable(xpwm->regs, PWM_CH_CONF, XPWM_INV_EN, polarity);
	return 0;
}

static int aic_xpwm_set_enable(struct udevice *dev, uint channel, bool enable)
{
	struct aic_xpwm_chip *xpwm = dev_get_priv(dev);
	u32 div;

	if (!enable) {
		xpwm_reg_enable(xpwm->regs, PWM_CONF, XPWM_CNT_EN, 0);
		return 0;
	}

	if (xpwm->clk_rate == xpwm->tb_clk_rate)
		div = 0;
	else
		div = xpwm->clk_rate / xpwm->tb_clk_rate - 1;

	if (div > PWM_CONF_CLKDIV_MAX) {
		dev_err(dev, "clkdiv %u is too big\n", div);
		return -ERANGE;
	}
	writel(div << PWM_CONF_CLK_DIV_SHIFT, xpwm->regs + PWM_CONF);

	xpwm_reg_enable(xpwm->regs, PWM_CH_CONF, XPWM_IDLE, xpwm->def_level);
	xpwm_reg_enable(xpwm->regs, PWM_CH_CONF, XPWM_INV_EN, xpwm->polarity);

	if (xpwm->xpwm_mode)
		xpwm_reg_enable(xpwm->regs, PWM_CONF, XPWM_MOD, 1);
	else
		xpwm_reg_enable(xpwm->regs, PWM_CONF, XPWM_MOD, 0);

	xpwm_reg_enable(xpwm->regs, PWM_CONF, XPWM_CNT_EN, 1);

	return 0;
}

static const struct pwm_ops aic_xpwm_ops = {
	.set_config = aic_xpwm_set_config,
	.set_invert = aic_xpwm_set_invert,
	.set_enable = aic_xpwm_set_enable,
};

static int aic_xpwm_cfg_clocks_from_dt(struct udevice *dev,
					 struct aic_xpwm_chip *xpwm)
{
	const char *clock_names;
	int ret;

	clock_names = dev_read_string(dev, "clock-names");
	if (!clock_names) {
		dev_err(dev, "No clock-names property found\n");
		return -ENODEV;
	}

	if (strncmp(clock_names, "xpwm", 4) == 0) {
		ret = clk_get_by_name(dev, "xpwm", &xpwm->clk);
		if (ret < 0) {
			dev_err(dev, "Failed to get xpwm clk\n");
			return ret;
		}

		ret = clk_get_by_name(dev, "xpwm_sdfm", &xpwm->set_rate);
		if (ret < 0) {
			dev_info(dev, "Unable to obtain xpwm_sdfm clk, use xpwm clk to set rate.\n");
			xpwm->set_rate = xpwm->clk;
		} else {
			dev_info(dev, "Successfully obtain xpwm_sdfm clk, use xpwm_sdfm clk to set rate.\n");
		}
	} else if (strncmp(clock_names, "pwm", 3) == 0) {
		ret = clk_get_by_name(dev, "pwm", &xpwm->clk);
		if (ret < 0) {
			dev_err(dev, "Failed to get pwm clk\n");
			return ret;
		}

		ret = clk_get_by_name(dev, "pwm_sdfm", &xpwm->set_rate);
		if (ret < 0) {
			dev_info(dev, "Unable to obtain pwm_sdfm clk, use pwm clk to set rate.\n");
			xpwm->set_rate = xpwm->clk;
		} else {
			dev_info(dev, "Successfully obtain pwm_sdfm clk, use pwm_sdfm clk to set rate.\n");
		}
	} else {
		dev_err(dev, "Unsupported clock name: %s\n", clock_names);
		return -EINVAL;
	}

	return 0;
}

static int aic_xpwm_parse_dt(struct udevice *dev)
{
	struct aic_xpwm_chip *xpwm = dev_get_priv(dev);
	u32 val;
	int ret;

	ret = dev_read_u32(dev, "clock-rate", &val);
	if (ret) {
		dev_warn(dev, "Can't parse clock-rate\n");
		return ret;
	}
	xpwm->clk_rate = val;

	ret = dev_read_u32(dev, "aic,tb-clk-rate", &xpwm->tb_clk_rate);
	if (ret || xpwm->tb_clk_rate == 0) {
		dev_err(dev, "Invalid tb-clk-rate %u\n", xpwm->tb_clk_rate);
		xpwm->tb_clk_rate = XPWM_DEFAULT_TB_CLK_RATE;
	}

	ret = dev_read_u32(dev, "aic,default-level", &val);
	if (ret) {
		dev_dbg(dev, "def_level not exist, default to 0\n");
		xpwm->def_level = 0;
	} else {
		xpwm->def_level = (bool)val;
	}

	ret = dev_read_u32(dev, "aic,polarity", &val);
	if (ret) {
		dev_dbg(dev, "polarity not exist, default to 0\n");
		xpwm->polarity = 0;
	} else {
		xpwm->polarity = (bool)val;
	}

	xpwm->xpwm_mode = dev_read_bool(dev, "aic,xpwm-mode");

	ret = aic_xpwm_cfg_clocks_from_dt(dev, xpwm);
	if (ret) {
		dev_err(dev, "Clocks configuration error!\n");
		return ret;
	}

	return 0;
}

static int aic_xpwm_of_to_plat(struct udevice *dev)
{
	struct aic_xpwm_chip *xpwm = dev_get_priv(dev);

	xpwm->regs = (u8 __iomem *)dev_read_addr(dev);
	return 0;
}

static int aic_xpwm_probe(struct udevice *dev)
{
	struct aic_xpwm_chip *xpwm = dev_get_priv(dev);
	int ret;

	ret = aic_xpwm_parse_dt(dev);
	if (ret)
		return ret;

	ret = clk_enable(&xpwm->clk);
	if (ret < 0) {
		dev_err(dev, "clk_enable() failed: %d\n", ret);
		return ret;
	}

	ret = clk_set_rate(&xpwm->set_rate, xpwm->clk_rate);
	if (ret) {
		dev_err(dev, "Failed to set clk_rate %lu\n", xpwm->clk_rate);
		goto out_disable_clk;
	}

	xpwm->clk_rate = clk_get_rate(&xpwm->set_rate);
	if (xpwm->clk_rate == 0) {
		dev_err(dev, "Failed to get clk_rate %lu\n", xpwm->clk_rate);
		goto out_disable_clk;
	}
	dev_info(dev, "Actually clk rate:%lu\n", xpwm->clk_rate);

	ret = reset_get_by_index(dev, 0, &xpwm->rst);
	if (ret < 0) {
		dev_warn(dev, "Failed to get reset (optional), ret=%d\n", ret);
	} else {
		reset_deassert(&xpwm->rst);
	}

	dev_info(dev, "ArtInChip XPWM Loaded.\n");
	return 0;

out_disable_clk:
	clk_disable(&xpwm->clk);
	return ret;
}

int pwm_status_show(struct udevice *dev)
{
	struct aic_xpwm_chip *xpwm = dev_get_priv(dev);
	u8 __iomem *regs = xpwm->regs;
	u32 ver = readl(regs + XPWM_VER);
	u32 conf = readl(regs + PWM_CONF);
	u32 ch_conf = readl(regs + PWM_CH_CONF);
	u32 sts = readl(regs + XPWM_STS_FLAG);
	u32 prd = readl(regs + XPWM_PRDV);
	u32 cmp = readl(regs + XPWM_CMPV);

	printf("XPWM V%d.%02d:\n", ver >> 8, ver & 0xFF);
	printf("  Mode: %s, Status: %s\n",
			conf & XPWM_MOD ? "XPWM" : "PWM",
			conf & XPWM_CNT_EN ? "Enabled" : "Disabled");
	printf("  Clock-rate: %lu, TB-Clock-rate: %u\n",
			xpwm->clk_rate, xpwm->tb_clk_rate);
	printf("  Default-level: %d, Polarity: %d\n",
			xpwm->def_level, xpwm->polarity);
	printf("  CONF: %08x, CH_CONF: %08x, STS: %08x\n",
			conf, ch_conf, sts);
	printf("  PRD: %u, CMP: %u\n", prd, cmp);

	return 0;
}

static const struct udevice_id aic_xpwm_ids[] = {
	{ .compatible = "artinchip,aic-xpwm-v1.0" },
	{},
};

U_BOOT_DRIVER(aic_xpwm) = {
	.name		= AIC_XPWM_NAME,
	.id		= UCLASS_PWM,
	.of_match	= aic_xpwm_ids,
	.ops		= &aic_xpwm_ops,
	.of_to_plat	= aic_xpwm_of_to_plat,
	.probe		= aic_xpwm_probe,
	.priv_auto	= sizeof(struct aic_xpwm_chip),
};
