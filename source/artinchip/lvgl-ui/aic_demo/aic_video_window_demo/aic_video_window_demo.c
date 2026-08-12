/*
 * Copyright (c) 2026, ArtInChip Technology Co., Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Authors:  Zequan Liang <zequan.liang@artinchip.com>
 */

#include "aic_ui.h"
#include "lvgl.h"
#include "aic_player.h"
#include "lv_aic_video_window.h"

static int player_event_handle(void *app_data, int event, int data1, int data2)
{
    int ret = 0;

    switch(event) {
        case AIC_PLAYER_EVENT_PLAY_END:
            break;
        case AIC_PLAYER_EVENT_PLAY_TIME:
            break;
        case AIC_PLAYER_EVENT_DEMUXER_FORMAT_DETECTED:
            break;
        case AIC_PLAYER_EVENT_DEMUXER_FORMAT_NOT_DETECTED:
            break;
        default:
            break;
    }
    return ret;
}

static char *get_real_path(char *src)
{
    char *real_path = (char *)src;
    if (real_path[1] == ':' && ((real_path[0] >= 'A' && real_path[0] <= 'Z') || (real_path[0] >= 'a' && real_path[0] <= 'z'))) {
        real_path += 2;
    }
    return real_path;
}

void ui_init(void)
{
    // By default, the display engine (DE) has two layers of content: the UI layer and the VIDEO layer, with the UI layer positioned on top of the VIDEO layer. If the content from the VIDEO layer needs to show through, the opacity of the UI layer needs to be set to 0.
    struct mpp_rect disp_rect = {0};
    disp_rect.width = 600;
    disp_rect.height = 300;

    lv_obj_t *window = lv_aic_video_window_create(lv_scr_act()); // the alpha value of the ui layer is cleared to 0.
    lv_aic_video_window_set_size(window, 600, 300);
    lv_aic_video_window_set_color(window, lv_color_hex(0x0));
    lv_obj_set_pos(window, 0, 0);

    struct aic_player *player = aic_player_create(NULL); // video playback is on the video layer.
    aic_player_set_event_callback(player, NULL, player_event_handle);
    aic_player_set_uri(player, get_real_path(LVGL_PATH(elevator_mjpeg.mp4)));
    aic_player_prepare_sync(player);
    aic_player_set_disp_rect(player, &disp_rect);
    aic_player_start(player);
}
