/*
 * Copyright (C) 2020-2023 ArtInChip Technology Co., Ltd.
 *
 * Authors:  <qi.xu@artinchip.com>
 */

#include <video/artinchip_fb.h>

int gst_aicfb_open();
void gst_aicfb_close(int fd);
int gst_aicfb_render(int fd, struct mpp_buf* buf);
int gst_get_screeninfo(int fd, int *width, int *height);
