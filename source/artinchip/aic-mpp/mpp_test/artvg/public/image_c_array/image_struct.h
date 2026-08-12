#ifndef IMAG_STRUCT_H
#define IMAG_STRUCT_H
#include "stdint.h"

#define IMAGE_HEADER_MAGIC (0x19)

#define ATTRIBUTE_MEM_ALIGN __attribute__((aligned(8)))
#define ATTRIBUTE_LARGE_CONST

enum _lv_color_format_t {
    COLOR_FORMAT_UNKNOWN           = 0,

    COLOR_FORMAT_RAW               = 0x01,
    COLOR_FORMAT_RAW_ALPHA         = 0x02,

    /*<=1 byte (+alpha) formats*/
    COLOR_FORMAT_L8                = 0x06,
    COLOR_FORMAT_I1                = 0x07,
    COLOR_FORMAT_I2                = 0x08,
    COLOR_FORMAT_I4                = 0x09,
    COLOR_FORMAT_I8                = 0x0A,
    COLOR_FORMAT_A8                = 0x0E,

    /*2 byte (+alpha) formats*/
    COLOR_FORMAT_RGB565            = 0x12,
    COLOR_FORMAT_ARGB8565          = 0x13,   /**< Not supported by sw renderer yet. */
    COLOR_FORMAT_RGB565A8          = 0x14    /**< Color array followed by Alpha array*/,

    /*3 byte (+alpha) formats*/
    COLOR_FORMAT_RGB888            = 0x0F,
    COLOR_FORMAT_ARGB8888          = 0x10,
    COLOR_FORMAT_XRGB8888          = 0x11,

    /*Formats not supported by software renderer but kept here so GPU can use it*/
    COLOR_FORMAT_A1                = 0x0B,
    COLOR_FORMAT_A2                = 0x0C,
    COLOR_FORMAT_A4                = 0x0D,

    /* reference to https://wiki.videolan.org/YUV/ */
    /*YUV planar formats*/
    COLOR_FORMAT_YUV_START         = 0x20,
    COLOR_FORMAT_I420              = COLOR_FORMAT_YUV_START,  /*YUV420 planar(3 plane)*/
    COLOR_FORMAT_I422              = 0x21,  /*YUV422 planar(3 plane)*/
    COLOR_FORMAT_I444              = 0x22,  /*YUV444 planar(3 plane)*/
    COLOR_FORMAT_I400              = 0x23,  /*YUV400 no chroma channel*/
    COLOR_FORMAT_NV21              = 0x24,  /*YUV420 planar(2 plane), UV plane in 'V, U, V, U'*/
    COLOR_FORMAT_NV12              = 0x25,  /*YUV420 planar(2 plane), UV plane in 'U, V, U, V'*/

    /*YUV packed formats*/
    COLOR_FORMAT_YUY2              = 0x26,  /*YUV422 packed like 'Y U Y V'*/
    COLOR_FORMAT_UYVY              = 0x27,  /*YUV422 packed like 'U Y V Y'*/

    COLOR_FORMAT_YUV_END           = COLOR_FORMAT_UYVY,
};

typedef struct {
    uint32_t magic: 8;          /*Magic number. Must be IMAGE_HEADER_MAGIC*/
    uint32_t cf : 8;            /*Color format: See `lv_color_format_t`*/
    uint32_t flags: 16;         /*Image flags, see `image_flags_t`*/

    uint32_t w: 16;
    uint32_t h: 16;
    uint32_t stride: 16;        /*Number of bytes in a row*/
    uint32_t reserved_2: 16;    /*Reserved to be used later*/
} image_header_t;

typedef struct {
    image_header_t header;   /**< A header describing the basics of the image*/
    uint32_t data_size;         /**< Size of the image in bytes*/
    const uint8_t * data;       /**< Pointer to the data of the image*/
    const void * reserved;      /**< A reserved field to make it has same size as lv_draw_buf_t*/
} image_dsc_t;
#endif