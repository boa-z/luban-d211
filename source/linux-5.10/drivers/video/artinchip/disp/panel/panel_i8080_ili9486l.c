// SPDX-License-Identifier: GPL-2.0-only
/*
 * Driver for I8080 ILI9486L panel.
 *
 * Copyright (C) 2020-2022 ArtInChip Technology Co., Ltd.
 * Authors:  Huahui Mai <huahui.mai@artinchip.com>
 */

#include <linux/module.h>
#include <linux/component.h>
#include <linux/platform_device.h>
#include <video/display_timing.h>
#include <dt-bindings/display/artinchip,aic-disp.h>
#include "panel_com.h"

#define PANEL_DEV_NAME		"panel_i8080_ili9486l"

/* Init sequence, each line consists of count, command, data... */
static u8 init_sequence[] = {
	10,	0xf2,	0x18,	0xa3,	0x12,	0x02,	0xb2,	0x12,	0xff,
			0x10,	0x00,
	3,	0xf8,	0x21,	0x04,
	3,	0xf9,	0x00,	0x08,
	7,	0xf1,	0x36,	0x04,	0x00,	0x3c,	0x0f,	0x8f,
	3,	0xc0,	0x19,	0x1a,
	2,	0xc1,	0x44,
	2,	0xc2,	0x11,
	3,	0xc5,	0x00,	0x06,
	3,	0xb1,	0x80,	0x11,
	2,	0xb4,	0x02,
	2,	0xb0,	0x00,
	4,	0xb6,	0x00,	0x22,	0x3b,
	2,	0xb7,	0x07,
	16,	0xe0,	0x1f,	0x25,	0x22,	0x0a,	0x05,	0x07,	0x50,
			0xa8,	0x40,	0x0d,	0x19,	0x07,	0x00,	0x00,
			0x00,
	16,	0xe1,	0x1f,	0x3f,	0x3f,	0x08,	0x06,	0x02,	0x3f,
			0x75,	0x2e,	0x08,	0x0b,	0x05,	0x1d,	0x1a,
			0x00,
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
};

static int ILI9486L_init(struct aic_panel *panel)
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

	ILI9486L_init(panel);
	dev_info(panel->dev, "ILI9486L has been Loaed\n");

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
	.pixelclock = 9216000,
	.hactive = 320,
	.hfront_porch = 2,
	.hback_porch = 3,
	.hsync_len = 1,
	.vactive = 480,
	.vfront_porch = 3,
	.vback_porch = 1,
	.vsync_len = 2,
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
	rgb->format = I8080_RGB666_8BIT;

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
