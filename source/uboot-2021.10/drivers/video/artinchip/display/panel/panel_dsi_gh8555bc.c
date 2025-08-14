// SPDX-License-Identifier: GPL-2.0-only
/*
 * Driver for gc8555bc DSI panel.
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

#define PANEL_DEV_NAME	"dsi_panel_gc8555bc"

struct gh8555bc {
	struct gpio_desc reset;
};

static inline struct gh8555bc *panel_to_gh8555bc(struct aic_panel *panel)
{
	return (struct gh8555bc *)panel->panel_private;
}

static int panel_enable(struct aic_panel *panel)
{
	struct gh8555bc *gh8555bc = panel_to_gh8555bc(panel);
	int ret;

	dm_gpio_set_value(&gh8555bc->reset, 1);
	aic_delay_ms(120);
	dm_gpio_set_value(&gh8555bc->reset, 0);
	aic_delay_ms(120);
	dm_gpio_set_value(&gh8555bc->reset, 1);
	aic_delay_ms(120);

	panel_di_enable(panel, 0);
	panel_dsi_send_perpare(panel);

	panel_dsi_dcs_send_seq(panel, 0xee, 0x50);		// page 1
	panel_dsi_dcs_send_seq(panel, 0xea, 0x85, 0x55);	// password
	panel_dsi_dcs_send_seq(panel, 0x22, 0x00);
	panel_dsi_dcs_send_seq(panel, 0x24, 0xa8);		// 反扫改B8
	panel_dsi_dcs_send_seq(panel, 0x25, 0x40);
	panel_dsi_dcs_send_seq(panel, 0x30, 0x00);
	panel_dsi_dcs_send_seq(panel, 0x31, 0x50, 0xb4, 0xe5, 0x03, 0x00);
	panel_dsi_dcs_send_seq(panel, 0x36, 0x18);
	panel_dsi_dcs_send_seq(panel, 0x37, 0x1e, 0x5a, 0x08, 0x08, 0x10);
	panel_dsi_dcs_send_seq(panel, 0x56, 0x83);		//设置00可关闭自动识别
	panel_dsi_dcs_send_seq(panel, 0x7a, 0x20);
	panel_dsi_dcs_send_seq(panel, 0x7d, 0x00);		//D8
	panel_dsi_dcs_send_seq(panel, 0x90, 0x02, 0x12);
	panel_dsi_dcs_send_seq(panel, 0x95, 0x74);
	panel_dsi_dcs_send_seq(panel, 0x97, 0x06);		//=08   //   smrt gip ON=08 OFF=38  05
	panel_dsi_dcs_send_seq(panel, 0x99, 0x00);

	panel_dsi_dcs_send_seq(panel, 0xee, 0x60);
	panel_dsi_dcs_send_seq(panel, 0x21, 0x01);		// osc
	panel_dsi_dcs_send_seq(panel, 0x27, 0x21);		// VDDD
	panel_dsi_dcs_send_seq(panel, 0x28, 0x12);
	panel_dsi_dcs_send_seq(panel, 0x29, 0x8d);		// 8d 89
	panel_dsi_dcs_send_seq(panel, 0x2c, 0xf9);
	panel_dsi_dcs_send_seq(panel, 0x30, 0x01);		// 00= 3lane , 01= 4lane   03 2lane
	panel_dsi_dcs_send_seq(panel, 0x32, 0xDc);
	panel_dsi_dcs_send_seq(panel, 0x3a, 0x26);
	panel_dsi_dcs_send_seq(panel, 0x3b, 0x00);		//c8->00
	panel_dsi_dcs_send_seq(panel, 0x3c, 0x00);		// vcom
	panel_dsi_dcs_send_seq(panel, 0x3d, 0x01, 0x81);	//VGH VGL
	panel_dsi_dcs_send_seq(panel, 0x42, 0x64, 0x64);	//  vspr vsnr
	panel_dsi_dcs_send_seq(panel, 0x44, 0x01);		//VGH
	panel_dsi_dcs_send_seq(panel, 0x46, 0x45);		//VGL
	panel_dsi_dcs_send_seq(panel, 0x7f, 0x24);		// vcsw
	panel_dsi_dcs_send_seq(panel, 0x80, 0x24);		// vcsw
	panel_dsi_dcs_send_seq(panel, 0x86, 0x20);
	panel_dsi_dcs_send_seq(panel, 0x90, 0x00);
	panel_dsi_dcs_send_seq(panel, 0x91, 0x44);
	panel_dsi_dcs_send_seq(panel, 0x92, 0x33);
	panel_dsi_dcs_send_seq(panel, 0x93, 0x9f);
	panel_dsi_dcs_send_seq(panel, 0x9a, 0x06);		// 640
	panel_dsi_dcs_send_seq(panel, 0x9b, 0x02, 0x38);	// 1136

	// gamma 2.2 2025/6/11
	panel_dsi_dcs_send_seq(panel, 0x5a, 0x05, 0x15, 0x29, 0x31, 0x33); //gamma n 0.4.8.12.20
	panel_dsi_dcs_send_seq(panel, 0x47, 0x05, 0x15, 0x29, 0x31, 0x33); //gamma P0.4.8.12.20

	panel_dsi_dcs_send_seq(panel, 0x4c, 0x3e, 0x37, 0x49, 0x2A, 0x26); //28.44.64.96.128.
	panel_dsi_dcs_send_seq(panel, 0x5f, 0x3e, 0x37, 0x49, 0x2A, 0x26); //28.44.64.96.128.

	panel_dsi_dcs_send_seq(panel, 0x64, 0x26, 0x08, 0x20, 0x1b, 0x29); //159.191.211.227.235
	panel_dsi_dcs_send_seq(panel, 0x51, 0x26, 0x08, 0x20, 0x1b, 0x29); //159.191.211.227.235

	panel_dsi_dcs_send_seq(panel, 0x69, 0x30, 0x3e, 0x55, 0x7f);	//243.247.251.255
	panel_dsi_dcs_send_seq(panel, 0x56, 0x30, 0x3e, 0x55, 0x7f);	//243.247.251.255

	panel_dsi_dcs_send_seq(panel, 0xee, 0x70);

	panel_dsi_dcs_send_seq(panel, 0x00, 0x01, 0x02, 0x00, 0x01);
	panel_dsi_dcs_send_seq(panel, 0x0C, 0x10, 0x05);
	//CYC0
	panel_dsi_dcs_send_seq(panel, 0x10, 0x04, 0x05, 0x00, 0x00, 0x00);
	//panel_dsi_dcs_send_seq(panel, 0x15, 0x00, 0x70, 0x0c, 0x08, 0x00);
	panel_dsi_dcs_send_seq(panel, 0x29, 0x10, 0x35);
	//gip0-gip21=gipL1-gipL22 FORWARD scan
	panel_dsi_dcs_send_seq(panel, 0x60, 0x3c, 0x3c, 0x3c, 0x3c, 0x3c);
	panel_dsi_dcs_send_seq(panel, 0x65, 0x3f, 0x3c, 0x17, 0x15, 0x13);
	panel_dsi_dcs_send_seq(panel, 0x6a, 0x11, 0x3c, 0x01, 0x3c, 0x3c);
	panel_dsi_dcs_send_seq(panel, 0x6f, 0x3c, 0x3f, 0x3f, 0x3c, 0x3c);
	panel_dsi_dcs_send_seq(panel, 0x74, 0x3c, 0x3c);

	//gip22-gip43=gipR1-gipR22 FORWARD scan
	panel_dsi_dcs_send_seq(panel, 0x80, 0x3c, 0x3c, 0x3c, 0x3c, 0x3c);
	panel_dsi_dcs_send_seq(panel, 0x85, 0x3f, 0x3c, 0x16, 0x14, 0x12);
	panel_dsi_dcs_send_seq(panel, 0x8a, 0x10, 0x3c, 0x00, 0x3c, 0x3c);
	panel_dsi_dcs_send_seq(panel, 0x8F, 0x3c, 0x3f, 0x3f, 0x3c, 0x3c);
	panel_dsi_dcs_send_seq(panel, 0x94, 0x3c, 0x3c);

#if 0
	//反扫GIP
	//gip0-gip21=gipL1-gipL22 backWARD scan
	panel_dsi_dcs_send_seq(panel, 0x60, 0x3c, 0x3c, 0x3c, 0x3f, 0x3c);
	panel_dsi_dcs_send_seq(panel, 0x65, 0x3c, 0x3c, 0x14, 0x16, 0x10);
	panel_dsi_dcs_send_seq(panel, 0x6a, 0x12, 0x3c, 0x00, 0x3c, 0x3c);
	panel_dsi_dcs_send_seq(panel, 0x6f, 0x3c, 0x3f, 0x3f, 0x3c, 0x3c);
	panel_dsi_dcs_send_seq(panel, 0x74, 0x3c, 0x3c);

	//gip22-gip43=gipR1-gipR22 backWARD scan
	panel_dsi_dcs_send_seq(panel, 0x80, 0x3c, 0x3c, 0x3c, 0x3f, 0x3c);
	panel_dsi_dcs_send_seq(panel, 0x85, 0x3c, 0x3c, 0x15, 0x17, 0x11);
	panel_dsi_dcs_send_seq(panel, 0x8a, 0x13, 0x3c, 0x01, 0x3c, 0x3c);
	panel_dsi_dcs_send_seq(panel, 0x8F, 0x3c, 0x3f, 0x3f, 0x3c, 0x3c);
	panel_dsi_dcs_send_seq(panel, 0x94, 0x3c, 0x3c);
#endif

	panel_dsi_dcs_send_seq(panel, 0xea, 0x00, 0x00);
	panel_dsi_dcs_send_seq(panel, 0xee, 0x00);		// ENTER PAGE0

	ret = panel_dsi_dcs_exit_sleep_mode(panel);
	if (ret < 0) {
		pr_err("Failed to exit sleep mode: %d\n", ret);
		return ret;
	}

	aic_delay_ms(600);

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
	.pixclock = 54000000,
	.xres = 640,
	.right_margin = 90,
	.left_margin = 20,
	.hsync_len = 20,
	.yres = 1136,
	.lower_margin = 24,
	.upper_margin = 12,
	.vsync_len = 5,
};

static struct panel_dsi dsi = {
	.format = DSI_FMT_RGB888,
	.mode = DSI_MOD_VID_BURST,
	.lane_num = 4,
};

static int panel_probe(struct udevice *dev)
{
	struct panel_priv *priv = dev_get_priv(dev);
	struct gh8555bc *gh8555bc;
	int ret;

	if (panel_parse_dts(dev) < 0)
		return -1;

	gh8555bc = malloc(sizeof(*gh8555bc));
	if (!gh8555bc)
		return -ENOMEM;

	ret = gpio_request_by_name(dev, "reset-gpios", 0,
					&gh8555bc->reset, GPIOD_IS_OUT);
	if (ret) {
		dev_err(dev, "failed to get reset gpio\n");
		free(gh8555bc);
		return ret;
	}

	priv->panel.dsi = &dsi;
	panel_init(priv, dev, &panel_vm, &panel_funcs, gh8555bc);

	return 0;
}

static const struct udevice_id panel_match_ids[] = {
	{.compatible = "artinchip,aic-dsi-panel-simple"},
	{ /* sentinel */}
};

U_BOOT_DRIVER(panel_dsi_gh8555bc) = {
	.name      = PANEL_DEV_NAME,
	.id        = UCLASS_PANEL,
	.of_match  = panel_match_ids,
	.probe     = panel_probe,
	.priv_auto = sizeof(struct panel_priv),
};

