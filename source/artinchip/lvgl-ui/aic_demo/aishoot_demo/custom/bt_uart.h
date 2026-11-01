/*
 * Copyright (c) 2024-2025, ArtInChip Technology Co., Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Authors:  haidong.pan <haidong.pan@artinchip.com>
 */

#ifndef _BT_UART_H
#define _BT_UART_H

int config_uart(char *dev_name);
int shutter_uart(void);
int rename_bt_uart(char *name);
int recv_uart(char *rcv_buf, int data_len);
int bt_uart_connect(char *msg);
#endif

