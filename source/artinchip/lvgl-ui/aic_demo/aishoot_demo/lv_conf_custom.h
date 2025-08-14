/*
 * Copyright (c) 2024-2025, ArtInChip Technology Co., Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Authors:  haidong.pan <haidong.pan@artinchip.com>
 */

/* lv_conf_custom.h for custom lv_conf.h file
 * example : 
 *     #undef LV_USE_LOG
 *     #define LV_USE_LOG 1
 */

#ifndef LV_CONF_CUSTOM_H
#define LV_CONF_CUSTOM_H

/* code  begin */

#undef LV_USE_FREETYPE
#define LV_USE_FREETYPE 1

#undef LV_COLOR_DEPTH
#define LV_COLOR_DEPTH 32

/* code end */

#endif  /* LV_CONF_CUSTOM_H */
