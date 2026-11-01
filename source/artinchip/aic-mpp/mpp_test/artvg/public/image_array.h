
/*
 * Copyright (c) 2023-2025, ArtInChip Technology Co., Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Authors:  ZeQuan Liang <zequan.liang@artinchip.com>
 */

#ifndef IMAGE_ARRAY_TEST_H
#define IMAGE_ARRAY_TEST_H

#include "./image_c_array/image_struct.h"

int image_array_read(const image_dsc_t *dsc, void *buff, int buff_len);

/**
 * Copy image data to a buffer with different stride
 * This function handles the case where source and destination buffers have
 * different stride values by copying data row-by-row.
 *
 * @param dsc: Image descriptor containing source data and stride info
 * @param dst: Destination buffer pointer
 * @param dst_stride: Destination buffer stride in bytes
 * @return: 0 on success, -1 on error
 */
int image_array_read_with_stride(const image_dsc_t *dsc, void *dst, int dst_stride);

#endif