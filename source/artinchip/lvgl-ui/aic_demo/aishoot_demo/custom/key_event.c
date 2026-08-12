/*
 * Copyright (c) 2024-2026, ArtInChip Technology Co., Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Authors:  haidong.pan <haidong.pan@artinchip.com>
 */

#include "key_event.h"
#include "bt_uart.h"
#include "bt_yc8628.h"
#include "tffcastserver.h"

#define BACKLIGHT_PATH "/sys/class/backlight/backlight"
#define MAX_BRIGHTNESS_FILE "max_brightness"
#define BRIGHTNESS_FILE "brightness"

static int set_brightness()
{
    char path[256] = {0};
    char buf[32] = {0};
    int value = 0;

    snprintf(path, sizeof(path), "%s/%s", BACKLIGHT_PATH, BRIGHTNESS_FILE);

    int fd = open(path, O_RDWR);
    if (fd < 0) {
        perror("open brightness fail\n");
        return -1;
    }

    ssize_t len = read(fd, buf, sizeof(buf) - 1);
    if (len <= 0) {
        perror("read brightness fail \n");
        return -1;
    }

    lseek(fd, 0, SEEK_SET);
        
    buf[len] = '\0';
    value = atoi(buf) + 3;
    if (value > 10) value = 3;
    snprintf(buf, sizeof(buf), "%d", value);
    if (write(fd, buf, strlen(buf)) < 0) {
        perror("write brightness");
        close(fd);
        return -1;
    }

    close(fd);
    return 0;
}

void handle_key_event(struct input_event* ev, struct key_state* state, void *data)
{
    printf("%s, %d\n", __func__, __LINE__);
#ifdef ENABLE_BT_YC8628
    client_t *cli = (client_t *)data;
#endif
    if (ev->type == EV_KEY) {
        if (ev->value == 1) { // down
            state->long_pressed = 0;
            state->code = ev->code;
            state->pressed = 1;
            state->repeat = 0;
            switch(ev->code) {
                case KEY_POWER:
#ifdef ENABLE_BT_YC8628
                    bt_yc_hid_camera_set(cli);
#else
                    shutter_uart();
#endif
                    break;
                case KEY_UP:
                    break;
                case KEY_DOWN:
                    break;
                default:
                    break;
            }
        } else if (ev->value == 2) {
            state->repeat++;
            switch(ev->code) {
                case KEY_DOWN:
                    break;
                case KEY_UP:
                    if (state->repeat >= REPEAT_COUNT && state->long_pressed == 0) {
                        TFFCast_setFullScreen();
                        state->repeat = 0;
                        state->long_pressed = 1;
                    }
                    break;
                case KEY_POWER:
                    break;
                default:
                    break;
            }
        } else if (ev->value == 0) { // release
            switch(ev->code) {
                case KEY_POWER:
                    break;
                case KEY_UP:
                    if (state->long_pressed == 0)
                        TFFCast_setRotate();
                    break;
                case KEY_DOWN:
                    set_brightness();
                    break;
                default:
                break;
            }
            state->long_pressed = 0;
            state->pressed = 0;
            state->repeat = 0;
        }
    }
}

