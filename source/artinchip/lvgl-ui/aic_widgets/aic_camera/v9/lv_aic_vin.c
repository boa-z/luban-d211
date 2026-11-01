/*
 * Copyright (c) 2023-2026, ArtInChip Technology Co., Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * LVGL VIN adapter implementation — wraps V4L2 for lv_aic_camera.
 * Supports DVP 1.0 and DVP 2.0.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <glob.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <linux/videodev2.h>
#include <linux/v4l2-subdev.h>
#include <linux/dma-buf.h>

#include "lvgl.h"
#include "lv_aic_vin.h"

/* ================================================================
 *  V4L2 / VIN adapter
 * ================================================================ */

/* DVP 1.0 default device paths */
#define DVP1_SENSOR_NAME "/dev/v4l-subdev0"
#define DVP1_VIN_NAME    "/dev/v4l-subdev1"
#define DVP1_VIDEO_NAME  "/dev/video0"

enum { DVP_VERSION_1 = 1, DVP_VERSION_2 = 2 };

static int detect_dvp_version(void)
{
    int version = DVP_VERSION_1;
    char buf[8] = {0};
    glob_t g = {0};
    FILE *fp;

    /* read from Device Tree via sysfs: compatible = "artinchip,aic-dvp-vX.Y" */
    if (glob("/sys/devices/platform/soc/*.dvp/version", 0, NULL, &g) != 0)
        return version;

    if (g.gl_pathc <= 0)
        goto exit;

    fp = fopen(g.gl_pathv[0], "r");
    if (!fp)
        goto exit;

    if (fgets(buf, sizeof(buf), fp)) {
        if (atoi(buf) >= 2)
            version = DVP_VERSION_2;
    }
    fclose(fp);

exit:
    globfree(&g);
    return version;
}

int lv_aic_vin_open(struct lv_aic_vin_dev *dev)
{
    const char *sensor, *vin_name, *video;
    int i, dvp_ver;

    if (!dev)
        return -1;

    dvp_ver = detect_dvp_version();
    dev->dvp_version = dvp_ver;
    printf("VIN: detected DVP version %d\n", dvp_ver);

    dev->sensor_fd = -1;
    dev->vin_fd = -1;
    for (i = 0; i < VIN_MAX_DEV_NUM; i++)
        dev->video_fd[i] = -1;

    if (!dev->dev_num)
        dev->dev_num = 1;

    /* Resolve paths: use dev->xxx_path if set, otherwise defaults */
    sensor = dev->sensor_name ? dev->sensor_name : DVP1_SENSOR_NAME;
    vin_name = (dvp_ver == DVP_VERSION_1) ? DVP1_VIN_NAME : dev->vin_name;
    /* Open sensor subdev */
    dev->sensor_fd = open(sensor, O_RDWR);
    if (dev->sensor_fd < 0) {
        fprintf(stderr, "VIN: open %s failed: %s\n", sensor, strerror(errno));
        return -1;
    }

    /* Open VIN controller subdev (DVP 1.0 only; DVP 2.0 optional in vin_name_fd) */
    if (vin_name) {
        dev->vin_fd = open(vin_name, O_RDWR);
        if (dev->vin_fd < 0)
            fprintf(stderr, "VIN: open %s failed: %s\n", vin_name, strerror(errno));
    }

    /* Open video capture devices */
    for (i = 0; i < dev->dev_num; i++) {
        video = dev->video_name[i] ? dev->video_name[i] : DVP1_VIDEO_NAME;
        dev->video_fd[i] = open(video, O_RDWR);
        if (dev->video_fd[i] < 0) {
            fprintf(stderr, "VIN: open %s failed: %s\n", video, strerror(errno));
            lv_aic_vin_close(dev);
            return -1;
        }
    }

    return 0;
}

void lv_aic_vin_close(struct lv_aic_vin_dev *dev)
{
    int i;

    if (!dev)
        return;

    if (dev->sensor_fd >= 0) {
        close(dev->sensor_fd);
        dev->sensor_fd = -1;
    }
    if (dev->vin_fd >= 0) {
        close(dev->vin_fd);
        dev->vin_fd = -1;
    }
    for (i = 0; i < VIN_MAX_DEV_NUM; i++) {
        if (dev->video_fd[i] >= 0) {
            close(dev->video_fd[i]);
            dev->video_fd[i] = -1;
        }
    }
}

int lv_aic_vin_get_sensor_fmt(struct lv_aic_vin_dev *dev, struct vin_fmt *fmt)
{
    struct v4l2_subdev_format f = {0};

    if (!dev || dev->sensor_fd < 0 || !fmt)
        return -1;

    f.pad = 0;
    f.which = V4L2_SUBDEV_FORMAT_ACTIVE;
    if (ioctl(dev->sensor_fd, VIDIOC_SUBDEV_G_FMT, &f) < 0) {
        perror("VIDIOC_SUBDEV_G_FMT");
        return -1;
    }

    fmt->width = f.format.width;
    fmt->height = f.format.height;
    fmt->code = f.format.code;
    fmt->colorspace = f.format.colorspace;
    fmt->bus_type = 0;
    return 0;
}

int lv_aic_vin_set_sensor_fmt(struct lv_aic_vin_dev *dev, struct vin_fmt *fmt)
{
    struct v4l2_subdev_format f = {0};

    if (!dev || dev->sensor_fd < 0 || !fmt)
        return -1;

    f.pad = 0;
    f.which = V4L2_SUBDEV_FORMAT_ACTIVE;
    f.format.width = fmt->width;
    f.format.height = fmt->height;
    f.format.code = fmt->code;

    if (ioctl(dev->sensor_fd, VIDIOC_SUBDEV_S_FMT, &f) < 0) {
        perror("VIDIOC_SUBDEV_S_FMT");
        return -1;
    }
    return 0;
}

int lv_aic_vin_set_vin_subdev_fmt(struct lv_aic_vin_dev *dev, struct vin_fmt *fmt)
{
    struct v4l2_subdev_format f = {0};

    if (!dev || dev->vin_fd < 0 || !fmt)
        return -1;

    f.pad = 0;
    f.which = V4L2_SUBDEV_FORMAT_ACTIVE;
    f.format.width = fmt->width;
    f.format.height = fmt->height;
    f.format.code = fmt->code;

    if (ioctl(dev->vin_fd, VIDIOC_SUBDEV_S_FMT, &f) < 0) {
        perror("VIDIOC_SUBDEV_S_FMT (vin ctrl)");
        return -1;
    }
    return 0;
}

int lv_aic_vin_set_out_fmt(struct lv_aic_vin_dev *dev, int dev_id, struct dvp_out_fmt *fmt)
{
    struct v4l2_format f = {0};

    if (!dev || dev_id >= dev->dev_num || dev->video_fd[dev_id] < 0 || !fmt)
        return -1;

    f.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    f.fmt.pix_mp.width = fmt->width;
    f.fmt.pix_mp.height = fmt->height;
    f.fmt.pix_mp.pixelformat = fmt->pixelformat;
    f.fmt.pix_mp.num_planes = fmt->num_planes;

    if (ioctl(dev->video_fd[dev_id], VIDIOC_S_FMT, &f) < 0) {
        perror("VIDIOC_S_FMT");
        return -1;
    }

    fmt->width = f.fmt.pix_mp.width;
    fmt->height = f.fmt.pix_mp.height;
    fmt->pixelformat = f.fmt.pix_mp.pixelformat;
    fmt->num_planes = f.fmt.pix_mp.num_planes;
    fmt->plane_fmt[0].sizeimage = f.fmt.pix_mp.plane_fmt[0].sizeimage;
    fmt->plane_fmt[0].bytesperline = f.fmt.pix_mp.plane_fmt[0].bytesperline;
    fmt->plane_fmt[1].sizeimage = f.fmt.pix_mp.plane_fmt[1].sizeimage;
    fmt->plane_fmt[1].bytesperline = f.fmt.pix_mp.plane_fmt[1].bytesperline;
    return 0;
}

int lv_aic_vin_req_buf(struct lv_aic_vin_dev *dev, int dev_id, struct lv_aic_video_data *vdata)
{
    struct v4l2_plane planes[VID_BUF_PLANE_NUM] = {0};
    struct v4l2_exportbuffer expbuf = {0};
    struct v4l2_requestbuffers req = {0};
    struct v4l2_buffer buf = {0};
    struct vin_video_buf *binfo;
    int fd = dev->video_fd[dev_id];
    unsigned int phy_addr;
    int i, j;

    if (!dev || dev_id >= dev->dev_num || fd < 0 || !vdata)
        return -1;

    req.count = vdata->num_buffers;
    req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    req.memory = V4L2_MEMORY_MMAP;

    if (ioctl(fd, VIDIOC_REQBUFS, &req) < 0) {
        perror("VIDIOC_REQBUFS");
        return -1;
    }

    for (i = 0; i < vdata->num_buffers; i++) {
        binfo = &vdata->binfo[i];
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index = i;
        buf.length = VID_BUF_PLANE_NUM;
        buf.m.planes = planes;

        if (ioctl(fd, VIDIOC_QUERYBUF, &buf) < 0) {
            perror("VIDIOC_QUERYBUF");
            lv_aic_vin_release_buf(vdata);
            return -1;
        }
        binfo->num_planes = VID_BUF_PLANE_NUM;
        for (j = 0; j < VID_BUF_PLANE_NUM; j++) {
            expbuf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
            expbuf.index = i;
            expbuf.plane = j;
            if (ioctl(fd, VIDIOC_EXPBUF, &expbuf) < 0) {
                perror("VIDIOC_EXPBUF");
                lv_aic_vin_release_buf(vdata);
                return -1;
            }

            binfo->planes[j].fd = expbuf.fd;
            binfo->planes[j].buf = 0;
            if (dev->dvp_version == DVP_VERSION_1) {
                if (ioctl(expbuf.fd, DMA_BUF_IOCTL_GET_PHY_ADDR, &phy_addr) < 0)
                    phy_addr = 0;
                binfo->planes[j].buf = phy_addr;
            }
            binfo->planes[j].len = planes[j].length;
            binfo->planes[j].handle = 0;
            binfo->planes[j].vaddr = mmap(NULL, planes[j].length, PROT_READ | PROT_WRITE,
                                          MAP_SHARED, fd, planes[j].m.mem_offset);
            if (binfo->planes[j].vaddr == MAP_FAILED) {
                fprintf(stderr, "VIN: buf %d plane %d mmap failed: %s\n", i, j, strerror(errno));
                lv_aic_vin_release_buf(vdata);
                return -1;
            }
        }
    }

    return 0;
}

void lv_aic_vin_release_buf(struct lv_aic_video_data *vdata)
{
    struct vin_video_buf *binfo = NULL;
    int i, j;

    for (i = 0; i < vdata->num_buffers; i++) {
        binfo = &vdata->binfo[i];
        for (j = 0; j < VID_BUF_PLANE_NUM; j++) {
            if (binfo->planes[j].vaddr && binfo->planes[j].len > 0) {
                munmap(binfo->planes[j].vaddr, binfo->planes[j].len);
                binfo->planes[j].vaddr = NULL;
                binfo->planes[j].len = 0;
            }
        }
    }
}

int lv_aic_vin_q_buf(struct lv_aic_vin_dev *dev, int dev_id, int index)
{
    int fd;

    if (!dev || dev_id >= dev->dev_num)
        return -1;
    fd = dev->video_fd[dev_id];
    if (fd < 0)
        return -1;

    struct v4l2_buffer buf = {0};
    struct v4l2_plane planes[VID_BUF_PLANE_NUM] = {0};

    buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    buf.memory = V4L2_MEMORY_MMAP;
    buf.index = index;
    buf.length = VID_BUF_PLANE_NUM;
    buf.m.planes = planes;

    if (ioctl(fd, VIDIOC_QBUF, &buf) < 0) {
        perror("VIDIOC_QBUF");
        return -1;
    }
    return 0;
}

int lv_aic_vin_dq_buf(struct lv_aic_vin_dev *dev, int dev_id, int *index)
{
    int fd;

    if (!dev || dev_id >= dev->dev_num || !index)
        return -1;
    fd = dev->video_fd[dev_id];
    if (fd < 0)
        return -1;

    struct v4l2_buffer buf = {0};
    struct v4l2_plane planes[VID_BUF_PLANE_NUM] = {0};

    buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    buf.memory = V4L2_MEMORY_MMAP;
    buf.length = VID_BUF_PLANE_NUM;
    buf.m.planes = planes;

    if (ioctl(fd, VIDIOC_DQBUF, &buf) < 0) {
        perror("VIDIOC_DQBUF");
        return -1;
    }

    *index = buf.index;
    return 0;
}

int lv_aic_vin_stream_on(struct lv_aic_vin_dev *dev, int dev_id)
{
    int fd;

    if (!dev || dev_id >= dev->dev_num)
        return -1;
    fd = dev->video_fd[dev_id];
    if (fd < 0)
        return -1;

    enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;

    if (ioctl(fd, VIDIOC_STREAMON, &type) < 0) {
        perror("VIDIOC_STREAMON");
        return -1;
    }
    return 0;
}

int lv_aic_vin_stream_off(struct lv_aic_vin_dev *dev, int dev_id)
{
    int fd;

    if (!dev || dev_id >= dev->dev_num)
        return -1;
    fd = dev->video_fd[dev_id];
    if (fd < 0)
        return -1;

    enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;

    if (ioctl(fd, VIDIOC_STREAMOFF, &type) < 0) {
        perror("VIDIOC_STREAMOFF");
        return -1;
    }
    return 0;
}
