/*
 * Copyright (C) 2020-2026 ArtInChip Technology Co. Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 *  author: <jianye.liang@artinchip.com>
 *  Desc: MPEG-PS demuxer
 *
 *  References:
 *    - ISO/IEC 13818-1 (MPEG-2 Systems)
 */

#define LOG_TAG "mpg"

// #define MPG_DEBUG 1

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "mpg.h"
#include "mpp_log.h"
#include "mpp_mem.h"
#include "aic_stream.h"

static inline uint32_t rb32(const unsigned char *p)
{
	return (p[0] << 24) | (p[1] << 16) | (p[2] << 8) | p[3];
}

static inline uint16_t rb16(const unsigned char *p)
{
	return (p[0] << 8) | p[1];
}

static inline int read_bit(const uint8_t *d, int *p, int n, int len)
{
	int v = 0;
	int max_bits = len * 8;
	int i;

	for (i = 0; i < n; i++) {
		if (*p >= max_bits)
			break;
		v = (v << 1) | ((d[*p >> 3] >> (7 - (*p & 7))) & 1);
		(*p)++;
	}
	return v;
}

static uint32_t read_ue_gelomb(const uint8_t *d, int *p, int len)
{
	int z = 0;
	int max_bits = len * 8;

	while (*p < max_bits && read_bit(d, p, 1, len) == 0 && z < 31)
		z++;
	if (*p >= max_bits)
		return 0;
	return z ? (1 << z) - 1 + read_bit(d, p, z, len) : 0;
}

static int parser_h264_sps(const uint8_t *sps, int len,
			   struct aic_codec_param *params)
{
	int prof, poc;

	if (len < 8)
		return -1;

	int p = 0;

	prof = read_bit(sps, &p, 8, len);
	read_bit(sps, &p, 16, len);
	read_ue_gelomb(sps, &p, len);

	if (prof != 66 && prof != 77 && prof != 88 && prof != 100) {
		logw("unsupported H264 profile %d", prof);
		return -1;
	}

	if (prof == 100) {
		read_ue_gelomb(sps, &p, len);
		read_ue_gelomb(sps, &p, len);
		read_ue_gelomb(sps, &p, len);
		read_bit(sps, &p, 1, len);
		if (read_bit(sps, &p, 1, len)) {
			loge("read_bit error");
			return -1;
		}
	}

	read_ue_gelomb(sps, &p, len);
	poc = read_ue_gelomb(sps, &p, len);
	if (poc == 0) {
		read_ue_gelomb(sps, &p, len);
	} else if (poc == 1) {
		read_bit(sps, &p, 1, len);
		read_ue_gelomb(sps, &p, len);
		read_ue_gelomb(sps, &p, len);
		int n = read_ue_gelomb(sps, &p, len);

		for (int i = 0; i < n; i++)
			read_ue_gelomb(sps, &p, len);
	}

	params->max_ref_frames = read_ue_gelomb(sps, &p, len);
	read_bit(sps, &p, 1, len);

	int w = read_ue_gelomb(sps, &p, len) + 1;
	int h = read_ue_gelomb(sps, &p, len) + 1;
	int f = read_bit(sps, &p, 1, len);

	params->width  = w * 16;
	params->height = (f ? h : h * 2) * 16;
	params->codec_id = CODEC_ID_H264;

	logi("H264 SPS: %dx%d", params->width, params->height);
	return 0;
}

static int find_start_code(const unsigned char *buf, int len, uint32_t code)
{
	int i;

	for (i = 0; i + 3 < len; i++) {
		if (buf[i] == 0 && buf[i + 1] == 0 && buf[i + 2] == 1 &&
		    buf[i + 3] == (code & 0xff))
			return i;
	}
	return -1;
}

static int detect_stream_type(uint32_t start_code)
{
	if (start_code >= 0x1E0 && start_code <= 0x1EF)
		return MPP_MEDIA_TYPE_VIDEO;
	if (start_code >= 0x1C0 && start_code <= 0x1DF)
		return MPP_MEDIA_TYPE_AUDIO;
	if (start_code == PRIVATE_STREAM_1)
		return MPP_MEDIA_TYPE_AUDIO;
	return MPP_MEDIA_TYPE_UNKNOWN;
}

static struct mpg_stream_ctx *add_stream(struct aic_mpg_parser *s,
					 int stream_id, int media_type)
{
	struct mpg_stream_ctx *st;

	if (s->ctx.nb_streams >= MPG_MAX_STREAMS)
		return NULL;

	st = (struct mpg_stream_ctx *)mpp_alloc(sizeof(*st));
	if (!st)
		return NULL;
	memset(st, 0, sizeof(*st));

	st->index = s->ctx.nb_streams;
	st->codecpar.codec_type = media_type;
	st->codecpar.codec_id = CODEC_ID_NONE;

	st->pes_buf = (unsigned char *)mpp_alloc(MPG_MAX_PES_SIZE);
	if (!st->pes_buf) {
		mpp_free(st);
		return NULL;
	}
	st->pes_buf_size = MPG_MAX_PES_SIZE;

	if (media_type == MPP_MEDIA_TYPE_VIDEO) {
		s->ctx.video_stream_index = s->ctx.nb_streams;
	} else {
		if (s->ctx.audio_stream_index < 0)
			s->ctx.audio_stream_index = s->ctx.nb_streams;
		st->codecpar.codec_id = CODEC_ID_MP3;
		st->codecpar.bits_per_coded_sample = 16;
	}

	s->ctx.streams[s->ctx.nb_streams++] = st;
	return st;
}

static int skip_pack_header(struct aic_stream *stream, int *is_mpeg2)
{
	unsigned char buf[12];
	int len;
	int stuffing = 0;

	len = aic_stream_read(stream, buf, 8);
	if (len < 8)
		return -1;

	int mpeg2 = ((buf[0] & 0xC0) == 0x40);

	if (is_mpeg2)
		*is_mpeg2 = mpeg2;

	if (mpeg2) {
		unsigned char ext[2];

		len = aic_stream_read(stream, ext, 2);
		if (len < 2)
			return -1;
		stuffing = ext[1] & 0x07;
	}

	if (stuffing > 0)
		aic_stream_seek(stream, stuffing, SEEK_CUR);

	return 0;
}

static int64_t parse_pes_header(struct aic_stream *stream, int *payload_len,
				int is_mpeg2)
{
	unsigned char buf[16];
	int len, pes_header_len = 0;
	int pts_dts_flags;
	int64_t pts = -1;

	len = aic_stream_read(stream, buf, 2);
	if (len < 2)
		return -1;
	*payload_len = rb16(buf);

	if (is_mpeg2) {
		len = aic_stream_read(stream, buf, 2);
		if (len < 2)
			return -1;
		pts_dts_flags = buf[1] >> 6;
		len = aic_stream_read(stream, buf, 1);
		if (len < 1)
			return -1;
		pes_header_len = buf[0];

		if (pts_dts_flags >= 2 && pes_header_len >= 5) {
			len = aic_stream_read(stream, buf, 5);
			if (len < 5)
				return -1;
			pts = ((int64_t)(buf[0] & 0x0E) << 29) |
			      ((int64_t)rb16(buf + 1) >> 1) |
			      ((int64_t)rb16(buf + 3) >> 1);
			pts /= 90;
			int remain = pes_header_len - 5;

			if (remain > 0)
				aic_stream_seek(stream, remain, SEEK_CUR);
		} else if (pes_header_len > 0) {
			aic_stream_seek(stream, pes_header_len, SEEK_CUR);
		}
		if (*payload_len > 0)
			*payload_len = *payload_len - 3 - pes_header_len;
	} else {
		/* MPEG-1 PES header */
		len = aic_stream_read(stream, buf, 1);
		if (len < 1)
			return -1;
		if (buf[0] == 0x0F) {
			pes_header_len = 1;
		} else {
			int ts_id = buf[0];
			int id = ts_id >> 4;

			if (id == 0x02 || id == 0x03) {
				len = aic_stream_read(stream, buf, 4);
				if (len < 4)
					return -1;
				pts = ((int64_t)(ts_id & 0x0E) << 29) |
				      ((int64_t)rb16(buf) >> 1) |
				      ((int64_t)rb16(buf + 2) >> 1);
				pts /= 90;
				pes_header_len = 5;
				if (id == 0x03) {
					aic_stream_seek(stream, 5, SEEK_CUR);
					pes_header_len += 5;
				}
			} else {
				pes_header_len = 1;
			}
		}
		if (*payload_len > 0)
			*payload_len = *payload_len - pes_header_len;
	}
	return pts;
}

static int read_payload(struct aic_stream *stream, unsigned char *dst,
			int max_len, int bounded_len)
{
	unsigned char peek[4];
	int total = 0;
	int need = bounded_len > 0 ? bounded_len : max_len;

	if (!dst || max_len <= 0)
		return 0;

	if (need > max_len)
		need = max_len;

	while (total < need) {
		int chunk = need - total;
		int64_t pos = aic_stream_tell(stream);
		int to_boundary = MPG_PACK_SIZE - (pos % MPG_PACK_SIZE);

		if (chunk > to_boundary)
			chunk = to_boundary;

		int64_t before = pos;
		int len = aic_stream_read(stream, peek, 4);

		if (len < 4)
			break;

		if (peek[0] == 0 && peek[1] == 0 && peek[2] == 1) {
			switch (peek[3]) {
			case 0xBA:
				skip_pack_header(stream, NULL);
				continue;
			case 0xBB: {
				unsigned char tmp[2];

				len = aic_stream_read(stream, tmp, 2);
				if (len < 2)
					break;
				aic_stream_seek(stream, rb16(tmp), SEEK_CUR);
				continue;
			}
			default:
				if (bounded_len <= 0 &&
				    peek[3] >= 0xBD && peek[3] <= 0xEF &&
				    peek[3] != 0xBE && peek[3] != 0xBF) {
					aic_stream_seek(stream, before,
							SEEK_SET);
					goto done;
				}
				break;
			}
		}

		aic_stream_seek(stream, before, SEEK_SET);
		len = aic_stream_read(stream, dst + total, chunk);
		if (len <= 0)
			break;
		total += len;
	}
done:
	return total;
}

static int find_frame_boundary(const unsigned char *buf, int len, int codec_id)
{
	int i;

	if (len < 68)
		return -1;

	if (codec_id == CODEC_ID_H264) {
		for (i = 64; i + 4 < len; i++) {
			if (buf[i] != 0 || buf[i + 1] != 0)
				continue;
			if (buf[i + 2] == 0 && buf[i + 3] == 1) {
				int nal = buf[i + 4] & 0x1F;

				if (nal == 1 || nal == 5 || nal == 7 ||
				    nal == 9)
					return i;
			} else if (buf[i + 2] == 1) {
				int nal = buf[i + 3] & 0x1F;

				if ((nal == 1 || nal == 5 || nal == 7 ||
				     nal == 9) &&
				    buf[i + 3] != 0x00 && buf[i + 3] != 0xB3)
					return i;
			}
		}
	} else {
		for (i = 64; i + 3 < len; i++) {
			if (buf[i] == 0 && buf[i + 1] == 0 &&
			    buf[i + 2] == 1) {
				unsigned char c = buf[i + 3];

				if (c == 0x00 || c == 0xB3)
					return i;
			}
		}
	}
	return -1;
}

static void setup_pkt_fields(struct aic_parser_packet *pkt,
			     struct mpg_stream_ctx *st,
			     int stream_index, int media_type,
			     int64_t pts, int64_t dts, int size)
{
	pkt->pts          = pts;
	pkt->dts          = dts;
	pkt->stream_index = stream_index;
	pkt->type         = media_type;
	pkt->flag         = 0;
	pkt->size         = size;
}

static int return_accumulated(struct mpg_stream_ctx *st,
			      struct aic_parser_packet *pkt,
			      int consume, int size)
{
	if (!consume)
		return PARSER_OK; /* Peek: keep data in pes_buf */

	if (size <= 0)
		return PARSER_OK;

	if (pkt->data)
		memcpy(pkt->data, st->pes_buf, size);
	else
		pkt->data = st->pes_buf;

	st->pes_data_len -= size;
	if (st->pes_data_len > 0)
		memmove(st->pes_buf, st->pes_buf + size, st->pes_data_len);
	return PARSER_OK;
}

static int skip_ps_header(struct aic_stream *stream, int stream_id,
			  int *is_mpeg2)
{
	int len;
	unsigned char hdr[2];

	if (stream_id == 0xBA) {
		skip_pack_header(stream, is_mpeg2);
		return 1;
	}
	if (stream_id == 0xBB || stream_id == 0xBE) {
		len = aic_stream_read(stream, hdr, 2);
		if (len < 2)
			return -1;
		aic_stream_seek(stream, rb16(hdr), SEEK_CUR);
		return 1;
	}
	if (stream_id < 0xBD || stream_id == 0xBF || stream_id > 0xEF) {
		return 1;
	}
	return 0;
}

static int handle_non_matching(struct aic_mpg_parser *s, int target_stream,
			       struct aic_parser_packet *pkt, int consume,
			       int64_t before_scan, struct mpg_stream_ctx *st)
{
	aic_stream_seek(s->stream, before_scan, SEEK_SET);
	if (!consume) {
		if (st && st->pes_data_len > 0) {
			int boundary = find_frame_boundary(st->pes_buf,
						       st->pes_data_len,
						       st->codecpar.codec_id);
			if (boundary > 0) {
				setup_pkt_fields(pkt, st, target_stream,
						 MPP_MEDIA_TYPE_VIDEO,
						 st->cur_pts, st->cur_dts,
						 boundary);
				return return_accumulated(st, pkt, 0,
							  boundary);
			}
		}
		return PARSER_EOS;
	}

	pkt->size = 0;
	return PARSER_OK;
}

static int flush_video_frame(struct mpg_stream_ctx *st, int target_stream,
			     struct aic_parser_packet *pkt, int consume)
{
	int boundary;

	if (!st || st->pes_data_len <= 0)
		return -1;

	boundary = find_frame_boundary(st->pes_buf, st->pes_data_len,
				       st->codecpar.codec_id);
	if (boundary <= 0)
		return -1;

	setup_pkt_fields(pkt, st, target_stream, MPP_MEDIA_TYPE_VIDEO,
			 st->cur_pts, st->cur_dts, boundary);
	logd("[rnp] frame: boundary=%d accum=%d consume=%d",
	     boundary, st->pes_data_len, consume);
	return return_accumulated(st, pkt, consume, boundary);
}

static int handle_audio_pes(struct aic_stream *stream, struct mpg_stream_ctx *st,
			    int64_t before_scan, struct aic_parser_packet *pkt,
			    int consume, int payload_len)
{
	int total;

	if (!consume) {
		aic_stream_seek(stream, before_scan, SEEK_SET);
		pkt->data = NULL;
		pkt->size = (payload_len > 0) ? payload_len : (MPG_MAX_PES_SIZE - 32);
		return PARSER_OK;
	}

	total = read_payload(stream, st->pes_buf, st->pes_buf_size, payload_len);
	if (total <= 0)
		return -1;

	pkt->size = total;
	if (pkt->data)
		memcpy(pkt->data, st->pes_buf, total);
	return PARSER_OK;
}

static int handle_video_payload(struct aic_stream *stream,
				struct mpg_stream_ctx *st,
				struct aic_parser_packet *pkt,
				int consume, int payload_len)
{
	int offset, buf_remain, total, boundary;

	offset = st ? st->pes_data_len : 0;
	buf_remain = (st && st->pes_buf_size > offset) ?
		      st->pes_buf_size - offset : 0;

	if (!st || buf_remain <= 0) {
		if (st && st->pes_data_len > 0) {
			pkt->size = st->pes_data_len;
			return return_accumulated(st, pkt, consume,
						  st->pes_data_len);
		}
		return PARSER_OK;
	}

	total = read_payload(stream, st->pes_buf + offset,
			     buf_remain, payload_len);
	if (total <= 0)
		return -1;

	st->pes_data_len = offset + total;
#ifdef MPG_DEBUG
	if (offset == 0 && total >= 16) {
		printf("[rnp] payload head: "
		       "%02x%02x%02x%02x %02x%02x%02x%02x "
		       "%02x%02x%02x%02x %02x%02x%02x%02x\n",
		       st->pes_buf[0], st->pes_buf[1],
		       st->pes_buf[2], st->pes_buf[3],
		       st->pes_buf[4], st->pes_buf[5],
		       st->pes_buf[6], st->pes_buf[7],
		       st->pes_buf[8], st->pes_buf[9],
		       st->pes_buf[10], st->pes_buf[11],
		       st->pes_buf[12], st->pes_buf[13],
		       st->pes_buf[14], st->pes_buf[15]);
	}
	printf("[rnp] read total=%d accum=%d\n", total, st->pes_data_len);
#endif
	boundary = find_frame_boundary(st->pes_buf, st->pes_data_len,
				       st->codecpar.codec_id);
	if (boundary > 0) {
#ifdef MPG_DEBUG
		printf("[rnp] frame complete: boundary=%d accum=%d\n",
		       boundary, st->pes_data_len);
#endif
		pkt->size = boundary;
		return return_accumulated(st, pkt, consume, boundary);
	}

	if (!consume)
		pkt->size = st->pes_data_len + (MPG_MAX_PES_SIZE / 4);

	logd("[rnp] no boundary yet, accum=%d, continue", st->pes_data_len);
	return -1;
}

static int read_next_pes(struct aic_mpg_parser *s, int target_stream,
			 struct aic_parser_packet *pkt, int consume)
{
	unsigned char header[4];
	int len, stream_id, ret;
	int is_video = (target_stream == s->ctx.video_stream_index);
	int64_t before_scan;
	struct mpg_stream_ctx *st = NULL;

	if (target_stream >= 0 && target_stream < s->ctx.nb_streams)
		st = s->ctx.streams[target_stream];

	/* Return accumulated frame if complete */
	if (is_video) {
		ret = flush_video_frame(st, target_stream, pkt, consume);
		if (ret != -1)
			return ret;
	}

	for (;;) {
		before_scan = aic_stream_tell(s->stream);
		len = aic_stream_read(s->stream, header, 4);
		if (len < 4) {
			/* EOS: flush any remaining accumulated data */
			if (st && st->pes_data_len > 0) {
				setup_pkt_fields(pkt, st, target_stream,
						 MPP_MEDIA_TYPE_VIDEO,
						 st->cur_pts, st->cur_dts,
						 st->pes_data_len);
				return return_accumulated(st, pkt, consume,
							  st->pes_data_len);
			}
			pkt->flag = PACKET_FLAG_EOS;
			pkt->size = 0;
			return PARSER_EOS;
		}

		if (header[0] != 0 || header[1] != 0 || header[2] != 1) {
			aic_stream_seek(s->stream, -3, SEEK_CUR);
			continue;
		}

		stream_id = header[3];

		len = skip_ps_header(s->stream, stream_id, &s->ctx.is_mpeg2);
		if (len == 1)
			continue;
		if (len < 0)
			return PARSER_EOS;

		int payload_len = 0;
		int64_t pts;

		pts = parse_pes_header(s->stream, &payload_len,
				      s->ctx.is_mpeg2);
#ifdef MPG_DEBUG
		printf("[rnp] PES hdr: stream=0x%02x mpeg2=%d len=%d"
		       " pts=%lld\n",
		       stream_id, s->ctx.is_mpeg2, payload_len,
		       (long long)pts);
#endif

		int media_type = detect_stream_type(0x100 | stream_id);
		int stream_idx = (media_type == MPP_MEDIA_TYPE_VIDEO) ?
				 s->ctx.video_stream_index :
				 s->ctx.audio_stream_index;

		/* Non-matching stream */
		if (stream_idx < 0 || stream_idx != target_stream)
			return handle_non_matching(s, target_stream, pkt,
						   consume, before_scan, st);

#ifdef MPG_DEBUG
		printf("[rnp] found PES stream=0x%02x idx=%d len=%d\n",
		       stream_id, stream_idx, payload_len);
#endif

		/* Save PTS from the first PES of a merged frame */
		if (st && st->pes_data_len == 0) {
			st->cur_pts = pts;
			st->cur_dts = pts;
		}

		setup_pkt_fields(pkt, st, stream_idx, media_type,
				 (st && st->pes_data_len > 0) ?
				 st->cur_pts : pts,
				 (st && st->pes_data_len > 0) ?
				 st->cur_dts : pts, 0);

		/* Audio: single PES, return immediately */
		if (!is_video) {
			ret = handle_audio_pes(s->stream, st, before_scan,
					       pkt, consume, payload_len);
			if (ret == PARSER_OK)
				return PARSER_OK;
			continue;
		}

		/* Video: accumulate into pes_buf */
		ret = handle_video_payload(s->stream, st, pkt, consume,
					   payload_len);
		if (ret == PARSER_OK)
			return PARSER_OK;
	}
}

static int mpg_probe(struct aic_mpg_parser *s)
{
	unsigned char buf[4];
	int len;

	len = aic_stream_read(s->stream, buf, 4);
	if (len < 4)
		return -1;

	if (rb32(buf) != PACK_START_CODE)
		return -1;

	aic_stream_seek(s->stream, 0, SEEK_SET);
	return 0;
}

static int parse_video_seq_header(const unsigned char *buf, int len,
				  struct aic_codec_param *params)
{
	int off = find_start_code(buf, len, 0xB3);

	if (off < 0 || off + 8 >= len)
		return -1;

	const unsigned char *p = buf + off + 4;

	params->width  = (p[0] << 4) | (p[1] >> 4);
	params->height = ((p[1] & 0x0F) << 8) | p[2];
	params->codec_id = CODEC_ID_MPEG12;

	logi("MPEG2 seq: %dx%d", params->width, params->height);
	return 0;
}

static void parse_audio_params(struct mpg_stream_ctx *ast,
			       const unsigned char *buf, int len)
{
	int i;

	if (ast->codecpar.sample_rate > 0)
		return;

	for (i = 0; i + 3 < len; i++) {
		const unsigned char *p = buf + i;

		if (p[0] != 0xFF || (p[1] & 0xE0) != 0xE0)
			continue;
		if ((p[1] & 0x18) == 0x08)
			continue;
		if ((p[1] & 0x06) == 0x00)
			continue;
		if ((p[2] & 0xF0) == 0xF0)
			continue;
		if ((p[2] & 0x0C) == 0x0C)
			continue;

		static const int freq_tab[3] = { 44100, 48000, 32000 };
		int version = (p[1] >> 3) & 0x03;
		int sr_idx  = (p[2] >> 2) & 0x03;
		int mode    = (p[3] >> 6) & 0x03;

		if (sr_idx >= 3)	/* reserved, skip */
			continue;
		ast->codecpar.sample_rate = freq_tab[sr_idx];
		if (version == 0)      /* MPEG-2.5 */
			ast->codecpar.sample_rate >>= 2;
		else if (version == 2) /* MPEG-2 */
			ast->codecpar.sample_rate >>= 1;

		ast->codecpar.channels = (mode == 3) ? 1 : 2;
#ifdef MPG_DEBUG
		printf("mpg: audio parsed: channels=%d, sample_rate=%d\n",
		       ast->codecpar.channels, ast->codecpar.sample_rate);
#endif
		return;
	}
	logw("mpg: no valid audio frame header found in probe buffer");
}

static void probe_scan_streams(struct aic_mpg_parser *s,
			       const unsigned char *buf, int len)
{
	int scanned = 0;

	while (scanned + 4 < len) {
		if (buf[scanned] != 0 || buf[scanned + 1] != 0 ||
		    buf[scanned + 2] != 1) {
			scanned++;
			continue;
		}

		int stream_id = buf[scanned + 3];

		if (stream_id == 0xBA) {
			scanned += 4;
			if (scanned + 8 <= len) {
				int mpeg2;

				mpeg2 = ((buf[scanned] & 0xC0) == 0x40);
				scanned += mpeg2 ? 12 : 8;
			}
			continue;
		}

		if (stream_id == 0xBB) {
			scanned += 4;
			if (scanned + 2 <= len)
				scanned += rb16(buf + scanned);
			continue;
		}

		if (stream_id < 0xBD || stream_id == 0xBF ||
		    stream_id > 0xEF) {
			scanned++;
			continue;
		}

		int media_type = detect_stream_type(0x100 | stream_id);

		if (media_type == MPP_MEDIA_TYPE_UNKNOWN) {
			scanned += 4;
			continue;
		}

		int found = 0;

		for (int i = 0; i < s->ctx.nb_streams; i++) {
			if (s->ctx.streams[i]->codecpar.codec_type ==
			    media_type) {
				found = 1;
				break;
			}
		}

		if (!found)
			add_stream(s, stream_id, media_type);

		if (scanned + 6 <= len) {
			int pes_pkt_len = rb16(buf + scanned + 4);

			if (pes_pkt_len > 0 &&
			    scanned + 6 + pes_pkt_len <= len)
				scanned += 6 + pes_pkt_len;
			else
				scanned += 4;
		} else {
			scanned += 4;
		}
	}
}

static void detect_video_codec(struct aic_mpg_parser *s,
			       const unsigned char *buf, int len)
{
	struct mpg_stream_ctx *vst;
	int i, off = -1;

	if (s->ctx.video_stream_index < 0)
		return;

	vst = s->ctx.streams[s->ctx.video_stream_index];
	if (vst->codecpar.codec_id != CODEC_ID_NONE)
		return;

	for (i = 0; i + 4 < len; i++) {
		if (buf[i] == 0 && buf[i + 1] == 0 &&
		    buf[i + 2] == 1 && (buf[i + 3] & 0x1F) == 7) {
			off = i;
			break;
		}
	}
	if (off >= 0 && off + 8 < len &&
	    parser_h264_sps(buf + off + 4, len - off - 4, &vst->codecpar) == 0) {
		logi("mpg: detected H264 video");
	} else if (parse_video_seq_header(buf, len, &vst->codecpar) < 0) {
		loge("neither H264 nor MPEG2");
	}
}

int mpg_read_header(struct aic_mpg_parser *s)
{
	unsigned char *buf;
	int len, pack_count, video_ok, audio_ok;
	int64_t file_size;

	if (mpg_probe(s) < 0) {
		loge("mpg_probe failed");
		return -1;
	}

	file_size = aic_stream_size(s->stream);
	s->ctx.file_size = file_size;
	s->ctx.audio_stream_index = -1;
	s->ctx.video_stream_index = -1;
	s->ctx.last_peeked_stream = -1;
	s->ctx.last_read_stream = -1;
	video_ok = 0;
	audio_ok = 0;

	buf = (unsigned char *)mpp_alloc(MPG_PACK_SIZE);
	if (!buf) {
		loge("mpp_alloc probe buffer failed");
		return -1;
	}

	for (pack_count = 0; pack_count < 16; pack_count++) {
		len = aic_stream_read(s->stream, buf, MPG_PACK_SIZE);
		if (len < 12)
			break;

		probe_scan_streams(s, buf, len);

		if (!video_ok && s->ctx.video_stream_index >= 0) {
			detect_video_codec(s, buf, len);
			if (s->ctx.streams[s->ctx.video_stream_index]->codecpar.codec_id
			    != CODEC_ID_NONE)
				video_ok = 1;
		}
		if (!audio_ok && s->ctx.audio_stream_index >= 0) {
			parse_audio_params(
				s->ctx.streams[s->ctx.audio_stream_index], buf, len);
			if (s->ctx.streams[s->ctx.audio_stream_index]->codecpar.sample_rate
			    > 0)
				audio_ok = 1;
		}

		if ((video_ok || (s->ctx.video_stream_index < 0 && pack_count >= 3)) &&
		    (audio_ok || (s->ctx.audio_stream_index < 0 && pack_count >= 3)))
			break;
	}

	mpp_free(buf);

	if (s->ctx.video_stream_index >= 0 && !video_ok)
		logw("no video codec info found in probe");
	if (s->ctx.audio_stream_index >= 0 && !audio_ok)
		logw("no audio params found in probe");

	aic_stream_seek(s->stream, 0, SEEK_SET);
#ifdef MPG_DEBUG
	printf("mpg: %d streams (video=%d, audio=%d) probe=%d PACKs\n",
	       s->ctx.nb_streams, s->ctx.video_stream_index,
	       s->ctx.audio_stream_index, pack_count);
#endif
	return 0;
}

void mpg_read_close(struct aic_mpg_parser *s)
{
	int i;

	for (i = 0; i < s->ctx.nb_streams; i++) {
		if (s->ctx.streams[i]) {
			if (s->ctx.streams[i]->pes_buf) {
				mpp_free(s->ctx.streams[i]->pes_buf);
				s->ctx.streams[i]->pes_buf = NULL;
			}
			mpp_free(s->ctx.streams[i]);
			s->ctx.streams[i] = NULL;
		}
	}
	s->ctx.nb_streams = 0;
}

int mpg_peek_packet(struct aic_mpg_parser *s, struct aic_parser_packet *pkt)
{
	int i, ret;

	memset(pkt, 0, sizeof(*pkt));

	/* If last read was audio, try video first for decoder pacing */
	if (s->ctx.last_read_stream == s->ctx.audio_stream_index &&
	    s->ctx.video_stream_index >= 0) {

		ret = read_next_pes(s, s->ctx.video_stream_index, pkt, 0);
		if (ret == PARSER_OK) {
			s->ctx.last_peeked_stream =
				s->ctx.video_stream_index;
			return PARSER_OK;
		}
	}

	for (i = 0; i < s->ctx.nb_streams; i++) {
		ret = read_next_pes(s, i, pkt, 0);

		if (ret == PARSER_OK) {
			s->ctx.last_peeked_stream = i;
			return PARSER_OK;
		}
	}
	return PARSER_EOS;
}

int mpg_read_packet(struct aic_mpg_parser *s, struct aic_parser_packet *pkt)
{
	int i, ret;

	if (s->ctx.last_peeked_stream >= 0) {
		int stream_idx = s->ctx.last_peeked_stream;

		s->ctx.last_peeked_stream = -1;
		s->ctx.last_read_stream = -1;
		ret = read_next_pes(s, stream_idx, pkt, 1);
		if (ret == PARSER_OK && pkt->size > 0) {
			s->ctx.last_read_stream = stream_idx;
			logd("mpg_read: frame=%d size=%d",
			     stream_idx, pkt->size);
			return PARSER_OK;
		}
	}

	for (i = 0; i < s->ctx.nb_streams; i++) {
		ret = read_next_pes(s, i, pkt, 1);
		if (ret == PARSER_OK && pkt->size > 0) {
			s->ctx.last_read_stream = i;
			logd("mpg_read: frame=%d size=%d",
			     i, pkt->size);
			return PARSER_OK;
		}
	}

	return PARSER_EOS;
}
