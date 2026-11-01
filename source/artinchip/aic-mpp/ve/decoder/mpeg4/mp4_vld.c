/*
 * Copyright (C) 2020-2025 ArtInChip Technology Co. Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 *  author: <xiaodong.zhao@artinchip.com>
 *  Desc: mpeg4 vld interface
 *
 */

#include <stdio.h>
#include "mp4_vars.h"
#include "mp4_getbits.h"
#include "mp4_vld.h"
#include "mpp_log.h"

int flvh263version;
#ifdef ARCH_BSP_V60
// LAST, RUN
int vldTableB19_lut[2][64] = {
    {
        27, 10, 5, 4, 3, 3, 3, 3, 2, 2, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0,  0,  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0,  0,  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
    },
    {
        8, 3, 2, 2, 2, 2, 2, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
    }
};
#endif

/* Table B-19 -- ESCL(a), LMAX values of intra macroblocks */
static inline int vld_table_b19(int last, int run)
{
    if (!last) { /* LAST == 0 */
        if (run == 0) {
            return 27;
        } else if (run == 1) {
            return 10;
        } else if (run == 2) {
            return 5;
        } else if (run == 3) {
            return 4;
        } else if (run <= 7) {
            return 3;
        } else if (run <= 9) {
            return 2;
        } else if (run <= 14) {
            return 1;
        } else { /* illegal? */
            return 0;
        }
    } else { /* LAST == 1 */
        if (run == 0) {
            return 8;
        } else if (run == 1) {
            return 3;
        } else if (run <= 6) {
            return 2;
        } else if (run <= 20) {
            return 1;
        } else { /* illegal? */
            return 0;
        }
    }
}



#ifdef ARCH_BSP_V60
// LAST, RUN
int vldTableB20_lut[2][64] = {
    {
        12, 6, 4, 3, 3, 3, 3, 2, 2, 2, 2, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
        1,  1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0,  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
    },
    {
        3, 2, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
        1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
    }
};
#endif

/* Table B-20 -- ESCL(b), LMAX values of inter macroblocks */
static inline int vld_table_b20(int last, int run)
{
    if (!last) { /* LAST == 0 */
        if (run == 0) {
            return 12;
        } else if (run == 1) {
            return 6;
        } else if (run == 2) {
            return 4;
        } else if (run <= 6) {
            return 3;
        } else if (run <= 10) {
            return 2;
        } else if (run <= 26) {
            return 1;
        } else { /* illegal? */
            return 0;
        }
    } else { /* LAST == 1 */
        if (run == 0) {
            return 3;
        } else if (run == 1) {
            return 2;
        } else if (run <= 40) {
            return 1;
        } else { /* illegal? */
            return 0;
        }
    }
}



#ifdef ARCH_BSP_V60
// LAST, LEVEL-- how many levels are there?
int vldTableB21_lut[2][64] = {
    {
        0, 14, 9, 7, 3, 2, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0,  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0,  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
    },
    {
        0, 20, 6, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0,  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0,  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
    }
};
#endif

/* Table B-21 -- ESCR(a), RMAX values of intra macroblocks */
static inline int vld_table_b21(int last, int level)
{
    if (!last) { /* LAST == 0 */
        if (level == 1) {
            return 14;
        } else if (level == 2) {
            return 9;
        } else if (level == 3) {
            return 7;
        } else if (level == 4) {
            return 3;
        } else if (level == 5) {
            return 2;
        } else if (level <= 10) {
            return 1;
        } else if (level <= 27) {
            return 0;
        } else { /* illegal? */
            return 0;
        }
    } else { /* LAST == 1 */
        if (level == 1) {
            return 20;
        } else if (level == 2) {
            return 6;
        } else if (level == 3) {
            return 1;
        } else if (level <= 8) {
            return 0;
        } else { /* illegal? */
            return 0;
        }
    }
}



#ifdef ARCH_BSP_V60
// [last][level]
int vldTableB22_lut[2][64] = {
    {
        0, 26, 10, 6, 2, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0,  0,  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0,  0,  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
    },
    {
        0, 40, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0,  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0,  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
    }
};
#endif

/* Table B-22 -- ESCR(b), RMAX values of inter macroblocks */
static inline int vld_table_b22(int last, int level)
{
    if (!last) { /* LAST == 0 */
        if (level == 1) {
            return 26;
        } else if (level == 2) {
            return 10;
        } else if (level == 3) {
            return 6;
        } else if (level == 4) {
            return 2;
        } else if (level <= 6) {
            return 1;
        } else if (level <= 12) {
            return 0;
        } else { /* illegal? */
            return 0;
        }
    } else { /* LAST == 1 */
        if (level == 1) {
            return 40;
        } else if (level == 2) {
            return 1;
        } else if (level == 3) {
            return 0;
        } else { /* illegal? */
            return 0;
        }
    }
}

static inline tab_type *vld_table_b16(mp4_stream_t *_ld, int code)
{
    mp4_stream_t *ld = _ld;

    tab_type *tab;

    if (code >= 512) {
        tab = &(tableB16_1[(code >> 5) - 16]);
    } else if (code >= 128) {
        tab = &(tableB16_2[(code >> 2) - 32]);
    } else if (code >= 8) {
        tab = &(tableB16_3[(code >> 0) - 8]);
    } else {
        /* invalid Huffman code */
        return (tab_type *)NULL;
    }
    flushbits(ld, tab->len);
    return tab;
}

static inline tab_type *vld_table_i2(mp4_stream_t *_ld, int code)
{
    mp4_stream_t *ld = _ld;

    tab_type *tab;

    if (code >= 512) {
        tab = &(tableI2_1[(code >> 5) - 16]);
    } else if (code >= 128) {
        tab = &(tableI2_2[(code >> 2) - 32]);
    } else if (code >= 8) {
        tab = &(tableI2_3[(code >> 0) - 8]);
    } else {
        /* invalid Huffman code */
        return (tab_type *)NULL;
    }
    flushbits(ld, tab->len);
    return tab;
}

static inline tab_type *vld_table_b17(mp4_stream_t *_ld, int code)
{
    mp4_stream_t *ld = _ld;

    tab_type *tab;

    if (code >= 512) {
        tab = &(tableB17_1[(code >> 5) - 16]);
    } else if (code >= 128) {
        tab = &(tableB17_2[(code >> 2) - 32]);
    } else if (code >= 8) {
        tab = &(tableB17_3[(code >> 0) - 8]);
    } else {
        /* invalid Huffman code */
        return (tab_type *)NULL;
    }
    flushbits(ld, tab->len);
    return tab;
}

#ifdef ARCH_BSP_V60

event_t vld_intra_dct(mp4_stream_t *_ld)
{
    mp4_stream_t *ld = _ld;

    event_t event;
    tab_type *tab = (tab_type *)NULL;
    int lmax, rmax;

    tab = vld_table_b16(ld, showbits(ld, 12));
    if (!tab) { /* bad code */
        event.run = event.level = event.last = -1;
        return event;
    }

    if (tab->val != ESCAPE) {
        event.run = (tab->val >> 6) & 63;
        event.level = tab->val & 63;
        event.last = (tab->val >> 12) & 1;
        event.level = getbits(ld, 1) ? -event.level : event.level;
    } else {
        /* this value is escaped - see para 7.4.1.3 */
        /* assuming short_video_header == 0 */
        switch (showbits(ld, 2)) {
        case 0x0: /* Type 1 */
        case 0x1: /* Type 1 */
        default:
            flushbits(ld, 1);
            tab = vld_table_b16(ld, showbits(ld, 12)); /* use table B-16 */
            if (!tab) {                              /* bad code */
                event.run = event.level = event.last = -1;
                return event;
            }
            event.run = (tab->val >> 6) & 63;
            event.level = tab->val & 63;
            event.last = (tab->val >> 12) & 1;
            //B-19
            lmax = vldTableB19_lut[event.last][event.run]; /* use table B-19 */
            event.level += lmax;
            event.level = getbits(ld, 1) ? -event.level : event.level;
            break;
        case 0x2: /* Type 2 */
            flushbits(ld, 2);
            tab = vld_table_b16(ld, showbits(ld, 12)); /* use table B-16 */
            if (!tab) {                              /* bad code */
                event.run = event.level = event.last = -1;
                break;
            }
            event.run = (tab->val >> 6) & 63;
            event.level = tab->val & 63;
            event.last = (tab->val >> 12) & 1;
            rmax = vldTableB21_lut[event.last][event.level]; /* use table B-21 */
            //B-21
            event.run = event.run + rmax + 1;
            event.level = getbits(ld, 1) ? -event.level : event.level;
            break;
        case 0x3: /* Type 3  - fixed length codes */
            flushbits(ld, 2);
            event.last = getbits(ld, 1);
            event.run = getbits(ld, 6);    /* table B-18 */
            getbits(ld, 1);                /* marker bit */
            event.level = getbits(ld, 12); /* table B-18 */
            /* sign extend level... */
            event.level = (event.level & 0x800) ? (event.level | (-1 ^ 0xfff)) : event.level;
            getbits(ld, 1); /* marker bit */
            break;
        }
    }

    return event;
}

#else

event_t vld_intra_dct(mp4_stream_t *_ld)
{
    mp4_stream_t *ld = _ld;

    event_t event;
    tab_type *tab = (tab_type *)NULL;
    int lmax, rmax;

    tab = vld_table_b16(ld, showbits(ld, 12));
    if (!tab) { /* bad code */
        event.run = event.level = event.last = -1;
        return event;
    }

    if (tab->val != ESCAPE) {
        event.run = (tab->val >> 6) & 63;
        event.level = tab->val & 63;
        event.last = (tab->val >> 12) & 1;
        event.level = getbits(ld, 1) ? -event.level : event.level;
    } else {
        /* this value is escaped - see para 7.4.1.3 */
        /* assuming short_video_header == 0 */
        switch (showbits(ld, 2)) {
        case 0x0: /* Type 1 */
        case 0x1: /* Type 1 */
        default:
            flushbits(ld, 1);
            tab = vld_table_b16(ld, showbits(ld, 12)); /* use table B-16 */
            if (!tab) {                              /* bad code */
                event.run = event.level = event.last = -1;
                return event;
            }
            event.run = (tab->val >> 6) & 63;
            event.level = tab->val & 63;
            event.last = (tab->val >> 12) & 1;
            lmax = vld_table_b19(event.last, event.run); /* use table B-19 */
            event.level += lmax;
            event.level = getbits(ld, 1) ? -event.level : event.level;
            break;
        case 0x2: /* Type 2 */
            flushbits(ld, 2);
            tab = vld_table_b16(ld, showbits(ld, 12)); /* use table B-16 */
            if (!tab) {                              /* bad code */
                event.run = event.level = event.last = -1;
                break;
            }
            event.run = (tab->val >> 6) & 63;
            event.level = tab->val & 63;
            event.last = (tab->val >> 12) & 1;
            rmax = vld_table_b21(event.last, event.level); /* use table B-21 */
            event.run = event.run + rmax + 1;
            event.level = getbits(ld, 1) ? -event.level : event.level;
            break;
        case 0x3: /* Type 3  - fixed length codes */
            flushbits(ld, 2);
            event.last = getbits(ld, 1);
            event.run = getbits(ld, 6);    /* table B-18 */
            getbits(ld, 1);                /* marker bit */
            event.level = getbits(ld, 12); /* table B-18 */
            /* sign extend level... */
            event.level = (event.level & 0x800) ? (event.level | (-1 ^ 0xfff)) : event.level;
            getbits(ld, 1); /* marker bit */
            break;
        }
    }

    return event;
}

event_t vld_rmg2_intra_dct(mp4_stream_t *_ld)
{
    mp4_stream_t *ld = _ld;

    event_t event;
    tab_type *tab = (tab_type *)NULL;
    // int lmax, rmax;

    tab = vld_table_b17(ld, showbits(ld, 12));
    if (!tab) { /* bad code */
        event.run = event.level = event.last = -1;
        return event;
    }
    if (tab->val != ESCAPE) {
        event.run = (tab->val >> 4) & 255;
        event.level = tab->val & 15;
        event.last = (tab->val >> 12) & 1;
        event.level = getbits(ld, 1) ? -event.level : event.level;
    } else {
        /* this value is escaped*/
        event.last = getbits(ld, 1);
        event.run = getbits(ld, 6);
        event.level = getbits(ld, 8);
        if (event.level >= 128)
            event.level = event.level - 256;

        if (event.level == -128) {
            int t;
            t = getbits(ld, 5);
            event.level = getbits(ld, 6);
            event.level <<= 26;
            event.level >>= 21;
            event.level |= t & 0x1f;
        }

        if (event.level == 0 || event.level == 128) {
            loge("invalid level:%d", event.level);
        }
    }

    return event;
}

event_t vld_intra_aic_dct(mp4_stream_t *_ld)
{
    mp4_stream_t *ld = _ld;

    event_t event;
    tab_type *tab = (tab_type *)NULL;
    // int lmax, rmax;

    tab = vld_table_i2(ld, showbits(ld, 12));
    if (!tab) { /* bad code */
        event.run = event.level = event.last = -1;
        return event;
    }

    if (tab->val != ESCAPE) {
        event.run = (tab->val >> 6) & 63;
        event.level = tab->val & 63;
        event.last = (tab->val >> 12) & 1;
        event.level = getbits(ld, 1) ? -event.level : event.level;
    } else {
        /* this value is escaped*/
        event.last = getbits(ld, 1);
        event.run = getbits(ld, 6);
        event.level = getbits(ld, 8);
        if (event.level >= 128)
            event.level = event.level - 256;

        if (event.level == -128) {
            int t;
            t = getbits(ld, 5);
            event.level = getbits(ld, 6);
            event.level <<= 26;
            event.level >>= 21;
            event.level |= t & 0x1f;
        }

        if (event.level == 0 || event.level == 128) {
            loge("invalid level:%d", event.level);
        }
    }

    return event;
}

#endif

event_t vld_shv_dct(mp4_stream_t *_ld)
{
    mp4_stream_t *ld = _ld;

    event_t event;
    tab_type *tab = (tab_type *)NULL;

    tab = vld_table_b17(ld, showbits(ld, 12));
    if (!tab) { /* bad code */
        event.run = event.level = event.last = -1;
        return event;
    }
    if (tab->val != ESCAPE) {
        event.run = (tab->val >> 4) & 255;
        event.level = tab->val & 15;
        event.last = (tab->val >> 12) & 1;
        event.level = getbits(ld, 1) ? -event.level : event.level;
    } else {
        /* this value is escaped - see para 7.4.1.3 */
        /* short_video_header == 1 */
        if (flvh263version != 1) {
            event.last = getbits(ld, 1);
            event.run = getbits(ld, 6);
            event.level = getbits(ld, 8);

            if (event.level >= 128)
                event.level = event.level - 256;

            if (event.level == 0 || event.level == 128) {
                loge("invalid level:%d", event.level);
            }
        } else {
            int is11;
            is11 = getbits(ld, 1);
            event.last = getbits(ld, 1);
            event.run = getbits(ld, 6);
            if (is11) {
                event.level = getbits(ld, 11); // level range is [-1023, 1023]
                if (event.level >= 1024) {
                    event.level = event.level - 2048;
                }
                if (event.level == 0 || event.level == 128) {
                    loge("invalid level:%d", event.level);
                }
            } else {
                event.level = getbits(ld, 7); // level range is [-63, 63]
                if (event.level >= 64)
                    event.level = event.level - 128;

                if (event.level == 0 || event.level == 128) {
                    loge("invalid level:%d", event.level);
                }
            }
        }
    }

    return event;
}

#ifdef ARCH_BSP_V60

event_t vld_inter_dct(mp4_stream_t *_ld)
{
    mp4_stream_t *ld = _ld;

    event_t event;
    tab_type *tab = (tab_type *)NULL;
    int lmax, rmax;

    tab = vld_table_b17(ld, showbits(ld, 12));
    if (!tab) { /* bad code */
        event.run = event.level = event.last = -1;
        return event;
    }
    if (tab->val != ESCAPE) {
        event.run = (tab->val >> 4) & 255;
        event.level = tab->val & 15;
        event.last = (tab->val >> 12) & 1;
        event.level = getbits(ld, 1) ? -event.level : event.level;
    } else {
        /* this value is escaped - see para 7.4.1.3 */
        /* assuming short_video_header == 0 */
        int mode = showbits(ld, 2);
#pragma if_prob(.9)
        if (mode == 0x0 || mode == 0x1) {
            flushbits(ld, 1);
            tab = vld_table_b17(ld, showbits(ld, 12)); /* use table B-17 */
            if (!tab) {                              /* bad code */
                event.run = event.level = event.last = -1;
                return event;
            }
            event.run = (tab->val >> 4) & 255;
            event.level = tab->val & 15;
            event.last = (tab->val >> 12) & 1;
            /* use table B-20 */
            lmax = vldTableB20_lut[event.last][event.run];
            event.level += lmax;
            event.level = getbits(ld, 1) ? -event.level : event.level;
        } else {
#pragma if_prob(.1)
            if (mode == 0x2) {
#pragma if_prob(.9)
                flushbits(ld, 2);
                tab = vld_table_b17(ld, showbits(ld, 12)); /* use table B-16 */
                if (!tab) {                              /* bad code */
                    event.run = event.level = event.last = -1;
                } else {
                    event.run = (tab->val >> 4) & 255;
                    event.level = tab->val & 15;
                    event.last = (tab->val >> 12) & 1;
                    //					rmax = vld_table_b22(event.last, event.level);
                    ///* use table B-22 */
                    rmax = vldTableB22_lut[event.last][event.level]; /* use table B-22 */
                    event.run = event.run + rmax + 1;
                    event.level = getbits(ld, 1) ? -event.level : event.level;
                }
            } else {
#pragma if_prob(.1)
                flushbits(ld, 2);
                event.last = getbits(ld, 1);
                event.run = getbits(ld, 6);    /* table B-18 */
                getbits(ld, 1);                /* marker bit */
                event.level = getbits(ld, 12); /* table B-18 */
                /* sign extend level... */
                event.level = (event.level & 0x800) ? (event.level | (-1 ^ 0xfff)) : event.level;
                getbits(ld, 1); /* marker bit */
            }
        }
    }

    return event;
}

#else

event_t vld_inter_dct(mp4_stream_t *_ld)
{
    mp4_stream_t *ld = _ld;

    event_t event;
    tab_type *tab = (tab_type *)NULL;
    int lmax, rmax;

    tab = vld_table_b17(ld, showbits(ld, 12));
    if (!tab) { /* bad code */
        event.run = event.level = event.last = -1;
        return event;
    }
    if (tab->val != ESCAPE) {
        event.run = (tab->val >> 4) & 255;
        event.level = tab->val & 15;
        event.last = (tab->val >> 12) & 1;
        event.level = getbits(ld, 1) ? -event.level : event.level;
    } else {
        /* this value is escaped - see para 7.4.1.3 */
        /* assuming short_video_header == 0 */
        int mode = showbits(ld, 2);
        switch (mode) {
        case 0x0: /* Type 1 */
        case 0x1: /* Type 1 */
        default:
            flushbits(ld, 1);
            tab = vld_table_b17(ld, showbits(ld, 12)); /* use table B-17 */
            if (!tab) {                              /* bad code */
                event.run = event.level = event.last = -1;
                return event;
            }
            event.run = (tab->val >> 4) & 255;
            event.level = tab->val & 15;
            event.last = (tab->val >> 12) & 1;
            lmax = vld_table_b20(event.last, event.run); /* use table B-20 */
            event.level += lmax;
            event.level = getbits(ld, 1) ? -event.level : event.level;
            break;
        case 0x2: /* Type 2 */
            flushbits(ld, 2);
            tab = vld_table_b17(ld, showbits(ld, 12)); /* use table B-17 */
            if (!tab) {                              /* bad code */
                event.run = event.level = event.last = -1;
                break;
            }
            event.run = (tab->val >> 4) & 255;
            event.level = tab->val & 15;
            event.last = (tab->val >> 12) & 1;
            rmax = vld_table_b22(event.last, event.level); /* use table B-22 */
            event.run = event.run + rmax + 1;
            event.level = getbits(ld, 1) ? -event.level : event.level;
            break;
        case 0x3: /* Type 3  - fixed length codes */
            flushbits(ld, 2);
            event.last = getbits(ld, 1);
            event.run = getbits(ld, 6);    /* table B-18 */
            getbits(ld, 1);                /* marker bit */
            event.level = getbits(ld, 12); /* table B-18 */
            /* sign extend level... */
            event.level = (event.level & 0x800) ? (event.level | (-1 ^ 0xfff)) : event.level;
            getbits(ld, 1); /* marker bit */
            break;
        }
    }

    return event;
}

event_t vld_inter_mq_dct(mp4_stream_t *_ld)
{
    mp4_stream_t *ld = _ld;

    event_t event;
    tab_type *tab = (tab_type *)NULL;
    //	int lmax, rmax;

    tab = vld_table_b17(ld, showbits(ld, 12));
    if (!tab) { /* bad code */
        event.run = event.level = event.last = -1;
        return event;
    }
    if (tab->val != ESCAPE) {
        event.run = (tab->val >> 4) & 255;
        event.level = tab->val & 15;
        event.last = (tab->val >> 12) & 1;
        event.level = getbits(ld, 1) ? -event.level : event.level;
    } else {
        /* this value is escaped */
        event.last = getbits(ld, 1);
        event.run = getbits(ld, 6);
        event.level = getbits(ld, 8);
        if (event.level >= 128)
            event.level = event.level - 256;

        if (event.level == -128) {
            int t;
            t = getbits(ld, 5);
            event.level = getbits(ld, 6);
            event.level <<= 26;
            event.level >>= 21;
            event.level |= t & 0x1f;
        }
    }

    return event;
}

#endif
