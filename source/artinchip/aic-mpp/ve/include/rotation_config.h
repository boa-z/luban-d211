/*
 * Copyright (C) 2020-2026 ArtInChip Technology Co. Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 *  author: <jianye.liang@artinchip.com>
 *  Desc: Rotation and mirror configuration table for video post-processing
 */

#ifndef ROTATION_CONFIG_H
#define ROTATION_CONFIG_H

#include "mpp_dec_type.h"

#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]))

/* Rotation configuration structure */
typedef struct {
    int rotate;         // Rotation angle (0/90/180/270)
    int flip_h;         // Horizontal mirror flag (0/1)
    int flip_v;         // Vertical mirror flag (0/1)
    int set_h_offset;   // Whether to set horizontal offset (0/1)
    int set_v_offset;   // Whether to set vertical offset (0/1)
    int h_v_switch;     // Whether to swap width and height (0/1)
} rotation_config;

/**
 * Rotation configuration table for all 16 possible combinations
 *
 * Each entry defines how to transform the frame for a specific
 * rotation and mirror combination. The table covers all combinations:
 * - 4 rotation angles (0°, 90°, 180°, 270°)
 * - 4 mirror combinations (none, H-flip, V-flip, H+V-flip)
 * Total: 16 entries
 */
static const rotation_config rotation_configs[] = {
    /* 0 rotation combinations */
    {MPP_ROTATION_0,   0, 1, 0, 1, 0},  // 0100: V-flip only
    {MPP_ROTATION_0,   1, 0, 1, 0, 0},  // 1000: H-flip only
    {MPP_ROTATION_0,   1, 1, 1, 1, 0},  // 1100: H-flip + V-flip

    /* 90 rotation combinations */
    {MPP_ROTATION_90,  0, 0, 0, 1, 1},  // 0001: 90 only
    {MPP_ROTATION_90,  0, 1, 1, 1, 1},  // 0101: 90 + V-flip
    {MPP_ROTATION_90,  1, 0, 1, 0, 1},  // 1001: 90 + H-flip
    {MPP_ROTATION_90,  1, 1, 1, 0, 1},  // 1101: 90 + H-flip + V-flip

    /* 180 rotation combinations */
    {MPP_ROTATION_180, 0, 0, 1, 1, 0},  // 0010: 180 only
    {MPP_ROTATION_180, 0, 1, 1, 0, 0},  // 0110: 180 + V-flip
    {MPP_ROTATION_180, 1, 0, 0, 1, 0},  // 1010: 180 + H-flip
    {MPP_ROTATION_180, 1, 1, 0, 0, 0},  // 1110: 180 + H-flip + V-flip

    /* 270 rotation combinations */
    {MPP_ROTATION_270, 0, 0, 1, 0, 1},  // 0011: 270 only
    {MPP_ROTATION_270, 0, 1, 0, 1, 1},  // 0111: 270 + V-flip
    {MPP_ROTATION_270, 1, 0, 0, 0, 1},  // 1011: 270 + H-flip
    {MPP_ROTATION_270, 1, 1, 0, 1, 1},  // 1111: 270 + H-flip + V-flip
};

/**
 * Swap two integer values
 */
static inline void swap_val(int *a, int *b)
{
    int tmp = *a;
    *a = *b;
    *b = tmp;
}

/**
 * Find rotation configuration by flags
 * @param rotate: Rotation angle (0/90/180/270)
 * @param flip_h: Horizontal mirror flag
 * @param flip_v: Vertical mirror flag
 * @return Pointer to matching config, or NULL if not found
 */
static inline const rotation_config* find_rotation_config(int rotate, int flip_h, int flip_v)
{
    for (int i = 0; i < ARRAY_SIZE(rotation_configs); i++) {
        if (rotation_configs[i].rotate == rotate &&
            rotation_configs[i].flip_h == flip_h &&
            rotation_configs[i].flip_v == flip_v) {
            return &rotation_configs[i];
        }
    }
    return NULL;
}

#endif /* ROTATION_CONFIG_H */
