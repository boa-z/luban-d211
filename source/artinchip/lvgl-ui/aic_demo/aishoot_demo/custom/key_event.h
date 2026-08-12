/*
 * Copyright (c) 2024-2026, ArtInChip Technology Co., Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Authors:  haidong.pan <haidong.pan@artinchip.com>
 */

#ifndef _KEY_EVENT_H
#define _KEY_EVENT_H

#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <linux/input.h>
#include <string.h>
#include <time.h>

#define REPEAT_COUNT 8    // repeat count

struct key_state {
    int code;
    int pressed;
    int repeat;
    int long_pressed;
};

void handle_key_event(struct input_event* ev, struct key_state* state, void *data);

#endif

