#include "stdlib.h"
#include "string.h"
#include "image_array.h"

int image_array_read(const image_dsc_t *dsc, void *buff, int buff_len)
{
    if (!dsc || dsc->data_size == 0 || dsc->data == NULL)
        return -1;

    int copy_len = (dsc->data_size < buff_len) ? dsc->data_size : buff_len;
    memcpy(buff, dsc->data, copy_len);
    return 0;
}

int image_array_read_with_stride(const image_dsc_t *dsc, void *dst, int dst_stride)
{
    if (!dsc || !dst || dsc->data == NULL)
        return -1;

    const uint8_t *src = dsc->data;
    uint8_t *dst_row = (uint8_t *)dst;
    int src_stride = dsc->header.stride;
    int copy_width = src_stride;  /* Copy full stride to preserve all data */
    int rows = dsc->header.h;

    /* Copy row by row to handle stride difference */
    for (int row = 0; row < rows; row++) {
        memcpy(dst_row, src, copy_width);
        src += src_stride;
        dst_row += dst_stride;
    }

    return 0;
}