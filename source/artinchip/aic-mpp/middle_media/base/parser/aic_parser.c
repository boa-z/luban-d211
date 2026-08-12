/*
 * Copyright (C) 2020-2026 ArtInChip Technology Co. Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 *  author: <jun.ma@artinchip.com>
 *  Desc: aic_parser
 */

#include <string.h>

#include "aic_mov_parser.h"
#include "aic_raw_parser.h"
#include "aic_mp3_parser.h"
#ifdef WAV_DEMUXER
#include "aic_wav_parser.h"
#endif
#ifdef AVI_DEMUXER
#include "aic_avi_parser.h"
#endif
#ifdef MKV_DEMUXER
#include "aic_mkv_parser.h"
#endif
#ifdef TS_DEMUXER
#include "aic_ts_parser.h"
#endif
#ifdef FLV_DEMUXER
#include "aic_flv_parser.h"
#endif
#ifdef RTSP_DEMUXER
#include "aic_rtsp_parser.h"
#endif
#ifdef HLS_DEMUXER
#include "aic_hls_parser.h"
#endif
#ifdef OGG_DEMUXER
#include "aic_ogg_parser.h"
#endif
#ifdef AAC_DECODER
#include "aic_aac_parser.h"
#endif
#ifdef APE_DECODER
#include "aic_ape_parser.h"
#endif
#ifdef FLAC_DECODER
#include "aic_flac_parser.h"
#endif
#ifdef MPG_DEMUXER
#include "aic_mpg_parser.h"
#endif

struct aic_parser_create_tbl {
	char  file_type[7];
	unsigned char len;
	s32 (*parser)(unsigned char *uri, struct aic_parser **parser);
};

struct aic_parser_create_tbl_ext {
	char *scheme;
	char *path_marker;
	char *query_key;
	char *query_value;
	s32 (*parser)(unsigned char *uri, struct aic_parser **parser);
};


struct aic_parser_create_tbl create_tbl[] = {
	{"mov", 3, aic_mov_parser_create},
	{"mp4", 3, aic_mov_parser_create},
	{"264", 3, aic_raw_parser_create},
	{"mp3", 3, aic_mp3_parser_create},
#ifdef WAV_DEMUXER
	{"wav", 3, aic_wav_parser_create},
#endif
#ifdef OGG_DEMUXER
	{"ogg", 3, aic_ogg_parser_create},
	{"opus", 4, aic_opus_parser_create},
#endif
#ifdef AVI_DEMUXER
	{"avi", 3, aic_avi_parser_create},
#endif
#ifdef MKV_DEMUXER
	{"mkv", 3, aic_mkv_parser_create},
#endif
#ifdef TS_DEMUXER
	{"ts", 2, aic_ts_parser_create},
#endif
#ifdef FLV_DEMUXER
	{"flv", 3, aic_flv_parser_create},
#endif
#ifdef RTSP_DEMUXER
	{"rtsp", 4, aic_rtsp_parser_create},
#endif
#ifdef HLS_DEMUXER
	{"m3u8", 4, aic_hls_parser_create},
	{"m3u", 3, aic_hls_parser_create},
#endif
#ifdef AAC_DECODER
	{"aac", 3, aic_aac_parser_create},
#endif
#ifdef APE_DECODER
    {"ape", 3, aic_ape_parser_create},
#endif
#ifdef FLAC_DECODER
	{"flac", 4, aic_flac_parser_create},
#endif
#ifdef MPG_DEMUXER
	{"mpg",  3, aic_mpg_parser_create},
	{"mpeg", 4, aic_mpg_parser_create},
#endif
};

struct aic_parser_create_tbl_ext create_tbl_ext[] = {
	//scheme,  path_marker,  query_key,    query_value,   parser

	// HTTP MP4 URL patterns - various query parameter formats
	{NULL,     NULL,        "mime_type",  "video_mp4",    aic_mov_parser_create},
	{NULL,     NULL,        "mime_type",  "mp4",          aic_mov_parser_create},
	{NULL,     NULL,        "mimeType",   "video_mp4",    aic_mov_parser_create},
	{NULL,     NULL,        "mimeType",   "mp4",          aic_mov_parser_create},
	// Format type patterns
	{NULL,     NULL,        "format",     "mp4",          aic_mov_parser_create},
	{NULL,     NULL,        "type",       "mp4",          aic_mov_parser_create},
	{NULL,     NULL,        "container",  "mp4",          aic_mov_parser_create},
	// CDN-specific path markers
	{NULL,     "/mp4/",     NULL,         NULL,           aic_mov_parser_create},

#ifdef TS_DEMUXER
	{NULL,     ".ts?",      NULL,         NULL,           aic_ts_parser_create},
	{NULL,     NULL,        "type",       "ts",           aic_ts_parser_create},
	{NULL,     NULL,        "format",     "ts",           aic_ts_parser_create},
#endif
#ifdef HLS_DEMUXER
	{"m3u8",   NULL,        NULL,         NULL,           aic_hls_parser_create},
	{NULL,     ".m3u8",     NULL,         NULL,           aic_hls_parser_create},
	{NULL,     "/m3u8",     NULL,         NULL,           aic_hls_parser_create},
	{NULL,     NULL,        "format",     "m3u8",         aic_hls_parser_create},
	{NULL,     NULL,        "type",       "m3u8",         aic_hls_parser_create},
	{NULL,     NULL,        "type",       "hls",          aic_hls_parser_create},
	{NULL,     "/hls/",     NULL,         NULL,           aic_hls_parser_create},
#endif
#ifdef FLV_DEMUXER
	{NULL,     ".flv?",     NULL,         NULL,           aic_flv_parser_create},
	{NULL,     "/live-",    NULL,         NULL,           aic_flv_parser_create},
#endif
};

static int aic_parser_match_ext(unsigned char *uri, struct aic_parser **parser)
{
	int size = sizeof(create_tbl_ext) / sizeof(struct aic_parser_create_tbl_ext);
	struct aic_parser_create_tbl_ext *tbl = create_tbl_ext;
	char *query = NULL;
	int i;

	// Find query string for query parameter matching
	query = strchr((char *)uri, '?');

	for (i = 0; i < size; i++) {
		// 1. Check scheme prefix: "m3u8://"
		if (tbl[i].scheme) {
			int len = strlen(tbl[i].scheme);
			if (!strncasecmp((char *)uri, tbl[i].scheme, len) &&
			    !strncmp((char *)uri + len, "://", 3)) {
				logi("matched scheme: %s\n", tbl[i].scheme);
				return tbl[i].parser(uri, parser);
			}
		}

		// 2. Check path marker: ".m3u8",  "/m3u8"， "/hls/"
		if (tbl[i].path_marker) {
			if (strstr((char *)uri, tbl[i].path_marker)) {
				logi("matched path_marker: %s\n", tbl[i].path_marker);
				return tbl[i].parser(uri, parser);
			}
		}

		// 3. Check query parameter: "format=m3u8", "type=hls"
		if (tbl[i].query_key && tbl[i].query_value && query) {
			char pattern[64] = {0};
			int len = snprintf(pattern, sizeof(pattern), "%s=%s",
			                tbl[i].query_key, tbl[i].query_value);
			char *match = query;

			while ((match = strstr(match, pattern)) != NULL) {
				// Check prefix: must be preceded by '?' or '&'
				if (match > query && *(match - 1) != '&' && *(match - 1) != '?') {
					match++;
					continue;
				}
				char *end = match + len;
				// Check suffix: must be followed by '&' or '\0'
				if (*end == '&' || *end == '\0') {
					logi("matched query: %s=%s\n",
					     tbl[i].query_key, tbl[i].query_value);
					return tbl[i].parser(uri, parser);
				}
				match++;
			}
		}
	}

	return -1; // No match
}

s32 aic_parser_create(unsigned char *uri, struct aic_parser **parser)
{
	char* query = NULL;
	char* ptr = NULL;
	int size = 0;
	int i = 0;

	if (uri == NULL) {
		loge("uri is null");
		return -1;
	}

	query = strchr((char *)uri, '?');
	ptr = strrchr((char *)uri, '.');

	size = sizeof(create_tbl)/sizeof(struct aic_parser_create_tbl);

	//1.First search:match the suffix(the last .xxx in the entire uri)
	if (ptr) {
		//If there's a query string, ensure the dot is before it
		if (query && ptr > query) {
			char *p = query - 1;
			ptr = NULL;
			while (p > (char *)uri) {
				if (*p == '.') {
					ptr = p;
					break;
				}
				p--;
			}
		}
		if (ptr) {
			for (i = 0; i < size; i++) {
				if (!strncmp(ptr + 1, create_tbl[i].file_type, create_tbl[i].len)) {
					return create_tbl[i].parser(uri, parser);
				}
			}
		}
	}

	logd("parser for (%s)\n", uri);
	//2.Second search: match the url extention type
	if (!aic_parser_match_ext(uri, parser)) {
		return 0;
	}

	loge("unkown parser for (%s)", uri);
	return -1;
}
