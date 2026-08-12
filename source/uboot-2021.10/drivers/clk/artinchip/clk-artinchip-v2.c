// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (c) 2025-2026 ArtInChip Inc.
 */

#include <common.h>
#include <command.h>
#include <clk-uclass.h>
#include <dm.h>
#include <dt-structs.h>
#include <errno.h>
#include <asm/io.h>
#include <dm/device-internal.h>
#include <dm/lists.h>
#include <dm/uclass-internal.h>
#include <linux/delay.h>
#include <div64.h>
#include <log.h>
#include "clk-aic-v2.h"

/* ALL chips:
 * Other vco of clock not change
 * The vco of pll_fra2 range from (768M~1560M) to (360M~1584M)
 *
 * Different clock version need to be distinguished
 */
static const struct pll_vco vco_arr[] = {
#ifdef CONFIG_CLK_ARTINCHIP_CMU_V1_0
	{360000000, 1584000000,  8},
#endif
#ifdef CONFIG_CLK_ARTINCHIP_CMU_V2_0
	{360000000, 1584000000, 17},
#endif
	{768000000, 1560000000, 0},
};

static void clk_vco_select(struct aic_pll *pll,
			   unsigned long *min, unsigned long *max)
{
	const struct pll_vco *vco;

	for (int i = 0; i < ARRAY_SIZE(vco_arr); i++) {
		vco = &vco_arr[i];
		if (pll->id == vco->id) {
			*min = vco->vco_min;
			*max = vco->vco_max;
			return;
		}
	}
	*min = vco_arr[ARRAY_SIZE(vco_arr) - 1].vco_min;
	*max = vco_arr[ARRAY_SIZE(vco_arr) - 1].vco_max;
}

static enum aic_clk_type aic_get_clk_info(struct aic_clk_tree *tree, u32 id,
					  u32 *index)
{
	int i;

	for (i = 0; i < tree->fixed_rate_cnt; i++) {
		if (id == tree->fixed_rate[i].id) {
			*index = i;
			return AIC_CLK_FIXED_RATE;
		}
	}

	for (i = 0; i < tree->pll_cnt; i++) {
		if (id == tree->plls[i].id) {
			*index = i;
			return AIC_CLK_PLL;
		}
	}

	for (i = 0; i < tree->fixed_parent_cnt; i++) {
		if (id == tree->fixed_parent_clk[i].id) {
			*index = i;
			return AIC_CLK_FIXED_PARENT;
		}
	}

	for (i = 0; i < tree->multi_parent_cnt; i++) {
		if (id == tree->multi_parent_clk[i].id) {
			*index = i;
			return AIC_CLK_MULTI_PARENT;
		}
	}

	for (i = 0; i < tree->auth_cnt; i++) {
		if (id == tree->auth_clk[i].id) {
			*index = i;
			return AIC_CLK_AUTH;
		}
	}

	return AIC_CLK_UNKNOWN;
}

static ulong fixed_rate_clk_get_rate(struct clk *clk, int index)
{
	struct aic_clk_priv *priv = dev_get_priv(clk->dev);
	struct aic_clk_tree *tree = priv->tree;

	return tree->fixed_rate[index].rate;
}

static ulong artinchip_get_parent_rate(struct clk *clk, u32 id)
{
	struct clk parent = {.id = id, .dev = clk->dev };

	return clk_get_rate(&parent);
}

static int pll_clk_enable(struct clk *clk, int index)
{
	u32 value;
	struct aic_clk_priv *priv = dev_get_priv(clk->dev);
	struct aic_clk_tree *tree = priv->tree;
	struct aic_pll *pll = &tree->plls[index];

	value = readl((uchar *)priv->base +  pll->gen_reg);
	value |= (1 << 18) | (1 << 16);
	writel(value, (uchar *)priv->base +  pll->gen_reg);

	return 0;
}

static int pll_clk_disable(struct clk *clk, int index)
{
	u32 value;
	struct aic_clk_priv *priv = dev_get_priv(clk->dev);
	struct aic_clk_tree *tree = priv->tree;
	struct aic_pll *pll = &tree->plls[index];

	value = readl((uchar *)priv->base +  pll->gen_reg);
	value &= ~(1 << 16);
	writel(value, (uchar *)priv->base +  pll->gen_reg);

	return 0;
}

static ulong pll_clk_get_rate(struct clk *clk, int index)
{
	u32 value, div_p, div_n, div_m, fra_in;
	u64 rate, rate_int, rate_fra;
	u32 fra_en = 0;
	struct aic_clk_priv *priv = dev_get_priv(clk->dev);
	struct aic_clk_tree *tree = priv->tree;
	struct aic_pll *pll = &tree->plls[index];

	value = readl((uchar *)priv->base +  pll->gen_reg);
	div_p = (value & 0x01);
	div_m = (value >> 4) & 0x03;
	div_n = (value >> 8) & 0xff;

	if (pll->sub_type == AIC_PLL_FRA)
		fra_en = readl((uchar *)priv->base +  pll->frac_reg) & (1 << 20);

	if (pll->sub_type == AIC_PLL_INT || !fra_en)
		rate = 24000000 / (div_p + 1) * (div_n + 1) / (div_m + 1);
	else {
		fra_in = readl((uchar *)priv->base +  pll->frac_reg) & 0x1FFFF;
		rate_int = 24000000 / (div_p + 1) * (div_n + 1) / (div_m + 1);
		rate_fra = (u64)24000000 / (div_p + 1) * fra_in;
		do_div(rate_fra, 0x1FFFF * (div_m + 1));
		rate = rate_int + rate_fra;
	}

	return rate;
}

static ulong pll_clk_round_rate(struct clk *clk, ulong rate, int index)
{
	u32 factor_n, factor_m, factor_p;
	ulong rrate, vco_rate, pll_vco_min, pll_vco_max;
	struct aic_clk_priv *priv = dev_get_priv(clk->dev);
	struct aic_clk_tree *tree = priv->tree;
	struct aic_pll *pll = &tree->plls[index];

	if (pll->sub_type != AIC_PLL_INT) {
		if (rate < pll->min_rate)
			return pll->min_rate;
		else if (pll->max_rate && rate > pll->max_rate)
			return pll->max_rate;
		else if (pll->sub_type == AIC_PLL_FRA)
			return rate;
	}

	clk_vco_select(pll, &pll_vco_min, &pll_vco_max);

	/* The frequency constraint of PLL_VCO is between 768M and 1560M
	 * But the PLL_VCO of pll_fra2 is between 360M and 1584M
	 */
	if (rate < pll_vco_min)
		factor_m = DIV_ROUND_UP(pll_vco_min, rate) - 1;
	else
		factor_m = 0;

	if (factor_m > 3)
		factor_m = 3;

	vco_rate = (factor_m + 1) * rate;
	if (vco_rate > pll_vco_max)
		vco_rate = pll_vco_max;

	factor_p = (vco_rate % 24000000) ? 1 : 0;
	if (!factor_p)
		return rate;
	else if (!(vco_rate % (24000000 / (factor_p + 1))))
		return rate;

	factor_n = vco_rate / 24000000 * (factor_p + 1)   - 1;

	rrate = 24000000 / (factor_p + 1) * (factor_n + 1) / (factor_m + 1);

	return rrate;
}


static ulong pll_clk_set_rate(struct clk *clk, ulong rate, int index)
{
	u32 reg_val, factor_p, factor_n, factor_m, sdm_en;
	u64 val, fra_in = 0;
	u8 fra_en;
	ulong vco_rate, pll_vco_min, pll_vco_max;
#ifdef CONFIG_CLK_ARTINCHIP_PLL_SDM
	u32 ppm_max, sdm_amp, sdm_step;
#endif
	struct aic_clk_priv *priv = dev_get_priv(clk->dev);
	struct aic_clk_tree *tree = priv->tree;
	struct aic_pll *pll = &tree->plls[index];

	clk_vco_select(pll, &pll_vco_min, &pll_vco_max);

	/* Calculate PLL parameters.
	 * The frequency constraint of PLL_VCO is between 768M and 1560M
	 * But the PLL_VCO of pll_fra2 is between 360M and 1584M
	 */
	if (rate < pll_vco_min)
		factor_m = DIV_ROUND_UP(pll_vco_min, rate) - 1;
	else
		factor_m = 0;

	if (factor_m > 3)
		factor_m = 3;

	vco_rate = (factor_m + 1) * rate;
	if (vco_rate > pll_vco_max)
		vco_rate = pll_vco_max;

	factor_p = (vco_rate % 24000000) ? 1 : 0;
	factor_n = vco_rate * (factor_p + 1) / 24000000  - 1;

	reg_val = readl((uchar *)priv->base +  pll->gen_reg);
	reg_val &= ~0xFFFF;
	reg_val |= (factor_n << 8) | (factor_m << 4) | (factor_p << 0);
	/* If SDM enable, set PLL_ICP = 0 */
	if (pll->sub_type == AIC_PLL_SDM)
		reg_val &= ~(0x1F << 24);
	writel(reg_val, (uchar *)priv->base +  pll->gen_reg);

	if (pll->sub_type == AIC_PLL_FRA) {
		val = rate % (24000000 * (factor_n + 1) /
			      (factor_m + 1) / (factor_p + 1));
		fra_en = val ? 1 : 0;
		if (fra_en) {
			fra_in = val * (factor_p + 1) *
				 (factor_m + 1) * 0x1FFFF;
			do_div(fra_in, 24000000);
		}
		/* Configure fractional division */
		writel(fra_en << 20 | fra_in, (uchar *)priv->base +  pll->frac_reg);
		/* when using decimal divsion, do not configure spreading parameters */
		sdm_en = (1UL << 31) | (2UL << 29);
		writel(sdm_en, (uchar *)priv->base +  pll->sdm_reg);
	}

#ifdef CONFIG_CLK_ARTINCHIP_PLL_SDM
	if (pll->sub_type == AIC_PLL_SDM) {
		ppm_max = 1000000 / (factor_n + 1);
		/* 1% spread */
		if (ppm_max < PLL_SDM_SPREAD_PPM)
			sdm_amp = 0;
		else
			sdm_amp = PLL_SDM_AMP_MAX -
				PLL_SDM_SPREAD_PPM * PLL_SDM_AMP_MAX / ppm_max;

		/* SDM uses triangular wave, 33KHz by default  */
		sdm_step = (u64)(PLL_SDM_AMP_MAX - sdm_amp) * 2 *
				PLL_SDM_SPREAD_FREQ / 24000000;
		if (sdm_step > 511)
			sdm_step = 511;

		reg_val = (1UL << 31) | (2 << PLL_SDM_MODE_BIT) |
			  (sdm_step << PLL_SDM_STEP_BIT) |
			  (3 << PLL_SDM_FREQ_BIT) |
			  (sdm_amp << PLL_SDM_AMP_BIT);

		writel(reg_val, (uchar *)priv->base +  pll->sdm_reg);
	}
#endif

	return 0;
}

static int auth_clk_write_request(struct aic_clk_priv *priv, struct aic_auth_clk *auth_clk)
{
	u32 val;

	val = (auth_clk->key_code << auth_clk->key_bit) | auth_clk->reg;
	writel(val, (uchar *)priv->base +  auth_clk->wr_auth_reg);

	udelay(100);

	val = readl((uchar *)priv->base +  auth_clk->wr_auth_reg);
	if (!(val & (1 << auth_clk->wr_auth_bit))) {
		log_err("Failed to request authorize clk: %x\n", auth_clk->reg);
		return -EIO;
	}

	return 0;
}

static int auth_clk_write(struct aic_clk_priv *priv, struct aic_auth_clk *auth_clk, u32 val)
{
	int ret;

	ret = auth_clk_write_request(priv, auth_clk);
	if (ret)
		return ret;

	writel(val, (uchar *)priv->base +  auth_clk->reg);

	return 0;
}

static int auth_clk_enable(struct clk *clk, int index)
{
	u32 val, i;
	struct aic_clk_priv *priv = dev_get_priv(clk->dev);
	struct aic_clk_tree *tree = priv->tree;
	struct aic_auth_clk *auth_clk = &tree->auth_clk[index];

	if (!auth_clk->table_gates) {
		log_err("auth_clk %x does not have valid table_gates\n", auth_clk->reg);
		return 0;
	}

	if (auth_clk->table_gates[0] < 0)
		return 0;

	val = readl((uchar *)priv->base +  auth_clk->reg);

	for (i = 0; i < auth_clk->num_gates; i++) {
		val |= (1 << auth_clk->table_gates[i]);
		auth_clk_write(priv, auth_clk, val);
	}

	return 0;
}

static int auth_clk_disable(struct clk *clk, int index)
{
	u32 val, i;
	struct aic_clk_priv *priv = dev_get_priv(clk->dev);
	struct aic_clk_tree *tree = priv->tree;
	struct aic_auth_clk *auth_clk = &tree->auth_clk[index];

	if (!auth_clk->table_gates) {
		log_err("auth_clk %x does not have valid table_gates\n", auth_clk->reg);
		return 0;
	}

	if (auth_clk->table_gates[0] < 0)
		return 0;

	val = readl((uchar *)priv->base +  auth_clk->reg);

	for (i = 0; i < auth_clk->num_gates; i++) {
		val &= ~(1 << auth_clk->table_gates[i]);
		auth_clk_write(priv, auth_clk, val);
	}

	return 0;
}

static void try_best_divider(u32 rate, u32 parent_rate, u32 max_div, u32 *div)
{
	u32 tmp, i, min_delta = U32_MAX, best_div = 0;

	for (i = 1; i <= max_div; i++) {
		tmp = i * rate;
		if (parent_rate == tmp) {
			best_div = i;
			goto __out;
		}

		if (abs(parent_rate - tmp) < min_delta) {
			min_delta = abs(parent_rate - tmp);
			best_div = i;
		}
	}

__out:
	*div = best_div;
}

static ulong auth_clk_set_rate(struct clk *clk, ulong rate, int index)
{
	u32 val, parent_index, parent_rate, div, div_max, div_mask;
	struct aic_clk_priv *priv = dev_get_priv(clk->dev);
	struct aic_clk_tree *tree = priv->tree;
	struct aic_auth_clk *auth_clk = &tree->auth_clk[index];

	if (!auth_clk->table_div) {
		log_err("auth_clk %x does not have valid table_div\n", auth_clk->reg);
		return -EINVAL;
	}

	val = readl((uchar *)priv->base +  auth_clk->reg);

	parent_index = (val >> auth_clk->mux_bit) & auth_clk->mux_mask;
	if (parent_index >= auth_clk->parent_cnt) {
		log_err("auth_clk %x get invalid parent index\n", auth_clk->reg);
		return -EINVAL;
	}

	if (auth_clk->parent_cnt != auth_clk->num_div) {
		log_err("auth_clk %x parent number is not equal to divider number!\n",
			auth_clk->reg);
		return -EINVAL;
	}

	if (auth_clk->table_div[parent_index].shift < 0)
		return 0;

	div_max = 1 << auth_clk->table_div[parent_index].wd.width;
	parent_rate = artinchip_get_parent_rate(clk, auth_clk->parent[parent_index]);
	try_best_divider(rate, parent_rate, div_max, &div);
	div_mask = div_max - 1;

	val &= ~(div_mask << auth_clk->table_div[parent_index].shift);
	val |= (div - 1) << auth_clk->table_div[parent_index].shift;
	auth_clk_write(priv, auth_clk, val);

	return 0;
}

static ulong auth_clk_get_rate(struct clk *clk, int index)
{
	u32 val, parent_index, parent_rate, rate, div, div_mask;
	struct aic_clk_priv *priv = dev_get_priv(clk->dev);
	struct aic_clk_tree *tree = priv->tree;
	struct aic_auth_clk *auth_clk = &tree->auth_clk[index];

	if (!auth_clk->table_div) {
		log_err("auth_clk %x does not have valid table_div\n", auth_clk->reg);
		return -EINVAL;
	}

	val = readl((uchar *)priv->base +  auth_clk->reg);

	parent_index = (val >> auth_clk->mux_bit) & auth_clk->mux_mask;
	if (parent_index >= auth_clk->parent_cnt) {
		log_err("auth_clk %x get invalid parent index\n", auth_clk->reg);
		return -EINVAL;
	}

	if (auth_clk->parent_cnt != auth_clk->num_div) {
		log_err("auth_clk %x parent number is not equal to divider number!\n",
			auth_clk->reg);
		return -EINVAL;
	}

	parent_rate = artinchip_get_parent_rate(clk, auth_clk->parent[parent_index]);

	if (auth_clk->table_div[parent_index].shift < 0) {
		rate = parent_rate / auth_clk->table_div[parent_index].wd.div;
	} else {
		div_mask = (1 << auth_clk->table_div[parent_index].wd.width) - 1;
		div = (val >> auth_clk->table_div[parent_index].shift) & div_mask;
		rate = parent_rate / (div + 1);
	}

	return rate;
}

static int auth_clk_set_parent(struct clk *clk, struct clk *parent, int index)
{
	int i;
	u32 val;
	struct aic_clk_priv *priv = dev_get_priv(clk->dev);
	struct aic_clk_tree *tree = priv->tree;
	struct aic_auth_clk *auth_clk = &tree->auth_clk[index];

	val = readl((void *)((long)(uchar *)priv->base +  auth_clk->reg));
	val &= ~(auth_clk->mux_mask << auth_clk->mux_bit);
	for (i = 0; i < auth_clk->parent_cnt; i++) {
		if (auth_clk->parent[i] == parent->id) {
			val |= (i << auth_clk->mux_bit);
			auth_clk_write(priv, auth_clk, val);
			return 0;
		}
	}

	return -EPERM;
}

static int fixed_parent_clk_enable(struct clk *clk, int index)
{
	u32 val, i;
	struct aic_clk_priv *priv = dev_get_priv(clk->dev);
	struct aic_clk_tree *tree = priv->tree;
	struct aic_fixed_parent_clk *fp_clk = &tree->fixed_parent_clk[index];

	if (!fp_clk->table_gates) {
		log_err("fp_clk %x does not have valid table_gates\n", fp_clk->reg);
		return -EINVAL;
	}

	if (fp_clk->table_gates[0] < 0)
		return 0;

	val = readl((uchar *)priv->base +  fp_clk->reg);

	for (i = 0; i < fp_clk->num_gates; i++) {
		val |= (1 << fp_clk->table_gates[i]);
		writel(val, (uchar *)priv->base +  fp_clk->reg);
	}

	return 0;
}

static int fixed_parent_clk_disable(struct clk *clk, int index)
{
	u32 val, i;
	struct aic_clk_priv *priv = dev_get_priv(clk->dev);
	struct aic_clk_tree *tree = priv->tree;
	struct aic_fixed_parent_clk *fp_clk = &tree->fixed_parent_clk[index];

	if (!fp_clk->table_gates) {
		log_err("fp_clk %x does not have valid table_gates\n", fp_clk->reg);
		return -EINVAL;
	}

	if (fp_clk->table_gates[0] < 0)
		return 0;

	val = readl((uchar *)priv->base +  fp_clk->reg);

	for (i = 0; i < fp_clk->num_gates; i++) {
		val &= ~(1 << fp_clk->table_gates[i]);
		writel(val, (uchar *)priv->base +  fp_clk->reg);
	}

	return 0;
}

static ulong fixed_parent_clk_get_rate(struct clk *clk, int index)
{
	u32 val, parent_rate, rate, div, div_mask;
	struct aic_clk_priv *priv = dev_get_priv(clk->dev);
	struct aic_clk_tree *tree = priv->tree;
	struct aic_fixed_parent_clk *fp_clk = &tree->fixed_parent_clk[index];

	parent_rate = artinchip_get_parent_rate(clk, fp_clk->parent);

	if (!fp_clk->table_div) {
		log_err("fp_clk %x does not have valid table_div\n", fp_clk->reg);
		return parent_rate;
	}

	if (fp_clk->table_div[0].shift < 0) {
		rate = parent_rate / fp_clk->table_div[0].wd.div;
	} else {
		val = readl((uchar *)priv->base +  fp_clk->reg);
		div_mask = (1 << fp_clk->table_div[0].wd.width) - 1;
		div = (val >> fp_clk->table_div[0].shift) & div_mask;
		rate = parent_rate / (div + 1);
	}

	return rate;
}

static ulong fixed_parent_clk_set_rate(struct clk *clk, ulong rate, int index)
{
	u32 val, parent_rate, div, div_mask, div_max;
	struct aic_clk_priv *priv = dev_get_priv(clk->dev);
	struct aic_clk_tree *tree = priv->tree;
	struct aic_fixed_parent_clk *fp_clk = &tree->fixed_parent_clk[index];

	parent_rate = artinchip_get_parent_rate(clk, fp_clk->parent);

	if (!fp_clk->table_div) {
		log_err("fp_clk %x does not have valid table_div\n", fp_clk->reg);
		return parent_rate;
	}

	if (fp_clk->table_div[0].shift < 0)
		return 0;

	div = DIV_ROUND_CLOSEST(parent_rate, rate);
	div_max = 1 << fp_clk->table_div[0].wd.width;
	if (div > div_max)
		div = div_max;

	val = readl((uchar *)priv->base +  fp_clk->reg);
	div_mask = div_max - 1;
	val &= ~(div_mask << fp_clk->table_div[0].shift);
	val |= ((div - 1) << fp_clk->table_div[0].shift);
	writel(val, (uchar *)priv->base +  fp_clk->reg);

	return 0;
}

static int multi_parent_clk_enable(struct clk *clk, int index)
{
	u32 val, i;
	struct aic_clk_priv *priv = dev_get_priv(clk->dev);
	struct aic_clk_tree *tree = priv->tree;
	struct aic_multi_parent_clk *mp_clk = &tree->multi_parent_clk[index];

	if (!mp_clk->table_gates) {
		log_err("mp_clk %x does not have valid table_gates\n", mp_clk->reg);
		return 0;
	}

	if (mp_clk->table_gates[0] < 0)
		return 0;

	val = readl((uchar *)priv->base +  mp_clk->reg);

	for (i = 0; i < mp_clk->num_gates; i++) {
		val |= (1 << mp_clk->table_gates[i]);
		writel(val, (uchar *)priv->base +  mp_clk->reg);
	}

	return 0;
}

static int multi_parent_clk_disable(struct clk *clk, int index)
{
	u32 val, i;
	struct aic_clk_priv *priv = dev_get_priv(clk->dev);
	struct aic_clk_tree *tree = priv->tree;
	struct aic_multi_parent_clk *mp_clk = &tree->multi_parent_clk[index];

	if (!mp_clk->table_gates) {
		log_err("mp_clk %x does not have valid table_gates\n", mp_clk->reg);
		return 0;
	}

	if (mp_clk->table_gates[0] < 0)
		return 0;

	val = readl((uchar *)priv->base +  mp_clk->reg);

	for (i = 0; i < mp_clk->num_gates; i++) {
		val &= ~(1 << mp_clk->table_gates[i]);
		writel(val, (uchar *)priv->base +  mp_clk->reg);
	}

	return 0;
}

static ulong multi_parent_clk_set_rate(struct clk *clk, ulong rate, int index)
{
	u32 val, parent_index, parent_rate, div, div_max, div_mask;
	struct aic_clk_priv *priv = dev_get_priv(clk->dev);
	struct aic_clk_tree *tree = priv->tree;
	struct aic_multi_parent_clk *mp_clk = &tree->multi_parent_clk[index];

	if (!mp_clk->table_div) {
		log_err("mp_clk1 %x does not have valid table_div\n", mp_clk->reg);
		return -EINVAL;
	}

	val = readl((uchar *)priv->base +  mp_clk->reg);

	parent_index = (val >> mp_clk->mux_bit) & mp_clk->mux_mask;
	if (parent_index >= mp_clk->parent_cnt) {
		log_err("mp_clk %x get invalid parent index\n", mp_clk->reg);
		return -EINVAL;
	}

	if (mp_clk->parent_cnt != mp_clk->num_div) {
		log_err("mp_clk %x parent number is not equal to divider number!\n", mp_clk->reg);
		return -EINVAL;
	}

	if (mp_clk->table_div[parent_index].shift < 0)
		return 0;

	div_max = 1 << mp_clk->table_div[parent_index].wd.width;
	parent_rate = artinchip_get_parent_rate(clk, mp_clk->parent[parent_index]);
	try_best_divider(rate, parent_rate, div_max, &div);
	div_mask = div_max - 1;

	val &= ~(div_mask << mp_clk->table_div[parent_index].shift);
	val |= (div - 1) << mp_clk->table_div[parent_index].shift;
	writel(val, (uchar *)priv->base +  mp_clk->reg);

	return 0;
}

static ulong multi_parent_clk_get_rate(struct clk *clk, int index)
{
	u32 val, parent_index, parent_rate, rate, div, div_mask;
	struct aic_clk_priv *priv = dev_get_priv(clk->dev);
	struct aic_clk_tree *tree = priv->tree;
	struct aic_multi_parent_clk *mp_clk = &tree->multi_parent_clk[index];

	if (!mp_clk->table_div) {
		log_err("mp_clk2 %x does not have valid table_div\n", mp_clk->reg);
		return -EINVAL;
	}

	val = readl((uchar *)priv->base +  mp_clk->reg);

	parent_index = (val >> mp_clk->mux_bit) & mp_clk->mux_mask;
	if (parent_index >= mp_clk->parent_cnt) {
		log_err("mp_clk %x get invalid parent index\n", mp_clk->reg);
		return -EINVAL;
	}

	if (mp_clk->parent_cnt != mp_clk->num_div) {
		log_err("mp_clk %x parent number is not equal to divider number!\n", mp_clk->reg);
		return -EINVAL;
	}

	parent_rate = artinchip_get_parent_rate(clk, mp_clk->parent[parent_index]);

	if (mp_clk->table_div[parent_index].shift < 0) {
		rate = parent_rate / mp_clk->table_div[parent_index].wd.div;
	} else {
		div_mask = (1 << mp_clk->table_div[parent_index].wd.width) - 1;
		div = (val >> mp_clk->table_div[parent_index].shift) & div_mask;
		rate = parent_rate / (div + 1);
	}

	return rate;
}

static int multi_parent_clk_set_parent(struct clk *clk, struct clk *parent, int index)
{
	int i;
	u32 val;
	struct aic_clk_priv *priv = dev_get_priv(clk->dev);
	struct aic_clk_tree *tree = priv->tree;
	struct aic_multi_parent_clk *mp_clk = &tree->multi_parent_clk[index];

	val = readl((void *)((long)(uchar *)priv->base +  mp_clk->reg));
	val &= ~(mp_clk->mux_mask << mp_clk->mux_bit);
	for (i = 0; i < mp_clk->parent_cnt; i++) {
		if (mp_clk->parent[i] == parent->id) {
			val |= (i << mp_clk->mux_bit);
			writel(val, (uchar *)priv->base +  mp_clk->reg);
			return 0;
		}
	}

	return -EPERM;
}

static struct aic_clk_ops aic_clk_type_ops[] = {
	/* ops handle for fixed rate clocks */
	{
		.get_rate = fixed_rate_clk_get_rate,
	},

	/* ops handle for pll clocks */
	{
		.enable = pll_clk_enable,
		.disable = pll_clk_disable,
		.get_rate = pll_clk_get_rate,
		.set_rate = pll_clk_set_rate,
		.round_rate = pll_clk_round_rate,
	},

	/* ops handle for fixed_parent_clk clocks */
	{
		.enable = fixed_parent_clk_enable,
		.disable = fixed_parent_clk_disable,
		.get_rate = fixed_parent_clk_get_rate,
		.set_rate = fixed_parent_clk_set_rate,
	},

	/* ops handle for multi_parent_clk clocks */
	{
		.enable = multi_parent_clk_enable,
		.disable = multi_parent_clk_disable,
		.set_rate = multi_parent_clk_set_rate,
		.get_rate = multi_parent_clk_get_rate,
		.set_parent = multi_parent_clk_set_parent,
	},

	/* ops handle for auth_clk clocks */
	{
		.enable = auth_clk_enable,
		.disable = auth_clk_disable,
		.set_rate = auth_clk_set_rate,
		.get_rate = auth_clk_get_rate,
		.set_parent = auth_clk_set_parent,
	},
};

static ulong artinchip_clk_get_rate(struct clk *clk)
{
	u32 index;
	enum aic_clk_type type;

	struct aic_clk_priv *priv = dev_get_priv(clk->dev);

	type = aic_get_clk_info(priv->tree, clk->id, &index);
	if (type != AIC_CLK_UNKNOWN && aic_clk_type_ops[type].get_rate)
		return aic_clk_type_ops[type].get_rate(clk, index);

	return 0;
}

static ulong artinchip_clk_set_rate(struct clk *clk, ulong rate)
{
	u32 index;
	enum aic_clk_type type;
	struct aic_clk_priv *priv = dev_get_priv(clk->dev);

	type = aic_get_clk_info(priv->tree, clk->id, &index);
	if (type != AIC_CLK_UNKNOWN && aic_clk_type_ops[type].set_rate)
		return aic_clk_type_ops[type].set_rate(clk, rate, index);

	return 0;
}

static int artinchip_clk_set_parent(struct clk *clk, struct clk *parent)
{
	u32 index;
	enum aic_clk_type type;

	struct aic_clk_priv *priv = dev_get_priv(clk->dev);

	type = aic_get_clk_info(priv->tree, clk->id, &index);
	if (type != AIC_CLK_UNKNOWN && aic_clk_type_ops[type].set_parent)
		return aic_clk_type_ops[type].set_parent(clk, parent, index);

	return 0;
}

static int artinchip_clk_enable(struct clk *clk)
{
	u32 index;
	enum aic_clk_type type;

	struct aic_clk_priv *priv = dev_get_priv(clk->dev);

	type = aic_get_clk_info(priv->tree, clk->id, &index);
	if (type != AIC_CLK_UNKNOWN && aic_clk_type_ops[type].enable)
		return aic_clk_type_ops[type].enable(clk, index);

	return 0;
}

static int artinchip_clk_disable(struct clk *clk)
{
	u32 index;
	enum aic_clk_type type;
	struct aic_clk_priv *priv = dev_get_priv(clk->dev);

	type = aic_get_clk_info(priv->tree, clk->id, &index);
	if (type != AIC_CLK_UNKNOWN && aic_clk_type_ops[type].disable)
		return aic_clk_type_ops[type].disable(clk, index);

	return 0;
}

static ulong artinchip_clk_round_rate(struct clk *clk, ulong rate)
{
	u32 index;
	enum aic_clk_type type;
	struct aic_clk_priv *priv = dev_get_priv(clk->dev);

	type = aic_get_clk_info(priv->tree, clk->id, &index);
	if (type != AIC_CLK_UNKNOWN && aic_clk_type_ops[type].round_rate)
		return aic_clk_type_ops[type].round_rate(clk, rate, index);

	return 0;
}

const struct clk_ops artinchip_clk_ops = {
	.get_rate   = artinchip_clk_get_rate,
	.set_rate   = artinchip_clk_set_rate,
	.set_parent = artinchip_clk_set_parent,
	.enable     = artinchip_clk_enable,
	.disable    = artinchip_clk_disable,
	.round_rate = artinchip_clk_round_rate,
};

int aic_clk_common_init(struct udevice *dev, struct aic_clk_tree *tree)
{
	struct aic_clk_priv *priv = dev_get_priv(dev);

	priv->base = dev_read_addr_ptr(dev);
	if (!priv->base)
		return -ENOENT;

	priv->tree = tree;

	return 0;
}
