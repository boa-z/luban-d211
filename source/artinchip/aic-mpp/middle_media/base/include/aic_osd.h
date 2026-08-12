/*
 * Copyright (C) 2025-2026 ArtInChip Technology Co., Ltd.
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 *  author: che.jiang@artinchip.com
 *  Desc: osd overlay
 */

#ifndef AIC_OSD_H
#define AIC_OSD_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include <sys/types.h>

#define AIC_OSD_TEXT_MAX 128
#define AIC_OSD_MAX_REGIONS 4
#define AIC_OSD_ASC_W 16
#define AIC_OSD_HZK_W 24
#define AIC_OSD_FONT_H 24
#define AIC_OSD_ASC_GLYPH_SIZE 48
#define AIC_OSD_HZK_GLYPH_SIZE 72
#define ASC_FONT_PATH "/usr/local/share/osd_font/ASC24"
#define HZK_FONT_PATH "/usr/local/share/osd_font/HZK24S"
#define UNI2GB_PATH   "/usr/local/share/osd_font/UNI2GB"

enum aic_osd_id {
	AIC_OSD_ID_TIME = 0,
	AIC_OSD_ID_RESERVED,
	AIC_OSD_ID_USER,
};

enum aic_osd_color {
	AIC_OSD_COLOR_WHITE = 0,
	AIC_OSD_COLOR_BLACK,
	AIC_OSD_COLOR_RED,
	AIC_OSD_COLOR_GREEN,
	AIC_OSD_COLOR_BLUE,
	AIC_OSD_COLOR_YELLOW,
};

enum aic_osd_type {
	AIC_OSD_TYPE_NEGATIVE_COLOR = 0,
	AIC_OSD_TYPE_EDGE_STROKE,
	AIC_OSD_TYPE_USER_COLOR,
};

struct aic_osd;

struct aic_osd *aic_osd_create(int frame_width, int frame_height);
void aic_osd_destroy(struct aic_osd *osd);
int aic_osd_enable_region(struct aic_osd *osd, int idx, int enable);
int aic_osd_update_text(struct aic_osd *osd, int idx, const char *text);

int aic_osd_draw(struct aic_osd *osd, uint8_t *y_buf, uint8_t *uv_buf,
		 int y_stride, int uv_stride);

int aic_osd_set_region(struct aic_osd *osd, int idx, int x, int y,
		       const char *text);
int aic_osd_set_type(struct aic_osd *osd, int idx, enum aic_osd_type type);
int aic_osd_set_color(struct aic_osd *osd, int idx, enum aic_osd_color color);
#ifdef __cplusplus
}
#endif

#endif /* AIC_OSD_H */
