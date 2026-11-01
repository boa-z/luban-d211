// SPDX-License-Identifier: GPL-2.0-only
/*
 * Driver for ili9881c DSI panel.
 *
 * Copyright (C) 2020-2026 ArtInChip Technology Co., Ltd.
 * Authors: huahui.mai <huahui.ami@artinchip.com>
 */

#include <linux/gpio/consumer.h>
#include <linux/module.h>
#include <linux/component.h>
#include <linux/platform_device.h>
#include <video/display_timing.h>

#include "panel_dsi.h"

#define PANEL_DEV_NAME		"dsi_panel_ili9881c"

struct ili9881c {
	struct gpio_desc *reset;
};

static inline struct ili9881c *panel_to_ili9881c(struct aic_panel *panel)
{
	return (struct ili9881c *)panel->panel_private;
}

static int tft050bp071_init(struct aic_panel *panel)
{
	int ret;

	panel_dsi_generic_send_seq(panel, 0xFF, 0x98, 0x81, 0x03);

	/* GIP_1 */
	panel_dsi_generic_send_seq(panel, 0x01, 0x00);
	panel_dsi_generic_send_seq(panel, 0x02, 0x00);
	panel_dsi_generic_send_seq(panel, 0x03, 0x73);
	panel_dsi_generic_send_seq(panel, 0x04, 0x00);
	panel_dsi_generic_send_seq(panel, 0x05, 0x00);
	panel_dsi_generic_send_seq(panel, 0x06, 0x08);
	panel_dsi_generic_send_seq(panel, 0x07, 0x00);
	panel_dsi_generic_send_seq(panel, 0x08, 0x00);
	panel_dsi_generic_send_seq(panel, 0x09, 0x1B);
	panel_dsi_generic_send_seq(panel, 0x0a, 0x01);
	panel_dsi_generic_send_seq(panel, 0x0b, 0x01);
	panel_dsi_generic_send_seq(panel, 0x0c, 0x0D);
	panel_dsi_generic_send_seq(panel, 0x0d, 0x01);
	panel_dsi_generic_send_seq(panel, 0x0e, 0x01);
	panel_dsi_generic_send_seq(panel, 0x0f, 0x26);
	panel_dsi_generic_send_seq(panel, 0x10, 0x26);
	panel_dsi_generic_send_seq(panel, 0x11, 0x00);
	panel_dsi_generic_send_seq(panel, 0x12, 0x00);
	panel_dsi_generic_send_seq(panel, 0x13, 0x02);
	panel_dsi_generic_send_seq(panel, 0x14, 0x00);
	panel_dsi_generic_send_seq(panel, 0x15, 0x00);
	panel_dsi_generic_send_seq(panel, 0x16, 0x00);
	panel_dsi_generic_send_seq(panel, 0x17, 0x00);
	panel_dsi_generic_send_seq(panel, 0x18, 0x00);
	panel_dsi_generic_send_seq(panel, 0x19, 0x00);
	panel_dsi_generic_send_seq(panel, 0x1a, 0x00);
	panel_dsi_generic_send_seq(panel, 0x1b, 0x00);
	panel_dsi_generic_send_seq(panel, 0x1c, 0x00);
	panel_dsi_generic_send_seq(panel, 0x1d, 0x00);
	panel_dsi_generic_send_seq(panel, 0x1e, 0x40);
	panel_dsi_generic_send_seq(panel, 0x1f, 0x00);
	panel_dsi_generic_send_seq(panel, 0x20, 0x06);
	panel_dsi_generic_send_seq(panel, 0x21, 0x01);
	panel_dsi_generic_send_seq(panel, 0x22, 0x00);
	panel_dsi_generic_send_seq(panel, 0x23, 0x00);
	panel_dsi_generic_send_seq(panel, 0x24, 0x00);
	panel_dsi_generic_send_seq(panel, 0x25, 0x00);
	panel_dsi_generic_send_seq(panel, 0x26, 0x00);
	panel_dsi_generic_send_seq(panel, 0x27, 0x00);
	panel_dsi_generic_send_seq(panel, 0x28, 0x33);
	panel_dsi_generic_send_seq(panel, 0x29, 0x03);
	panel_dsi_generic_send_seq(panel, 0x2a, 0x00);
	panel_dsi_generic_send_seq(panel, 0x2b, 0x00);
	panel_dsi_generic_send_seq(panel, 0x2c, 0x00);
	panel_dsi_generic_send_seq(panel, 0x2d, 0x00);
	panel_dsi_generic_send_seq(panel, 0x2e, 0x00);
	panel_dsi_generic_send_seq(panel, 0x2f, 0x00);
	panel_dsi_generic_send_seq(panel, 0x30, 0x00);
	panel_dsi_generic_send_seq(panel, 0x31, 0x00);
	panel_dsi_generic_send_seq(panel, 0x32, 0x00);
	panel_dsi_generic_send_seq(panel, 0x33, 0x00);
	panel_dsi_generic_send_seq(panel, 0x34, 0x00);
	panel_dsi_generic_send_seq(panel, 0x35, 0x00);
	panel_dsi_generic_send_seq(panel, 0x36, 0x00);
	panel_dsi_generic_send_seq(panel, 0x37, 0x00);
	panel_dsi_generic_send_seq(panel, 0x38, 0x00);
	panel_dsi_generic_send_seq(panel, 0x39, 0x00);
	panel_dsi_generic_send_seq(panel, 0x3a, 0x00);
	panel_dsi_generic_send_seq(panel, 0x3b, 0x00);
	panel_dsi_generic_send_seq(panel, 0x3c, 0x00);
	panel_dsi_generic_send_seq(panel, 0x3d, 0x00);
	panel_dsi_generic_send_seq(panel, 0x3e, 0x00);
	panel_dsi_generic_send_seq(panel, 0x3f, 0x00);
	panel_dsi_generic_send_seq(panel, 0x40, 0x00);
	panel_dsi_generic_send_seq(panel, 0x41, 0x00);
	panel_dsi_generic_send_seq(panel, 0x42, 0x00);
	panel_dsi_generic_send_seq(panel, 0x43, 0x00);
	panel_dsi_generic_send_seq(panel, 0x44, 0x00);

	/* GIP_2 */
	panel_dsi_generic_send_seq(panel, 0x50, 0x01);
	panel_dsi_generic_send_seq(panel, 0x51, 0x23);
	panel_dsi_generic_send_seq(panel, 0x52, 0x45);
	panel_dsi_generic_send_seq(panel, 0x53, 0x67);
	panel_dsi_generic_send_seq(panel, 0x54, 0x89);
	panel_dsi_generic_send_seq(panel, 0x55, 0xab);
	panel_dsi_generic_send_seq(panel, 0x56, 0x01);
	panel_dsi_generic_send_seq(panel, 0x57, 0x23);
	panel_dsi_generic_send_seq(panel, 0x58, 0x45);
	panel_dsi_generic_send_seq(panel, 0x59, 0x67);
	panel_dsi_generic_send_seq(panel, 0x5a, 0x89);
	panel_dsi_generic_send_seq(panel, 0x5b, 0xab);
	panel_dsi_generic_send_seq(panel, 0x5c, 0xcd);
	panel_dsi_generic_send_seq(panel, 0x5d, 0xef);

	/* GIP_3 */
	panel_dsi_generic_send_seq(panel, 0x5e, 0x11);
	panel_dsi_generic_send_seq(panel, 0x5f, 0x02);
	panel_dsi_generic_send_seq(panel, 0x60, 0x00);
	panel_dsi_generic_send_seq(panel, 0x61, 0x07);
	panel_dsi_generic_send_seq(panel, 0x62, 0x06);
	panel_dsi_generic_send_seq(panel, 0x63, 0x0E);
	panel_dsi_generic_send_seq(panel, 0x64, 0x0F);
	panel_dsi_generic_send_seq(panel, 0x65, 0x0C);
	panel_dsi_generic_send_seq(panel, 0x66, 0x0D);
	panel_dsi_generic_send_seq(panel, 0x67, 0x02);
	panel_dsi_generic_send_seq(panel, 0x68, 0x02);
	panel_dsi_generic_send_seq(panel, 0x69, 0x02);
	panel_dsi_generic_send_seq(panel, 0x6a, 0x02);
	panel_dsi_generic_send_seq(panel, 0x6b, 0x02);
	panel_dsi_generic_send_seq(panel, 0x6c, 0x02);
	panel_dsi_generic_send_seq(panel, 0x6d, 0x02);
	panel_dsi_generic_send_seq(panel, 0x6e, 0x05);
	panel_dsi_generic_send_seq(panel, 0x6f, 0x02);
	panel_dsi_generic_send_seq(panel, 0x70, 0x02);
	panel_dsi_generic_send_seq(panel, 0x71, 0x02);
	panel_dsi_generic_send_seq(panel, 0x72, 0x02);
	panel_dsi_generic_send_seq(panel, 0x73, 0x05);
	panel_dsi_generic_send_seq(panel, 0x74, 0x01);
	panel_dsi_generic_send_seq(panel, 0x75, 0x02);
	panel_dsi_generic_send_seq(panel, 0x76, 0x00);
	panel_dsi_generic_send_seq(panel, 0x77, 0x07);
	panel_dsi_generic_send_seq(panel, 0x78, 0x06);
	panel_dsi_generic_send_seq(panel, 0x79, 0x0E);
	panel_dsi_generic_send_seq(panel, 0x7a, 0x0F);
	panel_dsi_generic_send_seq(panel, 0x7b, 0x0C);
	panel_dsi_generic_send_seq(panel, 0x7c, 0x0D);
	panel_dsi_generic_send_seq(panel, 0x7d, 0x02);
	panel_dsi_generic_send_seq(panel, 0x7e, 0x02);
	panel_dsi_generic_send_seq(panel, 0x7f, 0x02);
	panel_dsi_generic_send_seq(panel, 0x80, 0x02);
	panel_dsi_generic_send_seq(panel, 0x81, 0x02);
	panel_dsi_generic_send_seq(panel, 0x82, 0x02);
	panel_dsi_generic_send_seq(panel, 0x83, 0x02);
	panel_dsi_generic_send_seq(panel, 0x84, 0x02);
	panel_dsi_generic_send_seq(panel, 0x85, 0x02);
	panel_dsi_generic_send_seq(panel, 0x86, 0x02);
	panel_dsi_generic_send_seq(panel, 0x87, 0x02);
	panel_dsi_generic_send_seq(panel, 0x88, 0x02);
	panel_dsi_generic_send_seq(panel, 0x89, 0x05);
	panel_dsi_generic_send_seq(panel, 0x8A, 0x01);

	/* CMD Page 4 */
	panel_dsi_generic_send_seq(panel, 0xFF, 0x98, 0x81, 0x04);
	panel_dsi_generic_send_seq(panel, 0x6C, 0x15);
	panel_dsi_generic_send_seq(panel, 0x6E, 0x1A);
	panel_dsi_generic_send_seq(panel, 0x6F, 0x25);
	panel_dsi_generic_send_seq(panel, 0x3A, 0xA4);
	panel_dsi_generic_send_seq(panel, 0x8D, 0x20);
	panel_dsi_generic_send_seq(panel, 0x87, 0xBA);

	/* CMD Page 1 */
	panel_dsi_generic_send_seq(panel, 0xFF, 0x98, 0x81, 0x01);
	panel_dsi_generic_send_seq(panel, 0x22, 0x0A);
	panel_dsi_generic_send_seq(panel, 0x31, 0x00);
	panel_dsi_generic_send_seq(panel, 0x53, 0x86);
	panel_dsi_generic_send_seq(panel, 0x55, 0x90);
	panel_dsi_generic_send_seq(panel, 0x50, 0x75);
	panel_dsi_generic_send_seq(panel, 0x51, 0x75);
	panel_dsi_generic_send_seq(panel, 0x60, 0x1B);
	panel_dsi_generic_send_seq(panel, 0x61, 0x01);
	panel_dsi_generic_send_seq(panel, 0x62, 0x0C);
	panel_dsi_generic_send_seq(panel, 0x63, 0x00);

	/* Gamma P */
	panel_dsi_generic_send_seq(panel, 0xA0, 0x00);
	panel_dsi_generic_send_seq(panel, 0xA1, 0x16);
	panel_dsi_generic_send_seq(panel, 0xA2, 0x23);
	panel_dsi_generic_send_seq(panel, 0xA3, 0x12);
	panel_dsi_generic_send_seq(panel, 0xA4, 0x15);
	panel_dsi_generic_send_seq(panel, 0xA5, 0x28);
	panel_dsi_generic_send_seq(panel, 0xA6, 0x1D);
	panel_dsi_generic_send_seq(panel, 0xA7, 0x1E);
	panel_dsi_generic_send_seq(panel, 0xA8, 0x80);
	panel_dsi_generic_send_seq(panel, 0xA9, 0x1D);
	panel_dsi_generic_send_seq(panel, 0xAA, 0x29);
	panel_dsi_generic_send_seq(panel, 0xAB, 0x6E);
	panel_dsi_generic_send_seq(panel, 0xAC, 0x18);
	panel_dsi_generic_send_seq(panel, 0xAD, 0x14);
	panel_dsi_generic_send_seq(panel, 0xAE, 0x48);
	panel_dsi_generic_send_seq(panel, 0xAF, 0x1E);
	panel_dsi_generic_send_seq(panel, 0xB0, 0x26);
	panel_dsi_generic_send_seq(panel, 0xB1, 0x4D);
	panel_dsi_generic_send_seq(panel, 0xB2, 0x60);
	panel_dsi_generic_send_seq(panel, 0xB3, 0x39);

	/* Gamma N */
	panel_dsi_generic_send_seq(panel, 0xC0, 0x00);
	panel_dsi_generic_send_seq(panel, 0xC1, 0x15);
	panel_dsi_generic_send_seq(panel, 0xC2, 0x23);
	panel_dsi_generic_send_seq(panel, 0xC3, 0x12);
	panel_dsi_generic_send_seq(panel, 0xC4, 0x15);
	panel_dsi_generic_send_seq(panel, 0xC5, 0x27);
	panel_dsi_generic_send_seq(panel, 0xC6, 0x1D);
	panel_dsi_generic_send_seq(panel, 0xC7, 0x1F);
	panel_dsi_generic_send_seq(panel, 0xC8, 0x7F);
	panel_dsi_generic_send_seq(panel, 0xC9, 0x1D);
	panel_dsi_generic_send_seq(panel, 0xCA, 0x29);
	panel_dsi_generic_send_seq(panel, 0xCB, 0x6F);
	panel_dsi_generic_send_seq(panel, 0xCC, 0x19);
	panel_dsi_generic_send_seq(panel, 0xCD, 0x16);
	panel_dsi_generic_send_seq(panel, 0xCE, 0x49);
	panel_dsi_generic_send_seq(panel, 0xCF, 0x1F);
	panel_dsi_generic_send_seq(panel, 0xD0, 0x26);
	panel_dsi_generic_send_seq(panel, 0xD1, 0x4D);
	panel_dsi_generic_send_seq(panel, 0xD2, 0x60);
	panel_dsi_generic_send_seq(panel, 0xD3, 0x39);

	/* CMD Page 0 */
	panel_dsi_generic_send_seq(panel, 0xFF, 0x98, 0x81, 0x00);
	panel_dsi_generic_send_seq(panel, 0x35, 0x00);

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
	aic_delay_ms(5);

	return 0;
}

static int ili9881c_default_init(struct aic_panel *panel)
{
	int ret;

	panel_dsi_dcs_send_seq(panel, 0xFF, 0x98, 0x81, 0x03);

	//GIP_1
	panel_dsi_dcs_send_seq(panel, 0x10, 0x00);
	panel_dsi_dcs_send_seq(panel, 0x02, 0x00);
	panel_dsi_dcs_send_seq(panel, 0x03, 0x53);
	panel_dsi_dcs_send_seq(panel, 0x04, 0x53);
	panel_dsi_dcs_send_seq(panel, 0x05, 0x13);
	panel_dsi_dcs_send_seq(panel, 0x06, 0x04);
	panel_dsi_dcs_send_seq(panel, 0x07, 0x02);
	panel_dsi_dcs_send_seq(panel, 0x08, 0x02);
	panel_dsi_dcs_send_seq(panel, 0x09, 0x00);
	panel_dsi_dcs_send_seq(panel, 0x0a, 0x00);
	panel_dsi_dcs_send_seq(panel, 0x0b, 0x00);
	panel_dsi_dcs_send_seq(panel, 0x0c, 0x00);
	panel_dsi_dcs_send_seq(panel, 0x0d, 0x00);
	panel_dsi_dcs_send_seq(panel, 0x0e, 0xb0);
	panel_dsi_dcs_send_seq(panel, 0x0f, 0x00);
	panel_dsi_dcs_send_seq(panel, 0x10, 0x00);
	panel_dsi_dcs_send_seq(panel, 0x11, 0x00);
	panel_dsi_dcs_send_seq(panel, 0x12, 0x00);
	panel_dsi_dcs_send_seq(panel, 0x13, 0x00);
	panel_dsi_dcs_send_seq(panel, 0x14, 0x00);
	panel_dsi_dcs_send_seq(panel, 0x15, 0x00);
	panel_dsi_dcs_send_seq(panel, 0x16, 0x00);
	panel_dsi_dcs_send_seq(panel, 0x17, 0x00);
	panel_dsi_dcs_send_seq(panel, 0x18, 0x00);
	panel_dsi_dcs_send_seq(panel, 0x19, 0x00);
	panel_dsi_dcs_send_seq(panel, 0x1a, 0x00);
	panel_dsi_dcs_send_seq(panel, 0x1b, 0x00);
	panel_dsi_dcs_send_seq(panel, 0x1c, 0x00);
	panel_dsi_dcs_send_seq(panel, 0x1d, 0x00);

	panel_dsi_dcs_send_seq(panel, 0x1e, 0xc0);
	panel_dsi_dcs_send_seq(panel, 0x1f, 0x80);
	panel_dsi_dcs_send_seq(panel, 0x20, 0x02);
	panel_dsi_dcs_send_seq(panel, 0x21, 0x09);
	panel_dsi_dcs_send_seq(panel, 0x22, 0x00);
	panel_dsi_dcs_send_seq(panel, 0x23, 0x00);
	panel_dsi_dcs_send_seq(panel, 0x24, 0x00);
	panel_dsi_dcs_send_seq(panel, 0x25, 0x00);
	panel_dsi_dcs_send_seq(panel, 0x26, 0x00);
	panel_dsi_dcs_send_seq(panel, 0x27, 0x00);
	panel_dsi_dcs_send_seq(panel, 0x28, 0x55);
	panel_dsi_dcs_send_seq(panel, 0x29, 0x03);
	panel_dsi_dcs_send_seq(panel, 0x2a, 0x00);
	panel_dsi_dcs_send_seq(panel, 0x2b, 0x00);
	panel_dsi_dcs_send_seq(panel, 0x2c, 0x00);
	panel_dsi_dcs_send_seq(panel, 0x2d, 0x00);
	panel_dsi_dcs_send_seq(panel, 0x2e, 0x00);
	panel_dsi_dcs_send_seq(panel, 0x2f, 0x00);
	panel_dsi_dcs_send_seq(panel, 0x30, 0x00);
	panel_dsi_dcs_send_seq(panel, 0x31, 0x00);
	panel_dsi_dcs_send_seq(panel, 0x32, 0x00);
	panel_dsi_dcs_send_seq(panel, 0x33, 0x00);
	panel_dsi_dcs_send_seq(panel, 0x34, 0x00);
	panel_dsi_dcs_send_seq(panel, 0x35, 0x00);
	panel_dsi_dcs_send_seq(panel, 0x36, 0x00);
	panel_dsi_dcs_send_seq(panel, 0x37, 0x00);

	panel_dsi_dcs_send_seq(panel, 0x38, 0x3c);
	panel_dsi_dcs_send_seq(panel, 0x39, 0x00);
	panel_dsi_dcs_send_seq(panel, 0x3a, 0x00);
	panel_dsi_dcs_send_seq(panel, 0x3b, 0x00);
	panel_dsi_dcs_send_seq(panel, 0x3c, 0x00);
	panel_dsi_dcs_send_seq(panel, 0x3d, 0x00);
	panel_dsi_dcs_send_seq(panel, 0x3e, 0x3c);
	panel_dsi_dcs_send_seq(panel, 0x3f, 0x00);
	panel_dsi_dcs_send_seq(panel, 0x40, 0x00);
	panel_dsi_dcs_send_seq(panel, 0x41, 0x00);
	panel_dsi_dcs_send_seq(panel, 0x42, 0x00);
	panel_dsi_dcs_send_seq(panel, 0x43, 0x00);
	panel_dsi_dcs_send_seq(panel, 0x44, 0x3c);

	//GIP_2
	panel_dsi_dcs_send_seq(panel, 0x50, 0x01);
	panel_dsi_dcs_send_seq(panel, 0x51, 0x23);
	panel_dsi_dcs_send_seq(panel, 0x52, 0x45);
	panel_dsi_dcs_send_seq(panel, 0x53, 0x67);
	panel_dsi_dcs_send_seq(panel, 0x54, 0x89);
	panel_dsi_dcs_send_seq(panel, 0x55, 0xab);
	panel_dsi_dcs_send_seq(panel, 0x56, 0x01);
	panel_dsi_dcs_send_seq(panel, 0x57, 0x23);
	panel_dsi_dcs_send_seq(panel, 0x58, 0x45);
	panel_dsi_dcs_send_seq(panel, 0x59, 0x67);
	panel_dsi_dcs_send_seq(panel, 0x5a, 0x89);
	panel_dsi_dcs_send_seq(panel, 0x5b, 0xab);
	panel_dsi_dcs_send_seq(panel, 0x5c, 0xcd);
	panel_dsi_dcs_send_seq(panel, 0x5d, 0xef);

	//GIP_2
	panel_dsi_dcs_send_seq(panel, 0x5e, 0x01);
	panel_dsi_dcs_send_seq(panel, 0x5f, 0x08);
	panel_dsi_dcs_send_seq(panel, 0x60, 0x02);
	panel_dsi_dcs_send_seq(panel, 0x61, 0x02);
	panel_dsi_dcs_send_seq(panel, 0x62, 0x0a);
	panel_dsi_dcs_send_seq(panel, 0x63, 0x15);
	panel_dsi_dcs_send_seq(panel, 0x64, 0x14);
	panel_dsi_dcs_send_seq(panel, 0x65, 0x02);
	panel_dsi_dcs_send_seq(panel, 0x66, 0x11);
	panel_dsi_dcs_send_seq(panel, 0x67, 0x10);
	panel_dsi_dcs_send_seq(panel, 0x68, 0x02);
	panel_dsi_dcs_send_seq(panel, 0x69, 0x0f);
	panel_dsi_dcs_send_seq(panel, 0x6a, 0x0e);
	panel_dsi_dcs_send_seq(panel, 0x6b, 0x02);
	panel_dsi_dcs_send_seq(panel, 0x6c, 0x0d);
	panel_dsi_dcs_send_seq(panel, 0x6d, 0x0c);
	panel_dsi_dcs_send_seq(panel, 0x6e, 0x06);
	panel_dsi_dcs_send_seq(panel, 0x6f, 0x02);
	panel_dsi_dcs_send_seq(panel, 0x70, 0x02);
	panel_dsi_dcs_send_seq(panel, 0x71, 0x02);
	panel_dsi_dcs_send_seq(panel, 0x72, 0x02);
	panel_dsi_dcs_send_seq(panel, 0x73, 0x02);
	panel_dsi_dcs_send_seq(panel, 0x74, 0x02);

	panel_dsi_dcs_send_seq(panel, 0x75, 0x06);
	panel_dsi_dcs_send_seq(panel, 0x76, 0x02);
	panel_dsi_dcs_send_seq(panel, 0x77, 0x02);
	panel_dsi_dcs_send_seq(panel, 0x78, 0x0a);
	panel_dsi_dcs_send_seq(panel, 0x79, 0x15);
	panel_dsi_dcs_send_seq(panel, 0x7a, 0x14);
	panel_dsi_dcs_send_seq(panel, 0x7b, 0x02);
	panel_dsi_dcs_send_seq(panel, 0x7c, 0x10);
	panel_dsi_dcs_send_seq(panel, 0x7d, 0x11);
	panel_dsi_dcs_send_seq(panel, 0x7e, 0x02);
	panel_dsi_dcs_send_seq(panel, 0x7f, 0x0c);
	panel_dsi_dcs_send_seq(panel, 0x80, 0x0d);
	panel_dsi_dcs_send_seq(panel, 0x81, 0x02);
	panel_dsi_dcs_send_seq(panel, 0x82, 0x0e);
	panel_dsi_dcs_send_seq(panel, 0x83, 0x0f);
	panel_dsi_dcs_send_seq(panel, 0x84, 0x08);
	panel_dsi_dcs_send_seq(panel, 0x85, 0x02);
	panel_dsi_dcs_send_seq(panel, 0x86, 0x02);
	panel_dsi_dcs_send_seq(panel, 0x87, 0x02);
	panel_dsi_dcs_send_seq(panel, 0x88, 0x02);
	panel_dsi_dcs_send_seq(panel, 0x89, 0x02);
	panel_dsi_dcs_send_seq(panel, 0x8a, 0x02);

	//CMD_Page 4
	panel_dsi_dcs_send_seq(panel, 0xFF, 0x98, 0x81, 0x04);
	panel_dsi_dcs_send_seq(panel, 0x6c, 0x15);
	panel_dsi_dcs_send_seq(panel, 0x6e, 0x30);
	panel_dsi_dcs_send_seq(panel, 0x6f, 0x33);
	panel_dsi_dcs_send_seq(panel, 0x8d, 0x1f);
	panel_dsi_dcs_send_seq(panel, 0x87, 0xba);
	panel_dsi_dcs_send_seq(panel, 0x26, 0x76);
	panel_dsi_dcs_send_seq(panel, 0xb2, 0xd1);
	panel_dsi_dcs_send_seq(panel, 0x35, 0x1f);
	panel_dsi_dcs_send_seq(panel, 0x33, 0x14);
	panel_dsi_dcs_send_seq(panel, 0x3a, 0xa9);
	panel_dsi_dcs_send_seq(panel, 0x3b, 0x3d);
	panel_dsi_dcs_send_seq(panel, 0x38, 0x01);
	panel_dsi_dcs_send_seq(panel, 0x39, 0x00);

	//CMD_Page 1
	panel_dsi_dcs_send_seq(panel, 0xFF, 0x98, 0x81, 0x01);
	panel_dsi_dcs_send_seq(panel, 0x22, 0x0a);
	panel_dsi_dcs_send_seq(panel, 0x31, 0x00);
	panel_dsi_dcs_send_seq(panel, 0x40, 0x53);
	panel_dsi_dcs_send_seq(panel, 0x50, 0xc0);
	panel_dsi_dcs_send_seq(panel, 0x51, 0xc0);
	panel_dsi_dcs_send_seq(panel, 0x53, 0x47);
	panel_dsi_dcs_send_seq(panel, 0x55, 0x46);
	panel_dsi_dcs_send_seq(panel, 0x60, 0x28);
	panel_dsi_dcs_send_seq(panel, 0x2e, 0xc8);

	panel_dsi_dcs_send_seq(panel, 0xa0, 0x01);
	panel_dsi_dcs_send_seq(panel, 0xa1, 0x10);
	panel_dsi_dcs_send_seq(panel, 0xa2, 0x1b);
	panel_dsi_dcs_send_seq(panel, 0xa3, 0x0c);
	panel_dsi_dcs_send_seq(panel, 0xa4, 0x14);
	panel_dsi_dcs_send_seq(panel, 0xa5, 0x25);
	panel_dsi_dcs_send_seq(panel, 0xa6, 0x1a);
	panel_dsi_dcs_send_seq(panel, 0xa7, 0x1d);
	panel_dsi_dcs_send_seq(panel, 0xa8, 0x6b);
	panel_dsi_dcs_send_seq(panel, 0xa9, 0x1b);
	panel_dsi_dcs_send_seq(panel, 0xaa, 0x26);
	panel_dsi_dcs_send_seq(panel, 0xab, 0x5b);
	panel_dsi_dcs_send_seq(panel, 0xac, 0x1b);
	panel_dsi_dcs_send_seq(panel, 0xad, 0x1c);
	panel_dsi_dcs_send_seq(panel, 0xae, 0x4f);
	panel_dsi_dcs_send_seq(panel, 0xaf, 0x24);
	panel_dsi_dcs_send_seq(panel, 0xb0, 0x2a);
	panel_dsi_dcs_send_seq(panel, 0xb1, 0x4e);
	panel_dsi_dcs_send_seq(panel, 0xb2, 0x5f);
	panel_dsi_dcs_send_seq(panel, 0xb3, 0x39);

	panel_dsi_dcs_send_seq(panel, 0xc0, 0x0f);
	panel_dsi_dcs_send_seq(panel, 0xc1, 0x1b);
	panel_dsi_dcs_send_seq(panel, 0xc2, 0x27);
	panel_dsi_dcs_send_seq(panel, 0xc3, 0x16);
	panel_dsi_dcs_send_seq(panel, 0xc4, 0x14);
	panel_dsi_dcs_send_seq(panel, 0xc5, 0x2b);
	panel_dsi_dcs_send_seq(panel, 0xc6, 0x1d);
	panel_dsi_dcs_send_seq(panel, 0xc7, 0x21);
	panel_dsi_dcs_send_seq(panel, 0xc8, 0x6c);
	panel_dsi_dcs_send_seq(panel, 0xc9, 0x1b);
	panel_dsi_dcs_send_seq(panel, 0xca, 0x26);
	panel_dsi_dcs_send_seq(panel, 0xcb, 0x5b);
	panel_dsi_dcs_send_seq(panel, 0xcc, 0x1b);
	panel_dsi_dcs_send_seq(panel, 0xcd, 0x1b);
	panel_dsi_dcs_send_seq(panel, 0xce, 0x4f);
	panel_dsi_dcs_send_seq(panel, 0xcf, 0x24);
	panel_dsi_dcs_send_seq(panel, 0xd0, 0x2a);
	panel_dsi_dcs_send_seq(panel, 0xd1, 0x4e);
	panel_dsi_dcs_send_seq(panel, 0xd2, 0x5f);
	panel_dsi_dcs_send_seq(panel, 0xd3, 0x39);

	panel_dsi_dcs_send_seq(panel, 0xFF, 0x98, 0x81, 0x00);
	panel_dsi_dcs_send_seq(panel, 0x35, 0x00);
	panel_dsi_dcs_send_seq(panel, 0x11);
	aic_delay_ms(120);
	panel_dsi_dcs_send_seq(panel, 0x29);
	aic_delay_ms(20);

	ret = panel_dsi_dcs_exit_sleep_mode(panel);
	if (ret < 0) {
		pr_err("Failed to exit sleep mode: %d\n", ret);
		return ret;
	}

	aic_delay_ms(200);

	ret = panel_dsi_dcs_set_display_on(panel);
	if (ret < 0) {
		pr_err("Failed to set display on: %d\n", ret);
		return ret;
	}

	aic_delay_ms(120);

	return 0;
}

static int panel_enable(struct aic_panel *panel)
{
	struct ili9881c *ili9881c = panel_to_ili9881c(panel);

	panel_di_enable(panel, 0);

	aic_delay_ms(20);
	gpiod_direction_output(ili9881c->reset, 1);
	aic_delay_ms(1);
	gpiod_direction_output(ili9881c->reset, 0);
	aic_delay_ms(10);
	gpiod_direction_output(ili9881c->reset, 1);
	aic_delay_ms(120);

	panel_dsi_send_perpare(panel);

	if (panel->id == 0)
		ili9881c_default_init(panel);
	else if (panel->id == 1)
		tft050bp071_init(panel);
	else
		dev_err(panel->dev, "Invalid panel id %d\n", panel->id);

	panel_dsi_setup_realmode(panel);
	panel_de_timing_enable(panel, 0);
	panel_backlight_enable(panel, 0);
	return 0;
}

static int panel_disable(struct aic_panel *panel)
{
	struct ili9881c *ili9881c = panel_to_ili9881c(panel);

	panel_default_disable(panel);

	gpiod_direction_output(ili9881c->reset, 0);
	aic_delay_ms(10);

	return 0;
}

static struct aic_panel_funcs panel_funcs = {
	.disable = panel_disable,
	.unprepare = panel_default_unprepare,
	.prepare = panel_default_prepare,
	.enable = panel_enable,
	.get_video_mode = panel_default_get_video_mode,
	.register_callback = panel_register_callback,
};

/* Init the videomode parameter, dts will override the initial value. */
static struct videomode default_800x1280_vm = {
	.pixelclock = 70000000,
	.hactive = 800,
	.hfront_porch = 100,
	.hback_porch = 48,
	.hsync_len = 8,
	.vactive = 1280,
	.vfront_porch = 15,
	.vback_porch = 16,
	.vsync_len = 6,
	.flags = DISPLAY_FLAGS_HSYNC_LOW | DISPLAY_FLAGS_VSYNC_LOW |
		DISPLAY_FLAGS_DE_HIGH | DISPLAY_FLAGS_PIXDATA_POSEDGE
};

static struct panel_dsi default_dsi = {
	.format = DSI_FMT_RGB888,
	.mode = DSI_MOD_VID_BURST,
	.lane_num = 4,
};

/* Init the videomode parameter, dts will override the initial value. */
static struct videomode tft050bp071_768x1024_vm = {
	.pixelclock = 80000000,
	.hactive = 768,
	.hfront_porch = 100,
	.hback_porch = 200,
	.hsync_len = 100,
	.vactive = 1024,
	.vfront_porch = 20,
	.vback_porch = 100,
	.vsync_len = 80,
	.flags = DISPLAY_FLAGS_HSYNC_LOW | DISPLAY_FLAGS_VSYNC_LOW |
		DISPLAY_FLAGS_DE_HIGH | DISPLAY_FLAGS_PIXDATA_POSEDGE
};

static struct panel_dsi tft050bp071_dsi = {
	.format = DSI_FMT_RGB888,
	.mode = DSI_MOD_VID_PULSE,
	.lane_num = 4,
};

static int panel_bind(struct device *dev, struct device *master, void *data)
{
	struct panel_comp *p;
	struct ili9881c *ili9881c;
	int ret, panel_id;
	struct panel_dsi *dsi;
	struct videomode *vm;

	p = devm_kzalloc(dev, sizeof(*p), GFP_KERNEL);
	if (!p)
		return -ENOMEM;

	ili9881c = devm_kzalloc(dev, sizeof(*ili9881c), GFP_KERNEL);
	if (!ili9881c)
		return -ENOMEM;

	ili9881c->reset = devm_gpiod_get(dev, "reset", GPIOD_ASIS);
	if (IS_ERR(ili9881c->reset)) {
		dev_err(dev, "failed to get reset gpio\n");
		return PTR_ERR(ili9881c->reset);
	}

	if (panel_parse_dts(p, dev) < 0)
		return -1;

	ret = of_property_read_u32(dev->of_node, "panel-id", &panel_id);
	if (ret) {
		dev_dbg(dev, "failed to get panel id, use default id 0\n");
		panel_id = 0;
	}

	if (panel_id == 0) {
		dsi = &default_dsi;
		vm = &default_800x1280_vm;
	} else {
		dsi = &tft050bp071_dsi;
		vm = &tft050bp071_768x1024_vm;
	}

	p->panel.id = panel_id;
	p->panel.dsi = dsi;
	panel_init(p, dev, vm, &panel_funcs, ili9881c);

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
