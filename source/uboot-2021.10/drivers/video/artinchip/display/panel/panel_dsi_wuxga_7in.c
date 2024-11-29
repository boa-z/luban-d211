// SPDX-License-Identifier: GPL-2.0-only
/*
 * Driver for DSI wuga panel.
 *
 * Copyright (C) 2020-2021 ArtInChip Technology Co., Ltd.
 * Authors:  Huahui Mai <huahui.mai@artinchip.com>
 */

#include <common.h>
#include <dm.h>
#include <dm/uclass-internal.h>
#include <dm/device_compat.h>
#include <video.h>
#include <panel.h>

#include "../hw/dsi_reg.h"
#include "panel_dsi.h"

#define PANEL_DEV_NAME		"dsi_panel_wuxga_7in"

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
	int ret;
	struct gpio_desc dcdc_en;
	struct gpio_desc reset;

	ret = gpio_request_by_name(panel->dev, "dcdc-en-gpios", 0, &dcdc_en,
			GPIOD_IS_OUT);
	if (ret) {
		dev_err(panel->dev, "Failed to get dcdc_en gpio\n");
		return ret;
	}

	ret = gpio_request_by_name(panel->dev, "reset-gpios", 0, &reset,
			GPIOD_IS_OUT);
	if (ret) {
		dev_err(panel->dev, "Failed to get reset gpio\n");
		return ret;
	}

	dm_gpio_set_value(&dcdc_en, 0);
	dm_gpio_set_value(&reset, 0);

	aic_delay_ms(10);
	dm_gpio_set_value(&dcdc_en, 1);
	aic_delay_ms(20);
	dm_gpio_set_value(&reset, 1);
	aic_delay_ms(10);
	dm_gpio_set_value(&dcdc_en, 0);
	aic_delay_ms(120);
	dm_gpio_set_value(&dcdc_en, 1);
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
	.prepare = panel_default_prepare,
	.enable = panel_enable,
	.get_video_mode = panel_default_get_video_mode,
	.register_callback = panel_register_callback,
};

/* Init the videomode parameter, dts will override the initial value. */
static struct fb_videomode panel_vm = {
	.pixclock = 150000000,
	.xres = 1200,
	.right_margin = 200,
	.left_margin = 200,
	.hsync_len = 100,
	.yres = 1920,
	.lower_margin = 3,
	.upper_margin = 6,
	.vsync_len = 2,
	.flag = DISPLAY_FLAGS_HSYNC_LOW | DISPLAY_FLAGS_VSYNC_LOW |
		DISPLAY_FLAGS_DE_HIGH | DISPLAY_FLAGS_PIXDATA_POSEDGE
};

static int panel_probe(struct udevice *dev)
{
	struct panel_priv *priv = dev_get_priv(dev);
	ofnode node = dev_ofnode(dev);
	struct panel_dsi *dsi;
	const char *str;

	dsi = malloc(sizeof(*dsi));
	if (!dsi)
		return -ENOMEM;

	if (panel_parse_dts(dev) < 0) {
		free(dsi);
		return -1;
	}

	str = ofnode_read_string(node, "dsi,mode");
	if (!str)
		dsi->mode = DSI_MOD_VID_PULSE;
	else
		dsi->mode = panel_mipi_str2mode(str);

	dsi->format = DSI_FMT_RGB888;
	dsi->lane_num = 4;
	priv->panel.dsi = dsi;

	panel_init(priv, dev, &panel_vm, &panel_funcs);

	return 0;
}

static const struct udevice_id panel_match_ids[] = {
	{.compatible = "artinchip,aic-dsi-panel-simple"},
	{ /* sentinel */}
};

U_BOOT_DRIVER(panel_dsi_wuxga_7in) = {
	.name      = PANEL_DEV_NAME,
	.id        = UCLASS_PANEL,
	.of_match  = panel_match_ids,
	.probe     = panel_probe,
	.priv_auto = sizeof(struct panel_priv),
};

