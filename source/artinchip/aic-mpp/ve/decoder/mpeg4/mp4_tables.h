/*
 * Copyright (C) 2020-2025 ArtInChip Technology Co. Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 *  author: <xiaodong.zhao@artinchip.com>
 *  Desc: mpeg4 tables interface
 *
 */

#ifndef _MP4_TABLES_H_
#define _MP4_TABLES_H_

extern unsigned int zig_zag_scan[64];
extern unsigned int alternate_horizontal_scan[64];
extern unsigned int alternate_vertical_scan[64];
extern unsigned int intra_quant_matrix[64];
extern unsigned int nonintra_quant_matrix[64];
extern unsigned int wmv1_scantable[4][64];
extern unsigned int wmv2_scantableA[64];
extern unsigned int wmv2_scantableB[64];

extern int DQtab[4];
extern unsigned int msk[33];
extern int roundtab[16];
extern tab_type MCBPCtabIntra[32];
extern tab_type MCBPCtabInter[256];
extern tab_type CBPYtab[48];
extern tab_type MVtab0[14];
extern tab_type MVtab1[96];
extern tab_type MVtab2[124];
extern tab_type tableB16_1[112];
extern tab_type tableB16_2[96];
extern tab_type tableB16_3[120];
extern tab_type tableB17_1[112];
extern tab_type tableB17_2[96];
extern tab_type tableB17_3[120];
extern tab_type tableI2_1[112];
extern tab_type tableI2_2[96];
extern tab_type tableI2_3[120];

#endif
