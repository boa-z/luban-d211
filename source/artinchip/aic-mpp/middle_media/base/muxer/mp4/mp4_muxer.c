/*
 * Copyright (C) 2020-2026 ArtInChip Technology Co. Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 *  author: <jun.ma@artinchip.com>
 *  Desc: mp4_muxer
 */
#include <string.h>
#include <time.h>
#include "mp4_muxer.h"
#include "mpp_log.h"
#include "mpp_mem.h"
#include "mpp_dec_type.h"
#include "aic_stream.h"
#include "aic_middle_media_common.h"

#define MODE_MP4  0x01
#define MOV_TIMESCALE 1000
#define AV_NOPTS_VALUE -1
#define MAX_RECORD_SEC  300   /* 5 minutes max per file */


static void wfourcc(struct aic_stream *s, const char *c)
{
	aic_stream_wl32(s, MKTAG(c[0], c[1], c[2], c[3]));
}

//FIXME support 64 bit variant with wide placeholders
static int64_t update_size(struct aic_stream  *s, int64_t pos)
{
	int64_t curpos = aic_stream_tell(s);

	aic_stream_seek(s, pos, SEEK_SET);
	aic_stream_wb32(s, curpos - pos); /* rewrite size */
	aic_stream_seek(s, curpos, SEEK_SET);
	return curpos - pos;
}

static int mov_write_ftyp_tag(struct aic_mov_muxer *s)
{
	int minor = 0x200;
	struct aic_stream* pb = s->stream;
	int64_t pos = aic_stream_tell(pb);

	aic_stream_wb32(pb, 0); /* size */
	wfourcc(pb, "ftyp");
	//MODE_MP4
	wfourcc(pb, "isom");
	aic_stream_wb32(pb, minor);
	//MODE_MP4
	wfourcc(pb, "isom");
	wfourcc(pb, "iso2");
	//MODE_MP4
	wfourcc(pb, "mp41");
	update_size(pb,pos);
	return 0;
}

static int mov_write_mdat_tag(struct aic_mov_muxer *s)
{
	aic_stream_wb32(s->stream, 8);    // placeholder for extended size field (64 bit)
	wfourcc(s->stream, "free");
	s->mdat_pos = aic_stream_tell(s->stream);
	aic_stream_wb32(s->stream, 0); /* size placeholder*/
	wfourcc(s->stream, "mdat");
	return 0;
}

static int64_t calc_pts_duration(struct aic_mov_muxer *s, struct mov_track *track)
{
	if (track->end_pts != AV_NOPTS_VALUE &&
		track->start_pts != AV_NOPTS_VALUE) {
		return track->end_pts - track->start_pts;
	}
	return track->track_duration;
}

static int mov_write_mdhd_tag(struct aic_mov_muxer *s,struct mov_track *track)
{
	int64_t duration = calc_pts_duration(s, track);
	int version = 0;
	struct aic_stream* pb = s->stream;

	aic_stream_wb32(pb, 32); /* size */
	wfourcc(pb, "mdhd");
	aic_stream_w8(pb, version);
	aic_stream_wb24(pb, 0); /* flags */
	aic_stream_wb32(pb, track->time); /* creation time */
	aic_stream_wb32(pb, track->time); /* modification time */
	/*maybe wrong */
	aic_stream_wb32(pb, track->timescale); /* time scale (sample rate for audio) */
	aic_stream_wb32(pb, duration); /* duration */
	aic_stream_wb16(pb, track->language); /* language */
	aic_stream_wb16(pb, 0); /* reserved (quality) */
	return 32;
}

static int mov_write_hdlr_tag(struct aic_mov_muxer *s,struct mov_track *track)
{
	const char *hdlr, *descr = NULL, *hdlr_type = NULL;
	struct aic_stream* pb = s->stream;
	int64_t pos = aic_stream_tell(pb);

	if (track) {
		hdlr = "\0\0\0\0";
		if (track->codec_para.codec_type == MPP_MEDIA_TYPE_VIDEO) {
			hdlr_type = "vide";
			descr     = "VideoHandler";
		} else if (track->codec_para.codec_type == MPP_MEDIA_TYPE_AUDIO) {
			hdlr_type = "soun";
			descr     = "SoundHandler";
		} else {
			loge("not support codec_type %d.", track->codec_para.codec_type);
			return -1;
		}
	}

	aic_stream_wb32(pb, 0); /* size */
	wfourcc(pb, "hdlr");
	aic_stream_wb32(pb, 0); /* Version & flags */
	aic_stream_write(pb, (void *)hdlr, 4); /* handler */
	wfourcc(pb, hdlr_type); /* handler type */
	aic_stream_wb32(pb, 0); /* reserved */
	aic_stream_wb32(pb, 0); /* reserved */
	aic_stream_wb32(pb, 0); /* reserved */
	aic_stream_w8(pb, strlen(descr)); /* pascal string: count */
	aic_stream_write(pb, (void *)descr, strlen(descr));       /* data */
	return update_size(pb, pos);
}

static int mov_write_vmhd_tag(struct aic_mov_muxer *s)
{
	struct aic_stream* pb = s->stream;

	aic_stream_wb32(pb, 0x14); /* size (always 0x14) */
	wfourcc(pb, "vmhd");
	aic_stream_wb32(pb, 0x01); /* version & flags */
	aic_stream_wb64(pb, 0); /* reserved (graphics mode = copy) */
	return 0x14;
}

static int mov_write_smhd_tag(struct aic_mov_muxer *s)
{
	struct aic_stream* pb = s->stream;

	aic_stream_wb32(pb, 16); /* size */
	wfourcc(pb, "smhd");
	aic_stream_wb32(pb, 0); /* version & flags */
	aic_stream_wb16(pb, 0); /* reserved (balance, normally = 0) */
	aic_stream_wb16(pb, 0); /* reserved */
	return 16;
}

static int mov_write_dref_tag(struct aic_mov_muxer *s)
{
	struct aic_stream* pb = s->stream;

	aic_stream_wb32(pb, 28); /* size */
	wfourcc(pb, "dref");
	aic_stream_wb32(pb, 0); /* version & flags */
	aic_stream_wb32(pb, 1); /* entry count */
	aic_stream_wb32(pb, 0xc); /* size */
	//FIXME add the alis and rsrc atom
	wfourcc(pb, "url ");
	aic_stream_wb32(pb, 1); /* version & flags */

	return 28;
}

static int mov_write_dinf_tag(struct aic_mov_muxer *s)
{
	struct aic_stream* pb = s->stream;
	int64_t pos = aic_stream_tell(pb);

	aic_stream_wb32(pb, 0); /* size */
	wfourcc(pb, "dinf");
	mov_write_dref_tag(s);
	return update_size(pb, pos);
}

/* ISO 14496-1 variable-length size encoding (tag + size + payload) */
static void put_descr(struct aic_stream *pb, int tag, unsigned int size)
{
	aic_stream_w8(pb, tag);
	if (size < 0x80) {
		aic_stream_w8(pb, size);
	} else if (size < 0x4000) {
		aic_stream_w8(pb, (size >> 7) | 0x80);
		aic_stream_w8(pb, size & 0x7F);
	} else if (size < 0x200000) {
		aic_stream_w8(pb, (size >> 14) | 0x80);
		aic_stream_w8(pb, ((size >> 7) & 0x7F) | 0x80);
		aic_stream_w8(pb, size & 0x7F);
	} else {
		aic_stream_w8(pb, (size >> 21) | 0x80);
		aic_stream_w8(pb, ((size >> 14) & 0x7F) | 0x80);
		aic_stream_w8(pb, ((size >> 7) & 0x7F) | 0x80);
		aic_stream_w8(pb, size & 0x7F);
	}
}

static unsigned compute_avg_bitrate(struct mov_track *track)
{
	uint64_t size = 0;
	int i;

	if (!track->track_duration)
		return 0;
	for (i = 0; i < track->entry; i++)
		size += track->cluster[i].size;
	return size * 8 * track->timescale / track->track_duration;
}

const struct codec_tag g_mp4_obj_type[] = {
	{ CODEC_ID_MP3         , 0x69 }, /* 13818-3 */
	{ CODEC_ID_MJPEG       , 0x6C }, /* 10918-1 */
	{ CODEC_ID_H264        , 0x21 }, /* 14496-10 AVC */
	{ CODEC_ID_AAC         , 0x40 }, /* 14496-3 AAC */
	{ CODEC_ID_NONE        ,    0 },
};

static unsigned int codec_get_tag(const struct codec_tag *tags, enum CodecID id)
{
	while (tags->id != CODEC_ID_NONE) {
		if (tags->id == id)
			return tags->tag;
		tags++;
	}
	return 0;
}

/* map sample rate → AAC samplingFrequencyIndex (0-11), 0x0F = escape */
static int aac_sr_index(int sr)
{
	static const int tab[] = { 96000, 88200, 64000, 48000, 44100,
				   32000, 24000, 22050, 16000, 12000,
				   11025, 8000 };
	for (int i = 0; i < 12; i++)
		if (sr == tab[i]) return i;
	return 0x0F;
}

static int mov_write_esds_tag(struct aic_mov_muxer *s, struct mov_track *track)
{
	struct aic_stream *pb = s->stream;
	int64_t pos = aic_stream_tell(pb);
	unsigned avg_bitrate;
	int dsi_len = 0;
	u8 dsi_buf[8];

	/* Build DecoderSpecificInfo for AAC (AudioSpecificConfig, 2 bytes) */
	if (track->codec_para.codec_id == CODEC_ID_AAC) {
		int sr_idx = aac_sr_index(track->codec_para.sample_rate);
		int ch = track->codec_para.channels;
		if (ch <= 0) ch = 1;
		if (sr_idx == 0x0F) {
			/* escape: write actual rate as 3 bytes after config */
			dsi_buf[0] = (2 << 3) | (0x0F >> 1);
			dsi_buf[1] = (0x0F << 7) | (ch << 3) | 0x04;
			dsi_buf[2] = (track->codec_para.sample_rate >> 16) & 0xFF;
			dsi_buf[3] = (track->codec_para.sample_rate >> 8) & 0xFF;
			dsi_buf[4] = track->codec_para.sample_rate & 0xFF;
			dsi_len = 5;
		} else {
			u16 cfg = (2 << 11) | (sr_idx << 7) | (ch << 3);
			dsi_buf[0] = (cfg >> 8) & 0xFF;
			dsi_buf[1] = cfg & 0xFF;
			dsi_len = 2;
		}
	}

	aic_stream_wb32(pb, 0); // size
	wfourcc(pb, "esds");
	aic_stream_wb32(pb, 0); // Version

	/*
	 * ES_Descriptor payload = es_id(2) + flags(1) +
	 *   DecoderConfig_desc(2 + 13 + dsi_len) + SLConfig_desc(2 + 1)
	 *   = 3 + 15 + dsi_len + 3 = 21 + dsi_len
	 */
	put_descr(pb, 0x03, 3 + 2 + 13 + dsi_len + 2 + 1);
	aic_stream_wb16(pb, track->track_id);
	aic_stream_w8(pb, 0x00);

	/* DecoderConfig descriptor (tag 0x04) */
	put_descr(pb, 0x04, 13 + dsi_len);
	aic_stream_w8(pb, codec_get_tag(g_mp4_obj_type,
					   track->codec_para.codec_id));
	if (track->codec_para.codec_type == MPP_MEDIA_TYPE_AUDIO)
		aic_stream_w8(pb, 0x15); /* AudioStream */
	else
		aic_stream_w8(pb, 0x11); /* VisualStream */

	avg_bitrate = compute_avg_bitrate(track);

	aic_stream_wb24(pb, 0); /* bufferSizeDB (3 bytes) */
	aic_stream_wb32(pb, avg_bitrate);  /* maxBitrate */
	aic_stream_wb32(pb, avg_bitrate);  /* avgBitrate */

	/* DecoderSpecificInfo (tag 0x05) — only if present */
	if (dsi_len > 0) {
		put_descr(pb, 0x05, dsi_len);
		aic_stream_write(pb, dsi_buf, dsi_len);
	}

	/* SLConfigDescriptor (tag 0x06) */
	put_descr(pb, 0x06, 1);
	aic_stream_w8(pb, 0x02);

	return update_size(pb, pos);
}
static int mov_write_video_tag(struct aic_mov_muxer *s,struct mov_track *track)
{
	struct aic_stream* pb = s->stream;
	int64_t pos = aic_stream_tell(pb);
	char compressor_name[32] = {0};

	//char compressor_name[32] = {0};
	aic_stream_wb32(pb, 0); /* size */
	aic_stream_wl32(pb, track->tag); // store it byteswapped
	aic_stream_wb32(pb, 0); /* Reserved */
	aic_stream_wb16(pb, 0); /* Reserved */
	aic_stream_wb16(pb, 1); /* Data-reference index */

	aic_stream_wb16(pb, 0); /* Codec stream version */

	aic_stream_wb16(pb, 0); /* Codec stream revision (=0) */
	aic_stream_wb32(pb, 0); /* Reserved */
	aic_stream_wb32(pb, 0); /* Reserved */
	aic_stream_wb32(pb, 0); /* Reserved */

	aic_stream_wb16(pb, track->codec_para.width); /* Video width */
	aic_stream_wb16(pb, track->codec_para.height); /* Video height */
	aic_stream_wb32(pb, 0x00480000); /* Horizontal resolution 72dpi */
	aic_stream_wb32(pb, 0x00480000); /* Vertical resolution 72dpi */
	aic_stream_wb32(pb, 0); /* Data size (= 0) */
	aic_stream_wb16(pb, 1); /* Frame count (= 1) */

	if (track->codec_para.codec_id == CODEC_ID_H264) {
		snprintf(compressor_name, sizeof(compressor_name), "Lavc59.37.100 %s", "libx264");
	} else {
		snprintf(compressor_name, sizeof(compressor_name), "Lavc59.37.100 %s", "mjpeg");
	}

	/* compressor name: 1 byte count + 31 bytes (padded with \0) */
	aic_stream_w8(pb, 31);
	aic_stream_write(pb, compressor_name, 31);

	aic_stream_wb16(pb, 0x18); /* Reserved */

	aic_stream_wb16(pb, 0xffff); /* Reserved */

	/* H.264 uses avcC, not esds; MJPEG uses esds */
	if (track->codec_para.codec_id != CODEC_ID_H264)
		mov_write_esds_tag(s, track);
	//mov_write_fiel_tag(pb, track, AV_FIELD_UNKNOWN);
	//mov_write_clli_tag(pb, track);
	//mov_write_mdcv_tag(pb, track);

	return update_size(pb, pos);
}

static int mov_write_audio_tag(struct aic_mov_muxer *s,struct mov_track *track)
{
	struct aic_stream* pb = s->stream;
	int64_t pos = aic_stream_tell(pb);
	int version = 0;
	uint32_t tag = track->tag;
	int ret = 0;

	aic_stream_wb32(pb, 0); /* size */
	aic_stream_wl32(pb, tag); // store it byteswapped
	aic_stream_wb32(pb, 0); /* Reserved */
	aic_stream_wb16(pb, 0); /* Reserved */
	aic_stream_wb16(pb, 1); /* Data-reference index, XXX  == 1 */

	/* SoundDescription */
	aic_stream_wb16(pb, version); /* Version */
	aic_stream_wb16(pb, 0); /* Revision level */
	aic_stream_wb32(pb, 0); /* Reserved */

	/*channels*/
	aic_stream_wb16(pb, track->codec_para.channels);
	/*bits_per_raw_sample*/
	aic_stream_wb16(pb, 16);

	aic_stream_wb16(pb, 0);
	aic_stream_wb16(pb, 0); /* packet size (= 0) */
	/*sample_rate*/
	aic_stream_wb16(pb, track->codec_para.sample_rate <= UINT16_MAX ?
					track->codec_para.sample_rate : 0);

	aic_stream_wb16(pb, 0); /* Reserved */

	ret = mov_write_esds_tag(s, track);
	if (ret < 0)
		return ret;

	ret = update_size(pb, pos);
	return ret;
}

static int mov_write_stsd_tag(struct aic_mov_muxer *s,struct mov_track *track)
{
	struct aic_stream* pb = s->stream;
	int64_t pos = aic_stream_tell(pb);
	int ret = 0;

	aic_stream_wb32(pb, 0); /* size */
	wfourcc(pb, "stsd");
	aic_stream_wb32(pb, 0); /* version & flags */
	aic_stream_wb32(pb, 1); /* entry count */
	if (track->codec_para.codec_type == MPP_MEDIA_TYPE_VIDEO)
		ret = mov_write_video_tag(s,track);
	else if (track->codec_para.codec_type == MPP_MEDIA_TYPE_AUDIO)
		ret = mov_write_audio_tag(s,track);

	if (ret < 0)
		return ret;

	return update_size(pb, pos);
}

static int get_cluster_duration(struct mov_track *track, int cluster_idx)
{
	int64_t next_pts;

	if (cluster_idx >= track->entry)
		return 0;

	if (cluster_idx + 1 == track->entry) {
		next_pts = track->track_duration + track->start_pts;
		logd("next_dts:%ld,track_duration:%ld,start_pts:%ld,dts:%ld",next_pts,track->track_duration,track->start_pts,track->cluster[cluster_idx].pts);
	} else {
		next_pts = track->cluster[cluster_idx + 1].pts;
	}
	next_pts -= track->cluster[cluster_idx].pts;

	return next_pts;
}

static int mov_write_stts_tag(struct aic_mov_muxer *s,struct mov_track *track)
{
	struct aic_stream* pb = s->stream;
	int i, duration, last_duration;
	uint32_t entries = 0;

	if (track->codec_para.codec_type == MPP_MEDIA_TYPE_AUDIO) {
		entries = 1;
		/* compute actual frame duration in timescale units */
		int sr = track->codec_para.sample_rate;
		int spf = sr >= 32000 ? 1152 : 576;
		last_duration = spf * MOV_TIMESCALE / sr;
	} else if (track->entry) {
		/* pass 1: count unique duration groups */
		for (i = 0; i < track->entry; i++) {
			duration = get_cluster_duration(track, i);
			if (i == 0 || duration != last_duration) {
				entries++;
				last_duration = duration;
			}
		}
	}

	aic_stream_wb32(pb, 16 + entries * 8); /* size */
	wfourcc(pb, "stts");
	aic_stream_wb32(pb, 0); /* version & flags */
	aic_stream_wb32(pb, entries); /* entry count */

	if (track->codec_para.codec_type == MPP_MEDIA_TYPE_AUDIO) {
		aic_stream_wb32(pb, track->sample_count);
		aic_stream_wb32(pb, last_duration);
	} else if (track->entry) {
		/* pass 2: write compressed groups */
		int count = 0;

		for (i = 0; i < track->entry; i++) {
			duration = get_cluster_duration(track, i);
			if (i == 0 || duration != last_duration) {
				if (i > 0) {
					aic_stream_wb32(pb, count);
					aic_stream_wb32(pb, last_duration);
				}
				last_duration = duration;
				count = 1;
			} else {
				count++;
			}
		}
		if (track->entry > 0) {
			aic_stream_wb32(pb, count);
			aic_stream_wb32(pb, last_duration);
		}
	}

	return 16 + entries * 8;
}

static int mov_write_stsc_tag(struct aic_mov_muxer *s,struct mov_track *track)
{
	int index = 0, oldval = -1, i;
	int64_t entrypos, curpos;
	struct aic_stream* pb = s->stream;
	int64_t pos = aic_stream_tell(pb);

	aic_stream_wb32(pb, 0); /* size */
	wfourcc(pb, "stsc");
	aic_stream_wb32(pb, 0); // version & flags
	entrypos = aic_stream_tell(pb);
	aic_stream_wb32(pb, track->chunk_count); // entry count
	for (i = 0; i < track->entry; i++) {
		if (oldval != track->cluster[i].samples_in_chunk && track->cluster[i].chunk_num) {
			aic_stream_wb32(pb, track->cluster[i].chunk_num); // first chunk
			aic_stream_wb32(pb, track->cluster[i].samples_in_chunk); // samples per chunk
			aic_stream_wb32(pb, 0x1); // sample description index
			oldval = track->cluster[i].samples_in_chunk;
			index++;
		}
	}
	curpos = aic_stream_tell(pb);
	aic_stream_seek(pb, entrypos, SEEK_SET);
	aic_stream_wb32(pb, index); // rewrite size
	aic_stream_seek(pb, curpos, SEEK_SET);
	return update_size(pb, pos);
}

static int mov_write_stsz_tag(struct aic_mov_muxer *s,struct mov_track *track)
{
	struct aic_stream* pb = s->stream;
	int equal_chunks = 1;
	int i, j, entries = 0, tst = -1, oldtst = -1;
	int64_t pos = aic_stream_tell(pb);

	aic_stream_wb32(pb, 0); /* size */
	wfourcc(pb, "stsz");
	aic_stream_wb32(pb, 0); /* version & flags */

	for (i = 0; i < track->entry; i++) {
		tst = track->cluster[i].size / track->cluster[i].entries;
		if (oldtst != -1 && tst != oldtst)
			equal_chunks = 0;
		oldtst = tst;
		entries += track->cluster[i].entries;
	}
	if (equal_chunks && track->entry) {
		int size = track->entry ? track->cluster[0].size / track->cluster[0].entries : 0;
		size = (1 > size) ? 1 : size; // adpcm mono case could make sSize == 0
		aic_stream_wb32(pb, size); // sample size
		aic_stream_wb32(pb, entries); // sample count
	} else {
		aic_stream_wb32(pb, 0); // sample size
		aic_stream_wb32(pb, entries); // sample count
		for (i = 0; i < track->entry; i++) {
			for (j = 0; j < track->cluster[i].entries; j++) {
				aic_stream_wb32(pb, track->cluster[i].size /
							track->cluster[i].entries);
			}
		}
	}
	return update_size(pb, pos);
}

static int co64_required(struct mov_track *track)
{
	if (track->entry > 0 && track->cluster[track->entry - 1].pos + track->data_offset > UINT32_MAX)
		return 1;
	return 0;
}

/* Chunk offset atom */
static int mov_write_stco_tag(struct aic_mov_muxer *s, struct mov_track *track)
{
	int i;
	struct aic_stream* pb = s->stream;
	int mode64 = co64_required(track); // use 32 bit size variant if possible
	int64_t pos = aic_stream_tell(pb);

	aic_stream_wb32(pb, 0); /* size */
	if (mode64)
		wfourcc(pb, "co64");
	else
		wfourcc(pb, "stco");
	aic_stream_wb32(pb, 0); /* version & flags */
	aic_stream_wb32(pb, track->chunk_count); /* entry count */
	for (i = 0; i < track->entry; i++) {
		if (!track->cluster[i].chunk_num)
			continue;
		if (mode64 == 1)
			aic_stream_wb64(pb, track->cluster[i].pos + track->data_offset);
		else
			aic_stream_wb32(pb, track->cluster[i].pos + track->data_offset);
	}
	return update_size(pb, pos);
}

static int mov_write_stbl_tag(struct aic_mov_muxer *s,struct mov_track *track)
{
	struct aic_stream* pb = s->stream;
	int64_t pos = aic_stream_tell(pb);
	int ret = 0;

	aic_stream_wb32(pb, 0); /* size */
	wfourcc(pb, "stbl");
	if ((ret = mov_write_stsd_tag(s,track)) < 0)
		return ret;
	mov_write_stts_tag(s, track);

	// if (track->codec_para.codec_type == MPP_MEDIA_TYPE_VIDEO &&
	// 	track->flags & MOV_TRACK_CTTS && track->entry) {
	// 	if ((ret = mov_write_ctts_tag(s,track)) < 0)
	// 		return ret;
	// }
	mov_write_stsc_tag(s, track);
	mov_write_stsz_tag(s, track);
	mov_write_stco_tag(s, track);
	// if (track->par->codec_id == AV_CODEC_ID_OPUS || track->par->codec_id == AV_CODEC_ID_AAC) {
	// 	mov_preroll_write_stbl_atoms(pb, track);
	// }
	return update_size(pb, pos);
}

static int mov_write_minf_tag(struct aic_mov_muxer *s,struct mov_track *track)
{
	struct aic_stream* pb = s->stream;
	int64_t pos = aic_stream_tell(pb);
	int ret;

	aic_stream_wb32(pb, 0); /* size */
	wfourcc(pb, "minf");
	if (track->codec_para.codec_type == MPP_MEDIA_TYPE_VIDEO) {
		mov_write_vmhd_tag(s);
	} else if (track->codec_para.codec_type == MPP_MEDIA_TYPE_AUDIO) {
		mov_write_smhd_tag(s);
	}
	mov_write_dinf_tag(s);
	if ((ret = mov_write_stbl_tag(s,track)) < 0) {
		return ret;
	}
	return update_size(pb, pos);
}

static int mov_write_mdia_tag(struct aic_mov_muxer *s,struct mov_track *track)
{
	struct aic_stream* pb = s->stream;
	int64_t pos = aic_stream_tell(pb);
	int ret;

	aic_stream_wb32(pb, 0); /* size */
	wfourcc(pb, "mdia");
	mov_write_mdhd_tag(s, track);
	mov_write_hdlr_tag(s, track);
	if ((ret = mov_write_minf_tag(s,track)) < 0)
		return ret;
	return update_size(pb, pos);
}

#define MOV_TKHD_FLAG_ENABLED       0x0001
#define MOV_TKHD_FLAG_IN_MOVIE      0x0002

/* transformation matrix
    |a  b  u|
    |c  d  v|
    |tx ty w| */
static void write_matrix(struct aic_mov_muxer *s, int16_t a, int16_t b, int16_t c,
							int16_t d, int16_t tx, int16_t ty)
{
	struct aic_stream* pb = s->stream;

	aic_stream_wb32(pb, a << 16);  /* 16.16 format */
	aic_stream_wb32(pb, b << 16);  /* 16.16 format */
	aic_stream_wb32(pb, 0);        /* u in 2.30 format */
	aic_stream_wb32(pb, c << 16);  /* 16.16 format */
	aic_stream_wb32(pb, d << 16);  /* 16.16 format */
	aic_stream_wb32(pb, 0);        /* v in 2.30 format */
	aic_stream_wb32(pb, tx << 16); /* 16.16 format */
	aic_stream_wb32(pb, ty << 16); /* 16.16 format */
	aic_stream_wb32(pb, 1 << 30);  /* w in 2.30 format */
}

static int mov_write_tkhd_tag(struct aic_mov_muxer *s,struct mov_track *track)
{
	struct aic_stream* pb = s->stream;
	int version = 0;
	int group   = 0;
	int flags   = MOV_TKHD_FLAG_IN_MOVIE|MOV_TKHD_FLAG_ENABLED;
	int64_t duration = calc_pts_duration(s,track);

	group = track->codec_para.codec_type;
	aic_stream_wb32(pb, 92);
	wfourcc(pb, "tkhd");
	aic_stream_w8(pb, version);
	aic_stream_wb24(pb, flags);
	aic_stream_wb32(pb, track->time); /* creation time */
	aic_stream_wb32(pb, track->time); /* modification time */
	aic_stream_wb32(pb, track->track_id); /* track-id */
	aic_stream_wb32(pb, 0); /* reserved */
	aic_stream_wb32(pb, duration);
	aic_stream_wb32(pb, 0); /* reserved */
	aic_stream_wb32(pb, 0); /* reserved */
	aic_stream_wb16(pb, 0); /* layer */
	aic_stream_wb16(pb, group); /* alternate group) */

	/* Volume, only for audio */
	if (track->codec_para.codec_type == MPP_MEDIA_TYPE_AUDIO)
		aic_stream_wb16(pb, 0x0100);
	else
		aic_stream_wb16(pb, 0);

	aic_stream_wb16(pb, 0); /* reserved */
	write_matrix(s,  1,  0,  0,  1, 0, 0);
	/* Track width and height, for visual only */
	if (track->codec_para.codec_type == MPP_MEDIA_TYPE_VIDEO) {
		aic_stream_wb32(pb, track->codec_para.width*0x10000);
		aic_stream_wb32(pb, track->codec_para.height*0x10000);
		//aic_stream_wb32(pb, track->height);
	} else {
		aic_stream_wb32(pb, 0);
		aic_stream_wb32(pb, 0);
	}
	return 0x5c;
}

static int mov_write_trak_tag(struct aic_mov_muxer *s,struct mov_track *track)
{
	int ret = 0;
	int64_t pos = aic_stream_tell(s->stream);
	struct aic_stream* pb = s->stream;

	aic_stream_wb32(pb, 0); /* size */
	wfourcc(pb, "trak");
	mov_write_tkhd_tag(s, track);

	// if (track->start_pts != AV_NOPTS_VALUE) {
	// 	mov_write_edts_tag(s, track);  // PSP Movies and several other cases require edts box
	// }

	if ((ret = mov_write_mdia_tag(s,track)) < 0)
		return ret;

	return update_size(pb, pos);

}

static void build_chunks(struct mov_track *trk)
{
	int i;
	struct mov_entry *chunk = &trk->cluster[0];
	uint64_t chunk_size = chunk->size;

	chunk->chunk_num = 1;
	if (trk->chunk_count)
		return;
	trk->chunk_count = 1;
	for (i = 1; i<trk->entry; i++) {
		if (chunk->pos + chunk_size == trk->cluster[i].pos &&
			chunk_size + trk->cluster[i].size < (1<<20)) {
			chunk_size             += trk->cluster[i].size;
			chunk->samples_in_chunk += trk->cluster[i].entries;
		} else {
			trk->cluster[i].chunk_num = chunk->chunk_num+1;
			chunk=&trk->cluster[i];
			chunk_size = chunk->size;
			trk->chunk_count++;
		}
	}
}

static int mov_write_mvhd_tag(struct aic_mov_muxer *s)
{
	int max_track_id = 1, i;
	int64_t max_track_len = 0;
	int version = 0;
	struct aic_stream* pb = s->stream;

	max_track_len = s->tracks[0].track_duration;
	max_track_id = s->tracks[0].track_id;

	for (i = 1; i < s->nb_streams; i++) {
		if (max_track_len < s->tracks[i].track_duration)
			max_track_len = s->tracks[i].track_duration;
		if (max_track_id < s->tracks[i].track_id)
			max_track_id = s->tracks[i].track_id;
	}

	aic_stream_wb32(pb,108); /* size */
	wfourcc(pb, "mvhd");
	aic_stream_w8(pb, version);
	aic_stream_wb24(pb, 0); /* flags */
	aic_stream_wb32(pb, s->time); /* creation time */
	aic_stream_wb32(pb, s->time); /* modification time */

	aic_stream_wb32(pb, MOV_TIMESCALE);
	aic_stream_wb32(pb, max_track_len); /* duration of longest track */

	aic_stream_wb32(pb, 0x00010000); /* reserved (preferred rate) 1.0 = normal */
	aic_stream_wb16(pb, 0x0100); /* reserved (preferred volume) 1.0 = normal */
	aic_stream_wb16(pb, 0); /* reserved */
	aic_stream_wb32(pb, 0); /* reserved */
	aic_stream_wb32(pb, 0); /* reserved */

	/* Matrix structure */
	write_matrix(s, 1, 0, 0, 1, 0, 0);

	aic_stream_wb32(pb, 0); /* reserved (preview time) */
	aic_stream_wb32(pb, 0); /* reserved (preview duration) */
	aic_stream_wb32(pb, 0); /* reserved (poster time) */
	aic_stream_wb32(pb, 0); /* reserved (selection time) */
	aic_stream_wb32(pb, 0); /* reserved (selection duration) */
	aic_stream_wb32(pb, 0); /* reserved (current time) */
	aic_stream_wb32(pb, max_track_id + 1); /* Next track id */
	return 0x6c;
}

static int mov_write_udta_tag(struct aic_mov_muxer *s)
{
	return 0;
}

static int mov_setup_track_ids(struct aic_mov_muxer *s)
{
	int i;

	if (s->track_ids_ok)
		return 0;

	for (i = 0; i < s->nb_streams; i++) {
		if (s->tracks[i].entry <= 0)
			continue;
		s->tracks[i].track_id = i + 1;
	}
	s->track_ids_ok = 1;
	return 0;
}

static int mov_write_moov_tag(struct aic_mov_muxer *s)
{
	int i;
	struct aic_stream* pb = s->stream;
	int64_t pos = aic_stream_tell(pb);

	aic_stream_wb32(pb, 0); /* size placeholder*/
	wfourcc(pb, "moov");
	mov_setup_track_ids(s);
	for (i = 0; i < s->nb_streams; i++) {
		s->tracks[i].time     = s->time;
		if (s->tracks[i].entry)
			build_chunks(&s->tracks[i]);
	}

	mov_write_mvhd_tag(s);

	for (i = 0; i < s->nb_streams; i++) {
		if (s->tracks[i].entry > 0) {
			int ret = mov_write_trak_tag(s, &s->tracks[i]);
			if (ret < 0)
				return ret;
		}
	}

	mov_write_udta_tag(s);

	return update_size(pb, pos);
}

const struct codec_tag codec_mp4_tags[] = {
	{ CODEC_ID_MJPEG,           MKTAG('m', 'j', 'p', 'a') },
	{ CODEC_ID_H264,            MKTAG('a', 'v', 'c', '1') },
	{ CODEC_ID_AAC,             MKTAG('m', 'p', '4', 'a') },
	{ CODEC_ID_MP3,             MKTAG('.', 'm', 'p', '3') },
	{ CODEC_ID_NONE,               0 },
};

static unsigned int mov_find_codec_tag(const struct codec_tag *tags, int codec_id)
{
	while (tags->id != CODEC_ID_NONE) {
		if (tags->id == codec_id)
			return tags->tag;
		tags++;
	}
	return CODEC_ID_NONE;
}

s32 mp4_init(struct aic_mov_muxer *s,struct aic_av_media_info *info)
{
	int max_sec, max_entries;
	int i = 0;

	s->nb_streams = 0;
	if (info->has_video) {
		s->nb_streams++;
	}
	if (info->has_audio) {
		s->nb_streams++;
	}
	if (s->nb_streams == 0) {
		loge("no stream");
		return -1;
	}
	s->tracks = mpp_alloc(s->nb_streams * sizeof(*s->tracks));
	if (!s->tracks) {
		loge("mpp_alloc fail");
		return -1;
	}
	memset(s->tracks, 0x00, s->nb_streams * sizeof(*s->tracks));
	s->mode = MODE_MP4;
	s->mdat_size = 0;
	s->track_ids_ok = 0;
	s->time = (uint64_t)time(NULL);

	/*init track */
	if (info->has_video) {
		s->tracks[i].codec_para.codec_type = MPP_MEDIA_TYPE_VIDEO;
		//need to transform
		if (info->video_stream.codec_type == MPP_CODEC_VIDEO_ENCODER_MJPEG) {
			s->tracks[i].codec_para.codec_id = CODEC_ID_MJPEG;
		} else if (info->video_stream.codec_type == MPP_CODEC_VIDEO_ENCODER_H264) {
			s->tracks[i].codec_para.codec_id = CODEC_ID_H264;
		} else {
			loge("video codec_type is %d", info->video_stream.codec_type);
			mpp_free(s->tracks);
			s->tracks = NULL;
			return -1;
		}
		s->tracks[i].codec_para.width =  info->video_stream.width;
		s->tracks[i].codec_para.height =  info->video_stream.height;
		s->tracks[i].codec_para.bit_rate =  info->video_stream.bit_rate;
		s->tracks[i].codec_para.frame_size = info->video_stream.frame_rate;
		s->tracks[i].timescale = MOV_TIMESCALE;
		s->tracks[i].start_pts = AV_NOPTS_VALUE;
		s->tracks[i].end_pts = AV_NOPTS_VALUE;
		s->tracks[i].tag  = mov_find_codec_tag(codec_mp4_tags, s->tracks[i].codec_para.codec_id);
		/* video sample_size stays 0 → variable frame size */
		s->tracks[i].data_offset = 0;
		i++;
	}
	if (info->has_audio) {
		s->tracks[i].codec_para.codec_type = MPP_MEDIA_TYPE_AUDIO;
		if (info->audio_stream[0].codec_type == MPP_CODEC_AUDIO_ENCODER_MP3) {
			s->tracks[i].codec_para.codec_id = CODEC_ID_MP3;
		} else if (info->audio_stream[0].codec_type == MPP_CODEC_AUDIO_ENCODER_AAC) {
			s->tracks[i].codec_para.codec_id = CODEC_ID_AAC;
		} else {
			loge("audio codec_type is %d", info->audio_stream[0].codec_type);
			mpp_free(s->tracks);
			s->tracks = NULL;
			return -1;
		}
		s->tracks[i].codec_para.channels =  info->audio_stream[0].nb_channel;
		s->tracks[i].codec_para.sample_rate =  info->audio_stream[0].sample_rate;
		s->tracks[i].codec_para.bits_per_coded_sample = info->audio_stream[0].bits_per_sample;
		s->tracks[i].codec_para.bit_rate = info->audio_stream[0].bit_rate;
		s->tracks[i].timescale = MOV_TIMESCALE;
		s->tracks[i].sample_size = 0;
		s->tracks[i].start_pts = AV_NOPTS_VALUE;
		s->tracks[i].end_pts = AV_NOPTS_VALUE;
		s->tracks[i].tag  = mov_find_codec_tag(codec_mp4_tags, s->tracks[i].codec_para.codec_id);
	}

	/* pre-allocate cluster arrays based on configured max duration */
	max_sec = (int)info->duration;

	if (max_sec <= 0)
		max_sec = MAX_RECORD_SEC;

	for (i = 0; i < s->nb_streams; i++) {
		if (s->tracks[i].codec_para.codec_type == MPP_MEDIA_TYPE_VIDEO) {
			int fps = s->tracks[i].codec_para.frame_size;

			if (fps <= 0)
				fps = 30;
			max_entries = max_sec * fps * 6 / 5; /* +20% margin */
		} else {
			max_entries = max_sec * 50;
		}
		s->tracks[i].cluster = mpp_alloc(max_entries * sizeof(*s->tracks[i].cluster));
		if (!s->tracks[i].cluster) {
			loge("cluster alloc fail");
			while (--i >= 0)
				mpp_free(s->tracks[i].cluster);
			mpp_free(s->tracks);
			s->tracks = NULL;
			return -1;
		}
		s->tracks[i].cluster_capacity = max_entries;
	}


	return 0;
}

s32 mp4_write_header(struct aic_mov_muxer *s)
{
	mov_write_ftyp_tag(s);
	mov_write_mdat_tag(s);
	return 0;
}

s32 mp4_write_packet(struct aic_mov_muxer *s,struct aic_av_packet *pkt)
{
	int samples_in_chunk;
	struct mov_track *trk = NULL;
	int size = pkt->size;

	if (pkt->type == MPP_MEDIA_TYPE_VIDEO) {
		trk = &s->tracks[0];
	} else if (pkt->type == MPP_MEDIA_TYPE_AUDIO) {
		trk = (s->nb_streams > 1) ? &s->tracks[1] : &s->tracks[0];
	} else {
		loge("unknown media type %d.", pkt->type);
		return -1;
	}

	// write data into file
	aic_stream_write(s->stream,pkt->data,pkt->size);

	if (trk->entry >= trk->cluster_capacity) {
		loge("cluster full (%d ~ %d entries), check max_record_sec",
			 trk->cluster_capacity, trk->entry);
		return -1;
	}

	if (trk->sample_size) {
		samples_in_chunk = size / trk->sample_size;
	} else {
		samples_in_chunk = 1;
	}

	trk->cluster[trk->entry].pos              = aic_stream_tell(s->stream) - size;
	trk->cluster[trk->entry].samples_in_chunk = samples_in_chunk;
	trk->cluster[trk->entry].chunk_num         = 0;
	trk->cluster[trk->entry].size             = size;
	trk->cluster[trk->entry].entries          = samples_in_chunk;
	/*pts and dts need to transform*/
	trk->cluster[trk->entry].dts              = pkt->dts;
	trk->cluster[trk->entry].pts              = pkt->pts;

	if (trk->start_pts == AV_NOPTS_VALUE) {
		trk->start_pts = pkt->pts;
	}

	trk->track_duration = pkt->pts - trk->start_pts + pkt->duration;
	trk->entry++;
	trk->sample_count += samples_in_chunk;
	s->mdat_size    += size;
	return 0;
}

s32 mp4_write_trailer(struct aic_mov_muxer *s)
{
	struct aic_stream* pb = s->stream;
	int64_t pos = aic_stream_tell(pb);

	aic_stream_seek(pb, s->mdat_pos, SEEK_SET);
	aic_stream_wb32(pb, s->mdat_size + 8);
	aic_stream_seek(pb, pos, SEEK_SET);
	mov_write_moov_tag(s);
	return 0;
}

s32 mp4_close(struct aic_mov_muxer *s)
{
	int i;

	if (!s->tracks) {
		aic_stream_close(s->stream);
		return -1;
	}
	for (i = 0; i < s->nb_streams; i++) {
		mpp_free(s->tracks[i].cluster);
	}
	mpp_free(s->tracks);
	aic_stream_close(s->stream);
	return 0;
}
