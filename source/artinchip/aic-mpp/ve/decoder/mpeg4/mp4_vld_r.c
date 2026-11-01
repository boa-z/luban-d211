/*
 * Copyright (C) 2020-2025 ArtInChip Technology Co. Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 *  author: <xiaodong.zhao@artinchip.com>
 *  Desc: mpeg4 vldr interface
 *
 */

#include "mp4_vars.h"
#include "mp4_getbits.h"
#include "mp4_vld_r.h"

static tab_type tableR1_intra[] = {
    {72449, 11}, {72705, 11}, {74241, 12}, {74497, 12},
    {74753, 13}, {75009, 13}, {75521, 14}, {75777, 14}
};

static tab_type tableR1_inter[] = {
    {72449, 11}, {72705, 11}, {74241, 12}, {74497, 12},
    {74753, 13}, {75009, 13}, {75521, 14}, {75777, 14}
};

// codes beginning with "0" and with length > 10
// sorted according the second "0" position and the last bit before the sign bit

static tab_type tableR0_intra11bits[] = {
    {3329, 11}, {2306, 11},  {1283, 11},  {1539, 11},
    {1795, 11}, {772, 11},   {517, 11},   {518, 11},
    {264, 11},  {265, 11},   {15, 11},    {16, 11},
    {17, 11},   {65539, 11}, {66050, 11}, {72193, 11}
};

static tab_type tableR0_intra12bits[] = {
    {2562, 12},  {1028, 12},  {1284, 12},  {1540, 12},  {773, 12},   {1029, 12},
    {266, 12},   {18, 12},    {19, 12},    {22, 12},    {65795, 12}, {66306, 12},
    {66562, 12}, {72961, 12}, {73217, 12}, {73473, 12}, {73729, 12}, {73985, 12}
};

static tab_type tableR0_intra13bits[] = {
    {3585, 13},  {3841, 13},  {2818, 13},  {2051, 13},  {2307, 13},  {1796, 13}, {774, 13},
    {519, 13},   {520, 13},   {521, 13},   {267, 13},   {20, 13},    {21, 13},   {23, 13},
    {65540, 13}, {66818, 13}, {67074, 13}, {67330, 13}, {67586, 13}, {67842, 13}
};

static tab_type tableR0_intra14bits[] = {
    {4097, 14},  {4353, 14},  {4609, 14},  {2052, 14}, {1285, 14},  {1030, 14},
    {1286, 14},  {775, 14},   {776, 14},   {522, 14},  {523, 14},   {268, 14},
    {269, 14},   {24, 14},    {25, 14},    {26, 14},   {65541, 14}, {65796, 14},
    {68098, 14}, {68354, 14}, {68610, 14}, {75265, 14}
};

static tab_type tableR0_intra15bits[] = {
    {27, 15},    {777, 15},   {1541, 15},  {1797, 15},  {2308, 15},  {3074, 15},  {4865, 15},
    {65797, 15}, {66051, 15}, {68866, 15}, {76033, 15}, {76289, 15}, {76545, 15}, {76801, 15}
};

static tab_type tableR0_inter11bits[] = {
    {10, 11},   {11, 11},    {262, 11},   {516, 11},
    {1027, 11}, {1283, 11},  {2562, 11},  {5377, 11},
    {5633, 11}, {5889, 11},  {6145, 11},  {6401, 11},
    {6657, 11}, {65539, 11}, {66050, 11}, {72193, 11}
};

static tab_type tableR0_inter12bits[] = {
    {12, 12},    {263, 12},   {517, 12},   {772, 12},   {1539, 12},  {1795, 12},
    {2818, 12},  {6913, 12},  {7169, 12},  {7425, 12},  {65795, 12}, {66306, 12},
    {66562, 12}, {72961, 12}, {73217, 12}, {73473, 12}, {73729, 12}, {73985, 12}
};

static tab_type tableR0_inter13bits[] = {
    {13, 13},    {14, 13},    {15, 13},    {16, 13},    {264, 13},   {773, 13},  {1028, 13},
    {1284, 13},  {2051, 13},  {3074, 13},  {7681, 13},  {7937, 13},  {8193, 13}, {8449, 13},
    {65540, 13}, {66818, 13}, {67074, 13}, {67330, 13}, {67586, 13}, {67842, 13}
};

static tab_type tableR0_inter14bits[] = {
    {17, 14},    {18, 14},    {265, 14},   {266, 14},  {518, 14},   {519, 14},
    {774, 14},   {1540, 14},  {2307, 14},  {3330, 14}, {3586, 14},  {3842, 14},
    {4098, 14},  {8705, 14},  {8961, 14},  {9217, 14}, {65541, 14}, {65796, 14},
    {68098, 14}, {68354, 14}, {68610, 14}, {75265, 14}
};

static tab_type tableR0_inter15bits[] = {
    {19, 15},    {775, 15},   {1029, 15},  {1796, 15},  {4354, 15},  {9473, 15},  {9729, 15},
    {65797, 15}, {66051, 15}, {68866, 15}, {76033, 15}, {76289, 15}, {76545, 15}, {76801, 15}
};

// all the other codes, code length from 4 to 10 (0000 to 1000000011)
// 0xffff (65535, 0) is a not valid code

static tab_type tableRmain_intra[] = {
    {ESC, 4},    {257, 4},    {ERR, 0},    {ERR, 0},   {513, 5},   {769, 5},    {1, 3},   {2, 3},
    {258, 5},    {4, 5},      {3, 4},      {65537, 4}, {1025, 6},  {1281, 6},   {ERR, 0}, {ERR, 0},
    {ERR, 0},    {ERR, 0},    {65793, 5},  {66049, 5}, {5, 6},     {6, 6},      {ERR, 0}, {ERR, 0},
    {66305, 6},  {66561, 6},  {ERR, 0},    {ERR, 0},   {1537, 7},  {1793, 7},   {ERR, 0}, {ERR, 0},
    {ERR, 0},    {ERR, 0},    {66817, 6},  {67073, 6}, {ERR, 0},   {ERR, 0},    {ERR, 0}, {ERR, 0},
    {ERR, 0},    {ERR, 0},    {ERR, 0},    {ERR, 0},   {514, 7},   {259, 7},    {ERR, 0}, {ERR, 0},
    {ERR, 0},    {ERR, 0},    {ERR, 0},    {ERR, 0},   {7, 7},     {67329, 7},  {ERR, 0}, {ERR, 0},
    {67585, 7},  {67841, 7},  {ERR, 0},    {ERR, 0},   {2049, 8},  {2305, 8},   {ERR, 0}, {ERR, 0},
    {ERR, 0},    {ERR, 0},    {68097, 7},  {68353, 7}, {ERR, 0},   {ERR, 0},    {ERR, 0}, {ERR, 0},
    {ERR, 0},    {ERR, 0},    {ERR, 0},    {ERR, 0},   {ERR, 0},   {ERR, 0},    {ERR, 0}, {ERR, 0},
    {ERR, 0},    {ERR, 0},    {ERR, 0},    {ERR, 0},   {ERR, 0},   {ERR, 0},    {ERR, 0}, {ERR, 0},
    {ERR, 0},    {ERR, 0},    {ERR, 0},    {ERR, 0},   {770, 8},   {1026, 8},   {ERR, 0}, {ERR, 0},
    {ERR, 0},    {ERR, 0},    {ERR, 0},    {ERR, 0},   {ERR, 0},   {ERR, 0},    {ERR, 0}, {ERR, 0},
    {ERR, 0},    {ERR, 0},    {ERR, 0},    {ERR, 0},   {260, 8},   {261, 8},    {ERR, 0}, {ERR, 0},
    {ERR, 0},    {ERR, 0},    {ERR, 0},    {ERR, 0},   {8, 8},     {9, 8},      {ERR, 0}, {ERR, 0},
    {65538, 8},  {68609, 8},  {ERR, 0},    {ERR, 0},   {2561, 9},  {1282, 9},   {ERR, 0}, {ERR, 0},
    {ERR, 0},    {ERR, 0},    {68865, 8},  {69121, 8}, {ERR, 0},   {ERR, 0},    {ERR, 0}, {ERR, 0},
    {ERR, 0},    {ERR, 0},    {ERR, 0},    {ERR, 0},   {ERR, 0},   {ERR, 0},    {ERR, 0}, {ERR, 0},
    {ERR, 0},    {ERR, 0},    {ERR, 0},    {ERR, 0},   {ERR, 0},   {ERR, 0},    {ERR, 0}, {ERR, 0},
    {ERR, 0},    {ERR, 0},    {ERR, 0},    {ERR, 0},   {ERR, 0},   {ERR, 0},    {ERR, 0}, {ERR, 0},
    {ERR, 0},    {ERR, 0},    {ERR, 0},    {ERR, 0},   {ERR, 0},   {ERR, 0},    {ERR, 0}, {ERR, 0},
    {ERR, 0},    {ERR, 0},    {ERR, 0},    {ERR, 0},   {ERR, 0},   {ERR, 0},    {ERR, 0}, {ERR, 0},
    {ERR, 0},    {ERR, 0},    {ERR, 0},    {ERR, 0},   {ERR, 0},   {ERR, 0},    {ERR, 0}, {ERR, 0},
    {ERR, 0},    {ERR, 0},    {ERR, 0},    {ERR, 0},   {515, 9},   {771, 9},    {ERR, 0}, {ERR, 0},
    {ERR, 0},    {ERR, 0},    {ERR, 0},    {ERR, 0},   {ERR, 0},   {ERR, 0},    {ERR, 0}, {ERR, 0},
    {ERR, 0},    {ERR, 0},    {ERR, 0},    {ERR, 0},   {ERR, 0},   {ERR, 0},    {ERR, 0}, {ERR, 0},
    {ERR, 0},    {ERR, 0},    {ERR, 0},    {ERR, 0},   {ERR, 0},   {ERR, 0},    {ERR, 0}, {ERR, 0},
    {ERR, 0},    {ERR, 0},    {ERR, 0},    {ERR, 0},   {262, 9},   {10, 9},     {ERR, 0}, {ERR, 0},
    {ERR, 0},    {ERR, 0},    {ERR, 0},    {ERR, 0},   {ERR, 0},   {ERR, 0},    {ERR, 0}, {ERR, 0},
    {ERR, 0},    {ERR, 0},    {ERR, 0},    {ERR, 0},   {11, 9},    {65794, 9},  {ERR, 0}, {ERR, 0},
    {ERR, 0},    {ERR, 0},    {ERR, 0},    {ERR, 0},   {69377, 9}, {69633, 9},  {ERR, 0}, {ERR, 0},
    {69889, 9},  {70145, 9},  {ERR, 0},    {ERR, 0},   {2817, 10}, {3073, 10},  {ERR, 0}, {ERR, 0},
    {ERR, 0},    {ERR, 0},    {70401, 9},  {70657, 9}, {ERR, 0},   {ERR, 0},    {ERR, 0}, {ERR, 0},
    {ERR, 0},    {ERR, 0},    {ERR, 0},    {ERR, 0},   {ERR, 0},   {ERR, 0},    {ERR, 0}, {ERR, 0},
    {ERR, 0},    {ERR, 0},    {ERR, 0},    {ERR, 0},   {ERR, 0},   {ERR, 0},    {ERR, 0}, {ERR, 0},
    {ERR, 0},    {ERR, 0},    {ERR, 0},    {ERR, 0},   {ERR, 0},   {ERR, 0},    {ERR, 0}, {ERR, 0},
    {ERR, 0},    {ERR, 0},    {ERR, 0},    {ERR, 0},   {ERR, 0},   {ERR, 0},    {ERR, 0}, {ERR, 0},
    {ERR, 0},    {ERR, 0},    {ERR, 0},    {ERR, 0},   {ERR, 0},   {ERR, 0},    {ERR, 0}, {ERR, 0},
    {ERR, 0},    {ERR, 0},    {ERR, 0},    {ERR, 0},   {ERR, 0},   {ERR, 0},    {ERR, 0}, {ERR, 0},
    {ERR, 0},    {ERR, 0},    {ERR, 0},    {ERR, 0},   {ERR, 0},   {ERR, 0},    {ERR, 0}, {ERR, 0},
    {ERR, 0},    {ERR, 0},    {ERR, 0},    {ERR, 0},   {ERR, 0},   {ERR, 0},    {ERR, 0}, {ERR, 0},
    {ERR, 0},    {ERR, 0},    {ERR, 0},    {ERR, 0},   {ERR, 0},   {ERR, 0},    {ERR, 0}, {ERR, 0},
    {ERR, 0},    {ERR, 0},    {ERR, 0},    {ERR, 0},   {ERR, 0},   {ERR, 0},    {ERR, 0}, {ERR, 0},
    {ERR, 0},    {ERR, 0},    {ERR, 0},    {ERR, 0},   {ERR, 0},   {ERR, 0},    {ERR, 0}, {ERR, 0},
    {ERR, 0},    {ERR, 0},    {ERR, 0},    {ERR, 0},   {ERR, 0},   {ERR, 0},    {ERR, 0}, {ERR, 0},
    {ERR, 0},    {ERR, 0},    {ERR, 0},    {ERR, 0},   {ERR, 0},   {ERR, 0},    {ERR, 0}, {ERR, 0},
    {ERR, 0},    {ERR, 0},    {ERR, 0},    {ERR, 0},   {ERR, 0},   {ERR, 0},    {ERR, 0}, {ERR, 0},
    {ERR, 0},    {ERR, 0},    {ERR, 0},    {ERR, 0},   {1538, 10}, {1794, 10},  {ERR, 0}, {ERR, 0},
    {ERR, 0},    {ERR, 0},    {ERR, 0},    {ERR, 0},   {ERR, 0},   {ERR, 0},    {ERR, 0}, {ERR, 0},
    {ERR, 0},    {ERR, 0},    {ERR, 0},    {ERR, 0},   {ERR, 0},   {ERR, 0},    {ERR, 0}, {ERR, 0},
    {ERR, 0},    {ERR, 0},    {ERR, 0},    {ERR, 0},   {ERR, 0},   {ERR, 0},    {ERR, 0}, {ERR, 0},
    {ERR, 0},    {ERR, 0},    {ERR, 0},    {ERR, 0},   {ERR, 0},   {ERR, 0},    {ERR, 0}, {ERR, 0},
    {ERR, 0},    {ERR, 0},    {ERR, 0},    {ERR, 0},   {ERR, 0},   {ERR, 0},    {ERR, 0}, {ERR, 0},
    {ERR, 0},    {ERR, 0},    {ERR, 0},    {ERR, 0},   {ERR, 0},   {ERR, 0},    {ERR, 0}, {ERR, 0},
    {ERR, 0},    {ERR, 0},    {ERR, 0},    {ERR, 0},   {ERR, 0},   {ERR, 0},    {ERR, 0}, {ERR, 0},
    {ERR, 0},    {ERR, 0},    {ERR, 0},    {ERR, 0},   {2050, 10}, {1027, 10},  {ERR, 0}, {ERR, 0},
    {ERR, 0},    {ERR, 0},    {ERR, 0},    {ERR, 0},   {ERR, 0},   {ERR, 0},    {ERR, 0}, {ERR, 0},
    {ERR, 0},    {ERR, 0},    {ERR, 0},    {ERR, 0},   {ERR, 0},   {ERR, 0},    {ERR, 0}, {ERR, 0},
    {ERR, 0},    {ERR, 0},    {ERR, 0},    {ERR, 0},   {ERR, 0},   {ERR, 0},    {ERR, 0}, {ERR, 0},
    {ERR, 0},    {ERR, 0},    {ERR, 0},    {ERR, 0},   {516, 10},  {263, 10},   {ERR, 0}, {ERR, 0},
    {ERR, 0},    {ERR, 0},    {ERR, 0},    {ERR, 0},   {ERR, 0},   {ERR, 0},    {ERR, 0}, {ERR, 0},
    {ERR, 0},    {ERR, 0},    {ERR, 0},    {ERR, 0},   {12, 10},   {13, 10},    {ERR, 0}, {ERR, 0},
    {ERR, 0},    {ERR, 0},    {ERR, 0},    {ERR, 0},   {14, 10},   {70913, 10}, {ERR, 0}, {ERR, 0},
    {71169, 10}, {71425, 10}, {ERR, 0},    {ERR, 0},   {3329, 11}, {2306, 11},  {ERR, 0}, {ERR, 0},
    {ERR, 0},    {ERR, 0},    {71681, 10}, {71937, 10}
};

static tab_type tableRmain_inter[] = {
    {ESC, 4},    {2, 4},      {ERR, 0},    {ERR, 0},   {3, 5},     {769, 5},    {1, 3},   {257, 3},
    {1025, 5},   {1281, 5},   {513, 4},    {65537, 4}, {258, 6},   {1537, 6},   {ERR, 0}, {ERR, 0},
    {ERR, 0},    {ERR, 0},    {65793, 5},  {66049, 5}, {1793, 6},  {2049, 6},   {ERR, 0}, {ERR, 0},
    {66305, 6},  {66561, 6},  {ERR, 0},    {ERR, 0},   {4, 7},     {514, 7},    {ERR, 0}, {ERR, 0},
    {ERR, 0},    {ERR, 0},    {66817, 6},  {67073, 6}, {ERR, 0},   {ERR, 0},    {ERR, 0}, {ERR, 0},
    {ERR, 0},    {ERR, 0},    {ERR, 0},    {ERR, 0},   {2305, 7},  {2561, 7},   {ERR, 0}, {ERR, 0},
    {ERR, 0},    {ERR, 0},    {ERR, 0},    {ERR, 0},   {2817, 7},  {67329, 7},  {ERR, 0}, {ERR, 0},
    {67585, 7},  {67841, 7},  {ERR, 0},    {ERR, 0},   {5, 8},     {6, 8},      {ERR, 0}, {ERR, 0},
    {ERR, 0},    {ERR, 0},    {68097, 7},  {68353, 7}, {ERR, 0},   {ERR, 0},    {ERR, 0}, {ERR, 0},
    {ERR, 0},    {ERR, 0},    {ERR, 0},    {ERR, 0},   {ERR, 0},   {ERR, 0},    {ERR, 0}, {ERR, 0},
    {ERR, 0},    {ERR, 0},    {ERR, 0},    {ERR, 0},   {ERR, 0},   {ERR, 0},    {ERR, 0}, {ERR, 0},
    {ERR, 0},    {ERR, 0},    {ERR, 0},    {ERR, 0},   {259, 8},   {770, 8},    {ERR, 0}, {ERR, 0},
    {ERR, 0},    {ERR, 0},    {ERR, 0},    {ERR, 0},   {ERR, 0},   {ERR, 0},    {ERR, 0}, {ERR, 0},
    {ERR, 0},    {ERR, 0},    {ERR, 0},    {ERR, 0},   {1026, 8},  {3073, 8},   {ERR, 0}, {ERR, 0},
    {ERR, 0},    {ERR, 0},    {ERR, 0},    {ERR, 0},   {3329, 8},  {3585, 8},   {ERR, 0}, {ERR, 0},
    {65538, 8},  {68609, 8},  {ERR, 0},    {ERR, 0},   {7, 9},     {260, 9},    {ERR, 0}, {ERR, 0},
    {ERR, 0},    {ERR, 0},    {68865, 8},  {69121, 8}, {ERR, 0},   {ERR, 0},    {ERR, 0}, {ERR, 0},
    {ERR, 0},    {ERR, 0},    {ERR, 0},    {ERR, 0},   {ERR, 0},   {ERR, 0},    {ERR, 0}, {ERR, 0},
    {ERR, 0},    {ERR, 0},    {ERR, 0},    {ERR, 0},   {ERR, 0},   {ERR, 0},    {ERR, 0}, {ERR, 0},
    {ERR, 0},    {ERR, 0},    {ERR, 0},    {ERR, 0},   {ERR, 0},   {ERR, 0},    {ERR, 0}, {ERR, 0},
    {ERR, 0},    {ERR, 0},    {ERR, 0},    {ERR, 0},   {ERR, 0},   {ERR, 0},    {ERR, 0}, {ERR, 0},
    {ERR, 0},    {ERR, 0},    {ERR, 0},    {ERR, 0},   {ERR, 0},   {ERR, 0},    {ERR, 0}, {ERR, 0},
    {ERR, 0},    {ERR, 0},    {ERR, 0},    {ERR, 0},   {ERR, 0},   {ERR, 0},    {ERR, 0}, {ERR, 0},
    {ERR, 0},    {ERR, 0},    {ERR, 0},    {ERR, 0},   {515, 9},   {1282, 9},   {ERR, 0}, {ERR, 0},
    {ERR, 0},    {ERR, 0},    {ERR, 0},    {ERR, 0},   {ERR, 0},   {ERR, 0},    {ERR, 0}, {ERR, 0},
    {ERR, 0},    {ERR, 0},    {ERR, 0},    {ERR, 0},   {ERR, 0},   {ERR, 0},    {ERR, 0}, {ERR, 0},
    {ERR, 0},    {ERR, 0},    {ERR, 0},    {ERR, 0},   {ERR, 0},   {ERR, 0},    {ERR, 0}, {ERR, 0},
    {ERR, 0},    {ERR, 0},    {ERR, 0},    {ERR, 0},   {3841, 9},  {4097, 9},   {ERR, 0}, {ERR, 0},
    {ERR, 0},    {ERR, 0},    {ERR, 0},    {ERR, 0},   {ERR, 0},   {ERR, 0},    {ERR, 0}, {ERR, 0},
    {ERR, 0},    {ERR, 0},    {ERR, 0},    {ERR, 0},   {4353, 9},  {65794, 9},  {ERR, 0}, {ERR, 0},
    {ERR, 0},    {ERR, 0},    {ERR, 0},    {ERR, 0},   {69377, 9}, {69633, 9},  {ERR, 0}, {ERR, 0},
    {69889, 9},  {70145, 9},  {ERR, 0},    {ERR, 0},   {8, 10},    {9, 10},     {ERR, 0}, {ERR, 0},
    {ERR, 0},    {ERR, 0},    {70401, 9},  {70657, 9}, {ERR, 0},   {ERR, 0},    {ERR, 0}, {ERR, 0},
    {ERR, 0},    {ERR, 0},    {ERR, 0},    {ERR, 0},   {ERR, 0},   {ERR, 0},    {ERR, 0}, {ERR, 0},
    {ERR, 0},    {ERR, 0},    {ERR, 0},    {ERR, 0},   {ERR, 0},   {ERR, 0},    {ERR, 0}, {ERR, 0},
    {ERR, 0},    {ERR, 0},    {ERR, 0},    {ERR, 0},   {ERR, 0},   {ERR, 0},    {ERR, 0}, {ERR, 0},
    {ERR, 0},    {ERR, 0},    {ERR, 0},    {ERR, 0},   {ERR, 0},   {ERR, 0},    {ERR, 0}, {ERR, 0},
    {ERR, 0},    {ERR, 0},    {ERR, 0},    {ERR, 0},   {ERR, 0},   {ERR, 0},    {ERR, 0}, {ERR, 0},
    {ERR, 0},    {ERR, 0},    {ERR, 0},    {ERR, 0},   {ERR, 0},   {ERR, 0},    {ERR, 0}, {ERR, 0},
    {ERR, 0},    {ERR, 0},    {ERR, 0},    {ERR, 0},   {ERR, 0},   {ERR, 0},    {ERR, 0}, {ERR, 0},
    {ERR, 0},    {ERR, 0},    {ERR, 0},    {ERR, 0},   {ERR, 0},   {ERR, 0},    {ERR, 0}, {ERR, 0},
    {ERR, 0},    {ERR, 0},    {ERR, 0},    {ERR, 0},   {ERR, 0},   {ERR, 0},    {ERR, 0}, {ERR, 0},
    {ERR, 0},    {ERR, 0},    {ERR, 0},    {ERR, 0},   {ERR, 0},   {ERR, 0},    {ERR, 0}, {ERR, 0},
    {ERR, 0},    {ERR, 0},    {ERR, 0},    {ERR, 0},   {ERR, 0},   {ERR, 0},    {ERR, 0}, {ERR, 0},
    {ERR, 0},    {ERR, 0},    {ERR, 0},    {ERR, 0},   {ERR, 0},   {ERR, 0},    {ERR, 0}, {ERR, 0},
    {ERR, 0},    {ERR, 0},    {ERR, 0},    {ERR, 0},   {ERR, 0},   {ERR, 0},    {ERR, 0}, {ERR, 0},
    {ERR, 0},    {ERR, 0},    {ERR, 0},    {ERR, 0},   {ERR, 0},   {ERR, 0},    {ERR, 0}, {ERR, 0},
    {ERR, 0},    {ERR, 0},    {ERR, 0},    {ERR, 0},   {261, 10},  {771, 10},   {ERR, 0}, {ERR, 0},
    {ERR, 0},    {ERR, 0},    {ERR, 0},    {ERR, 0},   {ERR, 0},   {ERR, 0},    {ERR, 0}, {ERR, 0},
    {ERR, 0},    {ERR, 0},    {ERR, 0},    {ERR, 0},   {ERR, 0},   {ERR, 0},    {ERR, 0}, {ERR, 0},
    {ERR, 0},    {ERR, 0},    {ERR, 0},    {ERR, 0},   {ERR, 0},   {ERR, 0},    {ERR, 0}, {ERR, 0},
    {ERR, 0},    {ERR, 0},    {ERR, 0},    {ERR, 0},   {ERR, 0},   {ERR, 0},    {ERR, 0}, {ERR, 0},
    {ERR, 0},    {ERR, 0},    {ERR, 0},    {ERR, 0},   {ERR, 0},   {ERR, 0},    {ERR, 0}, {ERR, 0},
    {ERR, 0},    {ERR, 0},    {ERR, 0},    {ERR, 0},   {ERR, 0},   {ERR, 0},    {ERR, 0}, {ERR, 0},
    {ERR, 0},    {ERR, 0},    {ERR, 0},    {ERR, 0},   {ERR, 0},   {ERR, 0},    {ERR, 0}, {ERR, 0},
    {ERR, 0},    {ERR, 0},    {ERR, 0},    {ERR, 0},   {1538, 10}, {1794, 10},  {ERR, 0}, {ERR, 0},
    {ERR, 0},    {ERR, 0},    {ERR, 0},    {ERR, 0},   {ERR, 0},   {ERR, 0},    {ERR, 0}, {ERR, 0},
    {ERR, 0},    {ERR, 0},    {ERR, 0},    {ERR, 0},   {ERR, 0},   {ERR, 0},    {ERR, 0}, {ERR, 0},
    {ERR, 0},    {ERR, 0},    {ERR, 0},    {ERR, 0},   {ERR, 0},   {ERR, 0},    {ERR, 0}, {ERR, 0},
    {ERR, 0},    {ERR, 0},    {ERR, 0},    {ERR, 0},   {2050, 10}, {2306, 10},  {ERR, 0}, {ERR, 0},
    {ERR, 0},    {ERR, 0},    {ERR, 0},    {ERR, 0},   {ERR, 0},   {ERR, 0},    {ERR, 0}, {ERR, 0},
    {ERR, 0},    {ERR, 0},    {ERR, 0},    {ERR, 0},   {4609, 10}, {4865, 10},  {ERR, 0}, {ERR, 0},
    {ERR, 0},    {ERR, 0},    {ERR, 0},    {ERR, 0},   {5121, 10}, {70913, 10}, {ERR, 0}, {ERR, 0},
    {71169, 10}, {71425, 10}, {ERR, 0},    {ERR, 0},   {10, 11},   {11, 11},    {ERR, 0}, {ERR, 0},
    {ERR, 0},    {ERR, 0},    {71681, 10}, {71937, 10}
};

static const tab_type *tableR0inter[] = {
    tableR0_inter11bits, tableR0_inter12bits,
    tableR0_inter13bits, tableR0_inter14bits,
    tableR0_inter15bits
};

static const tab_type *tableR0intra[] = {
    tableR0_intra11bits, tableR0_intra12bits,
    tableR0_intra13bits, tableR0_intra14bits,
    tableR0_intra15bits
};

// exclusive 15 bit mask
static unsigned int localmask[15] = {
    0x4000, 0x2000, 0x1000, 0x0800, 0x0400, 0x0200, 0x0100, 0x0080,
    0x0040, 0x0020, 0x0010, 0x0008, 0x0004, 0x0002, 0x0001
};

event_t rvld_intra_dct(mp4_stream_t *_ld)
{
    mp4_stream_t *ld = _ld;

    event_t event;
    tab_type *tab = (tab_type *)NULL;
    unsigned int code, len = 1; // codelength - 2 (exluded first bit and sign)

    code = showbits(ld, 15);

    if (!(code & localmask[0])) {
        /* first bit "0" */
        int zeroscount = 0;
        while ((zeroscount < 2) && (len <= 14)) // stop when find third bit "0"
        {
            zeroscount = (code & localmask[len]) ? zeroscount : zeroscount + 1;
            len++;
        }

        if (len <= 9) {
            code = code & 0x7fff;
            code = code >> (15 - (len + 1));
            tab = &tableRmain_intra[code];
        } else if (len < 15) {
            // index depends from second "0" position
            unsigned int position, bit_index, len_index, tab_index;
            for (position = 1; code & localmask[position]; position++) {

            }

            code = code & 0x7fff;
            code = code >> (15 - (len + 1));
            bit_index = (code & 0x0001);
            len_index = len - 10;
            tab_index = ((position - 1) << 1) + bit_index;
            tab = (tab_type *)&tableR0intra[len_index][tab_index];
        }
    } else {
        /* first bit 1 */
        for (; (!(code & localmask[len]) && (len < 14)); len++) {

        }
        len += 1; // considerate final bit ("1")

        if (len <= 9) {
            code = code & 0x7fff;
            code = code >> (15 - (len + 1));
            tab = &tableRmain_intra[code];
        } else if (len < 15) {
            int bit_index, len_index, tab_index;
            code = code & 0x7fff;
            code = code >> (15 - (len + 1));
            bit_index = (code & 0x0001);
            len_index = len - 10;
            tab_index = (len_index << 1) + bit_index;
            tab = &tableR1_intra[tab_index];
        }
    }

    if (!tab) { /* bad code */
        event.run = event.level = event.last = -1;
        return event;
    }

    flushbits(ld, tab->len);

    switch (tab->val) {
    case ESC:             /* fixed length codes */
        flushbits(ld, 1); /* bit s */
        event.last = getbits(ld, 1);
        event.run = getbits(ld, 6);
        getbits1(ld); /* marker bit */
        event.level = getbits(ld, 11);
        getbits1(ld); /* marker bit */
        event.level = getbits(ld, 5) ? -event.level : event.level;
        break;
    case ERR:
        event.run = event.level = event.last = -1;
        return event;
        break;
    default:
        event.run = (tab->val >> 8) & 255;
        event.level = tab->val & 255;
        event.last = (tab->val >> 16) & 1;
        event.level = getbits(ld, 1) ? -event.level : event.level;
        break;
    }

    return event;
}

event_t rvld_inter_dct(mp4_stream_t *_ld)
{
    mp4_stream_t *ld = _ld;

    event_t event;
    tab_type *tab = (tab_type *)NULL;
    int code, len = 1; // codelength - 2 (exluded first bit and sign)

    code = showbits(ld, 15);

    if (!(code & localmask[0])) {
        /* first bit 0 */
        int zeroscount = 0;
        while ((zeroscount < 2) && (len <= 14)) // stop when find third bit "0"
        {
            zeroscount = (code & localmask[len]) ? zeroscount : zeroscount + 1;
            len++;
        }

        if (len <= 9) {
            code = code & 0x7fff;
            code = code >> (15 - (len + 1));
            tab = &tableRmain_inter[code];
        } else if (len < 15) {
            // index depends from second "0" position
            int position, bit_index, len_index, tab_index;
            for (position = 1; code & localmask[position]; position++) {

            }

            code = code & 0x7fff;
            code = code >> (15 - (len + 1));
            bit_index = (code & 0x0001);
            len_index = len - 10;
            tab_index = ((position - 1) << 1) + bit_index;
            tab = (tab_type *)&tableR0inter[len_index][tab_index];
        }
    } else {
        /* first bit 1 */
        for (; (!(code & localmask[len]) && (len < 14)); len++) {

        }
        len += 1; // considerate final bit ("1")

        if (len <= 9) {
            code = code & 0x7fff;
            code = code >> (15 - (len + 1));
            tab = &tableRmain_inter[code];
        } else if (len < 15) {
            int bit_index, len_index, tab_index;
            code = code & 0x7fff;
            code = code >> (15 - (len + 1));
            bit_index = (code & 0x0001);
            len_index = len - 10;
            tab_index = (len_index << 1) + bit_index;
            tab = &tableR1_inter[tab_index];
        }
    }

    if (!tab) { /* bad code */
        event.run = event.level = event.last = -1;
        return event;
    }

    flushbits(ld, tab->len);

    switch (tab->val) {
    case ESC:             // fixed length codes
        flushbits(ld, 1); /* bit s */
        event.last = getbits(ld, 1);
        event.run = getbits(ld, 6);
        getbits1(ld); /* marker bit */
        event.level = getbits(ld, 11);
        getbits1(ld); /* marker bit */
        event.level = getbits(ld, 5) ? -event.level : event.level;
        break;
    case ERR:
        event.run = event.level = event.last = -1;
        return event;
        break;
    default:
        event.run = (tab->val >> 8) & 255;
        event.level = tab->val & 255;
        event.last = (tab->val >> 16) & 1;
        event.level = getbits(ld, 1) ? -event.level : event.level;
        break;
    }

    return event;
}
