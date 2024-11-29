// SPDX-License-Identifier: GPL-2.0-only
/*
 * Driver for SPI ILI9341 panel.
 *
 * Copyright (C) 2020-2022 ArtInChip Technology Co., Ltd.
 * Authors:  Huahui Mai <huahui.mai@artinchip.com>
 */

#include <linux/gpio/consumer.h>
#include <linux/module.h>
#include <linux/component.h>
#include <linux/platform_device.h>
#include <video/display_timing.h>
#include <dt-bindings/display/artinchip,aic-disp.h>

#include "panel_com.h"

#define PANEL_DEV_NAME		"panel_spi_ili9341"

static u8  init_sequence[] = {
	4,	0xcf,	0x00,	0xc1,	0x30,
	5,	0xed,	0x64,	0x03,	0x12,	0x81,
	4,	0xe8,	0x85,	0x01,	0x78,
	6,	0xcb,	0x39,	0x2c,	0x00,	0x34,	0x02,
	2,	0xf7,	0x20,
	3,	0xea,	0x00,	0x00,
	2,	0xc0,	0x21,
	2,	0xc1,	0x12,
	3,	0xc5,	0x5a,	0x5d,
	2,	0xc7,	0x82,
	2,	0x36,	0x08,
	2,	0x3a,	0x66,
	3,	0xb1,	0x00,	0x16,
	3,	0xb6,   0x0a,	0xa2,
	2,	0xf2,	0x00,
	2,	0xf2,	0x00,
	3,	0xf6,	0x01,	0x30,
	2,	0x26,	0x01,
	16,	0xe0,	0x0f,	0x09,	0x1e,	0x07,	0x0b,	0x01,	0x45,
		0x6d,	0x37,	0x08,	0x13,	0x01,	0x06,	0x06,	0x00,
	16,	0xe1,	0x00,	0x01,	0x18,	0x00,	0x0d,	0x00,	0x2a,
		0x44,	0x44,	0x04,	0x11,	0x0c,	0x30,	0x34,	0x0f,
	3,	0x2a,	0x00,	0x00,
	5,	0x2a,	0x00,	0x00,	0x00,	0xef,
	5,	0x2b,	0x00,	0x00,	0x01,	0x3f,
	1,	0x11,
	0,	120,
	1,	0x29,
};

static int ILI9341_init(struct aic_panel *panel)
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
	struct gpio_desc *reset;

	reset = devm_gpiod_get_optional(panel->dev, "reset", GPIOD_OUT_HIGH);
	if (IS_ERR_OR_NULL(reset)) {
		dev_err(panel->dev, "failed to get reset gpio\n");
		return PTR_ERR(reset);
	}

	gpiod_set_value(reset, 1);
	mdelay(5);
	gpiod_set_value(reset, 0);
	mdelay(5);
	gpiod_set_value(reset, 1);
	mdelay(5);

	panel_di_enable(panel, 0);
	if (unlikely(!panel->callbacks.di_send_cmd))
		panic("Have no di_send_cmd() API for SPI.\n");

	ILI9341_init(panel);
	dev_info(panel->dev, "ILI9341 has been Loaded\n");

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
	.pixelclock = 3000000,
	.hactive = 240,
	.hfront_porch = 2,
	.hback_porch = 3,
	.hsync_len = 1,
	.vactive = 320,
	.vfront_porch = 3,
	.vback_porch = 1,
	.vsync_len = 2,
	.flags = DISPLAY_FLAGS_HSYNC_LOW | DISPLAY_FLAGS_VSYNC_LOW |
		DISPLAY_FLAGS_DE_HIGH | DISPLAY_FLAGS_PIXDATA_POSEDGE
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
	rgb->format = SPI_4LINE_RGB888;

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
