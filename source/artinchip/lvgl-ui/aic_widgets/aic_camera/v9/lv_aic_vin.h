/*
 * Copyright (c) 2023-2026, ArtInChip Technology Co., Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * LVGL VIN (Video Input) adapter for Linux — wraps V4L2 for lv_aic_camera.
 * Supports DVP 1.0 and DVP 2.0.
 */

#ifndef _LV_AIC_VIN_H_
#define _LV_AIC_VIN_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define VID_BUF_NUM       6
#define VID_BUF_PLANE_NUM 2
#define VIN_MAX_PLANE_NUM 2
#define VIN_MAX_DEV_NUM   4
#define VIN_PATH_LEN      64

struct vin_fmt {
    uint32_t width;
    uint32_t height;
    uint32_t code;
    uint32_t bus_type;
    uint32_t colorspace;
};

struct dvp_plane_pix_format {
    uint32_t sizeimage;
    uint32_t bytesperline;
};

struct dvp_out_fmt {
    uint32_t width;
    uint32_t height;
    uint32_t pixelformat;
    uint32_t num_planes;
    struct dvp_plane_pix_format plane_fmt[VIN_MAX_PLANE_NUM];
};

struct vin_video_plane {
    int fd;       /* DVP 2.0: dma-buf fd */
    uint32_t buf; /* DVP 1.0: physical address */
    uint32_t len;
    uint32_t handle; /* DVP 2.0: DRM handle */
    char *vaddr;
};

struct vin_video_buf {
    uint32_t len;
    uint32_t offset;
    uint32_t id;
    uint32_t num_planes;
    struct vin_video_plane planes[VIN_MAX_PLANE_NUM];
};

struct lv_aic_video_data {
    uint32_t num_buffers;
    struct vin_video_buf binfo[VID_BUF_NUM + 1];
};

struct lv_aic_vin_dev {
    int vin_type;
    int dev_num;                   /* number of video devices */
    int sensor_fd;                 /* sensor subdev */
    int vin_fd;                    /* VIN controller subdev (DVP/CSI/DSI) */
    int video_fd[VIN_MAX_DEV_NUM]; /* video capture devices */

    int dvp_version;               /* DVP_VERSION_1 or DVP_VERSION_2 */

    /* device paths — set before lv_aic_vin_open(), or leave NULL for DVP 1.0 defaults */
    const char *sensor_name;
    const char *vin_name;
    const char *video_name[VIN_MAX_DEV_NUM];
};

/**********************
 * GLOBAL PROTOTYPES
 **********************/

int lv_aic_vin_open(struct lv_aic_vin_dev *dev);
void lv_aic_vin_close(struct lv_aic_vin_dev *dev);

int lv_aic_vin_get_sensor_fmt(struct lv_aic_vin_dev *dev, struct vin_fmt *fmt);
int lv_aic_vin_set_sensor_fmt(struct lv_aic_vin_dev *dev, struct vin_fmt *fmt);
int lv_aic_vin_set_vin_subdev_fmt(struct lv_aic_vin_dev *dev, struct vin_fmt *fmt);
int lv_aic_vin_set_out_fmt(struct lv_aic_vin_dev *dev, int dev_id, struct dvp_out_fmt *fmt);
int lv_aic_vin_req_buf(struct lv_aic_vin_dev *dev, int dev_id, struct lv_aic_video_data *vdata);
void lv_aic_vin_release_buf(struct lv_aic_video_data *vdata);
int lv_aic_vin_q_buf(struct lv_aic_vin_dev *dev, int dev_id, int index);
int lv_aic_vin_dq_buf(struct lv_aic_vin_dev *dev, int dev_id, int *index);
int lv_aic_vin_stream_on(struct lv_aic_vin_dev *dev, int dev_id);
int lv_aic_vin_stream_off(struct lv_aic_vin_dev *dev, int dev_id);

#ifdef __cplusplus
}
#endif

#endif /* _LV_AIC_VIN_H_ */
