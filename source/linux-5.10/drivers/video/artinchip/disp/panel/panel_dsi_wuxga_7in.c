// SPDX-License-Identifier: GPL-2.0-only
/*
 * Driver for wuxga_7in DSI panel.
 *
 * Copyright (C) 2020-2022 ArtInChip Technology Co., Ltd.
 * Authors: huahui.mai <huahui.ami@artinchip.com>
 */

#include <linux/gpio/consumer.h>
#include <linux/module.h>
#include <linux/component.h>
#include <linux/platform_device.h>
#include <video/display_timing.h>

#include "../hw/dsi_reg.h"
#include "panel_dsi.h"

#define PANEL_DEV_NAME		"dsi_panel_wuxga_7in"

struct wuxga {
	struct gpio_desc *dcdc_en;
	struct gpio_desc *reset;
};

static inline struct wuxga *panel_to_wuxga(struct aic_panel *panel)
{
	return (struct wuxga *)panel->panel_private;
}

static u8 init_sequence[] = {
	2, DSI_DT_DCS_WR_P0,	DSI_DCS_SOFT_RESET,
	1, 5,
	3, DSI_DT_GEN_WR_P2,	0xb0, 0x00,
	7, DSI_DT_GEN_LONG_WR,	0xb3, 0x04, 0x08, 0x00, 0x22, 0x00,
	3, DSI_DT_GEN_LONG_WR,	0xb4, 0x0c,
	4, DSI_DT_GEN_LONG_WR,	0xb6, 0x3a, 0xd3,
	3, DSI_DT_DCS_WR_P1,	0x51, 0xE6,
	3, DSI_DT_DCS_WR_P1,	0x53, 0x2c,
	3, DSI_DT_DCS_WR_P1,	DSI_DCS_SET_PIXEL_FORMAT,   0x77,
	3, DSI_DT_DCS_WR_P1,	DSI_DCS_SET_ADDRESS_MODE,   0x00,
	6, DSI_DT_DCS_LONG_WR,	DSI_DCS_SET_COLUMN_ADDRESS, 0x00, 0x00, 0x04, 0xaf,
	6, DSI_DT_DCS_LONG_WR,	DSI_DCS_SET_PAGE_ADDRESS, 0x00, 0x00, 0x07, 0x7f,
	2, DSI_DT_DCS_WR_P0,	DSI_DCS_EXIT_SLEEP_MODE,
	1, 120,
	2, DSI_DT_DCS_WR_P0,	DSI_DCS_SET_DISPLAY_ON
};

static int panel_gpio_init(struct aic_panel *panel)
{
	struct wuxga *wuxga = panel_to_wuxga(panel);

	aic_delay_ms(10);
	gpiod_set_value(wuxga->dcdc_en, 1);
	aic_delay_ms(20);
	gpiod_set_value(wuxga->reset, 1);
	aic_delay_ms(10);
	gpiod_set_value(wuxga->dcdc_en, 0);
	aic_delay_ms(120);
	gpiod_set_value(wuxga->dcdc_en, 1);
	aic_delay_ms(20);
	return 0;
}

static int panel_enable(struct aic_panel *panel)
{
	enum dsi_mode mode = panel->dsi->mode;

	u8 para_vid[] = {7, DSI_DT_GEN_LONG_WR, 0xb3, 0x14, 0x08, 0x00, 0x22, 0x00,
			 1, 120};
	u8 para_cmd[] = {3, DSI_DT_DCS_WR_P1, DSI_DCS_SET_TEAR_ON, 0x00,
			 1, 120};


	if (panel_gpio_init(panel) < 0)
		return -ENODEV;

	panel_di_enable(panel, 0);
	panel_dsi_send_perpare(panel);
	panel_send_command(init_sequence, ARRAY_SIZE(init_sequence), panel);

	if (mode == DSI_MOD_CMD_MODE)
		panel_send_command(para_cmd, ARRAY_SIZE(para_cmd), panel);
	else
		panel_send_command(para_vid, ARRAY_SIZE(para_vid), panel);

	panel_dsi_setup_realmode(panel);
	panel_de_timing_enable(panel, 0);
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
	.pixelclock = 150000000,
	.hactive = 1200,
	.hfront_porch = 200,
	.hback_porch = 200,
	.hsync_len = 100,
	.vactive = 1920,
	.vfront_porch = 3,
	.vback_porch = 6,
	.vsync_len = 2,
	.flags = DISPLAY_FLAGS_HSYNC_LOW | DISPLAY_FLAGS_VSYNC_LOW |
		DISPLAY_FLAGS_DE_HIGH | DISPLAY_FLAGS_PIXDATA_POSEDGE
};

static struct panel_dsi dsi = {
	.format = DSI_FMT_RGB888,
	.mode = DSI_MOD_VID_PULSE,
	.lane_num = 4,
};

static int panel_bind(struct device *dev, struct device *master, void *data)
{
	struct device_node *np = dev->of_node;
	struct panel_comp *p;
	struct wuxga *wuxga;
	const char *str;

	p = devm_kzalloc(dev, sizeof(*p), GFP_KERNEL);
	if (!p)
		return -ENOMEM;

	wuxga = devm_kzalloc(dev, sizeof(*wuxga), GFP_KERNEL);
	if (!wuxga)
		return -ENOMEM;

	wuxga->dcdc_en = devm_gpiod_get(dev, "dcdc-en", GPIOD_ASIS);
	if (IS_ERR(wuxga->dcdc_en)) {
		dev_err(dev, "failed to get power gpio\n");
		return PTR_ERR(wuxga->dcdc_en);
	}

	wuxga->reset = devm_gpiod_get(dev, "reset", GPIOD_ASIS);
	if (IS_ERR(wuxga->reset)) {
		dev_err(dev, "failed to get reset gpio\n");
		return PTR_ERR(wuxga->reset);
	}

	if (panel_parse_dts(p, dev) < 0)
		return -1;

	if (!of_property_read_string(np, "dsi,mode", &str))
		dsi.mode = panel_dsi_str2mode(str);

	p->panel.dsi = &dsi;
	panel_init(p, dev, &panel_vm, &panel_funcs, wuxga);

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
	{.compatible = "artinchip,aic-dsi-panel-simple"},
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

MODULE_AUTHOR("huahui.mai <huahui.mai@artinchip.com>");
MODULE_DESCRIPTION("AIC-" PANEL_DEV_NAME);
MODULE_LICENSE("GPL");
