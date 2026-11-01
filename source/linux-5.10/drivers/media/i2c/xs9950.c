// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2025-2026 ArtInChip Technology Co., Ltd.
 * Authors:  Matteo <duanmt@artinchip.com>
 *
 * XS9950 4-Channel Video Decoder Driver
 * Based on GM7150 driver structure and RTOS XS9950 implementation
 */

#include <linux/kernel.h>
#include <linux/version.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/fcntl.h>
#include <linux/mm.h>
#include <linux/miscdevice.h>
#include <linux/proc_fs.h>

#include <linux/fs.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <asm/uaccess.h>
#include <asm/io.h>
#include <linux/interrupt.h>
#include <linux/ioport.h>
#include <linux/string.h>
#include <linux/list.h>
#include <asm/delay.h>
#include <linux/timer.h>
#include <linux/delay.h>
#include <linux/poll.h>
#include <linux/kthread.h>
#include <linux/sched.h>
#include <uapi/linux/sched/types.h>

#include <linux/i2c.h>
#include <linux/i2c-dev.h>

#include <media/v4l2-async.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-device.h>
#include <media/v4l2-event.h>
#include <media/v4l2-fwnode.h>
#include <media/v4l2-subdev.h>

#include "xs9950.h"

#define DRV_NAME		"xs9950"

#define DEFAULT_VIN_CH		VIN1
#define DEFAULT_FORMAT		HD720P25
#define DEFAULT_V4L2_CODE	MEDIA_BUS_FMT_UYVY8_2X8

struct xs9950_v4l2_dev {
	struct i2c_client *i2c;
	struct gpio_desc *pwdn_gpio;

	struct v4l2_subdev sd;
	struct media_pad pad;
	struct v4l2_fwnode_endpoint ep; /* the parsed DT endpoint info */
	struct v4l2_mbus_framefmt fmt;
	struct v4l2_fract frame_interval;

	enum tp_vin_ch curr_ch;
	enum tp_fmt curr_fmt;
	enum tp_std curr_std;

	/* lock to protect all members below */
	struct mutex lock;
	bool streaming;
	bool free_run;
};

/* I2C write register (16bit address) */
static int xs9950_write_reg(struct i2c_client *client, u16 reg, u8 val)
{
	u8 buf[3];
	int ret;

	buf[0] = (reg >> 8) & 0xFF;  /* Register address high byte */
	buf[1] = reg & 0xFF;         /* Register address low byte */
	buf[2] = val;

	ret = i2c_master_send(client, buf, 3);
	if (ret != 3) {
		dev_err(&client->dev, "I2C write failed: reg=0x%04x, val=0x%02x, ret=%d\n",
			reg, val, ret);
		return -EIO;
	}

	return 0;
}

/* I2C read register (16bit address) */
static int xs9950_read_reg(struct i2c_client *client, u16 reg, u8 *val)
{
	struct i2c_msg msg[2];
	u8 reg_buf[2];
	u8 data_buf[1];
	int ret;

	reg_buf[0] = (reg >> 8) & 0xFF;  /* Register address high byte */
	reg_buf[1] = reg & 0xFF;         /* Register address low byte */

	msg[0].addr = client->addr;
	msg[0].flags = 0;
	msg[0].len = 2;
	msg[0].buf = reg_buf;

	msg[1].addr = client->addr;
	msg[1].flags = I2C_M_RD;
	msg[1].len = 1;
	msg[1].buf = data_buf;

	ret = i2c_transfer(client->adapter, msg, 2);
	if (ret != 2) {
		dev_err(&client->dev, "I2C read failed: reg=0x%04x, ret=%d\n", reg, ret);
		return -EIO;
	}

	*val = data_buf[0];
	return 0;
}

static void xs9950_power_on(struct xs9950_v4l2_dev *sensor)
{
	if (IS_ERR_OR_NULL(sensor->pwdn_gpio))
		return;

	/* PWDn high level power on */
	msleep(20);
	gpiod_set_value_cansleep(sensor->pwdn_gpio, 1);
	msleep(30);
}

static void xs9950_power_off(struct xs9950_v4l2_dev *sensor)
{
	if (IS_ERR_OR_NULL(sensor->pwdn_gpio))
		return;

	/* PWDn low level power off */
	gpiod_set_value_cansleep(sensor->pwdn_gpio, 0);
}

static inline enum tp_std xs9950_fmt_to_std(enum tp_fmt fmt)
{
	if (fmt <= CVBS_960H_N)
		return STD_CVBS;
	return STD_HDCCTV;
}

static void xs9950_set_resolution(struct xs9950_v4l2_dev *sensor, enum tp_fmt fmt)
{
	u16 h_active, v_active;
	u32 framerate;

	/* Video format parameters lookup table */
	static const struct {
		u16 width;
		u16 height;
		u32 fps;
	} fmt_params[] = {
		/* SD CVBS formats */
		[CVBS_PAL]     = { 720,  576, 25 },
		[CVBS_NTSC]    = { 720,  480, 30 },
		[CVBS_960H_P]  = { 960,  576, 25 },
		[CVBS_960H_N]  = { 960,  480, 30 },
		/* HD formats */
		[HD720P25]     = { 1280, 720, 25 },
		[HD720P30]     = { 1280, 720, 30 },
		[HD720P50]     = { 1280, 720, 50 },
		[HD720P60]     = { 1280, 720, 60 },
		[HD960P25]     = { 1280, 960, 25 },
		[HD960P30]     = { 1280, 960, 30 },
		[FHD1080P15]   = { 1920, 1080, 15 },
		[FHD1080P25]   = { 1920, 1080, 25 },
		[FHD1080P30]   = { 1920, 1080, 30 },
	};

	/* Use default 720P25 for invalid format */
	if (fmt >= ARRAY_SIZE(fmt_params)) {
		h_active = 1280;
		v_active = 720;
		framerate = 25;
	} else {
		h_active = fmt_params[fmt].width;
		v_active = fmt_params[fmt].height;
		framerate = fmt_params[fmt].fps;
	}

	/* Configure horizontal/vertical active pixel registers */
	xs9950_write_reg(sensor->i2c, 0x4310, (h_active >> 8) & 0xFF);
	xs9950_write_reg(sensor->i2c, 0x4311, h_active & 0xFF);
	xs9950_write_reg(sensor->i2c, 0x4312, (v_active >> 8) & 0xFF);
	xs9950_write_reg(sensor->i2c, 0x4313, v_active & 0xFF);

	sensor->fmt.width = h_active;
	sensor->fmt.height = v_active;
	sensor->frame_interval.numerator = 1;
	sensor->frame_interval.denominator = framerate;
}

static void xs9950_bt656_init(struct xs9950_v4l2_dev *sensor)
{
	/* BT656 base initialization sequence from RTOS driver */
	xs9950_write_reg(sensor->i2c, 0x4300, 0x05);
	xs9950_write_reg(sensor->i2c, 0x4300, 0x15);
	xs9950_write_reg(sensor->i2c, 0x4080, 0x07);
	xs9950_write_reg(sensor->i2c, 0x4119, 0x01);
	xs9950_write_reg(sensor->i2c, 0x0803, 0x00);
	xs9950_write_reg(sensor->i2c, 0x4020, 0x00);
	xs9950_write_reg(sensor->i2c, 0x080e, 0x00);
	xs9950_write_reg(sensor->i2c, 0x080e, 0x20);
	xs9950_write_reg(sensor->i2c, 0x080e, 0x28);
	xs9950_write_reg(sensor->i2c, 0x4020, 0x03);
	xs9950_write_reg(sensor->i2c, 0x0803, 0x0f);
	xs9950_write_reg(sensor->i2c, 0x0100, 0x35);
	xs9950_write_reg(sensor->i2c, 0x0104, 0x48);
	xs9950_write_reg(sensor->i2c, 0x0300, 0x3f);
	xs9950_write_reg(sensor->i2c, 0x0105, 0xe1);
	xs9950_write_reg(sensor->i2c, 0x0101, 0x42);
	xs9950_write_reg(sensor->i2c, 0x0102, 0x40);
	xs9950_write_reg(sensor->i2c, 0x0116, 0x3c);
	xs9950_write_reg(sensor->i2c, 0x0117, 0x23);
	xs9950_write_reg(sensor->i2c, 0x0333, 0x09);
	xs9950_write_reg(sensor->i2c, 0x0337, 0xd9);
	xs9950_write_reg(sensor->i2c, 0x0338, 0x0a);
	xs9950_write_reg(sensor->i2c, 0x01bf, 0x4e);
	xs9950_write_reg(sensor->i2c, 0x010e, 0x78);
	xs9950_write_reg(sensor->i2c, 0x010f, 0x92);
	xs9950_write_reg(sensor->i2c, 0x0110, 0x70);
	xs9950_write_reg(sensor->i2c, 0x0111, 0x40);
	xs9950_write_reg(sensor->i2c, 0x01e1, 0xff);
	xs9950_write_reg(sensor->i2c, 0x0314, 0x66);
	xs9950_write_reg(sensor->i2c, 0x0130, 0x10);
	xs9950_write_reg(sensor->i2c, 0x0315, 0x23);
	xs9950_write_reg(sensor->i2c, 0x0b64, 0x02);
	xs9950_write_reg(sensor->i2c, 0x01e2, 0x03);
	xs9950_write_reg(sensor->i2c, 0x0b55, 0x80);
	xs9950_write_reg(sensor->i2c, 0x0b56, 0x00);
	xs9950_write_reg(sensor->i2c, 0x0b59, 0x04);
	xs9950_write_reg(sensor->i2c, 0x0b5a, 0x01);
	xs9950_write_reg(sensor->i2c, 0x0b5c, 0x07);
	xs9950_write_reg(sensor->i2c, 0x0b5e, 0x05);
	xs9950_write_reg(sensor->i2c, 0x0b4b, 0x10);
	xs9950_write_reg(sensor->i2c, 0x0b4e, 0x05);
	xs9950_write_reg(sensor->i2c, 0x0b51, 0x21);
	xs9950_write_reg(sensor->i2c, 0x0b30, 0xbc);
	xs9950_write_reg(sensor->i2c, 0x0b31, 0x19);
	xs9950_write_reg(sensor->i2c, 0x0b15, 0x03);
	xs9950_write_reg(sensor->i2c, 0x0b16, 0x03);
	xs9950_write_reg(sensor->i2c, 0x0b17, 0x03);
	xs9950_write_reg(sensor->i2c, 0x0b07, 0x03);
	xs9950_write_reg(sensor->i2c, 0x0b08, 0x05);
	xs9950_write_reg(sensor->i2c, 0x0b1a, 0x10);
	xs9950_write_reg(sensor->i2c, 0x0158, 0x01);
	xs9950_write_reg(sensor->i2c, 0x0a88, 0x20);
	xs9950_write_reg(sensor->i2c, 0x0a61, 0x09);
	xs9950_write_reg(sensor->i2c, 0x0a62, 0x00);
	xs9950_write_reg(sensor->i2c, 0x0a63, 0x0e);
	xs9950_write_reg(sensor->i2c, 0x0a64, 0x00);
	xs9950_write_reg(sensor->i2c, 0x0a65, 0xfc);
	xs9950_write_reg(sensor->i2c, 0x0a67, 0xe5);
	xs9950_write_reg(sensor->i2c, 0x0a69, 0xef);
	xs9950_write_reg(sensor->i2c, 0x0a6b, 0x1b);
	xs9950_write_reg(sensor->i2c, 0x0a6d, 0x2f);
	xs9950_write_reg(sensor->i2c, 0x0a6f, 0x00);
	xs9950_write_reg(sensor->i2c, 0x0a71, 0xc2);
	xs9950_write_reg(sensor->i2c, 0x0a72, 0xff);
	xs9950_write_reg(sensor->i2c, 0x0a73, 0xd0);
	xs9950_write_reg(sensor->i2c, 0x0a74, 0xff);
	xs9950_write_reg(sensor->i2c, 0x0a75, 0x29);
	xs9950_write_reg(sensor->i2c, 0x0a77, 0x57);
	xs9950_write_reg(sensor->i2c, 0x0a78, 0x00);
	xs9950_write_reg(sensor->i2c, 0x0a79, 0x10);
	xs9950_write_reg(sensor->i2c, 0x0a7a, 0x00);
	xs9950_write_reg(sensor->i2c, 0x0a7b, 0xaa);
	xs9950_write_reg(sensor->i2c, 0x0a7d, 0xb2);
	xs9950_write_reg(sensor->i2c, 0x0a7f, 0x24);
	xs9950_write_reg(sensor->i2c, 0x0a80, 0x00);
	xs9950_write_reg(sensor->i2c, 0x0a81, 0x69);
	xs9950_write_reg(sensor->i2c, 0x0a82, 0x00);
	xs9950_write_reg(sensor->i2c, 0x0802, 0x02);
	xs9950_write_reg(sensor->i2c, 0x0501, 0x81);
	xs9950_write_reg(sensor->i2c, 0x0b74, 0xfc);
	xs9950_write_reg(sensor->i2c, 0x01dc, 0x01);
	xs9950_write_reg(sensor->i2c, 0x0804, 0x04);
	xs9950_write_reg(sensor->i2c, 0x4018, 0x01);
	xs9950_write_reg(sensor->i2c, 0x0b56, 0x01);
	xs9950_write_reg(sensor->i2c, 0x0b73, 0x02);
	xs9950_write_reg(sensor->i2c, 0x4210, 0x0c);
	xs9950_write_reg(sensor->i2c, 0x420b, 0x2f);
	xs9950_write_reg(sensor->i2c, 0x0504, 0x89);
	xs9950_write_reg(sensor->i2c, 0x0507, 0x0b);
	xs9950_write_reg(sensor->i2c, 0x0503, 0x00);
	xs9950_write_reg(sensor->i2c, 0x0502, 0x00);
	xs9950_write_reg(sensor->i2c, 0x015a, 0x00);
	xs9950_write_reg(sensor->i2c, 0x015b, 0x24);
	xs9950_write_reg(sensor->i2c, 0x015c, 0x80);
	xs9950_write_reg(sensor->i2c, 0x015d, 0x16);
	xs9950_write_reg(sensor->i2c, 0x015e, 0xd0);
	xs9950_write_reg(sensor->i2c, 0x015f, 0x02);
	xs9950_write_reg(sensor->i2c, 0x0160, 0xee);
	xs9950_write_reg(sensor->i2c, 0x0161, 0x02);
	xs9950_write_reg(sensor->i2c, 0x0165, 0x00);
	xs9950_write_reg(sensor->i2c, 0x0166, 0x0f);
	xs9950_write_reg(sensor->i2c, 0x4030, 0x15);
	xs9950_write_reg(sensor->i2c, 0x4134, 0x0a);
	xs9950_write_reg(sensor->i2c, 0x0803, 0x0f);
	xs9950_write_reg(sensor->i2c, 0x4412, 0x01);
	xs9950_write_reg(sensor->i2c, 0x0803, 0x1f);
	xs9950_write_reg(sensor->i2c, 0x10e3, 0x04);
	xs9950_write_reg(sensor->i2c, 0x10eb, 0xfd);
	xs9950_write_reg(sensor->i2c, 0x0800, 0x07);
	xs9950_write_reg(sensor->i2c, 0x0805, 0x07);
	xs9950_write_reg(sensor->i2c, 0x01c4, 0x11);
	xs9950_write_reg(sensor->i2c, 0x01ce, 0x01);
}

static void xs9950_bt656_fmt(struct xs9950_v4l2_dev *sensor, enum tp_fmt fmt)
{
	u8 sta_0507;

	/* Format-specific configuration for 720P */
	if ((fmt == HD720P25) || (fmt == HD720P30) || (fmt == HD720P50) || (fmt == HD720P60)) {
		xs9950_write_reg(sensor->i2c, 0x060b, 0x00);
		xs9950_write_reg(sensor->i2c, 0x0627, 0x14);
		xs9950_write_reg(sensor->i2c, 0x010c, 0x00);
		xs9950_write_reg(sensor->i2c, 0x0800, 0x05);
		xs9950_write_reg(sensor->i2c, 0x0805, 0x05);

		if (fmt == HD720P60) {
			xs9950_write_reg(sensor->i2c, 0x0b50, 0x08);
			xs9950_write_reg(sensor->i2c, 0x0b4e, 0x3f);
			xs9950_write_reg(sensor->i2c, 0x0b50, 0x3f);
			xs9950_write_reg(sensor->i2c, 0x0b51, 0x52);
			xs9950_write_reg(sensor->i2c, 0x4201, 0x00);
			xs9950_write_reg(sensor->i2c, 0x4203, 0x00);
			xs9950_write_reg(sensor->i2c, 0x4202, 0x00);
			xs9950_write_reg(sensor->i2c, 0x4204, 0x00);
			xs9950_write_reg(sensor->i2c, 0x0b4e, 0x06);
			xs9950_write_reg(sensor->i2c, 0x0b50, 0x07);
			xs9950_write_reg(sensor->i2c, 0x0b51, 0x21);
		} else {
			xs9950_write_reg(sensor->i2c, 0x0b50, 0x08);
		}

		xs9950_write_reg(sensor->i2c, 0x0e08, 0x00);

		if (fmt == HD720P25)
			xs9950_write_reg(sensor->i2c, 0x010d, 0x40);
		else if (fmt == HD720P30)
			xs9950_write_reg(sensor->i2c, 0x010d, 0x41);
		else if (fmt == HD720P50)
			xs9950_write_reg(sensor->i2c, 0x010d, 0x42);
		else if (fmt == HD720P60)
			xs9950_write_reg(sensor->i2c, 0x010d, 0x43);

		xs9950_write_reg(sensor->i2c, 0x010c, 0x01);
		xs9950_write_reg(sensor->i2c, 0x0121, 0x6a);
		xs9950_write_reg(sensor->i2c, 0x0122, 0x5b);
		xs9950_write_reg(sensor->i2c, 0x0130, 0x10);
		xs9950_write_reg(sensor->i2c, 0x01a9, 0x00);
		xs9950_write_reg(sensor->i2c, 0x01aa, 0x04);
		xs9950_write_reg(sensor->i2c, 0x0156, 0x00);
		xs9950_write_reg(sensor->i2c, 0x0157, 0x08);
		xs9950_write_reg(sensor->i2c, 0x0105, 0xe1);
		xs9950_write_reg(sensor->i2c, 0x0101, 0x42);
		xs9950_write_reg(sensor->i2c, 0x0102, 0x40);
		xs9950_write_reg(sensor->i2c, 0x0116, 0x3c);
		xs9950_write_reg(sensor->i2c, 0x0117, 0x23);
		xs9950_write_reg(sensor->i2c, 0x01e2, 0x03);
		xs9950_write_reg(sensor->i2c, 0x420b, 0x2f);
		xs9950_write_reg(sensor->i2c, 0x0100, 0x38);
		xs9950_write_reg(sensor->i2c, 0x0106, 0x80);
		xs9950_write_reg(sensor->i2c, 0x0107, 0x00);
		xs9950_write_reg(sensor->i2c, 0x0108, 0x80);
		xs9950_write_reg(sensor->i2c, 0x0109, 0x00);
		xs9950_write_reg(sensor->i2c, 0x011d, 0x17);
		xs9950_write_reg(sensor->i2c, 0x0e08, 0x01);
		xs9950_write_reg(sensor->i2c, 0x0a60, 0x04);
		xs9950_write_reg(sensor->i2c, 0x0156, 0x50);
		xs9950_write_reg(sensor->i2c, 0x0157, 0x07);
		xs9950_write_reg(sensor->i2c, 0x0156, 0x00);
		xs9950_write_reg(sensor->i2c, 0x0157, 0x08);
		xs9950_write_reg(sensor->i2c, 0x0158, 0x01);

		if (fmt == HD720P25)
			xs9950_write_reg(sensor->i2c, 0x0503, 0x00);
		else if (fmt == HD720P30)
			xs9950_write_reg(sensor->i2c, 0x0503, 0x01);
		else if (fmt == HD720P50)
			xs9950_write_reg(sensor->i2c, 0x0503, 0x02);
		else if (fmt == HD720P60)
			xs9950_write_reg(sensor->i2c, 0x0503, 0x03);

		xs9950_write_reg(sensor->i2c, 0x015a, 0x8b);
		xs9950_write_reg(sensor->i2c, 0x015b, 0x0e);
		xs9950_write_reg(sensor->i2c, 0x015e, 0xd0);
		xs9950_write_reg(sensor->i2c, 0x015f, 0x02);
		xs9950_write_reg(sensor->i2c, 0x0a60, 0x01);
	}
	/* CVBS PAL/NTSC configuration - Critical for analog camera detection */
	else if ((fmt == CVBS_PAL) || (fmt == CVBS_960H_P) || (fmt == CVBS_NTSC) || (fmt == CVBS_960H_N)) {
		xs9950_write_reg(sensor->i2c, 0x0e08, 0x00);
		xs9950_write_reg(sensor->i2c, 0x0102, 0x40);
		xs9950_write_reg(sensor->i2c, 0x0105, 0xe1);
		xs9950_write_reg(sensor->i2c, 0x0108, 0x80);
		xs9950_write_reg(sensor->i2c, 0x080d, 0x00);
		xs9950_write_reg(sensor->i2c, 0x0158, 0x01);
		xs9950_write_reg(sensor->i2c, 0x0a60, 0x00);
		xs9950_write_reg(sensor->i2c, 0x0a88, 0x20);
		xs9950_write_reg(sensor->i2c, 0x0121, 0x5a);
		xs9950_write_reg(sensor->i2c, 0x0122, 0x4b);
		xs9950_write_reg(sensor->i2c, 0x0125, 0x73);
		xs9950_write_reg(sensor->i2c, 0x010c, 0x00);
		xs9950_write_reg(sensor->i2c, 0x420b, 0x2f);
		xs9950_write_reg(sensor->i2c, 0x0100, 0x38);
		xs9950_write_reg(sensor->i2c, 0x0a60, 0x00);
		xs9950_write_reg(sensor->i2c, 0x0803, 0x1f);
		xs9950_write_reg(sensor->i2c, 0x080e, 0x1f);
		xs9950_write_reg(sensor->i2c, 0x0803, 0x1f);
		xs9950_write_reg(sensor->i2c, 0x080e, 0x3f);
		xs9950_write_reg(sensor->i2c, 0x080e, 0x3f);
		xs9950_write_reg(sensor->i2c, 0x0e08, 0x01);
		xs9950_write_reg(sensor->i2c, 0x0800, 0x04);
		xs9950_write_reg(sensor->i2c, 0x0805, 0x07);
		xs9950_write_reg(sensor->i2c, 0x0800, 0x04);
		xs9950_write_reg(sensor->i2c, 0x0800, 0x06);
		xs9950_write_reg(sensor->i2c, 0x0805, 0x0e);
		xs9950_write_reg(sensor->i2c, 0x0b50, 0x08);
		xs9950_write_reg(sensor->i2c, 0x0e08, 0x00);
		xs9950_write_reg(sensor->i2c, 0x010c, 0x00);
		xs9950_write_reg(sensor->i2c, 0x0305, 0xe1);
		xs9950_write_reg(sensor->i2c, 0x033b, 0x02);
		xs9950_write_reg(sensor->i2c, 0x0511, 0x00);
		xs9950_write_reg(sensor->i2c, 0x0158, 0x03);
		xs9950_write_reg(sensor->i2c, 0x0a60, 0x00);
		xs9950_write_reg(sensor->i2c, 0x0a88, 0x20);
		xs9950_write_reg(sensor->i2c, 0x0121, 0x5a);
		xs9950_write_reg(sensor->i2c, 0x0122, 0x4b);
		xs9950_write_reg(sensor->i2c, 0x0125, 0x73);
		xs9950_write_reg(sensor->i2c, 0x0126, 0x4c);
		xs9950_write_reg(sensor->i2c, 0x0505, 0x00);
		xs9950_write_reg(sensor->i2c, 0x0506, 0x00);
		xs9950_write_reg(sensor->i2c, 0x0106, 0x80);
		xs9950_write_reg(sensor->i2c, 0x0107, 0x00);
		xs9950_write_reg(sensor->i2c, 0x0108, 0x80);
		xs9950_write_reg(sensor->i2c, 0x0109, 0x00);

		if ((fmt == CVBS_PAL) || (fmt == CVBS_960H_P)) {
			xs9950_write_reg(sensor->i2c, 0x010a, 0x04);
			xs9950_write_reg(sensor->i2c, 0x010a, 0x04);
		} else {
			xs9950_write_reg(sensor->i2c, 0x010a, 0x12);
			xs9950_write_reg(sensor->i2c, 0x010a, 0x12);
		}

		xs9950_write_reg(sensor->i2c, 0x010b, 0x02);
		xs9950_write_reg(sensor->i2c, 0x010b, 0x02);
		xs9950_write_reg(sensor->i2c, 0x033a, 0x00);
		xs9950_write_reg(sensor->i2c, 0x0e08, 0x01);
		xs9950_write_reg(sensor->i2c, 0x0102, 0x40);
		xs9950_write_reg(sensor->i2c, 0x0105, 0xe1);
		xs9950_write_reg(sensor->i2c, 0x0108, 0x80);
		xs9950_write_reg(sensor->i2c, 0x0156, 0x00);
		xs9950_write_reg(sensor->i2c, 0x0157, 0x00);
		xs9950_write_reg(sensor->i2c, 0x0156, 0x00);
		xs9950_write_reg(sensor->i2c, 0x0157, 0x00);

		/* Read and modify 0x0507 register for 720H/960H mode */
		if (xs9950_read_reg(sensor->i2c, 0x0507, &sta_0507))
			return;

		if ((fmt == CVBS_PAL) || (fmt == CVBS_NTSC))
			sta_0507 &= ~(0x01 << 4); /* 720H mode */
		else
			sta_0507 |= (0x01 << 4); /* 960H mode */

		xs9950_write_reg(sensor->i2c, 0x0507, sta_0507);

		if ((fmt == CVBS_PAL) || (fmt == CVBS_960H_P))
			xs9950_write_reg(sensor->i2c, 0x0503, 0x48);
		else
			xs9950_write_reg(sensor->i2c, 0x0503, 0x60);

		xs9950_write_reg(sensor->i2c, 0x015a, 0xc0);
		xs9950_write_reg(sensor->i2c, 0x015b, 0x03);

		if ((fmt == CVBS_PAL) || (fmt == CVBS_960H_P)) {
			xs9950_write_reg(sensor->i2c, 0x015c, 0x00);
			xs9950_write_reg(sensor->i2c, 0x015d, 0x36);
			xs9950_write_reg(sensor->i2c, 0x015e, 0x20);
			xs9950_write_reg(sensor->i2c, 0x015f, 0x01);
		} else {
			xs9950_write_reg(sensor->i2c, 0x015c, 0x49);
			xs9950_write_reg(sensor->i2c, 0x015d, 0x00);
			xs9950_write_reg(sensor->i2c, 0x015e, 0xf0);
			xs9950_write_reg(sensor->i2c, 0x015f, 0x00);
		}

		xs9950_write_reg(sensor->i2c, 0x0160, 0x39);
		xs9950_write_reg(sensor->i2c, 0x0161, 0x01);
		xs9950_write_reg(sensor->i2c, 0x0165, 0xff);

		if ((fmt == CVBS_PAL) || (fmt == CVBS_960H_P))
			xs9950_write_reg(sensor->i2c, 0x0166, 0x05);
		else
			xs9950_write_reg(sensor->i2c, 0x0166, 0x00);

		/* Additional config for PAL/NTSC */
		if ((fmt == CVBS_PAL) || (fmt == CVBS_960H_P)) {
			xs9950_write_reg(sensor->i2c, 0x0336, 0xde);
			xs9950_write_reg(sensor->i2c, 0x033b, 0x02);
			xs9950_write_reg(sensor->i2c, 0x0316, 0x48);
		} else {
			xs9950_write_reg(sensor->i2c, 0x0316, 0x48);
			xs9950_write_reg(sensor->i2c, 0x0336, 0xde);
			xs9950_write_reg(sensor->i2c, 0x0337, 0x01);
		}
	}
	/* 1080P configuration */
	else if ((fmt == FHD1080P25) || (fmt == FHD1080P30)) {
		xs9950_write_reg(sensor->i2c, 0x060b, 0x00);
		xs9950_write_reg(sensor->i2c, 0x0627, 0x14);
		xs9950_write_reg(sensor->i2c, 0x010c, 0x00);
		xs9950_write_reg(sensor->i2c, 0x0800, 0x05);
		xs9950_write_reg(sensor->i2c, 0x0805, 0x05);
		xs9950_write_reg(sensor->i2c, 0x0b50, 0x08);
		xs9950_write_reg(sensor->i2c, 0x0e08, 0x00);

		if (fmt == FHD1080P25)
			xs9950_write_reg(sensor->i2c, 0x010d, 0x44);
		else
			xs9950_write_reg(sensor->i2c, 0x010d, 0x45);

		xs9950_write_reg(sensor->i2c, 0x010c, 0x01);
		xs9950_write_reg(sensor->i2c, 0x0121, 0x6a);
		xs9950_write_reg(sensor->i2c, 0x0122, 0x5b);
		xs9950_write_reg(sensor->i2c, 0x0130, 0x10);
		xs9950_write_reg(sensor->i2c, 0x01a9, 0x00);
		xs9950_write_reg(sensor->i2c, 0x01aa, 0x04);
		xs9950_write_reg(sensor->i2c, 0x0156, 0x00);
		xs9950_write_reg(sensor->i2c, 0x0157, 0x08);
		xs9950_write_reg(sensor->i2c, 0x0105, 0xe1);
		xs9950_write_reg(sensor->i2c, 0x0101, 0x42);
		xs9950_write_reg(sensor->i2c, 0x0102, 0x40);
		xs9950_write_reg(sensor->i2c, 0x0116, 0x3c);
		xs9950_write_reg(sensor->i2c, 0x0117, 0x23);
		xs9950_write_reg(sensor->i2c, 0x01e2, 0x03);
		xs9950_write_reg(sensor->i2c, 0x420b, 0x2f);
		xs9950_write_reg(sensor->i2c, 0x0100, 0x38);
		xs9950_write_reg(sensor->i2c, 0x0106, 0x80);
		xs9950_write_reg(sensor->i2c, 0x0107, 0x00);
		xs9950_write_reg(sensor->i2c, 0x0108, 0x80);
		xs9950_write_reg(sensor->i2c, 0x0109, 0x00);
		xs9950_write_reg(sensor->i2c, 0x010a, 0x1b);
		xs9950_write_reg(sensor->i2c, 0x010b, 0x01);
		xs9950_write_reg(sensor->i2c, 0x011d, 0x17);
		xs9950_write_reg(sensor->i2c, 0x0e08, 0x01);
		xs9950_write_reg(sensor->i2c, 0x0a60, 0x04);
		xs9950_write_reg(sensor->i2c, 0x0156, 0x50);
		xs9950_write_reg(sensor->i2c, 0x0157, 0x07);
		xs9950_write_reg(sensor->i2c, 0x0156, 0x00);
		xs9950_write_reg(sensor->i2c, 0x0157, 0x08);
		xs9950_write_reg(sensor->i2c, 0x0158, 0x01);

		if (fmt == FHD1080P25)
			xs9950_write_reg(sensor->i2c, 0x0503, 0x04);
		else
			xs9950_write_reg(sensor->i2c, 0x0503, 0x05);

		xs9950_write_reg(sensor->i2c, 0x015a, 0xd1);
		xs9950_write_reg(sensor->i2c, 0x015b, 0x15);
		xs9950_write_reg(sensor->i2c, 0x015e, 0x38);
		xs9950_write_reg(sensor->i2c, 0x015f, 0x04);
		xs9950_write_reg(sensor->i2c, 0x0160, 0x65);
		xs9950_write_reg(sensor->i2c, 0x0161, 0x04);

		if (fmt == FHD1080P25)
			xs9950_write_reg(sensor->i2c, 0x0165, 0x44);
		else
			xs9950_write_reg(sensor->i2c, 0x0165, 0x45);

		xs9950_write_reg(sensor->i2c, 0x0166, 0x0f);
		xs9950_write_reg(sensor->i2c, 0x0a60, 0x01);
	}
}

static void xs9950_afe_init(struct xs9950_v4l2_dev *sensor, enum tp_vin_ch ch)
{
	u8 ch_val;

	/* AFE power enable */
	xs9950_write_reg(sensor->i2c, 0x470a, 0x01);
	msleep(1);

	/* AFE clock configuration */
	xs9950_write_reg(sensor->i2c, 0x4704, 0x02);
	xs9950_write_reg(sensor->i2c, 0x4705, 0x01);

	/* Channel selection */
	switch (ch) {
	case VIN1:
		ch_val = 0x02;
		break;
	case VIN2:
		ch_val = 0x00;
		break;
	case VIN3:
		ch_val = 0x04;
		break;
	case VIN4:
		ch_val = 0x06;
		break;
	default:
		ch_val = 0x02;
	}

	/* Select video input channel - CRITICAL for signal detection! */
	xs9950_write_reg(sensor->i2c, 0x4200, ch_val);

	/* Configure EQ, Clamp, LPF */
	xs9950_write_reg(sensor->i2c, 0x4700, 0x03);
	xs9950_write_reg(sensor->i2c, 0x4701, 0x00);
	xs9950_write_reg(sensor->i2c, 0x4702, 0x02);
	xs9950_write_reg(sensor->i2c, 0x4703, 0x01);

	dev_info(&sensor->i2c->dev, "AFE init done: channel=%d, 0x4200=0x%02x\n", ch, ch_val);
}

static void xs9950_sensor_init(struct xs9950_v4l2_dev *sensor,
			       enum tp_vin_ch ch, enum tp_fmt fmt)
{
	enum tp_std std = xs9950_fmt_to_std(fmt);

	sensor->curr_ch = ch;
	sensor->curr_fmt = fmt;
	sensor->curr_std = std;

	/* Global reset */
	xs9950_write_reg(sensor->i2c, 0x0000, 0x01);
	msleep(2);
	xs9950_write_reg(sensor->i2c, 0x0000, 0x00);
	msleep(20);

	/* AFE front-end initialization */
	xs9950_afe_init(sensor, ch);

	/* BT656 initialization */
	xs9950_bt656_init(sensor);
	xs9950_bt656_fmt(sensor, fmt);

	/* Set resolution */
	xs9950_set_resolution(sensor, fmt);

	/* Enable video output */
	xs9950_write_reg(sensor->i2c, 0x4000, 0x01);

	/* Auto detect */
	xs9950_write_reg(sensor->i2c, 0x010c, 0x00);

	dev_info(&sensor->i2c->dev, "XS9950 sensor init done: ch=%d, fmt=%d, std=%d\n",
		 ch, fmt, std);
}

static int xs9950_chipid_check(struct xs9950_v4l2_dev *sensor)
{
	u8 id_h = 0, id_l = 0;

	if (xs9950_read_reg(sensor->i2c, XS9950_DEVICE_ID_H, &id_h) ||
	    xs9950_read_reg(sensor->i2c, XS9950_DEVICE_ID_L, &id_l))
		return -EIO;

	if (((id_h << 8) | id_l) != XS9950_CHIP_ID) {
		dev_err(&sensor->i2c->dev, "Invalid chip ID: 0x%02x%02x (expect 0x%04x)\n",
			id_h, id_l, XS9950_CHIP_ID);
		return -ENODEV;
	}

	dev_info(&sensor->i2c->dev, "Chip ID check pass: 0x%04x\n", XS9950_CHIP_ID);
	return 0;
}

static int xs9950_query_status(struct xs9950_v4l2_dev *sensor,
			       struct xs9950_ch_status *status)
{
	u8 reg_val;

	if (!status)
		return -EINVAL;

	memset(status, 0, sizeof(*status));

	/* Read VIDEO_STATUS_REGISTER_1 (0x00) */
	if (xs9950_read_reg(sensor->i2c, XS9950_REG_VIDEO_STATUS_1, &reg_val))
		return -EIO;

	status->is_hd = !!(reg_val & XS9950_STS1_HD_SD);
	status->sd_burst_detected = !!(reg_val & XS9950_STS1_SD_BURST_DETECT);
	status->free_run = !!(reg_val & XS9950_STS1_FREE_RUN);
	status->hspll_locked_fe = !!(reg_val & XS9950_STS1_HSPLL_LOCKED_FE);
	status->hspll_locked_be = !!(reg_val & XS9950_STS1_HSPLL_LOCKED_BE);
	status->vsync_locked = !!(reg_val & XS9950_STS1_VSYNC_LOCKED);
	status->color_kill = !!(reg_val & XS9950_STS1_COLOR_KILL);

	sensor->free_run = status->free_run;

	/* Read HD_VIDEO_STANDARD_READBACK (0x01) */
	if (xs9950_read_reg(sensor->i2c, XS9950_REG_HD_VIDEO_STD_RB, &status->hd_std_raw))
		return -EIO;
	status->hd_std_type = status->hd_std_raw & XS9950_HD_STD_TYPE_MASK;
	status->hd_std_format = status->hd_std_raw & XS9950_HD_STD_FORMAT_MASK;

	/* Read SD_VIDEO_STANDARD_READBACK (0x02) */
	if (xs9950_read_reg(sensor->i2c, XS9950_REG_SD_VIDEO_STD_RB, &status->sd_std_raw))
		return -EIO;
	status->sd_std_raw &= XS9950_SD_STD_MASK;

	/* Read VSYNC_STATUS_REGISTER_2 (0x03) */
	if (xs9950_read_reg(sensor->i2c, XS9950_REG_VSYNC_STATUS_2, &reg_val))
		return -EIO;
	status->has_pedestal = !!(reg_val & XS9950_VSYNC2_PEDESTAL);
	status->is_fifty_hz = !!(reg_val & XS9950_VSYNC2_FIFTY_HZ);

	/* Read SIGNAL_LOSS_FACT (0x04~0x05) */
	if (xs9950_read_reg(sensor->i2c, XS9950_REG_SIGNAL_LOSS_MSB, &reg_val))
		return -EIO;
	status->signal_loss = (reg_val << 8);
	if (xs9950_read_reg(sensor->i2c, XS9950_REG_SIGNAL_LOSS_LSB, &reg_val))
		return -EIO;
	status->signal_loss |= reg_val;

	/* Read SYNC_DEPTH (0x06~0x07) */
	if (xs9950_read_reg(sensor->i2c, XS9950_REG_SYNC_DEPTH_MSB, &reg_val))
		return -EIO;
	status->sync_depth = ((reg_val & 0x03) << 8);
	if (xs9950_read_reg(sensor->i2c, XS9950_REG_SYNC_DEPTH_LSB, &reg_val))
		return -EIO;
	status->sync_depth |= reg_val;

	/* Read HD_STATUS (0x08) */
	if (xs9950_read_reg(sensor->i2c, XS9950_REG_HD_STATUS, &reg_val))
		return -EIO;
	status->hd_sync_depth = ((reg_val & XS9950_HD_STS_SYNC_DEPTH_MSB_MASK) >> 6) << 8;
	status->hd_free_run = !!(reg_val & XS9950_HD_STS_FREE_RUN);
	status->hd_hspll_locked_fe = !!(reg_val & XS9950_HD_STS_HSPLL_LOCKED_FE);
	status->hd_hspll_locked_be = !!(reg_val & XS9950_HD_STS_HSPLL_LOCKED_BE);
	status->hd_vsync_locked = !!(reg_val & XS9950_HD_STS_VSYNC_LOCKED);
	status->hd_color_kill = !!(reg_val & XS9950_HD_STS_COLOR_KILL);

	/* Read SD_STATUS (0x09) */
	if (xs9950_read_reg(sensor->i2c, XS9950_REG_SD_STATUS, &reg_val))
		return -EIO;
	status->sd_sync_depth = ((reg_val & XS9950_SD_STS_SYNC_DEPTH_MSB_MASK) >> 6) << 8;
	status->sd_free_run = !!(reg_val & XS9950_SD_STS_FREE_RUN);
	status->sd_hspll_locked_fe = !!(reg_val & XS9950_SD_STS_HSPLL_LOCKED_FE);
	status->sd_hspll_locked_be = !!(reg_val & XS9950_SD_STS_HSPLL_LOCKED_BE);
	status->sd_vsync_locked = !!(reg_val & XS9950_SD_STS_VSYNC_LOCKED);
	status->sd_color_kill = !!(reg_val & XS9950_SD_STS_COLOR_KILL);

	return 0;
}

static void xs9950_print_status(struct xs9950_v4l2_dev *sensor,
				const struct xs9950_ch_status *s)
{
	static const char *hd_std_type_str[] = { "HDCVI", "AHD", "TVI", "Rsvd" };
	static const char *sd_std_str[] = {
		"NTSC-JM", "NTSC-443", "PAL-M", "PAL-60", "PAL-CN", "PAL-BGHID",
		"Rsvd6", "Rsvd7", "Rsvd8", "Rsvd9", "RsvdA", "RsvdB",
		"RsvdC", "RsvdD", "RsvdE", "No Signal"
	};

	dev_dbg(&sensor->i2c->dev, "---------------- XS9950 Status -----------------\n");
	dev_info(&sensor->i2c->dev, "Video mode: %s\n", s->is_hd ? "HD" : "SD");
	dev_info(&sensor->i2c->dev, "Free run  : %s\n", s->free_run ? "YES (no signal)" : "NO");
	dev_info(&sensor->i2c->dev, "Vsync lock: %s\n", s->vsync_locked ? "LOCKED" : "UNLOCKED");
	dev_info(&sensor->i2c->dev, "PLL FE/BE : %s/%s\n",
		 s->hspll_locked_fe ? "LOCKED" : "UNLOCKED",
		 s->hspll_locked_be ? "LOCKED" : "UNLOCKED");
	dev_dbg(&sensor->i2c->dev, "Color kill: %s\n", s->color_kill ? "YES (no color)" : "NO");
	dev_dbg(&sensor->i2c->dev, "SD burst  : %s\n",
		 s->sd_burst_detected ? "detected" : "not detected");

	if (s->is_hd) {
		dev_dbg(&sensor->i2c->dev, "HD standard: %s (type=0x%02X, fmt=0x%02X)\n",
			 hd_std_type_str[(s->hd_std_type >> 6) & 0x03],
			 s->hd_std_type, s->hd_std_format);
		dev_dbg(&sensor->i2c->dev, "HD free run : %s\n", s->hd_free_run ? "YES" : "NO");
		dev_dbg(&sensor->i2c->dev, "HD PLL FE/BE: %s/%s\n",
			 s->hd_hspll_locked_fe ? "LOCKED" : "UNLOCKED",
			 s->hd_hspll_locked_be ? "LOCKED" : "UNLOCKED");
		dev_dbg(&sensor->i2c->dev, "HD vsync    : %s\n",
			 s->hd_vsync_locked ? "LOCKED" : "UNLOCKED");
		dev_dbg(&sensor->i2c->dev, "HD color kill: %s\n",
			 s->hd_color_kill ? "YES" : "NO");
	} else {
		dev_dbg(&sensor->i2c->dev, "SD standard: %s\n", sd_std_str[s->sd_std_raw & 0x0F]);
		dev_dbg(&sensor->i2c->dev, "Pedestal   : %s\n", s->has_pedestal ? "YES" : "NO");
		dev_dbg(&sensor->i2c->dev, "Field rate : %s\n", s->is_fifty_hz ? "50Hz" : "60Hz");
		dev_dbg(&sensor->i2c->dev, "SD free run : %s\n", s->sd_free_run ? "YES" : "NO");
		dev_dbg(&sensor->i2c->dev, "SD PLL FE/BE: %s/%s\n",
			 s->sd_hspll_locked_fe ? "LOCKED" : "UNLOCKED",
			 s->sd_hspll_locked_be ? "LOCKED" : "UNLOCKED");
		dev_dbg(&sensor->i2c->dev, "SD vsync    : %s\n",
			 s->sd_vsync_locked ? "LOCKED" : "UNLOCKED");
		dev_dbg(&sensor->i2c->dev, "SD color kill: %s\n",
			 s->sd_color_kill ? "YES" : "NO");
	}

	dev_dbg(&sensor->i2c->dev, "Signal loss : %u\n", s->signal_loss);
	dev_dbg(&sensor->i2c->dev, "Sync depth  : %u (SD %u, HD %u)\n",
		 s->sync_depth, s->sd_sync_depth, s->hd_sync_depth);
	dev_dbg(&sensor->i2c->dev, "------------------------------------------------\n");
}

static void xs9950_wait_lock(struct xs9950_v4l2_dev *sensor)
{
	u32 timeout = 100, cnt = 0;
	u8 video_loss = 0;

	while (1) {
		msleep(10);

		if (xs9950_read_reg(sensor->i2c, XS9950_REG_VIDEO_STATUS_1, &video_loss))
			break;

		video_loss &= XS9950_STS1_FREE_RUN;
		if (!video_loss)
			break;

		cnt++;
		if (cnt > timeout) {
			dev_err(&sensor->i2c->dev, "Wait video source timeout!\n");
			return;
		}
	}
	msleep(300);
}

/******************************************************************************
 * V4L2 API of XS9950
 ******************************************************************************/

static inline struct xs9950_v4l2_dev *to_xs9950_dev(struct v4l2_subdev *sd)
{
	return container_of(sd, struct xs9950_v4l2_dev, sd);
}

static int xs9950_g_frame_interval(struct v4l2_subdev *sd,
				   struct v4l2_subdev_frame_interval *fi)
{
	struct xs9950_v4l2_dev *sensor = to_xs9950_dev(sd);

	fi->interval = sensor->frame_interval;
	return 0;
}

static int xs9950_s_frame_interval(struct v4l2_subdev *sd,
				   struct v4l2_subdev_frame_interval *fi)
{
	struct xs9950_v4l2_dev *sensor = to_xs9950_dev(sd);

	if (fi->pad != 0)
		return -EINVAL;

	if (sensor->streaming)
		return -EBUSY;

	dev_dbg(&sensor->i2c->dev, "Set FR %d-%d\n",
		fi->interval.numerator, fi->interval.denominator);

	return 0;
}

static int xs9950_enum_mbus_code(struct v4l2_subdev *sd,
				 struct v4l2_subdev_pad_config *cfg,
				 struct v4l2_subdev_mbus_code_enum *code)
{
	if (code->pad != 0)
		return -EINVAL;

	code->code = DEFAULT_V4L2_CODE;
	return 0;
}

static int xs9950_s_stream(struct v4l2_subdev *sd, int enable)
{
	struct xs9950_v4l2_dev *sensor = to_xs9950_dev(sd);

	dev_dbg(&sensor->i2c->dev, "Streaming %s\n", enable ? "On" : "Off");

	if (enable) {
		xs9950_write_reg(sensor->i2c, 0x4000, 0x01);
		sensor->streaming = true;
	} else {
		xs9950_write_reg(sensor->i2c, 0x4000, 0x00);
		sensor->streaming = false;
	}

	return 0;
}

static int xs9950_get_fmt(struct v4l2_subdev *sd,
			  struct v4l2_subdev_pad_config *cfg,
			  struct v4l2_subdev_format *format)
{
	struct xs9950_v4l2_dev *sensor = to_xs9950_dev(sd);

	if (format->pad != 0)
		return -EINVAL;

	format->format = sensor->fmt;
	return 0;
}

static int xs9950_set_fmt(struct v4l2_subdev *sd,
			  struct v4l2_subdev_pad_config *cfg,
			  struct v4l2_subdev_format *format)
{
	struct xs9950_v4l2_dev *sensor = to_xs9950_dev(sd);
	struct v4l2_mbus_framefmt *fmt = &format->format;

	if (format->pad != 0)
		return -EINVAL;

	if (sensor->streaming)
		return -EBUSY;

	dev_dbg(&sensor->i2c->dev,
		"Set format: code %#x, colorspace %#x, %d x %d\n",
		fmt->code, fmt->colorspace, fmt->width, fmt->height);

	return 0;
}

static const struct v4l2_subdev_core_ops xs9950_core_ops = {
	.log_status = v4l2_ctrl_subdev_log_status,
	.subscribe_event = v4l2_ctrl_subdev_subscribe_event,
	.unsubscribe_event = v4l2_event_subdev_unsubscribe,
};

static const struct v4l2_subdev_video_ops xs9950_video_ops = {
	.g_frame_interval = xs9950_g_frame_interval,
	.s_frame_interval = xs9950_s_frame_interval,
	.s_stream = xs9950_s_stream,
};

static const struct v4l2_subdev_pad_ops xs9950_pad_ops = {
	.enum_mbus_code = xs9950_enum_mbus_code,
	.get_fmt = xs9950_get_fmt,
	.set_fmt = xs9950_set_fmt,
};

static const struct v4l2_subdev_ops xs9950_subdev_ops = {
	.core = &xs9950_core_ops,
	.video = &xs9950_video_ops,
	.pad = &xs9950_pad_ops,
};

static int xs9950_probe(struct i2c_client *client)
{
	struct device *dev = &client->dev;
	struct fwnode_handle *endpoint = NULL;
	struct xs9950_v4l2_dev *sensor = NULL;
	struct v4l2_mbus_framefmt *fmt = NULL;
	struct xs9950_ch_status status;
	int ret = 0;

	sensor = devm_kzalloc(dev, sizeof(struct xs9950_v4l2_dev), GFP_KERNEL);
	if (!sensor)
		return -ENOMEM;

	/* request optional power down pin */
	sensor->pwdn_gpio = devm_gpiod_get_optional(dev, "powerdown", GPIOD_OUT_HIGH);
	if (IS_ERR_OR_NULL(sensor->pwdn_gpio))
		dev_dbg(dev, "Failed to parse powerdown-gpio\n");
	else
		xs9950_power_on(sensor);

	fmt = &sensor->fmt;
	fmt->code = DEFAULT_V4L2_CODE;
	fmt->colorspace = V4L2_COLORSPACE_SRGB;
	fmt->ycbcr_enc = V4L2_MAP_YCBCR_ENC_DEFAULT(fmt->colorspace);
	fmt->quantization = V4L2_QUANTIZATION_FULL_RANGE;
	fmt->xfer_func = V4L2_MAP_XFER_FUNC_DEFAULT(fmt->colorspace);
	fmt->field = V4L2_FIELD_NONE;

	sensor->i2c = client;

	/* Chip ID check */
	ret = xs9950_chipid_check(sensor);
	if (ret)
		goto err_power_off;

	/* Initialize sensor */
	xs9950_sensor_init(sensor, DEFAULT_VIN_CH, DEFAULT_FORMAT);

	/* Wait for video lock */
	xs9950_wait_lock(sensor);

	/* Query and print status */
	if (xs9950_query_status(sensor, &status) == 0)
		xs9950_print_status(sensor, &status);

	endpoint = fwnode_graph_get_next_endpoint(dev_fwnode(&client->dev), NULL);
	if (!endpoint) {
		dev_err(dev, "endpoint node not found\n");
		ret = -EINVAL;
		goto err_power_off;
	}

	ret = v4l2_fwnode_endpoint_parse(endpoint, &sensor->ep);
	fwnode_handle_put(endpoint);
	if (ret) {
		dev_err(dev, "Could not parse endpoint\n");
		goto err_power_off;
	}

	if (sensor->ep.bus_type != V4L2_MBUS_BT656) {
		dev_err(dev, "Unsupported bus type %d\n", sensor->ep.bus_type);
		ret = -EINVAL;
		goto err_power_off;
	}

	v4l2_i2c_subdev_init(&sensor->sd, client, &xs9950_subdev_ops);

	sensor->sd.flags |= V4L2_SUBDEV_FL_HAS_DEVNODE | V4L2_SUBDEV_FL_HAS_EVENTS;
	sensor->pad.flags = MEDIA_PAD_FL_SOURCE;
	sensor->sd.entity.function = MEDIA_ENT_F_CAM_SENSOR;
	ret = media_entity_pads_init(&sensor->sd.entity, 1, &sensor->pad);
	if (ret)
		goto err_power_off;

	ret = v4l2_async_register_subdev_sensor_common(&sensor->sd);
	if (ret)
		goto err_media_entity_cleanup;

	dev_info(dev, "Register %s to V4L2 device\n", DRV_NAME);

	return 0;

err_media_entity_cleanup:
	media_entity_cleanup(&sensor->sd.entity);
err_power_off:
	xs9950_power_off(sensor);
	return ret;
}

static int xs9950_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct xs9950_v4l2_dev *sensor = to_xs9950_dev(sd);

	v4l2_async_unregister_subdev(&sensor->sd);
	media_entity_cleanup(&sensor->sd.entity);

	xs9950_power_off(sensor);

	return 0;
}

static const struct i2c_device_id xs9950_id[] = {
	{DRV_NAME, 0},
	{},
};
MODULE_DEVICE_TABLE(i2c, xs9950_id);

static const struct of_device_id xs9950_dt_ids[] = {
	{ .compatible = "xs9950" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, xs9950_dt_ids);

static struct i2c_driver xs9950_i2c_driver = {
	.driver = {
		.name  = DRV_NAME,
		.of_match_table	= xs9950_dt_ids,
	},
	.id_table = xs9950_id,
	.probe_new = xs9950_probe,
	.remove   = xs9950_remove,
};

module_i2c_driver(xs9950_i2c_driver);

MODULE_DESCRIPTION("XS9950 4-Channel Video Decoder Linux Driver");
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Matteo <duanmt@artinchip.com>");
