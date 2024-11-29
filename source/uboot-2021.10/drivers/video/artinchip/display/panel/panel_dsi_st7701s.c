// SPDX-License-Identifier: GPL-2.0-only
/*
 * Driver for st7701s DSI panel.
 *
 * Copyright (C) 2020-2021 ArtInChip Technology Co., Ltd.
 * Authors: huahui.mai <huahui.ami@artinchip.com>
 */

#include <common.h>
#include <dm.h>
#include <dm/uclass-internal.h>
#include <dm/device_compat.h>
#include <video.h>
#include <panel.h>

#include "panel_dsi.h"

#define PANEL_DEV_NAME		"dsi_panel_st7701s"

static u8 init_sequence[] = {
	/* Out sleep */
	2, 	DSI_DT_DCS_WR_P0,	0x11,
	1,	120,

	/* Display Control Setting */
	7,	DSI_DT_DCS_LONG_WR,	0xFF, 0x77, 0x01, 0x00,	0x00, 0x10,
	4,	DSI_DT_DCS_LONG_WR,	0xC0, 0x63, 0x00,
	4,	DSI_DT_DCS_LONG_WR,	0xC1, 0x11, 0x02,
	4,	DSI_DT_DCS_LONG_WR,	0xC2, 0x37, 0x00,
	3,	DSI_DT_DCS_WR_P1,	0xCC, 0x18,
	3,	DSI_DT_DCS_WR_P1,	0xC7, 0x00,

	/* GAMMA Set */
	12,	DSI_DT_DCS_LONG_WR,	0xB0, 0x40, 0xC9, 0x91, 0x07, 0x02,
					0x09, 0x09, 0x1F, 0x04, 0x50,
	5,	DSI_DT_DCS_LONG_WR,	0x0F, 0xE4, 0x29, 0xDF,
	12,	DSI_DT_DCS_LONG_WR,	0xB1, 0x40, 0xCB, 0xD0, 0x11, 0x92,
					0x07, 0x00, 0x08, 0x07, 0x1C,
	7,	DSI_DT_DCS_LONG_WR,	0x06, 0x53, 0x12, 0x63, 0xEB, 0xDF,

	/* Power Control Registers Initial */
	7,	DSI_DT_DCS_LONG_WR,	0xFF, 0x77, 0x01, 0x00, 0x00, 0x11,
	3,	DSI_DT_DCS_WR_P1,	0xB0, 0x65,

	/* Vcom Setting */
	3,	DSI_DT_DCS_WR_P1,	0xB1, 0x6A,

	/* End Vcom Setting */
	3,	DSI_DT_DCS_WR_P1,	0xB2, 0x87,
	3,	DSI_DT_DCS_WR_P1,	0xB3, 0x80,
	3,	DSI_DT_DCS_WR_P1,	0xB5, 0x49,
	3,	DSI_DT_DCS_WR_P1,	0xB7, 0x85,
	3,	DSI_DT_DCS_WR_P1,	0xB8, 0x20,
	3,	DSI_DT_DCS_WR_P1,	0xB9, 0x10,
	3,	DSI_DT_DCS_WR_P1,	0xC1, 0x78,
	3,	DSI_DT_DCS_WR_P1,	0xC2, 0x78,
	3,	DSI_DT_DCS_WR_P1,	0xD0, 0x88,
	1,	100,

	/* GIP Set  */
	5,	DSI_DT_DCS_LONG_WR,	0xE0, 0x00, 0x00, 0x02,
	13,	DSI_DT_DCS_LONG_WR,	0xE1, 0x08, 0x00, 0x0A, 0x00, 0x07,
					0x00, 0x09, 0x00, 0x00, 0x33, 0x33,
	15,	DSI_DT_DCS_LONG_WR,	0xE2, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00,
	6,	DSI_DT_DCS_LONG_WR,	0xE3, 0x00, 0x00, 0x33, 0x33,
	4,	DSI_DT_DCS_LONG_WR,	0xE4, 0x44, 0x44,
	18,	DSI_DT_DCS_LONG_WR,	0xE5, 0x0E, 0x60, 0xA0, 0xA0, 0x10, 0x60, 0xA0, 0xA0, 0x0A, 0x60,
		0xA0, 0xA0, 0x0C, 0x60, 0xA0, 0xA0,
	6,	DSI_DT_DCS_LONG_WR,	0xE6, 0x00, 0x00, 0x33, 0x33,
	4,	DSI_DT_DCS_LONG_WR,	0xE7, 0x44, 0x44,
	18,	DSI_DT_DCS_LONG_WR,	0xE8, 0x0D, 0x60, 0xA0, 0xA0, 0x0F, 0x60, 0xA0, 0xA0, 0x09, 0x60,
		0xA0, 0xA0, 0x0B, 0x60, 0xA0, 0xA0,
	9,	DSI_DT_DCS_LONG_WR,	0xEB, 0x02, 0x01, 0xE4, 0xE4, 0x44, 0x00, 0x40,
	4,	DSI_DT_DCS_LONG_WR,	0xEC, 0x02, 0x01,
	18,	DSI_DT_DCS_LONG_WR,	0xED, 0xAB, 0x89, 0x76, 0x54, 0x01, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
		0xFF, 0x10, 0x45, 0x67, 0x98, 0xBA,
	7,	DSI_DT_DCS_LONG_WR,	0xFF, 0x77, 0x01, 0x00, 0x00, 0x00,

	3,	DSI_DT_DCS_WR_P1,	0x36, 0x00,
	3,	DSI_DT_DCS_WR_P1,	0x3A, 0x70,
	2,	DSI_DT_DCS_WR_P0,	0x29,
	1,	120
};

static int panel_enable(struct aic_panel *panel)
{
	panel_di_enable(panel, 0);

	panel_dsi_send_perpare(panel);
	panel_send_command(init_sequence, ARRAY_SIZE(init_sequence), panel);
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
	.pixclock = 27000000,
	.xres = 480,
	.right_margin = 20,
	.left_margin = 35,
	.hsync_len = 4,
	.yres = 800,
	.lower_margin = 10,
	.upper_margin = 20,
	.vsync_len = 4,
	.flag = DISPLAY_FLAGS_HSYNC_LOW | DISPLAY_FLAGS_VSYNC_LOW |
		DISPLAY_FLAGS_DE_HIGH | DISPLAY_FLAGS_PIXDATA_POSEDGE
};

static int panel_probe(struct udevice *dev)
{
	struct panel_priv *priv = dev_get_priv(dev);
	struct panel_dsi *dsi;

	dsi = malloc(sizeof(*dsi));
	if (!dsi)
		return -ENOMEM;

	if (panel_parse_dts(dev) < 0) {
		free(dsi);
		return -1;
	}

	dsi->format = DSI_FMT_RGB888;
	dsi->mode = DSI_MOD_VID_PULSE;
	dsi->lane_num = 2;
	priv->panel.dsi = dsi;

	panel_init(priv, dev, &panel_vm, &panel_funcs);

	return 0;
}

static const struct udevice_id panel_match_ids[] = {
	{.compatible = "artinchip,aic-dsi-panel-simple"},
	{ /* sentinel */}
};

U_BOOT_DRIVER(panel_dsi_st7701s) = {
	.name      = PANEL_DEV_NAME,
	.id        = UCLASS_PANEL,
	.of_match  = panel_match_ids,
	.probe     = panel_probe,
	.priv_auto = sizeof(struct panel_priv),
};
