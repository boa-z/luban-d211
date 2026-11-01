// SPDX-License-Identifier: GPL-2.0-only
/*
 * Driver for nv3051 DSI panel.
 *
 * Copyright (C) 2025 ArtInChip Technology Co., Ltd.
 * Authors: huahui.mai <huahui.ami@artinchip.com>
 */

#include <common.h>
#include <dm.h>
#include <dm/uclass-internal.h>
#include <dm/device_compat.h>
#include <video.h>
#include <panel.h>

#include "panel_dsi.h"

#define NV3051_BIST	0
#define PANEL_DEV_NAME	"dsi_panel_nv3051"

struct nv3051 {
	struct gpio_desc reset;
};

static inline struct nv3051 *panel_to_nv3051(struct aic_panel *panel)
{
	return (struct nv3051 *)panel->panel_private;
}

static int panel_enable(struct aic_panel *panel)
{
	struct nv3051 *nv3051 = panel_to_nv3051(panel);
	int ret;

	dm_gpio_set_value(&nv3051->reset, 1);
	aic_delay_ms(120);
	dm_gpio_set_value(&nv3051->reset, 0);
	aic_delay_ms(120);
	dm_gpio_set_value(&nv3051->reset, 1);
	aic_delay_ms(120);

	panel_di_enable(panel, 0);
	panel_dsi_send_perpare(panel);

	panel_dsi_dcs_send_seq(panel, 0xFF, 0x30);
	panel_dsi_dcs_send_seq(panel, 0xFF, 0x52);
	panel_dsi_dcs_send_seq(panel, 0xFF, 0x01);
	panel_dsi_dcs_send_seq(panel, 0xE3, 0x01);
#if NV3051_BIST
	panel_dsi_dcs_send_seq(panel, 0xF0, 0x0F);
	panel_dsi_dcs_send_seq(panel, 0xF6, 0xC0);
#endif
	panel_dsi_dcs_send_seq(panel, 0x20, 0xB0);
	panel_dsi_dcs_send_seq(panel, 0x25, 0x10);
	panel_dsi_dcs_send_seq(panel, 0x28, 0x6F);
	panel_dsi_dcs_send_seq(panel, 0x29, 0xc4);
	panel_dsi_dcs_send_seq(panel, 0x2A, 0x6F);
	panel_dsi_dcs_send_seq(panel, 0x37, 0xC4);
	panel_dsi_dcs_send_seq(panel, 0x38, 0xCF);
	panel_dsi_dcs_send_seq(panel, 0x39, 0x03);
	panel_dsi_dcs_send_seq(panel, 0x44, 0x00);
	panel_dsi_dcs_send_seq(panel, 0x48, 0x0F);
	panel_dsi_dcs_send_seq(panel, 0x49, 0x03);
	panel_dsi_dcs_send_seq(panel, 0x59, 0xFE);
	panel_dsi_dcs_send_seq(panel, 0x5C, 0x00);
	panel_dsi_dcs_send_seq(panel, 0x80, 0x20);
	panel_dsi_dcs_send_seq(panel, 0x91, 0x77);
	panel_dsi_dcs_send_seq(panel, 0x92, 0x77);
	panel_dsi_dcs_send_seq(panel, 0x99, 0x13);
	panel_dsi_dcs_send_seq(panel, 0x9B, 0x10);
	panel_dsi_dcs_send_seq(panel, 0xA0, 0x55);
	panel_dsi_dcs_send_seq(panel, 0xA1, 0x50);
	panel_dsi_dcs_send_seq(panel, 0xA4, 0x9C);
	panel_dsi_dcs_send_seq(panel, 0xA3, 0x58);
	panel_dsi_dcs_send_seq(panel, 0xA7, 0x02);
	panel_dsi_dcs_send_seq(panel, 0xA8, 0x01);
	panel_dsi_dcs_send_seq(panel, 0xA9, 0x21);
	panel_dsi_dcs_send_seq(panel, 0xAA, 0xFC);
	panel_dsi_dcs_send_seq(panel, 0xAB, 0x28);
	panel_dsi_dcs_send_seq(panel, 0xAC, 0x06);
	panel_dsi_dcs_send_seq(panel, 0xAD, 0x06);
	panel_dsi_dcs_send_seq(panel, 0xAE, 0x06);
	panel_dsi_dcs_send_seq(panel, 0xAF, 0x03);
	panel_dsi_dcs_send_seq(panel, 0xB0, 0x08);
	panel_dsi_dcs_send_seq(panel, 0xB1, 0x26);
	panel_dsi_dcs_send_seq(panel, 0xB2, 0x28);
	panel_dsi_dcs_send_seq(panel, 0xB3, 0x28);
	panel_dsi_dcs_send_seq(panel, 0xB4, 0x03);
	panel_dsi_dcs_send_seq(panel, 0xB5, 0x08);
	panel_dsi_dcs_send_seq(panel, 0xB6, 0x26);
	panel_dsi_dcs_send_seq(panel, 0xB7, 0x08);
	panel_dsi_dcs_send_seq(panel, 0xB8, 0x26);
	panel_dsi_dcs_send_seq(panel, 0xC0, 0x00);
	panel_dsi_dcs_send_seq(panel, 0xC1, 0x00);
	panel_dsi_dcs_send_seq(panel, 0xC2, 0x00);
	panel_dsi_dcs_send_seq(panel, 0xFF, 0x30);
	panel_dsi_dcs_send_seq(panel, 0xFF, 0x52);
	panel_dsi_dcs_send_seq(panel, 0xFF, 0x02);
	panel_dsi_dcs_send_seq(panel, 0xB1, 0x19);
	panel_dsi_dcs_send_seq(panel, 0xD1, 0x11);
	panel_dsi_dcs_send_seq(panel, 0xB4, 0x35);
	panel_dsi_dcs_send_seq(panel, 0xD4, 0x2F);
	panel_dsi_dcs_send_seq(panel, 0xB2, 0x1C);
	panel_dsi_dcs_send_seq(panel, 0xD2, 0x12);
	panel_dsi_dcs_send_seq(panel, 0xB3, 0x39);
	panel_dsi_dcs_send_seq(panel, 0xD3, 0x2F);
	panel_dsi_dcs_send_seq(panel, 0xB6, 0x2E);
	panel_dsi_dcs_send_seq(panel, 0xD6, 0x24);
	panel_dsi_dcs_send_seq(panel, 0xB7, 0x4A);
	panel_dsi_dcs_send_seq(panel, 0xD7, 0x40);
	panel_dsi_dcs_send_seq(panel, 0xC1, 0x07);
	panel_dsi_dcs_send_seq(panel, 0xE1, 0x07);
	panel_dsi_dcs_send_seq(panel, 0xB8, 0x0D);
	panel_dsi_dcs_send_seq(panel, 0xD8, 0x0D);
	panel_dsi_dcs_send_seq(panel, 0xB9, 0x05);
	panel_dsi_dcs_send_seq(panel, 0xD9, 0x05);
	panel_dsi_dcs_send_seq(panel, 0xBD, 0x15);
	panel_dsi_dcs_send_seq(panel, 0xDD, 0x11);
	panel_dsi_dcs_send_seq(panel, 0xBC, 0x13);
	panel_dsi_dcs_send_seq(panel, 0xDC, 0x0F);
	panel_dsi_dcs_send_seq(panel, 0xBB, 0x11);
	panel_dsi_dcs_send_seq(panel, 0xDB, 0x0D);
	panel_dsi_dcs_send_seq(panel, 0xBA, 0x11);
	panel_dsi_dcs_send_seq(panel, 0xDA, 0x0D);
	panel_dsi_dcs_send_seq(panel, 0xBE, 0x1A);
	panel_dsi_dcs_send_seq(panel, 0xDE, 0x16);
	panel_dsi_dcs_send_seq(panel, 0xBF, 0x11);
	panel_dsi_dcs_send_seq(panel, 0xDF, 0x0D);
	panel_dsi_dcs_send_seq(panel, 0xC0, 0x18);
	panel_dsi_dcs_send_seq(panel, 0xE0, 0x14);
	panel_dsi_dcs_send_seq(panel, 0xB5, 0x32);
	panel_dsi_dcs_send_seq(panel, 0xD5, 0x37);
	panel_dsi_dcs_send_seq(panel, 0xB0, 0x02);
	panel_dsi_dcs_send_seq(panel, 0xD0, 0x05);
	panel_dsi_dcs_send_seq(panel, 0xff, 0x30);
	panel_dsi_dcs_send_seq(panel, 0xff, 0x52);
	panel_dsi_dcs_send_seq(panel, 0xff, 0x03);
	panel_dsi_dcs_send_seq(panel, 0x00, 0x00);
	panel_dsi_dcs_send_seq(panel, 0x01, 0x00);
	panel_dsi_dcs_send_seq(panel, 0x02, 0x00);
	panel_dsi_dcs_send_seq(panel, 0x03, 0x00);
	panel_dsi_dcs_send_seq(panel, 0x07, 0x00);
	panel_dsi_dcs_send_seq(panel, 0x08, 0x87);
	panel_dsi_dcs_send_seq(panel, 0x09, 0x86);
	panel_dsi_dcs_send_seq(panel, 0x30, 0x00);
	panel_dsi_dcs_send_seq(panel, 0x31, 0x00);
	panel_dsi_dcs_send_seq(panel, 0x32, 0x00);
	panel_dsi_dcs_send_seq(panel, 0x33, 0x00);
	panel_dsi_dcs_send_seq(panel, 0x34, 0xA1);
	panel_dsi_dcs_send_seq(panel, 0x35, 0x07);
	panel_dsi_dcs_send_seq(panel, 0x36, 0x07);
	panel_dsi_dcs_send_seq(panel, 0x37, 0x33);
	panel_dsi_dcs_send_seq(panel, 0x40, 0x85);
	panel_dsi_dcs_send_seq(panel, 0x41, 0x84);
	panel_dsi_dcs_send_seq(panel, 0x42, 0x83);
	panel_dsi_dcs_send_seq(panel, 0x43, 0x82);
	panel_dsi_dcs_send_seq(panel, 0x44, 0x44);
	panel_dsi_dcs_send_seq(panel, 0x45, 0x73);
	panel_dsi_dcs_send_seq(panel, 0x46, 0x74);
	panel_dsi_dcs_send_seq(panel, 0x47, 0x44);
	panel_dsi_dcs_send_seq(panel, 0x48, 0x75);
	panel_dsi_dcs_send_seq(panel, 0x49, 0x76);
	panel_dsi_dcs_send_seq(panel, 0x50, 0x81);
	panel_dsi_dcs_send_seq(panel, 0x51, 0x00);
	panel_dsi_dcs_send_seq(panel, 0x52, 0x01);
	panel_dsi_dcs_send_seq(panel, 0x53, 0x02);
	panel_dsi_dcs_send_seq(panel, 0x54, 0x44);
	panel_dsi_dcs_send_seq(panel, 0x55, 0x77);
	panel_dsi_dcs_send_seq(panel, 0x56, 0x78);
	panel_dsi_dcs_send_seq(panel, 0x57, 0x44);
	panel_dsi_dcs_send_seq(panel, 0x58, 0x71);
	panel_dsi_dcs_send_seq(panel, 0x59, 0x72);
	panel_dsi_dcs_send_seq(panel, 0x80, 0x0f);
	panel_dsi_dcs_send_seq(panel, 0x81, 0x0F);
	panel_dsi_dcs_send_seq(panel, 0x83, 0x0F);
	panel_dsi_dcs_send_seq(panel, 0x85, 0x0E);
	panel_dsi_dcs_send_seq(panel, 0x87, 0x07);
	panel_dsi_dcs_send_seq(panel, 0x88, 0x06);
	panel_dsi_dcs_send_seq(panel, 0x89, 0x05);
	panel_dsi_dcs_send_seq(panel, 0x8A, 0x04);
	panel_dsi_dcs_send_seq(panel, 0x8C, 0x00);
	panel_dsi_dcs_send_seq(panel, 0x8E, 0x0F);
	panel_dsi_dcs_send_seq(panel, 0x90, 0x0E);
	panel_dsi_dcs_send_seq(panel, 0x91, 0x0E);
	panel_dsi_dcs_send_seq(panel, 0x93, 0x0F);
	panel_dsi_dcs_send_seq(panel, 0x94, 0x0F);
	panel_dsi_dcs_send_seq(panel, 0x96, 0x0f);
	panel_dsi_dcs_send_seq(panel, 0x97, 0x0F);
	panel_dsi_dcs_send_seq(panel, 0x99, 0x0F);
	panel_dsi_dcs_send_seq(panel, 0x9B, 0x0E);
	panel_dsi_dcs_send_seq(panel, 0x9D, 0x07);
	panel_dsi_dcs_send_seq(panel, 0x9E, 0x06);
	panel_dsi_dcs_send_seq(panel, 0x9F, 0x05);
	panel_dsi_dcs_send_seq(panel, 0xA0, 0x04);
	panel_dsi_dcs_send_seq(panel, 0xA2, 0x00);
	panel_dsi_dcs_send_seq(panel, 0xa4, 0x0F);
	panel_dsi_dcs_send_seq(panel, 0xA6, 0x0E);
	panel_dsi_dcs_send_seq(panel, 0xA7, 0x0E);
	panel_dsi_dcs_send_seq(panel, 0xA9, 0x0F);
	panel_dsi_dcs_send_seq(panel, 0xaA, 0x0F);
	panel_dsi_dcs_send_seq(panel, 0xFF, 0x30);
	panel_dsi_dcs_send_seq(panel, 0xFF, 0x52);
	panel_dsi_dcs_send_seq(panel, 0xFF, 0x00);
	panel_dsi_dcs_send_seq(panel, 0x36, 0x02);
	panel_dsi_dcs_send_seq(panel, 0x53, 0x2c);

	ret = panel_dsi_dcs_exit_sleep_mode(panel);
	if (ret < 0) {
		pr_err("Failed to exit sleep mode: %d\n", ret);
		return ret;
	}
	aic_delay_ms(120);

	ret = panel_dsi_dcs_set_display_on(panel);
	if (ret < 0) {
		pr_err("Failed to set display on: %d\n", ret);
		return ret;
	}
	aic_delay_ms(120);

	panel_dsi_setup_realmode(panel);
	panel_de_timing_enable(panel, 0);
	panel_backlight_enable(panel, 0);
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
	.pixclock = 50000000,
	.xres = 640,
	.right_margin = 20,
	.left_margin = 20,
	.hsync_len = 20,
	.yres = 1136,
	.lower_margin = 40,
	.upper_margin = 8,
	.vsync_len = 4,
};

static struct panel_dsi dsi = {
	.format = DSI_FMT_RGB888,
	.mode = DSI_MOD_VID_EVENT,
	.lane_num = 4,
};

static int panel_probe(struct udevice *dev)
{
	struct panel_priv *priv = dev_get_priv(dev);
	struct nv3051 *nv3051;
	int ret;

	if (panel_parse_dts(dev) < 0)
		return -1;

	nv3051 = malloc(sizeof(*nv3051));
	if (!nv3051)
		return -ENOMEM;

	ret = gpio_request_by_name(dev, "reset-gpios", 0,
					&nv3051->reset, GPIOD_IS_OUT);
	if (ret) {
		dev_err(dev, "failed to get reset gpio\n");
		free(nv3051);
		return ret;
	}

	priv->panel.dsi = &dsi;
	panel_init(priv, dev, &panel_vm, &panel_funcs, nv3051);

	return 0;
}

static const struct udevice_id panel_match_ids[] = {
	{.compatible = "artinchip,aic-dsi-panel-simple"},
	{ /* sentinel */}
};

U_BOOT_DRIVER(panel_dsi_nv3051) = {
	.name      = PANEL_DEV_NAME,
	.id        = UCLASS_PANEL,
	.of_match  = panel_match_ids,
	.probe     = panel_probe,
	.priv_auto = sizeof(struct panel_priv),
};

