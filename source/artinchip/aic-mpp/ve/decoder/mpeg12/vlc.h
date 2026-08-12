/*
 * Copyright (C) 2020-2025 ArtInChip Technology Co. Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 *  author: <xiaodong.zhao@artinchip.com>
 *  Desc: vlc interface
 *
 */

#ifndef VLC_H
#define VLC_H

#include <stdint.h>

#define MPEG2_INTRA_Q_SIZE 64
#define FF_ZIGZAG_DIRECT_SIZE 64
#define FF_ALTERNATE_VERTICAL_SCAN_SIZE 64
#define ZIGZAG_DIRECT_ROWS 2
#define FF_MPEG12_VLC_DC_LUM_BITS_SIZE 12
#define DCTTABFIRST_SIZE 32
#define DCTTABNEXT_SIZE 32
#define DCTTAB0_SIZE 128
#define DCTTAB0A_SIZE 512
#define DCTTAB1_SIZE 32
#define DCTTAB1A_SIZE 32
#define DCTTAB2_SIZE 64
#define DCTTAB3_SIZE 64
#define DCTTAB4_SIZE 64
#define DCTTAB5_SIZE 64
#define DCTTAB6_SIZE 64
#define DCT_DC_SIZE_LUMINANCE_TABLE_SIZE 512
#define DCT_DC_SIZE_CHROMINANCE_TABLE_SIZE 1024
#define MOTION_CODE_TABLE_SIZE 2048
#define MACROBLOCK_ADDRESS_INCREMENT_TABLE_SIZE 2048
#define CODED_BLOCK_PATTERN_TABLE_SIZE 512
#define MACROBLOCK_TYPE_P_TABLE_SIZE 64
#define MACROBLOCK_TYPE_B_TABLE_SIZE 64

struct DCTtab {
    int run, level, len, sign;
};

struct VLC1 {
    int len;
    unsigned int code;
    int value;
};

struct VLC2 {
    int len;
    unsigned int code;
    int run;
    int level;
};

struct vlc_tab1 {
    int len;
    int value;
};

struct vlc_tab2 {
    char len;
    char run;
    char level;
    char padding_byte;
};

extern unsigned char mpeg2_intra_q[MPEG2_INTRA_Q_SIZE];
extern unsigned char ff_zigzag_direct[FF_ZIGZAG_DIRECT_SIZE];
extern unsigned char ff_alternate_vertical_scan[FF_ALTERNATE_VERTICAL_SCAN_SIZE];

extern unsigned long DCTtabfirst[DCTTABFIRST_SIZE];
extern unsigned long DCTtabnext[DCTTABNEXT_SIZE];
extern unsigned long DCTtab0[DCTTAB0_SIZE];
extern unsigned long DCTtab0a[DCTTAB0A_SIZE];
extern unsigned long DCTtab1[DCTTAB1_SIZE];
extern unsigned long DCTtab1a[DCTTAB1A_SIZE];
extern unsigned long DCTtab2[DCTTAB2_SIZE];
extern unsigned long DCTtab3[DCTTAB3_SIZE];
extern unsigned long DCTtab4[DCTTAB4_SIZE];
extern unsigned long DCTtab5[DCTTAB5_SIZE];
extern unsigned long DCTtab6[DCTTAB6_SIZE];
extern struct vlc_tab1 dct_dc_size_luminance_table[DCT_DC_SIZE_LUMINANCE_TABLE_SIZE];
extern struct vlc_tab1 dct_dc_size_chrominance_table[DCT_DC_SIZE_CHROMINANCE_TABLE_SIZE];
extern struct vlc_tab1 motion_code_table[MOTION_CODE_TABLE_SIZE];
extern struct vlc_tab1 macroblock_address_increment_table[MACROBLOCK_ADDRESS_INCREMENT_TABLE_SIZE];
extern struct vlc_tab1 coded_block_pattern_table[CODED_BLOCK_PATTERN_TABLE_SIZE];
extern struct vlc_tab1 macroblock_type_p_table[MACROBLOCK_TYPE_P_TABLE_SIZE];
extern struct vlc_tab1 macroblock_type_b_table[MACROBLOCK_TYPE_B_TABLE_SIZE];

void init_vlcs();

#endif
