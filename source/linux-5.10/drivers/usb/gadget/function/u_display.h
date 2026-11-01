/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * f_display.c -- USB display (display) function driver
 *
 * Copyright (C) 2023-2026, ArtInChip Technology Co., Ltd
 * Author: ArtInChip
 */

#ifndef U_DISPLAY_H
#define U_DISPLAY_H

#include <linux/usb/composite.h>

struct f_display_opts {
	struct usb_function_instance func_inst;
	u8 port_num;
};

struct gdisplay {
	struct usb_function		func;
	struct usb_ep			*in;
	struct usb_ep			*out;
};


#endif /* U_HID_H */
