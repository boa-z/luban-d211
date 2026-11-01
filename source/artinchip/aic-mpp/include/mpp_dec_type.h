/*
 * Copyright (C) 2020-2026 ArtInChip Technology Co. Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 *  author: <qi.xu@artinchip.com>
 *  Desc: mpp_dec_type
 */

#ifndef MPP_DEC_TYPE_H
#define MPP_DEC_TYPE_H

#include <stdint.h>
#include <stddef.h>
// the header file is in dir linux/include/uapi/video
#include <video/mpp_types.h>

#ifndef u8
	typedef uint8_t		u8;
#endif
#ifndef u16
	typedef uint16_t	u16;
#endif
#ifndef u32
	typedef uint32_t	u32;
#endif
#ifndef u64
	typedef uint64_t	u64;
#endif

#ifndef s8
	typedef int8_t		s8;
#endif
#ifndef s16
	typedef int16_t		s16;
#endif
#ifndef s32
	typedef int32_t		s32;
#endif
#ifndef s64
	typedef int64_t		s64;
	#define FMT_d64		"%ld"
	#define FMT_x64		"%lx"
#endif


#define MPP_MAX(a, b) ((a)>(b)? (a) : (b))
#define MPP_MIN(a, b) ((a)<(b)? (a) : (b))

/* flags for mpp_frame */
#define FRAME_FLAG_EOS		(1 << 0)
#define FRAME_FLAG_ERROR	(1 << 1)
#define FRAME_FLAG_SOS		(1 << 2)

/* packet flag */
#define PACKET_FLAG_EOS		(1 << 0)
#define PACKET_FLAG_EXTRA_DATA	(1 << 1)
#define PACKET_FLAG_SOS		(1 << 2)
#define PACKET_FLAG_KEY		(1 << 3)

/**
 * struct mpp_packet - mpp packet buffer
 * @data: mpp packet virtual address
 * @size: mpp packet buffer size
 * @pts: pts of packet
 * @flags: buffer flags
 */
struct mpp_packet {
	void *data;
	int size;
	long long pts;
	unsigned int flag;
};

/**
 * struct mpp_scale - mpp scale ratio
 * @hor_scale: horizontal scale ratio
 * @ver_scale: vertical scale ration
 * (1- 1/2 scale; 2 - 1/4 scale; 3 - 1/8 scale)
 */
struct mpp_scale_ratio {
	int hor_scale;
	int ver_scale;
};

/**
 * struct mpp_dec_crop_info - crop info
 * @crop_x: start pos in x for crop
 * @crop_y: start pos in y for crop
 * @crop_width: width of crop window
 * @crop_height: height of crop window
 */
struct mpp_dec_crop_info {
	int crop_x;
	int crop_y;
	int crop_width;
	int crop_height;
};

/**
 * struct mpp_dec_output_pos - start pos of output
 * @output_pos_x: start pos in x for output
 * @output_pos_y: start pos in y for output
 */
struct mpp_dec_output_pos {
	int output_pos_x;
	int output_pos_y;
};

enum mpp_codec_type {
	MPP_CODEC_VIDEO_DECODER_H264 = 0x1000,         // decoder
	MPP_CODEC_VIDEO_DECODER_MJPEG,
	MPP_CODEC_VIDEO_DECODER_PNG,
	MPP_CODEC_VIDEO_DECODER_AICP,
	MPP_CODEC_VIDEO_DECODER_MPEG12,
	MPP_CODEC_VIDEO_DECODER_MPEG4,
	MPP_CODEC_VIDEO_DECODER_MPEG4_311,

	MPP_CODEC_VIDEO_ENCODER_H264 = 0x2000,         // encoder
	MPP_CODEC_VIDEO_ENCODER_MJPEG,
};

enum mpp_dec_cmd {
	MPP_DEC_INIT_CMD_SET_EXT_FRAME_ALLOCATOR,            // frame buffer allocator
	MPP_DEC_INIT_CMD_SET_ROT_FLIP_FLAG,
	MPP_DEC_INIT_CMD_SET_SCALE,
	MPP_DEC_INIT_CMD_SET_CROP_INFO,
	MPP_DEC_INIT_CMD_SET_OUTPUT_POS,
	MPP_DEC_SET_MAX_RESOLUTION,
	MPP_DEC_SET_NO_B_FRAME,
	MPP_DEC_SET_DROP_B_FRAME,		// drop non-reference B-frames
	MPP_DEC_GET_READY_PACKET_NUMBER,
	MPP_DEC_GET_RENDER_FRAME_NUMBER,
};

enum aic_audio_codec_type {
	MPP_CODEC_AUDIO_DECODER_UNKOWN = -1,
	MPP_CODEC_AUDIO_ENCODER_UNKOWN = -1,

	MPP_CODEC_AUDIO_DECODER_MP3,         // decoder
	MPP_CODEC_AUDIO_DECODER_AAC,
	MPP_CODEC_AUDIO_DECODER_PCM,
	MPP_CODEC_AUDIO_DECODER_VORBIS,
	MPP_CODEC_AUDIO_DECODER_OPUS,
	MPP_CODEC_AUDIO_DECODER_AAC_ELD,
	MPP_CODEC_AUDIO_DECODER_ALAC,
	MPP_CODEC_AUDIO_DECODER_APE,
	MPP_CODEC_AUDIO_DECODER_FLAC,
	MPP_CODEC_AUDIO_DECODER_WMA,

	MPP_CODEC_AUDIO_ENCODER_MP3 = 0x100, // encoder
	MPP_CODEC_AUDIO_ENCODER_AAC,
};

enum mpp_dec_errno {
	// if mpp_dec_get_packet return DEC_NO_EMPTY_PACKET, we should wait a minute then call again
	// it happen in send bitstream fast than decode
	DEC_NO_EMPTY_PACKET			= 4, // no packet in empty list

	// if decode return DEC_NO_READY_PACKET, we should wait a minute then call again
	// it happen in decode faster than send bitstream
	DEC_NO_READY_PACKET			= 3, //

	// if decode return DEC_NO_EMPTY_FRAME, we should wait a minute then call again
	// it happen in decode faster than render
	DEC_NO_EMPTY_FRAME 			= 2, //

	// if mpp_dec_get_frame return DEC_NO_RENDER_FRAME, we should wait a minute then call again
	// it happen in render faster than decode
	DEC_NO_RENDER_FRAME 		= 1, //

	DEC_OK					= 0,
	// decode
	DEC_ERR_NOT_SUPPORT 			= -1,

	DEC_ERR_NULL_PTR			= -2,

	// if frame manager not create, mpp_dec_get_frame return DEC_ERR_FM_NOT_CREATE.
	// app should wait a minute to get frame
	DEC_ERR_FM_NOT_CREATE			= -3,
};

enum mpp_enc_errno {
	// if mpp_enc_get_frame return DEC_NO_EMPTY_FRAME, we should wait a minute then call again
	// it happen in send frame fast than encode
	ENC_NO_EMPTY_FRAME			= 4, // no frame in empty list

	// if encode return ENC_NO_READY_FRANE, we should wait a minute then call again
	// it happen in encode faster than send frame
	ENC_NO_READY_FRAME			= 3, //

	// if encode return ENC_NO_EMPTY_PACKET, we should wait a minute then call again
	// it happen in encode faster than user get packet
	ENC_NO_EMPTY_PACKET 			= 2, //

	// if mpp_enc_get_packet return ENC_NO_READY_PACKET, we should wait a minute then call again
	// it happen in user get packet faster than encode
	ENC_NO_READY_PACKET 		= 1, //

	ENC_OK					= 0,

	// encode
	ENC_ERR_NOT_SUPPORT 			= -1,

	ENC_ERR_NULL_PTR			= -2,

	// if packet manager not create, mpp_enc_get_packet return ENC_ERR_PM_NOT_CREATE.
	// app should wait a minute to get packet
	ENC_ERR_PM_NOT_CREATE			= -3,

	// encoder interneal error
	ENC_ERR_INTERNAL			= -4,
};

#endif
