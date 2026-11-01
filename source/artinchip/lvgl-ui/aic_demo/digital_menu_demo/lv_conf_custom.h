/*
 * Copyright (C) 2025 ArtInChip Technology Co., Ltd.
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * lv_conf_custom.h for custom lv_conf.h file
 * example :
 *     #undef LV_USE_LOG
 *     #define LV_USE_LOG 1
 */

#ifndef LV_CONF_CUSTOM_H
#define LV_CONF_CUSTOM_H

/* code  begin */

#undef LV_USE_FREETYPEo
#define LV_USE_FREETYPE 1

#define MTP_MOUNT_DIR "/usr/local/share/lvgl_data/font"

#define USE_HOST_DEMO              1
#define USE_DEVICE_DEMO            0
#define USE_TRY_MTP_MOUNTED        0
#define USE_DOUBLE_SDL_DISP        0 // PC simulator

/* code end */

#endif  /* LV_CONF_CUSTOM_H */
