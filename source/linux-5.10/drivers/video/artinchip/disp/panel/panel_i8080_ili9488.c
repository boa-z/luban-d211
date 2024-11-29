// SPDX-License-Identifier: GPL-2.0-only
/*
 * Driver for I8080 ILI9488 panel.
 *
 * Copyright (C) 2022-2023 ArtInChip Technology Co., Ltd.
 * Authors:  Huahui Mai <huahui.mai@artinchip.com>
 */

#include <linux/module.h>
#include <linux/component.h>
#include <linux/platform_device.h>
#include <video/display_timing.h>
#include <dt-bindings/display/artinchip,aic-disp.h>
#include "panel_com.h"

#define PANEL_DEV_NAME		"panel_i8080_ili9488"

/* Init sequence, each line consists of count, command, data... */
static u8 init_sequence[] = {
	5,	0xf7,	0xa9,	0x51,	0x2c,	0x82,
	5,	0xec,	0x00,	0x02,	0x03,	0x7a,
	3,	0xc0,	0x13,	0x13,
	2,	0xc1,	0x41,
	4,	0xc5,	0x00,	0x28,	0x80,
	3,	0xb1,	0xb0,	0x11,
	2,	0xb4,	0x02,
	3,	0xb6,	0x02,	0x22,
	2,	0xb7,	0xc6,
	3,	0xbe,	0x00,	0x04,
	2,	0xe9,	0x00,
	2,	0xb7,	0x07,
	4,	0xf4,	0x00,	0x00,	0x0f,
	16,	0xe0,	0x00,	0x04,	0x0e,	0x08,	0x17,	0x0a,	0x40,
		0x79,	0x4d,	0x07,	0x0e,	0x0a,	0x1a,	0x1d,	0x0f,
	16,	0xe1,	0x00,	0x1b,	0x1f,	0x02,	0x10,	0x05,	0x32,
		0x34,	0x43,	0x02,	0x0a,	0x09,	0x33,	0x37,	0x0f,
	4,	0xf4,	0x00,	0x00,	0x0f,
	2,	0x35,	0x00,
	3,	0x44,	0x00,	0x10,
	7,	0x33,	0x00,	0x00,	0x01,	0xe0,	0x00,	0x00,
	3,	0x37,	0x00,	0x00,
	5,	0x2a,	0x00,	0x00,	0x01,	0x3f,
	5,	0x2b,	0x00,	0x00,	0x01,	0xdf,
	2,	0x36,	0x08,
	2,	0x3a,	0x66,
	1,	0x11,
	0,	120,
	1,	0x29,
	0,	0x50,
};

static int ILI9488_init(struct aic_panel *panel)
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
		panic("Have no di_send_cmd() API for I8080.\n");

	ILI9488_init(panel);
	dev_info(panel->dev, "ILI9488 has been Loaed\n");

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
	.pixelclock = 8000000,
	.hactive = 320,
	.hfront_porch = 2,
	.hback_porch = 3,
	.hsync_len = 1,
	.vactive = 480,
	.vfront_porch = 3,
	.vback_porch = 2,
	.vsync_len = 1,
	.flags = DISPLAY_FLAGS_HSYNC_LOW | DISPLAY_FLAGS_VSYNC_LOW |
		DISPLAY_FLAGS_DE_HIGH | DISPLAY_FLAGS_PIXDATA_POSEDGE
};

static int panel_i8080_parse_dt(struct device *dev, struct panel_comp *p)
{
	struct panel_rgb *rgb;

	rgb = devm_kzalloc(dev, sizeof(*rgb), GFP_KERNEL);
	if (!rgb)
		return -ENOMEM;

	memset(rgb, 0, sizeof(*rgb));

	if (panel_parse_dts(p, dev) < 0)
		return -1;

	rgb->mode = I8080;
	rgb->format = I8080_RGB666_16BIT_3CYCLE;

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

	ret = panel_i8080_parse_dt(dev, p);
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
