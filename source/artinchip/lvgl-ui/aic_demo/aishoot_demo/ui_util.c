/*
 * Copyright (c) 2024-2025, ArtInChip Technology Co., Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Authors:  haidong.pan <haidong.pan@artinchip.com>
 */

#include <stdio.h>
#include "ui_util.h"

bool screen_is_loading(lv_obj_t *scr)
{
#if LVGL_VERSION_MAJOR == 8
    lv_obj_t *act_scr = lv_scr_act();
    lv_disp_t *d = lv_obj_get_disp(scr);
    if (d->prev_scr == NULL && (d->scr_to_load == NULL || d->scr_to_load == act_scr))
        return false;
    else
        return true;
#else
    return false;
#endif
}

void ui_style_init(lv_style_t *style)
{
  if (style->prop_cnt >= 1)
    lv_style_reset(style);
  else
    lv_style_init(style);
}
