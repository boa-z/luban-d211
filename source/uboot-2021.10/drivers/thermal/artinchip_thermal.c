// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2025 ArtInChip Technology Co.,Ltd
 * Author: Matteo <duanmt@artinchip.com>
 */

#include <config.h>
#include <common.h>
#include <asm/io.h>
#include <dm.h>
#include <dm/device_compat.h>
#include <clk.h>
#include <reset.h>
#include <errno.h>
#include <thermal.h>

#define AIC_TSEN_NAME		"aic-thermal"
#define AIC_TSEN_MAX_CH		2

/* Register definition of Thermal Sensor Controller */
#define TSEN_MCR	0x000
#define TSEN_INTR	0x004
#define TSENn_CFG(n)	(0x100 + (((n) & 0x3) << 5))
#define TSENn_ITV(n)	(0x100 + (((n) & 0x3) << 5) + 0x4)
#define TSENn_FIL(n)	(0x100 + (((n) & 0x3) << 5) + 0x8)
#define TSENn_DATA(n)	(0x100 + (((n) & 0x3) << 5) + 0xC)
#define TSENn_INT(n)	(0x100 + (((n) & 0x3) << 5) + 0x10)
#define TSEN_VERSION	0xFFC

#define TSEN_MCR_CH0_EN			BIT(16)
#define TSEN_MCR_CH_EN(n)		(TSEN_MCR_CH0_EN << (n))
#define TSEN_MCR_ACQ_SHIFT		8
#define TSEN_MCR_ACQ_MASK		GENMASK(15, 8)
#define TSEN_MCR_MAX_SHIFT		4
#define TSEN_MCR_MAX_MASK		GENMASK(5, 4)
#define TSEN_MCR_EN			BIT(0)

#define TSEN_INTR_CH_INT_FLAG(n)	(BIT(16) << (n))
#define TSEN_INTR_CH_INT_EN(n)		(BIT(0) << (n))

#define TSENn_CFG_ADC_ACQ_VAL		0xff
#define TSENn_CFG_ADC_ACQ_SHIFT		8
#define TSENn_CFG_ADC_ACQ_MASK		GENMASK(15, 8)
#define TSENn_CFG_HIGH_ADC_PRIORITY	BIT(4)
#define TSENn_CFG_PERIOD_SAMPLE_EN	BIT(1)
#define TSENn_CFG_SINGLE_SAMPLE_EN	BIT(0)

#define TSENn_ITV_SHIFT			16

#define TSENn_FIL_2_POINTS		1
#define TSENn_FIL_4_POINTS		2
#define TSENn_FIL_8_POINTS		3

#define TSENn_INT_DAT_OVW_FLAG		BIT(17)
#define TSENn_INT_DAT_RDY_FLAG		BIT(16)
#define TSENn_INT_DAT_OVW_IE		BIT(1)
#define TSENn_INT_DATA_RDY_IE		BIT(0)

#define TSEN_NVMEM_CELL_NUM			8

#define TSEN_THS0_ADC_VAL_LOW			0
#define TSEN_THS0_ADC_VAL_LOW_MASK		BIT(12)
#define TSEN_THS1_ADC_VAL_LOW			1
#define TSEN_THS1_ADC_VAL_LOW_MASK		BIT(12)
#define TSEN_THS0_ADC_VAL_HIGH			2
#define TSEN_THS0_ADC_VAL_HIGH_MASK		BIT(12)
#define TSEN_THS1_ADC_VAL_HIGH			3
#define TSEN_THS1_ADC_VAL_HIGH_MASK		BIT(12)
#define TSEN_THS_ENV_TEMP_LOW			4
#define TSEN_THS_ENV_TEMP_LOW_MASK		BIT(4)
#define TSEN_THS_ENV_TEMP_HIGH			5
#define TSEN_THS_ENV_TEMP_HIGH_MASK		BIT(8)
#define TSEN_LDO30_BG_CTRL			6
#define TSEN_LDO30_BG_CTRL_MASK			GENMASK(7, 0)
#define TSEN_CP_VERSION				7
#define TSEN_CP_VERSION_MASK			BIT(6)
#define TSEN_ENV_TEMP_LOW_SIGN_MASK		BIT(3)
#define TSEN_ENV_TEMP_HIGH_SIGN_MASK		BIT(7)
#define TSEN_ENV_TEMP_LOW_BASE			25
#define TSEN_ENV_TEMP_HIGH_BASE			65

#define TSEN_VOLTAGE_SCALE_UNIT			2.14285
#define TSEN_TRIM_VOLTAGE_BOUNDARY_VAL		0x80
#define TSEN_ORIGIN_STANDARD_VOLTAGE		3000 // = 3 * 1000

#define TSEN_CP_VERSION_DIFF_TYPE		0xA
#define TSEN_SINGLE_POINT_CALI_K_CPU		-1132
#define TSEN_SINGLE_POINT_CALI_K_GPAI		-1159

#define TSEN_CPU_ZONE_TRIPS_NUM			1
#define TSEN_GPAI_ZONE_TRIPS_NUM		0

#define THERMAL_CORE_TEMP_AMPN_SCALE		1000
#define TSEN_CALIB_ACCURACY_SCALE		10 // = 10000 / 1000

#define thermal_get_pdata(d)	((struct aic_tsen_plat_data *)dev_get_driver_data(d))

enum aic_tsen_mode {
	AIC_TSEN_MODE_SINGLE = 0,
	AIC_TSEN_MODE_PERIOD = 1
};

struct aic_tsen_ch_dat {
	char name[16];
	int slope;	// 10000 * actual slope
	int offset;	// 10000 * actual offset
};

/* tsen compile-time platform data */
struct aic_tsen_plat_data {
	const u32		num;
	struct aic_tsen_ch_dat	ch[AIC_TSEN_MAX_CH];
};

struct aic_thermal_ch {
	u32 id;
	bool available;
	enum aic_tsen_mode mode;
	u32 latest_data; // 1000 * actual temperature value
};

struct aic_thermal_dev {
	struct udevice *dev;
	void __iomem *regs;
	struct clk clk;
	struct reset_ctl rst;
	u32 pclk_rate;

	struct aic_thermal_ch chan;
	u32 cell_data[TSEN_NVMEM_CELL_NUM];
};

/* Temperature = ADC data * slope + offset.
 * 1. Temperature accuracy adopts 1000.
 * 2. Slope and Offset accuracy adopts 10000.
 * Because the difference between the slope value of the CPU position and the
 * gpai position lies in the fourth digit.
 */
static s32 tsen_data2temp(u32 ch, u16 data, struct aic_tsen_ch_dat *dat)
{
	int temp;

	if (data == 4095 || data == 0)
		return 0;

	temp = dat->slope * data;
	temp += dat->offset;
	if ((temp % TSEN_CALIB_ACCURACY_SCALE) < TSEN_CALIB_ACCURACY_SCALE / 2)
		temp = temp / TSEN_CALIB_ACCURACY_SCALE;
	else
		temp = temp / TSEN_CALIB_ACCURACY_SCALE + 1;
	pr_debug("%s() ch%d temp: %d -> %d.%03d\n", __func__, ch, data,
		 temp / THERMAL_CORE_TEMP_AMPN_SCALE,
		 temp % THERMAL_CORE_TEMP_AMPN_SCALE);
	return temp;
}

static void tsen_enable(void __iomem *regs, bool enable)
{
	int mcr;

	mcr = readl(regs + TSEN_MCR);
	if (enable)
		mcr |= TSEN_MCR_EN;
	else
		mcr &= ~TSEN_MCR_EN;

	mcr |= TSENn_CFG_ADC_ACQ_MASK;
	writel(mcr, regs + TSEN_MCR);
}

static void tsen_ch_enable(void __iomem *regs, u32 ch, bool enable)
{
	int mcr = readl(regs + TSEN_MCR);

	if (enable)
		mcr |= TSEN_MCR_CH_EN(ch);
	else
		mcr &= ~TSEN_MCR_CH_EN(ch);

	writel(mcr, regs + TSEN_MCR);
}

static void tsen_single_mode(void __iomem *regs, u32 ch)
{
	u32 val;

	writel(TSENn_FIL_8_POINTS, regs + TSENn_FIL(ch));

	val = readl(regs + TSENn_CFG(ch));
	val &= ~TSENn_CFG_ADC_ACQ_MASK;
	val |= TSENn_CFG_ADC_ACQ_VAL << TSENn_CFG_ADC_ACQ_SHIFT;
	writel(val, regs + TSENn_CFG(ch));

	writel(TSENn_CFG_SINGLE_SAMPLE_EN | readl(regs + TSENn_CFG(ch)),
	       regs + TSENn_CFG(ch));
}

int aic_thermal_get_temp(struct udevice *dev, int *temp)
{
	struct aic_tsen_plat_data *pdata = thermal_get_pdata(dev);
	struct aic_thermal_dev *thermal = dev_get_plat(dev);
	struct aic_thermal_ch *chan = NULL;
	int status, timeout = 0x10000;
	u32 ch = 0;

	if (!thermal || !thermal->chan.available)
		return -1;

	chan = &thermal->chan;
	ch = chan->id;
	if (ch >= AIC_TSEN_MAX_CH) {
		printf("Invalid thermal channel No: %d\n", ch);
		return -1;
	}
	tsen_single_mode(thermal->regs, ch);
	tsen_ch_enable(thermal->regs, ch, true);

	while (timeout--) {
		status = readl(thermal->regs + TSENn_INT(ch));
		if (!(status & TSENn_INT_DAT_RDY_FLAG))
			continue;

		chan->latest_data = readl(thermal->regs + TSENn_DATA(ch));
		*temp = tsen_data2temp(ch, chan->latest_data, &pdata->ch[ch]);
		break;
	}

	tsen_ch_enable(thermal->regs, ch, false);
	if (!timeout)
		return -1;
	else
		return 0;
}

static int aic_thermal_of_to_plat(struct udevice *dev)
{
	struct aic_tsen_plat_data *pdata = thermal_get_pdata(dev);
	struct aic_thermal_dev *thermal = dev_get_plat(dev);
	struct aic_thermal_ch *chan = &thermal->chan;
	const void *fdt = gd->fdt_blob;
	int node, subnode, ch = 0;

#if !defined(CONFIG_SPL_BUILD) || defined(CONFIG_SPL_CLK_ARTINCHIP)
	struct clk pclk;
	int ret = 0;

	ret = clk_get_by_name(dev, "tsen", &thermal->clk);
	if (ret < 0) {
		printf("Failed to get thermal clock\n");
		return ret;
	}
	ret = clk_get_by_name(dev, "pclk", &pclk);
	if (ret < 0) {
		printf("Failed to get thermal pclk\n");
		return ret;
	}
	thermal->pclk_rate = clk_get_rate(&pclk);

	ret = reset_get_by_index(dev, 0, &thermal->rst);
	if (ret && ret != -ENOENT) {
		printf("Failed to get thermal reset\n");
		return ret;
	}
#endif
	thermal->regs = (void *)devfdt_get_addr(dev);

	node = dev_of_offset(dev);
	fdt_for_each_subnode(subnode, fdt, node) {
		if (!fdtdec_get_is_enabled(fdt, subnode)) {
			ch++;
			continue;
		}

		if (ch >= pdata->num)
			break;

		chan->id = ch++;
		chan->available = true;

		break; // Only need one single channel in SPL
	}

	if (!chan->available)
		return -1;
	else
		return 0;
}

extern struct udevice *get_efuse_device(void);
extern int efuse_read(struct udevice *dev, int offset, void *buf, int size);

static int aic_thermal_parse_efuse_cell(struct udevice *th_dev,
					struct udevice *efuse_dev, int subnode)
{
	char *cell_name[TSEN_NVMEM_CELL_NUM] = {"t01", "t11", "t02", "t12",
						"envtemp1", "envtemp2",
						"ldobg", "cpversion"};
	struct aic_thermal_dev *thermal = dev_get_plat(th_dev);
	const void *fdt = gd->fdt_blob;
	int ret, i, val;
	fdt_addr_t addr;
	fdt_size_t size;
	const char *name = fdt_get_name(fdt, subnode, NULL);

	for (i = 0; i < ARRAY_SIZE(cell_name); i++) {
		if (strncmp(name, cell_name[i], strlen(cell_name[i])))
			continue;

		/* Found a desired cell, so parse it */

		addr = fdtdec_get_addr(fdt, subnode, "reg");
		if (addr == FDT_ADDR_T_NONE)
			return 0;

		size = addr & 0xff;
		if (!size || size > 4) {
			dev_warn(th_dev, "Invalid length %ld for cell %s\n",
				 (long)size, cell_name[i]);
			return 0;
		}
		addr >>= 32;

		ret = efuse_read(efuse_dev, addr, &val, size);
		if (ret != size) {
			dev_warn(th_dev, "Failed to read cell %s\n", cell_name[i]);
			return -1;
		}

		if ((i != TSEN_THS_ENV_TEMP_LOW) &&
		    (i != TSEN_THS_ENV_TEMP_HIGH) && (val == 0)) {
			dev_info(th_dev, "%s is empty in eFuse\n", name);
			return 0;
		}

		addr = fdtdec_get_addr(fdt, subnode, "bits");
		if (addr == FDT_ADDR_T_NONE) {
			dev_warn(th_dev, "Failed to parse bits of %s\n", name);
			return -1;
		}
		size = addr & 0xff;
		addr >>= 32;
		thermal->cell_data[i] = (val >> addr) & GENMASK(size - 1, 0);
		break;
	}

	return 0;
}

static int aic_thermal_read_efuse(struct udevice *dev)
{
	struct udevice *efuse_dev = NULL;
	const void *fdt = gd->fdt_blob;
	int subnode, node;

	efuse_dev = get_efuse_device();
	if (!efuse_dev) {
		printf("Failed to get eFuse device\n");
		return -1;
	}

	node = dev_of_offset(efuse_dev);
	fdt_for_each_subnode(subnode, fdt, node) {
		if (aic_thermal_parse_efuse_cell(dev, efuse_dev, subnode))
			return -1;
	}

	return 0;
}

/* The temperature obtained from eFuse contains sign bits.
 * For this purpose, this function converts data through sign bit mask
 */
static int aic_tsen_env_temp_cali(u8 sign_mask, u8 val)
{
	if (val & sign_mask)
		return -(val & (sign_mask - 1));
	else
		return val & (sign_mask - 1);
}

int aic_tsen_single_point_cali1(struct aic_thermal_dev *tsen)
{
	int origin_vol = 0;
	int origin_adc = 0;
	int cali_scale = 0;
	u32 *cell = tsen->cell_data;
	u32 ldo30_bg_ctrl = 0;
	int env_temp_low = TSEN_ENV_TEMP_LOW_BASE;
	int standard_vol = TSEN_ORIGIN_STANDARD_VOLTAGE;
	int vol_scale_unit = TSEN_VOLTAGE_SCALE_UNIT;
	struct aic_tsen_plat_data *pdata = thermal_get_pdata(tsen->dev);
	struct aic_tsen_ch_dat *dat = &pdata->ch[tsen->chan.id];
	s32 slopes[AIC_TSEN_MAX_CH] = {TSEN_SINGLE_POINT_CALI_K_CPU,
				       TSEN_SINGLE_POINT_CALI_K_GPAI};

	env_temp_low += aic_tsen_env_temp_cali(TSEN_ENV_TEMP_LOW_SIGN_MASK,
					       cell[TSEN_THS_ENV_TEMP_LOW]);
	cali_scale = THERMAL_CORE_TEMP_AMPN_SCALE * TSEN_CALIB_ACCURACY_SCALE;

	ldo30_bg_ctrl = cell[TSEN_LDO30_BG_CTRL] & TSEN_LDO30_BG_CTRL_MASK;
	if (ldo30_bg_ctrl > TSEN_TRIM_VOLTAGE_BOUNDARY_VAL)
		origin_vol = standard_vol - (255 - ldo30_bg_ctrl) * vol_scale_unit;
	else
		origin_vol = standard_vol + ldo30_bg_ctrl * vol_scale_unit;

	origin_adc = origin_vol * cell[TSEN_THS0_ADC_VAL_LOW + tsen->chan.id] / standard_vol;
	dat->offset = env_temp_low * cali_scale - dat->slope * origin_adc;
	dat->slope = slopes[tsen->chan.id];

	return 0;
}

int aic_tsen_single_point_cali2(struct aic_thermal_dev *tsen)
{
	int cali_scale = 0;
	u32 *cell = tsen->cell_data;
	int env_temp_low = TSEN_ENV_TEMP_LOW_BASE;
	struct aic_tsen_plat_data *pdata = thermal_get_pdata(tsen->dev);
	struct aic_tsen_ch_dat *dat = &pdata->ch[tsen->chan.id];
	s32 slopes[AIC_TSEN_MAX_CH] = {TSEN_SINGLE_POINT_CALI_K_CPU,
				       TSEN_SINGLE_POINT_CALI_K_GPAI};

	env_temp_low += aic_tsen_env_temp_cali(TSEN_ENV_TEMP_LOW_SIGN_MASK,
					       cell[TSEN_THS_ENV_TEMP_LOW]);
	cali_scale = THERMAL_CORE_TEMP_AMPN_SCALE * TSEN_CALIB_ACCURACY_SCALE;

	dat->offset = env_temp_low * cali_scale -
			dat->slope * cell[TSEN_THS0_ADC_VAL_LOW + tsen->chan.id];
	dat->slope = slopes[tsen->chan.id];
	return 0;
}

static void aic_tsen_curve_fitting(struct aic_thermal_dev *thermal)
{
	int cp_version = thermal->cell_data[TSEN_CP_VERSION];

	pr_debug("CP version: %d\n", cp_version);
	if (cp_version == 0)
		return;
	else if (cp_version < TSEN_CP_VERSION_DIFF_TYPE)
		aic_tsen_single_point_cali1(thermal);
	else
		aic_tsen_single_point_cali2(thermal);
}

static int aic_thermal_probe(struct udevice *dev)
{
	struct aic_thermal_dev *thermal = dev_get_plat(dev);
	int ret = 0;

	if (!thermal->chan.available)
		return 0;

	ret = clk_enable(&thermal->clk);
	if (ret) {
		printf("Failed to enable thermal clock, return %d\n", ret);
		return ret;
	}
	ret = reset_deassert(&thermal->rst);
	if (ret < 0) {
		printf("Failed to deassert thermal reset\n");
		return ret;
	}

	thermal->dev = dev;
	aic_thermal_read_efuse(dev);
	aic_tsen_curve_fitting(thermal);

	tsen_enable(thermal->regs, true);
	return 0;
}

static int aic_thermal_remove(struct udevice *dev)
{
	struct aic_thermal_dev *thermal = dev_get_plat(dev);

	tsen_enable(thermal->regs, false);
	reset_assert(&thermal->rst);
	clk_disable(&thermal->clk);
	return 0;
}

static const struct dm_thermal_ops aic_thermal_ops = {
	.get_temp	= aic_thermal_get_temp,
};

static struct aic_tsen_plat_data aic_tsen_data_v10 = {2, {
	{AIC_TSEN_NAME "-cpu",  -1134, 2439001},
	{AIC_TSEN_NAME "-gpai", -1139, 2450566}}
};

static const struct udevice_id aic_thermal_ids[] = {
	{
		.compatible = "artinchip,aic-tsen-v1.0",
		.data = (ulong)&aic_tsen_data_v10,
	},
	{ }
};

U_BOOT_DRIVER(artinchip_thermal) = {
	.name	= AIC_TSEN_NAME,
	.id	= UCLASS_THERMAL,
	.ops	= &aic_thermal_ops,
	.of_match = aic_thermal_ids,
	.probe	= aic_thermal_probe,
	.remove = aic_thermal_remove,
	.of_to_plat = aic_thermal_of_to_plat,
	.plat_auto	= sizeof(struct aic_thermal_dev),
	.flags  = DM_FLAG_PRE_RELOC,
};
