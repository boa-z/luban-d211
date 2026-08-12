/*
 * Copyright (C) 2020-2025 ArtInChip Technology Co. Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 *  author: <xiaodong.zhao@artinchip.com>
 *  Desc: ogg parser
 */

#include <stdlib.h>
#include <inttypes.h>
#include "aic_stream.h"
#include "ogg_parser.h"
#include "mpp_mem.h"
#include "mpp_log.h"

int ogg_parse_header(struct aic_ogg_parser *s)
{
    struct ogg_dec_context *ogg = &s->ctx;
    ogg_page_header_t page_header = {0};
    int found = 0, ret;

    aic_stream_seek(s->stream, 0, SEEK_SET);

    while (!found) {
        aic_stream_read(s->stream, &page_header, sizeof(ogg_page_header_t));

        // check OGG signature
        if (memcmp(page_header.capture_pattern, "OggS", 4) != 0) {
            loge("check OGG signature fail!");
            break;
        }

        // read segment table
        uint8_t segment_table[255];
        ret = aic_stream_read(s->stream, segment_table, page_header.page_segments);
        if (ret != page_header.page_segments) {
            loge("read segment table fail");
            break;
        }

        int total_segment_size = 0;
        for (int i = 0; i < page_header.page_segments; i++) {
            total_segment_size += segment_table[i];
        }

        if (total_segment_size == 0) {
            continue;
        }

        uint8_t* segment_data = (uint8_t*)mpp_alloc(total_segment_size);
        if (!segment_data) {
            loge("malloc segment_data fail!");
            break;
        }
        ret = aic_stream_read(s->stream, segment_data, total_segment_size);
        if (ret != total_segment_size) {
            loge("read segment_data fail! ret:%d size:%d", ret, total_segment_size);
            mpp_free(segment_data);
            break;
        }

        // parse packet
        int offset = 0;
        for (int seg = 0; seg < page_header.page_segments && offset < total_segment_size;) {
            // current packet size
            int packet_size = 0;
            while (seg < page_header.page_segments) {
                packet_size += segment_table[seg];
                if (segment_table[seg] < 255) {
                    seg++;
                    break;
                }
                seg++;
            }

            if (packet_size < sizeof(vorbis_header_t)) {
                offset += packet_size;
                continue;
            }

            // check Vorbis header type（type=1）
            vorbis_header_t* vorbis_header = (vorbis_header_t*)(segment_data + offset);
            if (vorbis_header->packet_type == 1 &&
                memcmp(vorbis_header->vorbis_str, "vorbis", 6) == 0) {

                // check framing flag
                if (segment_data[offset + packet_size - 1] == 1) {
                    found = 1;
                    memcpy(&s->vorbis_header, vorbis_header, sizeof(vorbis_header_t));
                    break;
                }
            }

            offset += packet_size;
        }

        mpp_free(segment_data);
    }

    ogg->filesize =  aic_stream_size(s->stream);
    aic_stream_seek(s->stream, 0, SEEK_SET);
    ogg_sync_init(&ogg->oy);    /* Now we can read pages */

    return 0;
}

static uint16_t read_le16(const uint8_t *data) {
    return data[0] | (data[1] << 8);
}

static uint32_t read_le32(const uint8_t *data) {
    return data[0] | (data[1] << 8) | (data[2] << 16) | (data[3] << 24);
}

int opus_parse_header(struct aic_ogg_parser *s)
{
    struct ogg_dec_context *opus = &s->ctx;

    aic_stream_seek(s->stream, 0, SEEK_SET);

    // read Ogg header
    uint8_t header[27]; // Ogg header is 27 byte
    if (aic_stream_read(s->stream, header, 27) != 27) {
        loge("read Ogg header fail!");
        return -1;
    }

    if (memcmp(header, "OggS", 4) != 0) {
        loge("not valid Ogg file!");
        return -1;
    }

    uint8_t segment_count = header[26];
    uint8_t segment_sizes[255];
    if (aic_stream_read(s->stream, segment_sizes, segment_count) != segment_count) {
        loge("read segment size fail!");
        return -1;
    }

    // cal first packet size
    size_t packet_size = 0;
    for (int i = 0; i < segment_count; i++) {
        packet_size += segment_sizes[i];
    }

    if (packet_size == 0) {
        loge("packet size is 0");
        return -1;
    }

    // read first packet data
    uint8_t *packet_data = malloc(packet_size);
    if (!packet_data) {
        loge("malloc packet_data fail!");
        return -1;
    }

    if (aic_stream_read(s->stream, packet_data, packet_size) != packet_size) {
        loge("aic_stream_read fail!");
        free(packet_data);
        return -1;
    }

    if (packet_size < 19 || memcmp(packet_data, "OpusHead", 8) != 0) {
        loge("not valid Opus file!");
        free(packet_data);
        return -1;
    }

    // parse Opus header
    s->opus_header.version = packet_data[8];
    s->opus_header.channels = packet_data[9];
    s->opus_header.pre_skip = read_le16(packet_data + 10);
    s->opus_header.sample_rate = read_le32(packet_data + 12);
    s->opus_header.output_gain = read_le16(packet_data + 16);
    s->opus_header.mapping_family = packet_data[18];

    opus->filesize =  aic_stream_size(s->stream);
    aic_stream_seek(s->stream, 0, SEEK_SET);
    ogg_sync_init(&opus->oy);    /* Now we can read pages */

    return 0;
}

int ogg_close(struct aic_ogg_parser *s)
{
    struct ogg_dec_context *ogg = &s->ctx;

    ogg_stream_clear(&ogg->os);
    ogg_sync_clear(&ogg->oy);

    return 0;
}

int ogg_seek_packet(struct aic_ogg_parser *s, s64 seek_time)
{
    return 0;
}

int ogg_peek_packet(struct aic_ogg_parser *s, struct aic_parser_packet *pkt)
{
    int ret, bytes;
    int64_t pos;
    struct ogg_dec_context *ogg = &s->ctx;

    pkt->size = 0;

    pos = aic_stream_tell(s->stream);
    if (pos >= ogg->filesize) {
        return PARSER_EOS;
    }

read_data:
    if (OGG_STATE_READ == ogg->state) {
        char *buffer = ogg_sync_buffer(&ogg->oy, 4096);
        if (buffer) {
            bytes = aic_stream_read(s->stream, buffer, 4096);
            ogg_sync_wrote(&ogg->oy, bytes);
            if (0 == bytes) {
                logi("EOS:%d\n", 1);
                pkt->flag |= PACKET_EOS;
            }
        } else {
            loge("ogg_sync_buffer return NULL!");
            return PARSER_NOMEM;
        }

        ret = ogg_sync_pageout(&ogg->oy, &ogg->og);
        if (ret == 0) {
            goto read_data;    /* need more data */
        }

        if (ret < 0) { /* missing or corrupt data at this page position */
            loge("Corrupt or missing data in bitstream; continuing...");
            return PARSER_ERROR;
        } else {
            /**
            Get the serial number and set up the rest of decode
            serialno first; use it to set up a logical stream
            */
            if (0 == s->frame_id) {
                ogg_stream_init(&ogg->os, ogg_page_serialno(&ogg->og));
            }

            ogg_stream_pagein(&ogg->os, &ogg->og); /* can safely ignore errors at this point */
            ogg->state = OGG_STATE_PARSE;
        }
    }

    if (OGG_STATE_PARSE == ogg->state) {
        ret = ogg_stream_packetout(&ogg->os, &ogg->op);
        if (ret == 0) {
            ogg->state = OGG_STATE_READ;
            goto read_data;    /* need more data */
        }

        if (ret < 0) {  /* missing or corrupt data at this page position */

            /* no reason to complain; already complained above */
        } else {
            /* we have a packet. */
            pkt->size = ogg->op.bytes;
            // pkt->pts = s->frame_id * s->header.frame_duration;
            pkt->type = MPP_MEDIA_TYPE_AUDIO;
        }
    }

    if (ogg_page_eos(&ogg->og))
    {
        logi("ogg file eos!\n");
        pkt->flag |= PACKET_EOS;
    }

    return 0;
}

int ogg_read_packet(struct aic_ogg_parser *s, struct aic_parser_packet *pkt)
{
    struct ogg_dec_context *ogg = &s->ctx;
    int64_t pos;

    pos = aic_stream_tell(s->stream);
    if (pos >= ogg->filesize) {
        pkt->flag |= PACKET_EOS;
    }

    memcpy(pkt->data, ogg->op.packet, ogg->op.bytes);
    s->frame_id++;

    return 0;
}
