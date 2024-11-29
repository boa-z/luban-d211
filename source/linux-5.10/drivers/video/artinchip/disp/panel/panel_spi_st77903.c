// SPDX-License-Identifier: GPL-2.0-only
/*
 * Driver for SPI ST77903 panel.
 *
 * Copyright (C) 2022-2023 ArtInChip Technology Co., Ltd.
 * Authors:  Huahui Mai <huahui.mai@artinchip.com>
 */

#include <linux/gpio/consumer.h>
#include <linux/module.h>
#include <linux/component.h>
#include <linux/platform_device.h>
#include <video/display_timing.h>
#include <dt-bindings/display/artinchip,aic-disp.h>

#include "panel_com.h"

#define PANEL_DEV_NAME		"panel_spi_ST77903"

/* Init sequence, each line consists of count, command, data... */
static u8  init_sequence[] = {
	2,	0xf0,	0xc3,
	2,	0xf0,	0x96,
	2,	0xf0,	0xa5,
	2,	0Xe9,	0x20,
	5,	0xe7,	0x80,	0x77,	0x1f,	0xcc,
	5,	0xc1,	0x00,	0x0a,	0xac,	0x11,
	5,	0xc2,	0x00,	0x0a,	0xac,	0x11,
	5,	0xc3,	0x66,	0x04,	0x77,	0x01,
	5,	0xc4,	0x66,	0x04,	0x77,	0x01,
	2,	0xc5,	0x40,
	6,	0xc8,	0x3e,	0x08,	0x08,	0x08,	0xa5,
	15,	0xe0,	0xf3,	0x0f,	0x12,	0x05,	0x07,	0x03,	0x32,
		0x44,	0x4a,	0x09,	0x14,	0x14,	0x2e,	0x33,
	15,	0xe1,	0xf3,	0x0f,	0x13,	0x05,	0x07,	0x03,	0x32,
		0x33,	0x4a,	0x08,	0x14,	0x14,	0x2e,	0x33,
	15,	0xe5,	0x9a,	0xf5,	0x95,	0x34,	0x22,	0x25,	0x10,
		0x25,	0x25,	0x25,	0x25,	0x25,	0x25,	0x25,
	15,	0xe6,	0x9a,	0xf5,	0x95,	0x57,	0x22,	0x25,	0x10,
		0x22,	0x22,	0x22,	0x22,	0x22,	0x22,	0x22,
	3,	0xec,	0x40,	0x07,
	2,	0x36,	0x04,
	2,	0xb2,	0x10,
	2,	0xb3,	0x01,
	2,	0xb4,	0x00,
	5,	0xb5,	0x00,	0x04,	0x00,	0x04,
	10,	0xa5,	0x00,	0x01,	0x40,	0x00,	0x00,	0x17,	0x2a,
		0x0a,	0x02,
	10,	0xa6,	0xa0,	0x12,	0x40,	0x01,	0x00,	0x11,	0x2a,
		0x0a,	0x02,
	8,	0xba,	0x59,	0x02,	0x03,	0x00,	0x22,	0x02,	0x00,
	9,	0xbb,	0x00,	0x35,	0x00,	0x35,	0x88,	0x8b,	0x0b,
		0x00,
	9,	0xbc,	0x00,	0x35,	0x00,	0x35,	0x88,	0x8b,	0x0b,
		0x00,
	12,	0xbd,	0x44,	0xff,	0xff,	0xff,	0x15,	0x51,	0xff,
		0xff,	0x87,	0xff,	0x02,
	2,	0xf9,	0x3c,
	3,	0xb6,	0x9f,	0x1d,
	3,	0xd5,	0x00,	0x08,
	2,	0x3a,	0x07,
	2,	0x35,	0x00,
	1,	0x20,
	1,	0x11,
	0,	120,
	1,	0x29,
};

static int ST77903_init(struct aic_panel *panel)
{
	u8 num, code;
	u8 *p;

	for (p = init_sequence; p < init_sequence + sizeof(init_sequence);) {
		num = *p++;
		code = *p++;

		if (num == 0) {
			aic_delay_ms(code);
		} else if (num == 1) {
			panel->callbacks.di_send_cmd(code, NULL, 0);
		} else {
			aic_delay_us(5);
			panel->callbacks.di_send_cmd(code, p, num - 1);
			p += num - 1;
		}
	}
	return 0;
}

int panel_enable(struct aic_panel *panel)
{
	panel_di_enable(panel, 0);
	if (unlikely(!panel->callbacks.di_send_cmd))
		panic("Have no di_send_cmd() API for SPI.\n");

	ST77903_init(panel);
	dev_info(panel->dev, "ST77903 has been Loaded\n");

	panel_de_timing_enable(panel, 0);
	panel_backlight_enable(panel, 0);

	return 0;
}

static struct aic_panel_funcs panel_funcs = {
	.disable = panel_default_disable,
	.unprepare = panel_default_unprepare,
	.prepare = panel_default_prepare,
	.enable = panel_enable,
	.get_video_mode = panel_default_get_video_mode,
	.register_callback = panel_register_callback,
};

/* Init the videomode parameter, dts will override the initial value. */
static struct videomode panel_vm = {
	.pixelclock = 6000000,
	.hactive = 240,
	.hfront_porch = 20,
	.hback_porch = 20,
	.hsync_len = 20,
	.vactive = 321,
	.vfront_porch = 20,
	.vback_porch = 20,
	.vsync_len = 10,
	.flags = DISPLAY_FLAGS_HSYNC_LOW | DISPLAY_FLAGS_VSYNC_LOW |
		DISPLAY_FLAGS_DE_HIGH | DISPLAY_FLAGS_PIXDATA_POSEDGE
};

static struct spi_cfg spi = {
	.qspi_mode = 1,
	.vbp_num = 20,
	.code1_cfg = 0xD8,
	.code = {0xDE, 0x00, 0x00},
};

static int panel_spi_parse_dt(struct device *dev, struct panel_comp *p)
{
	struct panel_rgb *rgb;

	rgb = devm_kzalloc(dev, sizeof(*rgb), GFP_KERNEL);
	if (!rgb)
		return -ENOMEM;

	memset(rgb, 0, sizeof(*rgb));

	if (panel_parse_dts(p, dev) < 0)
		return -1;

	rgb->mode = SPI;
	rgb->format = SPI_4SDA_RGB666;

	rgb->fbtft_par.first_line = 0x61;
	rgb->fbtft_par.other_line = 0x60;
	rgb->fbtft_par.spi = &spi;

	p->panel.rgb = rgb;
	return 0;
}

static int panel_bind(struct device *dev,
			struct device *master, void *data)
{
	struct panel_comp *p;
	int ret;

	p = devm_kzalloc(dev, sizeof(*p), GFP_KERNEL);
	if (!p)
		return -ENOMEM;

	ret = panel_spi_parse_dt(dev, p);
	if (ret < 0)
		return ret;

	panel_init(p, dev, &panel_vm, &panel_funcs, NULL);

	dev_set_drvdata(dev, p);
	return 0;
}

static const struct component_ops panel_com_ops = {
	.bind	= panel_bind,
	.unbind	= panel_default_unbind,
};

static int panel_probe(struct platform_device *pdev)
{
	dev_info(&pdev->dev, "%s()\n", __func__);
	return component_add(&pdev->dev, &panel_com_ops);
}

static int panel_remove(struct platform_device *pdev)
{
	component_del(&pdev->dev, &panel_com_ops);
	return 0;
}

static const struct of_device_id panal_of_table[] = {
	{.compatible = "artinchip,aic-general-rgb-panel"},
	{ /* sentinel */}
};
MODULE_DEVICE_TABLE(of, panal_of_table);

static struct platform_driver panel_driver = {
	.probe = panel_probe,
	.remove = panel_remove,
	.driver = {
		.name = PANEL_DEV_NAME,
		.of_match_table = panal_of_table,
	},
};

module_platform_driver(panel_driver);

MODULE_AUTHOR("Huahui Mai <huahui.mai@artinchip.com>");
MODULE_DESCRIPTION("AIC-" PANEL_DEV_NAME);
MODULE_LICENSE("GPL");
