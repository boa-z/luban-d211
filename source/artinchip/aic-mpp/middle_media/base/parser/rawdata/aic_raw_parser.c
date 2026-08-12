/*
 * Copyright (C) 2020-2026 ArtInChip Technology Co. Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 *  author: <qi.xu@artinchip.com>
 *  Desc: parser for H.264/H.265 raw data
 */

#include <unistd.h>
#include <string.h>
#include <stddef.h>
#include <stdlib.h>
#include <inttypes.h>
#include <fcntl.h>
#include "aic_raw_parser.h"
#include "mpp_log.h"
#include "mpp_mem.h"
#include "mpp_dec_type.h"
#include "aic_stream.h"

#define STEAM_BUF_LEN (512*1024)

struct aic_raw_parser {
	struct aic_parser base;
	struct aic_stream* stream;

	unsigned char* stream_buf;
	int cur_read_pos;
	int buf_len;
	int valid_size;
	int cur_read_len;
	int stream_end_flag;
	int packet_index;

	/* cached SPS info */
	int width, height, fps;
};

/* -- bit helpers (copied from mpegts.c) -- */
static uint32_t read_bit(const uint8_t *d, int *p, int n, int len)
{
	uint32_t v = 0;
	int max = len * 8;

	for (int i = 0; i < n && *p < max; i++, (*p)++)
		v = (v << 1) | ((d[*p >> 3] >> (7 - (*p & 7))) & 1);
	return v;
}

static uint32_t read_ue(const uint8_t *d, int *p, int len)
{
	int z = 0, max = len * 8;

	while (*p < max && read_bit(d, p, 1, len) == 0 && z < 31)
		z++;
	if (*p >= max)
		return 0;
	return z ? (1 << z) - 1 + read_bit(d, p, z, len) : 0;
}

/* EPB strip */
static int rbsp_clean(const uint8_t *n, int len, uint8_t *o, int max)
{
	int w = 0;

	for (int i = 0; i < len && w < max; i++) {
		if (i + 2 < len && !n[i] && !n[i + 1] && n[i + 2] == 3) {
			o[w++] = 0;
			o[w++] = 0;
			i += 2;
			continue;
		}
		o[w++] = n[i];
	}
	return (w < max) ? w : -1;
}

/*
 * Parse SPS RBSP (no NAL header, EPB already stripped).
 * Adapted from mpegts.c:parser_h264_sps + VUI timing.
 */
static int sps_parse(const uint8_t *d, int len, int *w, int *h, int *fps)
{
	int p = 0;

	*w = 0;
	*h = 0;
	*fps = 0;
	if (len < 7)
		return -1;

	int prof = read_bit(d, &p, 8, len);

	read_bit(d, &p, 16, len); /* constraint + level */
	read_ue(d, &p, len);      /* sps_id */

	if (prof == 100) { /* High */
		read_ue(d, &p, len);
		read_ue(d, &p, len);
		read_ue(d, &p, len);
		read_bit(d, &p, 1, len);
		if (read_bit(d, &p, 1, len))
			return -1;
	}

	read_ue(d, &p, len); /* log2_max_frame_num */
	int poc = read_ue(d, &p, len);

	if (poc == 0) {
		read_ue(d, &p, len);
	} else if (poc == 1) {
		read_bit(d, &p, 1, len);
		read_ue(d, &p, len);
		read_ue(d, &p, len);
		int n = read_ue(d, &p, len);

		while (n--)
			read_ue(d, &p, len);
	}

	read_ue(d, &p, len);     /* max_num_ref_frames */
	read_bit(d, &p, 1, len); /* gaps */
	int pw = read_ue(d, &p, len) + 1;
	int ph = read_ue(d, &p, len) + 1;
	int fm = read_bit(d, &p, 1, len);

	*w = pw * 16;
	*h = (fm ? ph : ph * 2) * 16;

	if (!fm)
		read_bit(d, &p, 1, len);
	read_bit(d, &p, 1, len);

	if (read_bit(d, &p, 1, len)) { /* crop */
		read_ue(d, &p, len);
		read_ue(d, &p, len);
		read_ue(d, &p, len);
		read_ue(d, &p, len);
	}

	/* VUI */
	if (read_bit(d, &p, 1, len)) {
		/* aspect_ratio */
		if (read_bit(d, &p, 1, len)) {
			int idc = read_bit(d, &p, 8, len);

			if (idc == 255) {
				read_bit(d, &p, 16, len);
				read_bit(d, &p, 16, len);
			}
		}
		/* overscan */
		if (read_bit(d, &p, 1, len))
			read_bit(d, &p, 1, len);
		/* video_signal */
		if (read_bit(d, &p, 1, len)) {
			read_bit(d, &p, 3, len);
			read_bit(d, &p, 1, len);
			if (read_bit(d, &p, 1, len)) {
				read_bit(d, &p, 8, len);
				read_bit(d, &p, 8, len);
				read_bit(d, &p, 8, len);
			}
		}
		/* chroma_loc */
		if (read_bit(d, &p, 1, len)) {
			read_ue(d, &p, len);
			read_ue(d, &p, len);
		}
		/* timing */
		if (read_bit(d, &p, 1, len)) {
			int nu = read_bit(d, &p, 32, len);
			int ts = read_bit(d, &p, 32, len);

			read_bit(d, &p, 1, len);
			if (nu > 0 && ts > 0)
				*fps = ts / (2 * nu);
		}
	}
	return 0;
}

static int get_data(struct aic_raw_parser* p)
{
	int r_len = 0;

	if(p->valid_size <= 0) {
		r_len = aic_stream_read(p->stream, p->stream_buf, p->buf_len);
		if(r_len <= 0) {
			return r_len;
		}
	} else {
		memmove(p->stream_buf, (p->stream_buf + p->cur_read_pos), p->valid_size);

		int len = p->buf_len - p->valid_size;
		r_len = aic_stream_read(p->stream, p->stream_buf + p->valid_size, len);
		if(r_len < 0) {
			return r_len;
		}
	}

	p->cur_read_len += r_len;
	p->valid_size += r_len;
	p->cur_read_pos = 0;
	return r_len;
}

s32 raw_peek(struct aic_parser *parser ,struct aic_parser_packet *pkt)
{
	int i = 0;
	char tmp_buf[3];
	int find_start_code = 0;
	int start = 0;
	int stream_data_len = -1;
	int ret = 0;
	char *cur_data_ptr = NULL;
	struct aic_raw_parser *p = (struct aic_raw_parser*)parser;

	if (p->stream_end_flag) {
		return PARSER_EOS;
	}

	if (p->valid_size <= 0) {
		if (get_data(p) < 0) {
			loge("get data error");
			return -1;
		}
	}
	pkt->flag = 0; 
	pkt->type = MPP_MEDIA_TYPE_VIDEO;

find_start_code:
	cur_data_ptr = (char *)(p->stream_buf + p->cur_read_pos);
	logd("data: %x, %x, %x, %x, %x, %x, %x, %x", *(cur_data_ptr), *(cur_data_ptr + 1),
		*(cur_data_ptr+2),*(cur_data_ptr+3),
		*(cur_data_ptr+4),*(cur_data_ptr+5),*(cur_data_ptr+6),*(cur_data_ptr+7));

	// find the first start_code
	for(i = 0; i < (p->valid_size - 3); i++) {
		tmp_buf[0] = *(cur_data_ptr + i);
		tmp_buf[1] = *(cur_data_ptr + i + 1);
		tmp_buf[2] = *(cur_data_ptr + i + 2);
		if(tmp_buf[0] == 0 && tmp_buf[1] == 0 && tmp_buf[2] == 1) {
			find_start_code = 1;
			break;
		}
	}

	logd("find_start_code = %d, i = %d, validSize = %d",\
		find_start_code, i, p->valid_size);
	if(find_start_code == 1) {
		p->cur_read_pos += i;
		start = i;

		// if the last byte is 0x00, read_pos minus 1
		if (p->cur_read_pos && (*(cur_data_ptr + i -1) == 0)) {
			p->cur_read_pos -= 1;
			start -= 1;
		}
		find_start_code = 0;

		// find the next start code
		for(i += 3; i < (p->valid_size - 3); i++) {
			logv("cur_data_ptr = %p, i = %d", cur_data_ptr, i);
			tmp_buf[0] = *(cur_data_ptr + i);
			tmp_buf[1] = *(cur_data_ptr + i + 1);
			tmp_buf[2] = *(cur_data_ptr + i + 2);
			if(tmp_buf[0] == 0 && tmp_buf[1] == 0 && tmp_buf[2] == 1) {
				find_start_code = 1;
				break;
			}
		}

		if(find_start_code == 1) {
			if(*(cur_data_ptr + i - 1) == 0) {
				stream_data_len = i - start - 1;
			} else {
				stream_data_len = i - start;
			}
		} else {
			ret = get_data(p);
			if(ret == -1)
				return -1;
			if(ret == 0) {
				logi("eos, file_size: %"PRId64", cur_read: %d", aic_stream_size(p->stream), p->cur_read_len);
				stream_data_len = p->valid_size - start;
				pkt->flag |= PACKET_FLAG_EOS;
				pkt->size = stream_data_len;
				p->stream_end_flag = 1;
				return 0;
			}

			goto find_start_code;
		}
	} else {
		ret = get_data(p);
		if (ret == -1 || ret == 0) {
			return -1;
		}

		goto find_start_code;
	}

	pkt->size = stream_data_len;
	return 0;
}

s32 raw_read(struct aic_parser *parser ,struct aic_parser_packet *pkt)
{
	struct aic_raw_parser *p = (struct aic_raw_parser*)parser;
	if(pkt->size <= 0)
		return -1;

	char* read_ptr = (char*)(p->stream_buf + p->cur_read_pos);

	logd("read data: %x, %x, %x, %x", *read_ptr,*(read_ptr+1), *(read_ptr+2), *(read_ptr+3));

	memcpy(pkt->data, read_ptr, pkt->size);

	pkt->pts = p->packet_index*33000;/* default 30 fps*/
	p->packet_index++;

	p->cur_read_pos += pkt->size;
	p->valid_size -= pkt->size;

	return 0;
}

s32 raw_get_media_info(struct aic_parser *parser ,struct aic_parser_av_media_info *media)
{
	struct aic_raw_parser *p = (struct aic_raw_parser *)parser;

	media->has_audio = 0;
	media->has_video = 1;
	media->seek_able = 0;
	media->duration = 0;
	media->video_stream.codec_type = MPP_CODEC_VIDEO_DECODER_H264;
	media->video_stream.width  = p->width;
	media->video_stream.height = p->height;
	media->video_stream.frame_rate = p->fps > 0 ? p->fps : 30;

	return 0;
}

s32 raw_seek(struct aic_parser *parser , s64 time)
{
	// not support
	return -1;
}

s32 raw_init(struct aic_parser *parser)
{
	struct aic_raw_parser *p = (struct aic_raw_parser *)parser;
	int n, pos = 0;

	if (!p)
		return -1;

	if (p->valid_size <= 0) {
		n = aic_stream_read(p->stream, p->stream_buf, p->buf_len);
		if (n < 10)
			return -1;
		p->valid_size = n;
	} else {
		n = p->valid_size;
	}

	while (pos < n - 4) {
		int sc = 0;
		if (p->stream_buf[pos] == 0 && p->stream_buf[pos + 1] == 0) {
			if (p->stream_buf[pos + 2] == 1)
				sc = 3;
			else if (p->stream_buf[pos + 2] == 0 &&
				 p->stream_buf[pos + 3] == 1)
				sc = 4;
		}
		if (sc) {
			int t = p->stream_buf[pos + sc] & 0x1F;
			if (t == 7) { /* SPS */
				int end = pos + sc + 1;
				while (end < n - 3) {
					if (p->stream_buf[end] == 0 &&
					    p->stream_buf[end + 1] == 0 &&
					    (p->stream_buf[end + 2] == 1 ||
					     (p->stream_buf[end + 2] == 0 &&
					      p->stream_buf[end + 3] == 1)))
						break;
					end++;
				}
				/* NAL hdr at pos+sc, RBSP at pos+sc+1, len = end-pos-sc-1 */
				int nal_len = end - pos - sc;
				uint8_t clean[256];
				int clen = rbsp_clean(p->stream_buf + pos + sc + 1,
						      nal_len - 1, clean, sizeof(clean));
				if (clen > 0)
					sps_parse(clean, clen,
						  &p->width, &p->height,
						  &p->fps);
				return 0;
			}
			pos += sc + 1;
		} else {
			pos++;
		}
	}

	aic_stream_seek(p->stream, 0, SEEK_SET);
	return 0;
}

s32 raw_destroy(struct aic_parser *parser)
{
	struct aic_raw_parser *impl = (struct aic_raw_parser *)parser;
	if (impl == NULL) {
		return -1;
	}

	aic_stream_close(impl->stream);
	mpp_free(impl->stream_buf);
	mpp_free(impl);
	return 0;
}

s32 aic_raw_parser_create(unsigned char *uri, struct aic_parser **parser)
{
	struct aic_raw_parser *impl = NULL;

	impl = (struct aic_raw_parser *)mpp_alloc(sizeof(struct aic_raw_parser));
	if (impl == NULL) {
		loge("mpp_alloc raw_parser failed!!!!!\n");
		return -1;
	}
	memset(impl, 0, sizeof(struct aic_raw_parser));

	impl->stream_buf = (unsigned char*)mpp_alloc(STEAM_BUF_LEN);
	if (!impl->stream_buf) {
		loge("mpp_alloc fail !!!!\n");
		goto exit;
	}
	impl->buf_len = STEAM_BUF_LEN;

	if (aic_stream_open((char *)uri, &impl->stream, O_RDONLY) < 0) {
		loge("stream open fail");
		goto exit;
	}
	impl->base.get_media_info	= raw_get_media_info;
	impl->base.peek			= raw_peek;
	impl->base.read			= raw_read;
	impl->base.control		= NULL;
	impl->base.destroy		= raw_destroy;
	impl->base.seek			= raw_seek;
	impl->base.init			= raw_init;

	*parser = &impl->base;
	return 0;

exit:
	if (impl->stream) {
		aic_stream_close(impl->stream);
	}
	if (impl->stream_buf) {
		mpp_free(impl->stream_buf);
	}
	if (impl) {
		mpp_free(impl);
	}
	return -1;
}
