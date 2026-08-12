/*
 * Copyright (C) 2024-2026 ArtInChip Technology Co. Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Desc: MPEG-TS muxer — PAT/PMT/PES encapsulation, 188-byte TS packets
 *
 * Reference: ISO/IEC 13818-1, FFmpeg libavformat/mpegtsenc.c
 */

#define LOG_TAG "ts_muxer"

#include <string.h>
#include <stddef.h>
#include <stdint.h>
#include "ts_muxer.h"
#include "mpp_log.h"
#include "mpp_mem.h"
#include "aic_middle_media_common.h"

#define TS_PACKET_SIZE     188
#define TS_PID_PAT         0x0000
#define TS_PID_PMT         0x1000
#define TS_PID_SDT         0x0011
#define TS_PID_VIDEO       0x0100
#define TS_PID_AUDIO       0x0101
#define TS_AUDIO_STREAM_ID 0xc0
#define TS_VIDEO_STREAM_ID 0xe0

static const u32 crc_table[256] = {
	0x00000000, 0x04c11db7, 0x09823b6e, 0x0d4326d9, 0x130476dc, 0x17c56b6b,
	0x1a864db2, 0x1e475005, 0x2608edb8, 0x22c9f00f, 0x2f8ad6d6, 0x2b4bcb61,
	0x350c9b64, 0x31cd86d3, 0x3c8ea00a, 0x384fbdbd, 0x4c11db70, 0x48d0c6c7,
	0x4593e01e, 0x4152fda9, 0x5f15adac, 0x5bd4b01b, 0x569796c2, 0x52568b75,
	0x6a1936c8, 0x6ed82b7f, 0x639b0da6, 0x675a1011, 0x791d4014, 0x7ddc5da3,
	0x709f7b7a, 0x745e66cd, 0x9823b6e0, 0x9ce2ab57, 0x91a18d8e, 0x95609039,
	0x8b27c03c, 0x8fe6dd8b, 0x82a5fb52, 0x8664e6e5, 0xbe2b5b58, 0xbaea46ef,
	0xb7a96036, 0xb3687d81, 0xad2f2d84, 0xa9ee3033, 0xa4ad16ea, 0xa06c0b5d,
	0xd4326d90, 0xd0f37027, 0xddb056fe, 0xd9714b49, 0xc7361b4c, 0xc3f706fb,
	0xceb42022, 0xca753d95, 0xf23a8028, 0xf6fb9d9f, 0xfbb8bb46, 0xff79a6f1,
	0xe13ef6f4, 0xe5ffeb43, 0xe8bccd9a, 0xec7dd02d, 0x34867077, 0x30476dc0,
	0x3d044b19, 0x39c556ae, 0x278206ab, 0x23431b1c, 0x2e003dc5, 0x2ac12072,
	0x128e9dcf, 0x164f8078, 0x1b0ca6a1, 0x1fcdbb16, 0x018aeb13, 0x054bf6a4,
	0x0808d07d, 0x0cc9cdca, 0x7897ab07, 0x7c56b6b0, 0x71159069, 0x75d48dde,
	0x6b93dddb, 0x6f52c06c, 0x6211e6b5, 0x66d0fb02, 0x5e9f46bf, 0x5a5e5b08,
	0x571d7dd1, 0x53dc6066, 0x4d9b3063, 0x495a2dd4, 0x44190b0d, 0x40d816ba,
	0xaca5c697, 0xa864db20, 0xa527fdf9, 0xa1e6e04e, 0xbfa1b04b, 0xbb60adfc,
	0xb6238b25, 0xb2e29692, 0x8aad2b2f, 0x8e6c3698, 0x832f1041, 0x87ee0df6,
	0x99a95df3, 0x9d684044, 0x902b669d, 0x94ea7b2a, 0xe0b41de7, 0xe4750050,
	0xe9362689, 0xedf73b3e, 0xf3b06b3b, 0xf771768c, 0xfa0e5055, 0xfecf4de2,
	0xc680f05f, 0xc241ede8, 0xcf02cb31, 0xcbc3d686, 0xd5848683, 0xd1459b34,
	0xdc04bded, 0xd8c5a05a, 0x690ce0ee, 0x6dcdfd59, 0x608edb80, 0x644fc637,
	0x7a089632, 0x7ec98b85, 0x738aad5c, 0x774bb0eb, 0x4f040d56, 0x4bc510e1,
	0x46863638, 0x42472b8f, 0x5c007b8a, 0x58c1663d, 0x558240e4, 0x51435d53,
	0x251d3b9e, 0x21dc2629, 0x2c9f00f0, 0x285e1d47, 0x36194d42, 0x32d850f5,
	0x3f9b762c, 0x3b5a6b9b, 0x0315d626, 0x07d4cb91, 0x0a97ed48, 0x0e56f0ff,
	0x1011a0fa, 0x14d0bd4d, 0x19939b94, 0x1d528623, 0xf12f560e, 0xf5ee4bb9,
	0xf8ad6d60, 0xfc6c70d7, 0xe22b20d2, 0xe6ea3d65, 0xeba91bbc, 0xef68060b,
	0xd727bbb6, 0xd3e6a601, 0xdea580d8, 0xda649d6f, 0xc423cd6a, 0xc0e2d0dd,
	0xcda1f604, 0xc960ebb3, 0xbd3e8d7e, 0xb9ff90c9, 0xb4bcb610, 0xb07daba7,
	0xae3afba2, 0xaafbe615, 0xa7b8c0cc, 0xa379dd7b, 0x9b3660c6, 0x9ff77d71,
	0x92b45ba8, 0x9675461f, 0x8832161a, 0x8cf30bad, 0x81b02d74, 0x857130c3,
	0x5d8a9099, 0x594b8d2e, 0x5408abf7, 0x50c9b640, 0x4e8ee645, 0x4a4ffbf2,
	0x470cdd2b, 0x43cdc09c, 0x7b827d21, 0x7f436096, 0x7200464f, 0x76c15bf8,
	0x68860bfd, 0x6c47164a, 0x61043093, 0x65c52d24, 0x119b4be9, 0x155a565e,
	0x18197087, 0x1cd86d30, 0x029f3d35, 0x065e2082, 0x0b1d065b, 0x0fdc1bec,
	0x3793a651, 0x3352bbe6, 0x3e119d3f, 0x3ad08088, 0x2497d08d, 0x2056cd3a,
	0x2d15ebe3, 0x29d4f654, 0xc5a92679, 0xc1683bce, 0xcc2b1d17, 0xc8ea00a0,
	0xd6ad50a5, 0xd26c4d12, 0xdf2f6bcb, 0xdbee767c, 0xe3a1cbc1, 0xe760d676,
	0xea23f0af, 0xeee2ed18, 0xf0a5bd1d, 0xf464a0aa, 0xf9278673, 0xfde69bc4,
	0x89b8fd09, 0x8d79e0be, 0x803ac667, 0x84fbdbd0, 0x9abc8bd5, 0x9e7d9662,
	0x933eb0bb, 0x97ffad0c, 0xafb010b1, 0xab710d06, 0xa6322bdf, 0xa2f33668,
	0xbcb4666d, 0xb8757bda, 0xb5365d03, 0xb1f740b4
};

static void write_u16be(u8 *p, u16 v)
{
	p[0] = v >> 8;
	p[1] = v & 0xff;
}
static void write_u32be(u8 *p, u32 v)
{
	write_u16be(p, v >> 16);
	write_u16be(p + 2, v & 0xffff);
}

/* MPEG-2 CRC32 (polynomial 0x04C11DB7) */
static u32 crc32_ts(const u8 *data, int len)
{
	u32 crc = 0xffffffff;
	for (int i = 0; i < len; i++)
		crc = (crc << 8) ^ crc_table[((crc >> 24) ^ data[i]) & 0xff];
	return crc;
}

/* write TS header: sync_byte|TEI|PUSI|TP|PID|TSC|AFC|CC = 4 bytes */
static void write_ts_header(u8 *p, u16 pid, int pusi, int afc, u8 cc)
{
	p[0] = 0x47;
	p[1] = (pid >> 8) | (pusi ? 0x40 : 0);
	p[2] = pid & 0xff;
	p[3] = (afc << 4) | (cc & 0x0f);
}

/* write a PSI section (PAT/PMT) into TS packets */
static int write_psi(struct aic_ts_muxer *muxer, u16 pid, u8 table_id, u8 *data,
		     int len, u8 *cc)
{
	int total = len; /* caller's len already includes CRC */
	int packets = (total + TS_PACKET_SIZE - 5) / (TS_PACKET_SIZE - 5);
	int pos = 0;

	for (int i = 0; i < packets; i++) {
		u8 buf[TS_PACKET_SIZE] = { 0xff };
		int pusi = (i == 0);
		int afc = 1; /* payload only */
		int payload_start = 4;

		write_ts_header(buf, pid, pusi, afc, (*cc)++);

		/* pointer field if PUSI */
		if (pusi) {
			buf[4] = 0x00; /* pointer field = 0, section starts immediately */
			payload_start = 5;
		}

		int room = TS_PACKET_SIZE - payload_start;
		int copy = total - pos;
		if (copy > room)
			copy = room;

		if (pos == 0) {
			/* first packet: section header + data */
			memcpy(buf + payload_start, data, copy);
		} else {
			memcpy(buf + payload_start, data + pos, copy);
		}
		pos += copy;

		aic_stream_write(muxer->stream, buf, TS_PACKET_SIZE);
	}

	return 0;
}

/*
 * Build PAT (3-byte ISO 13818-1 section header).
 *   buf[0] = table_id
 *   buf[1] = section_syntax(1) | '0'(1) | reserved(2) | section_len[11:8](4)
 *   buf[2] = section_len[7:0]
 *   buf[3..] = section data
 */
static int build_pat(struct aic_ts_muxer *muxer, u8 *buf, int len)
{
	u8 *p = buf;

	memset(buf, 0, len);

	*p++ = 0x00; /* table_id = PAT */
	p++; /* skip section_length placeholder (buf[1]) */
	p++; /* skip section_length low byte    (buf[2]) */

	/* section data starts at buf[3] */
	write_u16be(p, 1); /* transport_stream_id */
	p += 2;
	*p++ = 0xc1; /* version=0, current_next=1 */
	*p++ = 0x00; /* section_number */
	*p++ = 0x00; /* last_section_number */
	write_u16be(p, 1); /* program_number=1 */
	p += 2;
	*p++ = 0xe0 | ((TS_PID_PMT >> 8) & 0x1f);
	*p++ = TS_PID_PMT & 0xff;

	/* CRC32 over everything written so far */
	u32 crc = crc32_ts(buf, (int)(p - buf));
	write_u32be(p, crc);
	p += 4;

	/* section_length = bytes after section_length field (includes CRC) */
	int slen = (int)(p - buf - 3);
	buf[1] = 0xb0 | ((slen >> 8) & 0x0f);
	buf[2] = slen & 0xff;

	return write_psi(muxer, TS_PID_PAT, 0x00, buf, (int)(p - buf),
			 &muxer->cc_pat);
}

/*
 * Build PMT (3-byte ISO 13818-1 section header, same layout as PAT).
 */
static int build_pmt(struct aic_ts_muxer *muxer, u8 *buf, int len)
{
	u8 *p = buf;

	memset(buf, 0, len);

	*p++ = 0x02; /* table_id = PMT */
	p++; /* skip section_length_hi (buf[1]) */
	p++; /* skip section_length_lo (buf[2]) */

	/* section data starts at buf[3] */
	write_u16be(p, 1); /* program_number */
	p += 2;
	*p++ = 0xc1; /* version=0, current_next=1 */
	*p++ = 0x00;
	*p++ = 0x00;
	*p++ = 0xe0 | ((TS_PID_VIDEO >> 8) & 0x1f);
	*p++ = TS_PID_VIDEO & 0xff;
	write_u16be(p, 0); /* program_info_length=0 */
	p += 2;

	/* video stream */
	if (muxer->has_video) {
		*p++ = muxer->video_type;
		*p++ = 0xe0 | ((TS_PID_VIDEO >> 8) & 0x1f);
		*p++ = TS_PID_VIDEO & 0xff;
		write_u16be(p, 0); /* ES_info_length=0 */
		p += 2;
	}

	/* audio stream */
	if (muxer->has_audio) {
		*p++ = muxer->audio_type;
		*p++ = 0xe0 | ((TS_PID_AUDIO >> 8) & 0x1f);
		*p++ = TS_PID_AUDIO & 0xff;
		write_u16be(p, 0);
		p += 2;
	}

	/* CRC32 */
	u32 crc = crc32_ts(buf, (int)(p - buf));
	write_u32be(p, crc);
	p += 4;

	/* fill section_length */
	int slen = (int)(p - buf - 3);
	buf[1] = 0xb0 | ((slen >> 8) & 0x0f);
	buf[2] = slen & 0xff;

	return write_psi(muxer, TS_PID_PMT, 0x02, buf, (int)(p - buf),
			 &muxer->cc_pmt);
}

/*
 * Build SDT (Service Description Table), table_id = 0x42, pid = 0x0011.
 * Provides a human-readable service name for the TS stream.
 */
static int build_sdt(struct aic_ts_muxer *muxer, u8 *buf, int len)
{
	static const char *provider = "ArtInChip";
	static const char *service = "Service01";
	int plen = strlen(provider);
	int slen = strlen(service);
	u8 *p = buf;

	memset(buf, 0, len);

	*p++ = 0x42; /* table_id = SDT (current TS) */
	p++; /* skip section_length_hi */
	p++; /* skip section_length_lo */

	write_u16be(p, 1); /* transport_stream_id = 1 */
	p += 2;
	*p++ = 0xc1; /* version=0, current_next=1 */
	*p++ = 0x00; /* section_number */
	*p++ = 0x00; /* last_section_number */
	write_u16be(p, 1); /* original_network_id = 1 */
	p += 2;
	*p++ = 0xff; /* reserved */

	/* service loop: one service (program_number=1) */
	write_u16be(p, 1); /* service_id = 1 */
	p += 2;
	*p++ = 0xfc; /* EIT_schedule=0, EIT_pf=0, running=4, free_CA=0 */
	/* reserved(6) + EIT_schedule(1) + EIT_pf(1) + running(3) + CA(1) = 12 bits */
	write_u16be(p, plen + slen + 5); /* descriptors_loop_length */
	p += 2;

	/* service_descriptor: tag = 0x48 */
	*p++ = 0x48;
	*p++ = plen + slen + 3; /* descriptor_length */
	*p++ = 0x01; /* service_type = digital TV */
	*p++ = plen;
	memcpy(p, provider, plen);
	p += plen;
	*p++ = slen;
	memcpy(p, service, slen);
	p += slen;

	/* CRC32 */
	u32 crc = crc32_ts(buf, (int)(p - buf));
	write_u32be(p, crc);
	p += 4;

	/* section_length */
	int section_len = (int)(p - buf - 3);
	buf[1] = 0xb0 | ((section_len >> 8) & 0x0f);
	buf[2] = section_len & 0xff;

	return write_psi(muxer, TS_PID_SDT, 0x42, buf, (int)(p - buf),
			 &muxer->cc_sdt);
}

/* write PES header before elementary stream data */
static int write_pes(struct aic_ts_muxer *muxer, u16 pid,
		     struct aic_av_packet *pkt, u8 *cc, int stream_id)
{
	u8 *data = pkt->data;
	int size = pkt->size;
	u64 pts = pkt->pts * 90LLU; /* ms -> 90 kHz ticks */

	int pes_hdr_len = 0;
	int pes_data_len;

	u8 buf[TS_PACKET_SIZE];
	u8 pes_hdr[32];

	/* PES start code */
	pes_hdr[0] = 0x00;
	pes_hdr[1] = 0x00;
	pes_hdr[2] = 0x01;
	pes_hdr[3] = stream_id;

	/* PES packet length */
	if (size + 8 > 0xffff)
		write_u16be(pes_hdr + 4, 0); /* unbounded */
	else
		write_u16be(pes_hdr + 4, size + 8);

	/* flags: PTS only */
	pes_hdr[6] = 0x80; /* pts_dts_flags = 10 */
	pes_hdr[7] = 0x80;
	pes_hdr[8] = 5; /* PES header data length = 5 bytes (just PTS) */

	/* PTS: 33 bits */
	u8 *p = pes_hdr + 9;
	*p++ = 0x20 | ((pts >> 30) & 0x07) | 0x01;
	*p++ = (pts >> 22) & 0xff;
	*p++ = ((pts >> 15) & 0x7f) | 0x01;
	*p++ = (pts >> 7) & 0xff;
	*p++ = ((pts & 0x7f) << 1) | 0x01;
	pes_hdr_len = 14;
	pes_data_len = size;

	/* write TS packets */
	int total = pes_hdr_len + pes_data_len;
	int packets = (total + TS_PACKET_SIZE - 5) / (TS_PACKET_SIZE - 5);
	int off = 0;

	for (int i = 0; i < packets; i++) {
		memset(buf, 0xff, TS_PACKET_SIZE);

		int pusi = (i == 0);
		int copy = total - off;
		int payload_start;

		if (copy >= TS_PACKET_SIZE - 4) {
			/* data fills the packet: payload only */
			copy = TS_PACKET_SIZE - 4;
			payload_start = 4;
			write_ts_header(buf, pid, pusi, 1, (*cc)++);
		} else {
			/*
			 * data does not fill the packet: absorb the padding
			 * with an adaptation field and right-align the payload
			 * to the end, so stuffing bytes never enter the PES.
			 */
			int stuffing = (TS_PACKET_SIZE - 4) - copy;
			write_ts_header(buf, pid, pusi, 3, (*cc)++); /* afc=3 */
			if (stuffing == 1) {
				buf[4] = 0; /* adaptation_field_length=0, no flags */
			} else {
				buf[4] = stuffing - 1; /* adaptation_field_length */
				buf[5] = 0x00; /* flags: no PCR/splice */
				/* buf[6..] already 0xff from memset = stuffing */
			}
			payload_start = TS_PACKET_SIZE - copy;
		}

		/* copy [off, off+copy): may span PES header and ES data */
		if (off < pes_hdr_len) {
			int hcopy = pes_hdr_len - off;
			if (hcopy > copy)
				hcopy = copy;
			memcpy(buf + payload_start, pes_hdr + off, hcopy);
			int dcopy = copy - hcopy;
			if (dcopy > 0)
				memcpy(buf + payload_start + hcopy, data,
				       dcopy);
		} else {
			memcpy(buf + payload_start, data + (off - pes_hdr_len),
			       copy);
		}

		aic_stream_write(muxer->stream, buf, TS_PACKET_SIZE);
		off += copy;
	}

	return 0;
}

s32 ts_init(struct aic_ts_muxer *muxer, struct aic_av_media_info *info)
{
	muxer->video_type = 0;
	muxer->audio_type = 0;
	muxer->has_video = info->has_video;
	muxer->has_audio = info->has_audio;

	if (muxer->has_video) {
		if (info->video_stream.codec_type ==
		    MPP_CODEC_VIDEO_ENCODER_MJPEG)
			muxer->video_type =
				0x06; /* stream_type for MJPEG in PMT */
		else if (info->video_stream.codec_type ==
			 MPP_CODEC_VIDEO_ENCODER_H264)
			muxer->video_type = 0x1b;
	}

	if (muxer->has_audio) {
		if (info->audio_stream[0].codec_type ==
		    MPP_CODEC_AUDIO_ENCODER_MP3)
			muxer->audio_type = 0x04; /* MPEG-2 Audio */
		else if (info->audio_stream[0].codec_type ==
			 MPP_CODEC_AUDIO_ENCODER_AAC)
			muxer->audio_type = 0x0f; /* AAC */
	}

	/* reset continuity counters: keep init self-contained instead of
	 * relying on the caller having zeroed the struct */
	muxer->cc_pat = 0;
	muxer->cc_pmt = 0;
	muxer->cc_sdt = 0;
	muxer->cc_video = 0;
	muxer->cc_audio = 0;

	return 0;
}

s32 ts_write_header(struct aic_ts_muxer *muxer)
{
	int len = 1024;
	u8 *buf = mpp_alloc(len);
	if (!buf)
		return -1;

	build_sdt(muxer, buf, len);
	build_pat(muxer, buf, len);
	build_pmt(muxer, buf, len);

	mpp_free(buf);
	return 0;
}

s32 ts_write_packet(struct aic_ts_muxer *muxer, struct aic_av_packet *packet)
{
	if (!packet)
		return -1;

	if (packet->type == MPP_MEDIA_TYPE_VIDEO && muxer->has_video) {
		write_pes(muxer, TS_PID_VIDEO, packet, &muxer->cc_video, TS_VIDEO_STREAM_ID);
	} else if (packet->type == MPP_MEDIA_TYPE_AUDIO && muxer->has_audio) {
		write_pes(muxer, TS_PID_AUDIO, packet, &muxer->cc_audio, TS_AUDIO_STREAM_ID);
	}

	return 0;
}

s32 ts_write_trailer(struct aic_ts_muxer *muxer)
{
	/* flush and write end-of-stream markers (optional for file-based TS) */
	(void)muxer;
	return 0;
}

s32 ts_close(struct aic_ts_muxer *muxer)
{
	aic_stream_close(muxer->stream);
	muxer->stream = NULL;
	return 0;
}
