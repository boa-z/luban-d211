/*
 * Copyright (C) 2025-2026 ArtInChip Technology Co., Ltd.
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 *  author: che.jiang@artinchip.com
 *  Desc: osd overlay
 */

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include "mpp_log.h"
#include "mpp_mem.h"
#include "aic_osd.h"

#define CHAR_SAMPLE_STEP_X 3
#define CHAR_SAMPLE_STEP_Y 4
#define UNICODE_CJK_START 0x4E00
#define UNICODE_CJK_END 0x9FFF

struct aic_osd_region {
	bool enabled;
	int x, y;
	int w, h;
	char text[AIC_OSD_TEXT_MAX];
	enum aic_osd_type type;
	enum aic_osd_color color;
};

struct aic_osd {
	int frame_w, frame_h;
	uint8_t *asc_font;
	uint8_t *hzk_font;
	uint16_t *uni2gb;
	struct aic_osd_region regions[AIC_OSD_MAX_REGIONS];
};

static int calc_text_width(struct aic_osd *osd, const char *text);

static int load_font_file(const char *path, uint8_t **buf, int *size)
{
	struct stat st;
	int fd = open(path, O_RDONLY);
	if (fd < 0) {
		loge("font not found: %s", path);
		*buf = NULL;
		return -1;
	}
	if (fstat(fd, &st) < 0) {
		close(fd);
		*buf = NULL;
		return -1;
	}
	*size = st.st_size;
	*buf = mpp_alloc(*size);
	if (!*buf) {
		close(fd);
		return -1;
	}
	read(fd, *buf, *size);
	close(fd);
	logi("font loaded: %s, %d bytes", path, *size);
	return 0;
}

struct aic_osd *aic_osd_create(int frame_width, int frame_height)
{
	struct aic_osd *osd = mpp_alloc(sizeof(struct aic_osd));
	int sz;
	if (!osd)
		return NULL;
	memset(osd, 0, sizeof(struct aic_osd));
	osd->frame_w = frame_width;
	osd->frame_h = frame_height;
	uint8_t *uni2gb_data = NULL;

	load_font_file(ASC_FONT_PATH, &osd->asc_font, &sz);
	load_font_file(HZK_FONT_PATH, &osd->hzk_font, &sz);
	load_font_file(UNI2GB_PATH, &uni2gb_data, &sz);
	osd->uni2gb = (uint16_t *)uni2gb_data;
	return osd;
}

void aic_osd_destroy(struct aic_osd *osd)
{
	if (!osd)
		return;
	mpp_free(osd->asc_font);
	mpp_free(osd->hzk_font);
	mpp_free(osd->uni2gb);
	mpp_free(osd);
}

int aic_osd_set_region(struct aic_osd *osd, int idx, int x, int y,
		       const char *text)
{
	if (!osd || idx < 0 || idx >= AIC_OSD_MAX_REGIONS) {
		loge("Set osd region failed, osd %p, idx %d", osd, idx);
		return -1;
	}

	struct aic_osd_region *r = &osd->regions[idx];
	r->x = x;
	r->y = y;

	if (text) {
		strncpy(r->text, text, AIC_OSD_TEXT_MAX - 1);
		r->text[AIC_OSD_TEXT_MAX - 1] = '\0';
	}

	r->w = calc_text_width(osd, r->text);
	r->h = AIC_OSD_FONT_H;
	return 0;
}

int aic_osd_enable_region(struct aic_osd *osd, int idx, int enable)
{
	if (!osd || idx < 0 || idx >= AIC_OSD_MAX_REGIONS) {
		loge("Enable osd failed, osd %p, idx %d", osd, idx);
		return -1;
	}

	osd->regions[idx].enabled = enable;
	return 0;
}

int aic_osd_update_text(struct aic_osd *osd, int idx, const char *text)
{
	if (!osd || idx < 0 || idx >= AIC_OSD_MAX_REGIONS || !text)
		return -1;

	struct aic_osd_region *r = &osd->regions[idx];
	strncpy(r->text, text, AIC_OSD_TEXT_MAX - 1);
	r->text[AIC_OSD_TEXT_MAX - 1] = '\0';
	r->w = calc_text_width(osd, r->text);
	return 0;
}

int aic_osd_set_type(struct aic_osd *osd, int idx, enum aic_osd_type type)
{
	if (!osd || idx < 0 || idx >= AIC_OSD_MAX_REGIONS) {
		loge("Set osd type failed, osd %p, idx %d", osd, idx);
		return -1;
	}

	osd->regions[idx].type = type;
	return 0;
}

int aic_osd_set_color(struct aic_osd *osd, int idx, enum aic_osd_color color)
{
	if (!osd || idx < 0 || idx >= AIC_OSD_MAX_REGIONS) {
		loge("Set osd color failed, osd %p, idx %d", osd, idx);
		return -1;
	}

	osd->regions[idx].color = color;
	return 0;
}

/* BT.601 limited-range YUV values */
static void get_yuv_val(enum aic_osd_color color, uint8_t *y, uint8_t *u,
			uint8_t *v)
{
	switch (color) {
	case AIC_OSD_COLOR_WHITE:
		*y = 235;
		*u = 128;
		*v = 128;
		break;
	case AIC_OSD_COLOR_BLACK:
		*y = 16;
		*u = 128;
		*v = 128;
		break;
	case AIC_OSD_COLOR_RED:
		*y = 82;
		*u = 90;
		*v = 240;
		break;
	case AIC_OSD_COLOR_GREEN:
		*y = 145;
		*u = 54;
		*v = 34;
		break;
	case AIC_OSD_COLOR_BLUE:
		*y = 41;
		*u = 240;
		*v = 110;
		break;
	case AIC_OSD_COLOR_YELLOW:
		*y = 210;
		*u = 16;
		*v = 146;
		break;
	default:
		*y = 235;
		*u = 128;
		*v = 128;
		break;
	}
}

/* Decode one UTF-8 char; returns Unicode code point, sets *advance bytes consumed */
static uint32_t utf8_decode(const uint8_t *p, int *advance)
{
	if (*p < 0x80) {
		*advance = 1;
		return *p;
	}
	if (*p >= 0xC2 && *p <= 0xDF && (p[1] & 0xC0) == 0x80) {
		*advance = 2;
		return ((*p & 0x1F) << 6) | (p[1] & 0x3F);
	}
	if (*p >= 0xE0 && *p <= 0xEF && (p[1] & 0xC0) == 0x80 &&
	    (p[2] & 0xC0) == 0x80) {
		*advance = 3;
		return ((*p & 0x0F) << 12) | ((p[1] & 0x3F) << 6) |
		       (p[2] & 0x3F);
	}
	*advance = 1;
	return *p;
}

static unsigned int uni2gb_lookup(struct aic_osd *osd, uint32_t cp)
{
	if (cp >= UNICODE_CJK_START && cp <= UNICODE_CJK_END && osd->uni2gb)
		return osd->uni2gb[cp - UNICODE_CJK_START];
	return 0xFFFF;
}

static const uint8_t *get_glyph(struct aic_osd *osd, const uint8_t *text,
				int *width, int *advance)
{
	int adv;
	uint32_t cp = utf8_decode(text, &adv);
	*advance = adv;

	/* CJK: use HZK font via Unicode→GB2312 lookup */
	if (osd->hzk_font && cp >= UNICODE_CJK_START) {
		unsigned int off = uni2gb_lookup(osd, cp);
		if (off != 0xFFFF) {
			*width = AIC_OSD_HZK_W;
			return osd->hzk_font + off * AIC_OSD_HZK_GLYPH_SIZE;
		}
	}
	/* ASCII: use ASC font (16px wide), or fallback to HZK if available */
	*width = AIC_OSD_ASC_W;
	if (osd->asc_font && cp >= 0x20 && cp <= 0x7E)
		return osd->asc_font + (cp - 0x20) * AIC_OSD_ASC_GLYPH_SIZE;
	/* last resort: use HZK font (24px) for non-CJK codepoints without ASC */
	if (osd->hzk_font)
		return osd->hzk_font;
	return NULL;
}

static int calc_text_width(struct aic_osd *osd, const char *text)
{
	int w = 0;
	const uint8_t *p = (const uint8_t *)text;
	while (*p) {
		int adv;
		uint32_t cp = utf8_decode(p, &adv);
		if (cp >= UNICODE_CJK_START && uni2gb_lookup(osd, cp) != 0xFFFF)
			w += AIC_OSD_HZK_W;
		else
			w += AIC_OSD_ASC_W;
		p += adv;
	}
	return w;
}

static int sample_char_bg(uint8_t *y_buf, int y_stride, int char_x, int char_y,
			  int char_w)
{
	int sum = 0, cnt = 0;

	for (int row = 0; row < AIC_OSD_FONT_H; row += CHAR_SAMPLE_STEP_Y) {
		for (int col = 0; col < char_w; col += CHAR_SAMPLE_STEP_X) {
			sum += y_buf[(char_y + row) * y_stride + char_x + col];
			cnt++;
		}
	}
	return cnt ? sum / cnt : 128;
}

static void set_nv12_uv(uint8_t *uv_buf, int uv_stride, int px, int py,
			uint8_t u, uint8_t v)
{
	int uv_x = px >> 1;
	int uv_y = py >> 1;
	uv_buf[uv_y * uv_stride + uv_x * 2] = u;
	uv_buf[uv_y * uv_stride + uv_x * 2 + 1] = v;
}

static void clip_region(struct aic_osd *osd, struct aic_osd_region *r)
{
	if (r->x < 0)
		r->x = 0;
	if (r->y < 0)
		r->y = 0;
	if (r->x + r->w > osd->frame_w)
		r->w = osd->frame_w - r->x;
	if (r->y + r->h > osd->frame_h)
		r->h = osd->frame_h - r->y;
}

static void draw_char_invert(uint8_t *y_buf, int y_stride, int char_x,
			     int char_y, int char_w, const uint8_t *glyph)
{
	int avg = sample_char_bg(y_buf, y_stride, char_x, char_y, char_w);
	uint8_t y_val = (avg > 128) ? 0 : 255;

	for (int row = 0; row < AIC_OSD_FONT_H; row++) {
		for (int col = 0; col < char_w; col++) {
			int bidx = row * ((char_w + 7) / 8) + col / 8;
			if (glyph[bidx] & (0x80 >> (col % 8))) {
				y_buf[(char_y + row) * y_stride + char_x + col] =
					y_val;
			}
		}
	}
}

static void draw_char_color(uint8_t *y_buf, uint8_t *uv_buf, int y_stride,
			    int uv_stride, int char_x, int char_y, int char_w,
			    const uint8_t *glyph, uint8_t y_val, uint8_t u_val,
			    uint8_t v_val)
{
	for (int row = 0; row < AIC_OSD_FONT_H; row++) {
		for (int col = 0; col < char_w; col++) {
			int bidx = row * ((char_w + 7) / 8) + col / 8;
			if (glyph[bidx] & (0x80 >> (col % 8))) {
				int px = char_x + col;
				int py = char_y + row;
				y_buf[py * y_stride + px] = y_val;
				set_nv12_uv(uv_buf, uv_stride, px, py, u_val,
					    v_val);
			}
		}
	}
}

static void draw_region_negative(struct aic_osd *osd, struct aic_osd_region *r,
				 uint8_t *y_buf, int y_stride)
{
	const uint8_t *p = (const uint8_t *)r->text;
	int cx = r->x;
	while (*p) {
		int w, adv;
		const uint8_t *glyph = get_glyph(osd, p, &w, &adv);
		if (!glyph) {
			p += adv;
			continue;
		}
		if (cx + w > osd->frame_w)
			break;
		draw_char_invert(y_buf, y_stride, cx, r->y, w, glyph);
		cx += w;
		p += adv;
	}
}

static void draw_region_color(struct aic_osd *osd, struct aic_osd_region *r,
			      uint8_t *y_buf, uint8_t *uv_buf, int y_stride,
			      int uv_stride, uint8_t y_val, uint8_t u_val,
			      uint8_t v_val)
{
	const uint8_t *p = (const uint8_t *)r->text;
	int cx = r->x;
	while (*p) {
		int w, adv;
		const uint8_t *glyph = get_glyph(osd, p, &w, &adv);
		if (!glyph) {
			p += adv;
			continue;
		}
		if (cx + w > osd->frame_w)
			break;
		draw_char_color(y_buf, uv_buf, y_stride, uv_stride, cx, r->y, w,
				glyph, y_val, u_val, v_val);
		cx += w;
		p += adv;
	}
}

int aic_osd_draw(struct aic_osd *osd, uint8_t *y_buf, uint8_t *uv_buf,
		 int y_stride, int uv_stride)
{
	if (!osd || !y_buf)
		return -1;

	for (int i = 0; i < AIC_OSD_MAX_REGIONS; i++) {
		struct aic_osd_region *r = &osd->regions[i];
		if (!r->enabled || r->text[0] == '\0')
			continue;

		clip_region(osd, r);

		switch (r->type) {
		case AIC_OSD_TYPE_NEGATIVE_COLOR:
			draw_region_negative(osd, r, y_buf, y_stride);
			break;
		case AIC_OSD_TYPE_USER_COLOR: {
			uint8_t y_val, u_val, v_val;
			get_yuv_val(r->color, &y_val, &u_val, &v_val);
			draw_region_color(osd, r, y_buf, uv_buf, y_stride,
					  uv_stride, y_val, u_val, v_val);
			break;
		}
		case AIC_OSD_TYPE_EDGE_STROKE:
			break;
		default:
			break;
		}
	}
	return 0;
}
