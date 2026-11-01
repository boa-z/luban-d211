/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright (c) 2025 ArtInChip Inc.
 */
#ifndef __DRV_CLK_AIC_V2_H
#define __DRV_CLK_AIC_V2_H

#include <clk.h>

#define PLL_SDM_AMP_BIT		(0)
#define PLL_SDM_FREQ_BIT	(17)
#define PLL_SDM_STEP_BIT	(20)
#define PLL_SDM_MODE_BIT	(29)
#define PLL_SDM_EN_BIT		(31)

#define PLL_SDM_AMP_MAX		(0x20000)
#define PLL_SDM_SPREAD_PPM	(10000)
#define PLL_SDM_SPREAD_FREQ	(33000)

/* define types of pll */
enum aic_pll_type {
	AIC_PLL_INT,	/* integer pll */
	AIC_PLL_FRA,	/* fractional pll */
	AIC_PLL_SDM,	/* spread spectrum pll */
};

enum aic_fixed_parent_type {
	AIC_FPCLK_NORMAL,
	AIC_FPCLK_FIXED_FACTOR,
};

enum aic_clk_type {
	AIC_CLK_FIXED_RATE = 0,
	AIC_CLK_PLL = 1,
	AIC_CLK_FIXED_PARENT = 2,
	AIC_CLK_MULTI_PARENT = 3,
	AIC_CLK_AUTH = 4,
	AIC_CLK_UNKNOWN,
};

/*
 * This structure is designed for compatible with multiple dividers.
 * For example, CPU has three parent clocks: CLK_24M, PLL_INT0, PLL_INT1,
 * with corresponding frequency division coefficients of 1, (DIV + 1), 2, respectively.
 * If shift < 0, wd.div represents the frequency division.
 * If shift >= 0, wd.width represents the width of frequency division in register.
 */
struct table_div {
	s16 shift;
	union {
		u16 width;
		u16 div;
	} wd;
};

struct pll_vco {
	ulong vco_min;
	ulong vco_max;
	ulong id;
};

/**
 * struct aic_fixed_rate - A handle to (allowing control of) a fixed rate clock.
 *
 * Clocks such as HOSC, LOSC for ex., they just have a rate information.
 *
 * @id: The clock signal ID within the provider.
 * @rate: The clock rate (in HZ).
 */
struct aic_fixed_rate {
	u32 id;
	u32 rate;
	enum aic_clk_type type;
};

/**
 * struct clk_aic_pll - A handle to (allowing control of) a pll clock.
 *
 * @id: The clock signal ID within the provider.
 * @gen_reg: Register address for general configuration.
 * @frac_reg: Register address for fractional configuration.
 * @sdm_reg: Register address for spread configuration.
 * @type: Type of the pll, defined as enum aic_pll_type.
 */
struct aic_pll {
	u32 id;
	u32 gen_reg;
	u32 frac_reg;
	u32 sdm_reg;
	enum aic_pll_type sub_type;
	ulong min_rate;
	ulong max_rate;
};

/**
 * struct aic_multi_parent_clk - A handle to (allowing control of) a clock that has multi parents.
 *
 * @id: The clock signal ID within the provider.
 * @reg: Register address for clock configuration.
 * @table_gates: a pointer to the array of clock gates.
 * @num_gates: the number of clock gates
 * @parent: Parents' id array.
 * @parent_cnt: count of parents in the parent array;
 * @table_div: a pointer to the array of clock dividers.
 * @num_div: the number of clock dividers, it should be equal to @parent_cnt.
 * @mux_bit: bit shift for mux
 * @mux_mask: bits mask for getting mux;
 */
struct aic_multi_parent_clk {
	u32 id;
	u32 reg;
	s8 *table_gates;
	u8 num_gates;
	u32 *parent;
	u8 parent_cnt;
	struct table_div *table_div;
	u8 num_div;
	u8 mux_bit;
	u8 mux_mask;
};

/**
 * struct aic_periph_clk - A handle to (allowing control of) a module clock.
 *
 * The periphral modules' clock has a fixed parent, a bus gate, a module
 * gate and a dividor.
 *
 * @id: The clock signal ID within the provider.
 * @parent: Parent list, the count should be matched with width of @mux_mask.
 * @reg: Register address for clock configuration.
 * @table_gates: a pointer to the array of clock gates.
 * @num_gates: the number of clock gates
 * @table_div: a pointer to the array of clock dividers.
 * @num_div: the number of clock dividers
 */
struct aic_fixed_parent_clk {
	u32 id;
	u32 parent;
	u32 reg;
	s8 *table_gates;
	u8 num_gates;
	struct table_div *table_div;
	u8 num_div;
};

/**
 * struct aic_periph_clk - A handle to (allowing control of) a clock that need to
 * request write opetations
 *
 * @id: The clock signal ID within the provider.
 * @parent: Parent list, the count should be matched with width of @mux_mask.
 * @reg: Register address for clock configuration.
 * @wr_auth_reg: Register address for request write operation
 * @key_code: the key value must be written to wr_auth_reg
 * @key_bit: the offset of @key_code in @wr_auth_reg
 * @key_mask: the mask of @key_code
 * @wr_auth_bit: the offset of request status, must check @wr_auth_bit after request
 * @table_gates: a pointer to the array of clock gates.
 * @num_gates: the number of clock gates
 * @mux_bit: bit shift for mux
 * @mux_mask: bits mask for getting mux;
 * @table_div: a pointer to the array of clock dividers.
 * @num_div: the number of clock dividers
 */
struct aic_auth_clk {
	u8 id;
	u32 *parent;
	u8 parent_cnt;
	u32 reg;
	u32 wr_auth_reg;
	u32 key_code;
	u8 key_bit;
	u16 key_mask;
	u8 wr_auth_bit;
	s8 *table_gates;
	u8 num_gates;
	u8 mux_bit;
	u8 mux_mask;
	struct table_div *table_div;
	u8 num_div;
};

/**
 * struct aic_clk_tree - clock tree information.
 * @fixed_rate_cnt: the number of fixed_rate clock
 * @pll_cnt: the number of fixed_rate clock
 * @fixed_parent_cnt: the number of fixed_parent clock
 * @multi_parent_cnt: the number of multi_parent clock
 * @auth_cnt: the number of clocks that need to request write opetations
 * @fixed_rate: fixed rate clocks array
 * @plls: pll clocks array
 * @fixed_parent_clk: system clocks array
 * @multi_parent_clk: periph clocks array
 * @auth_clk: request write clocks array
 */
struct aic_clk_tree {
	u16 fixed_rate_cnt;
	u16 pll_cnt;
	u16 fixed_parent_cnt;
	u16 multi_parent_cnt;
	u16 auth_cnt;
	struct aic_fixed_rate *fixed_rate;
	struct aic_pll *plls;
	struct aic_fixed_parent_clk *fixed_parent_clk;
	struct aic_multi_parent_clk *multi_parent_clk;
	struct aic_auth_clk *auth_clk;
};

struct aic_clk_ops {
	int (*enable)(struct clk *clk, int index);
	int (*disable)(struct clk *clk, int index);
	ulong (*get_rate)(struct clk *clk, int index);
	ulong (*set_rate)(struct clk *clk, ulong rate, int index);
	int (*set_parent)(struct clk *clk, struct clk *parent, int index);
	ulong (*round_rate)(struct clk *clk, ulong rate, int index);
};

struct aic_clk_priv {
	void *base;
	struct aic_clk_tree *tree;
};

#define CLK_FIXED_RATE(_id, _rate)		{.id = _id, .rate = _rate}

#define CLK_PLL(_id, _gen_reg, _frac_reg, _sdm_reg, _sub_type)	\
	{							\
		.id = _id,					\
		.gen_reg = _gen_reg,				\
		.frac_reg = _frac_reg,				\
		.sdm_reg = _sdm_reg,				\
		.sub_type = _sub_type,				\
	}

#define CLK_PLL_VIDEO(_id, _gen_reg, _frac_reg, _sdm_reg, _sub_type, _min_rate, _max_rate)	\
	{											\
		.id = _id,									\
		.gen_reg = _gen_reg,								\
		.frac_reg = _frac_reg,								\
		.sdm_reg = _sdm_reg,								\
		.sub_type = _sub_type,								\
		.min_rate = _min_rate,								\
		.max_rate = _max_rate,								\
	}

#define FPCLK(_id, _parent, _reg, _table_gates, _table_div)	\
	{							\
		.id = _id,					\
		.parent = _parent,				\
		.reg = _reg,					\
		.table_gates = _table_gates,			\
		.num_gates = ARRAY_SIZE(_table_gates),		\
		.table_div = _table_div,			\
		.num_div = ARRAY_SIZE(_table_div),		\
	}

#define MPCLK(_id, _parent, _reg, _table_gates, _mux_shift, _mux_width, _table_div)	\
	{										\
		.id = _id,								\
		.reg = _reg,								\
		.parent = _parent,							\
		.parent_cnt = ARRAY_SIZE(_parent),					\
		.table_gates = _table_gates,						\
		.num_gates = ARRAY_SIZE(_table_gates),					\
		.mux_bit = _mux_shift,							\
		.mux_mask = BIT(_mux_width) - 1,					\
		.table_div = _table_div,						\
		.num_div = ARRAY_SIZE(_table_div),					\
	}

#define AUTHCLK(_id, _parent, _reg, _wr_auth_reg, _key_code, _key, _keyw, _wr_auth_bit,	\
				_table_gates, _mux, _muxw, _table_div)			\
{											\
	.id = _id,									\
	.parent = _parent,								\
	.parent_cnt = ARRAY_SIZE(_parent),						\
	.reg = _reg,									\
	.wr_auth_reg = _wr_auth_reg,							\
	.key_code = _key_code,								\
	.key_bit = _key,								\
	.key_mask = BIT(_keyw) - 1,							\
	.wr_auth_bit = _wr_auth_bit,							\
	.table_gates = _table_gates,							\
	.num_gates = ARRAY_SIZE(_table_gates),						\
	.mux_bit = _mux,								\
	.mux_mask = BIT(_muxw) - 1,							\
	.table_div = _table_div,							\
	.num_div = ARRAY_SIZE(_table_div),						\
}

int aic_clk_common_init(struct udevice *dev, struct aic_clk_tree *tree);
extern const struct clk_ops artinchip_clk_ops;

#endif	/* __DRV_CLK_AIC_V2__H */
