// SPDX-License-Identifier: GPL-2.0-only
/*
 * Driver for general DSI panel.
 *
 * Copyright (C) 2020-2021 ArtInChip Technology Co., Ltd.
 * Authors:  Matteo <duanmt@artinchip.com>
 */

#include <common.h>
#include <dm.h>
#include <dm/uclass-internal.h>
#include <dm/device_compat.h>
#include <video.h>
#include <panel.h>

#include "panel_dsi.h"

#define PANEL_DEV_NAME		"dsi_panel_b080xan"

static int panel_enable(struct aic_panel *panel)
{
	u8 cmd[2] = {DSI_DCS_EXIT_SLEEP_MODE, DSI_DCS_SET_DISPLAY_ON};

	if (unlikely(!panel->callbacks.di_send_cmd))
		panic("Have no send_cmd() API for DSI.\n");

	panel_di_enable(panel, 0);
	panel_dsi_send_perpare(panel);

	panel->callbacks.di_send_cmd(DSI_DT_DCS_WR_P0, &cmd[0], 1);
	aic_delay_ms(200);
	panel->callbacks.di_send_cmd(DSI_DT_DCS_WR_P0, &cmd[1], 1);
	aic_delay_ms(200);

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
	.pixclock = 80000000,
	.xres = 768,
	.right_margin = 100,
	.left_margin = 200,
	.hsync_len = 100,
	.yres = 1024,
	.lower_margin = 20,
	.upper_margin = 100,
	.vsync_len = 80,
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
	dsi->lane_num = 4;
	priv->panel.dsi = dsi;

	panel_init(priv, dev, &panel_vm, &panel_funcs);

	return 0;
}

static const struct udevice_id panel_match_ids[] = {
	{.compatible = "artinchip,aic-dsi-panel-simple"},
	{ /* sentinel */}
};

U_BOOT_DRIVER(panel_dsi_b080xan) = {
	.name      = PANEL_DEV_NAME,
	.id        = UCLASS_PANEL,
	.of_match  = panel_match_ids,
	.probe     = panel_probe,
	.priv_auto = sizeof(struct panel_priv),
};

