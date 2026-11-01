/*
 * Copyright (c) 2026, ArtInChip Technology Co., Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Authors:  zequan liang <zequan.liang@artinchip.com>
 */

#include "vg_drm.h"

#include <string.h>
#include <stdio.h>
#include <inttypes.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdint.h>
#include <sys/mman.h>
#include <unistd.h>
#include <sys/ioctl.h>

/**********************
 *  STATIC PROTOTYPES
 **********************/
static uint32_t get_plane_property_id(drm_dev_t * drm_dev, const char * name);
static uint32_t get_crtc_property_id(drm_dev_t * drm_dev, const char * name);
static uint32_t get_conn_property_id(drm_dev_t * drm_dev, const char * name);
static void page_flip_handler(int fd, unsigned int sequence, unsigned int tv_sec,
                              unsigned int tv_usec, void * user_data);
static int drm_get_plane_props(drm_dev_t * drm_dev);
static int drm_get_crtc_props(drm_dev_t * drm_dev);
static int drm_get_conn_props(drm_dev_t * drm_dev);
static int drm_add_plane_property(drm_dev_t * drm_dev, const char * name, uint64_t value);
static int drm_add_crtc_property(drm_dev_t * drm_dev, const char * name, uint64_t value);
static int drm_add_conn_property(drm_dev_t * drm_dev, const char * name, uint64_t value);
static int drm_atomic_set_plane(void * user_data, drm_dev_t * drm_dev, drm_buffer_t * buf);
static int find_plane(drm_dev_t * drm_dev, unsigned int fourcc, uint32_t * plane_id,
                      uint32_t crtc_id, uint32_t crtc_idx);
static int drm_find_connector(drm_dev_t * drm_dev, int64_t connector_id);
static int drm_open(const char * path);
static int drm_close(int fd);
static int drm_setup(drm_dev_t * drm_dev, const char * device_path,
                     int64_t connector_id, unsigned int fourcc);
static int drm_allocate_dumb(drm_dev_t * drm_dev, drm_buffer_t * buf);
static int drm_setup_buffers(drm_dev_t * drm_dev);

int drm_device_open(drm_dev_t * drm_dev, const char * file, int64_t connector_id)
{
    int ret;

    ret = drm_setup(drm_dev, file, connector_id, DRM_FORMAT_ARGB8888);
    if (ret) {
        close(drm_dev->fd);
        drm_dev->fd = -1;
        return -1;
    }

    ret = drm_setup_buffers(drm_dev);
    if (ret) {
        VG_LOG_ERROR("DRM buffer allocation failed");
        close(drm_dev->fd);
        drm_dev->fd = -1;
        return -1;
    }

    return 0;
}

int drm_device_close(drm_dev_t * drm_dev)
{
    if (drm_dev->fd >= 0) {
        if (drm_dev->plane) {
            drmModeFreePlane(drm_dev->plane);
            drm_dev->plane = NULL;
        }

        if (drm_dev->crtc) {
            drmModeFreeCrtc(drm_dev->crtc);
            drm_dev->crtc = NULL;
        }

        if (drm_dev->conn) {
            drmModeFreeConnector(drm_dev->conn);
            drm_dev->conn = NULL;
        }

        if (drm_dev->saved_crtc) {
            drmModeFreeCrtc(drm_dev->saved_crtc);
            drm_dev->saved_crtc = NULL;
        }

        /* 释放缓冲区资源 */
        for (int i = 0; i < 2; i++) {
            if (drm_dev->drm_bufs[i].map != NULL) {
                munmap(drm_dev->drm_bufs[i].map, drm_dev->drm_bufs[i].size);
                drm_dev->drm_bufs[i].map = NULL;
            }

            if (drm_dev->drm_bufs[i].id > 0) {
                drmModeRmFB(drm_dev->fd, drm_dev->drm_bufs[i].id);
                drm_dev->drm_bufs[i].id = 0;
            }

            if (drm_dev->drm_bufs[i].handle > 0) {
                struct drm_mode_destroy_dumb dreq;
                memset(&dreq, 0, sizeof(dreq));
                dreq.handle = drm_dev->drm_bufs[i].handle;
                drmIoctl(drm_dev->fd, DRM_IOCTL_MODE_DESTROY_DUMB, &dreq);
                drm_dev->drm_bufs[i].handle = 0;
            }

            if (drm_dev->drm_bufs[i].fd > 0) {
                close(drm_dev->drm_bufs[i].fd);
                drm_dev->drm_bufs[i].fd = -1;
            }
        }

        drm_close(drm_dev->fd);
        drm_dev->fd = -1;
    }

    return 0;
}

int drm_wait_vsync(drm_dev_t * drm_dev)
{
    struct pollfd pfd;
    pfd.fd = drm_dev->fd;
    pfd.events = POLLIN;

    while (drm_dev->req) {
        int ret;
        do {
            ret = poll(&pfd, 1, -1);
        } while (ret == -1 && errno == EINTR);

        if (ret > 0) {
            drmHandleEvent(drm_dev->fd, &drm_dev->drm_event_ctx);
        } else {
            VG_LOG_ERROR("aic display thread poll failed: %s", strerror(errno));
            return -1;
        }
    }

    return 0;
}

int drm_buffer_flush(drm_dev_t *drm_dev, int idx)
{
    if (drm_atomic_set_plane(drm_dev, drm_dev, &drm_dev->drm_bufs[idx])) {
        VG_LOG_ERROR("Flush fail");
        return -1;
    } else {
        VG_LOG_TRACE("Flush done");
    }
    return 0;
}

int drm_buffer_to_mpp_buffer(drm_dev_t * drm_dev, int idx, struct mpp_buf *buf)
{
    if (idx < 0 || idx >= 2) {
        VG_LOG_ERROR("Invalid buffer index: %d", idx);
        return -1;
    }

    buf->buf_type = MPP_DMA_BUF_FD;
    buf->size.width = drm_dev->width;
    buf->size.height = drm_dev->height;
    buf->fd[0] = drm_dev->drm_bufs[idx].fd;
    buf->stride[0] = drm_dev->drm_bufs[idx].pitch;
    buf->format = MPP_FMT_ARGB_8888; /* Using fixed format for now, could use drm_dev->drm_bufs[idx].fourcc */

    return 0;
}

void *drm_framebuffer_get_map(drm_dev_t * drm_dev, int id)
{
    /* Return buffer based on index, default to 0 if index is invalid */
    if (id >= 0 && id < 2)
        return (void *)drm_dev->drm_bufs[id].map;
    else {
        VG_LOG_WARN("Invalid framebuffer index: %d, using buffer 0", id);
        return (void *)drm_dev->drm_bufs[0].map;
    }
}

int dmabuf_sync(int fd, int flags)
{
    struct dma_buf_sync sync = {
        .flags = flags | DMA_BUF_SYNC_RW,
    };
    return ioctl(fd, DMA_BUF_IOCTL_SYNC, &sync);
}

/**********************
 *   STATIC FUNCTIONS
 **********************/


static uint32_t get_plane_property_id(drm_dev_t * drm_dev, const char * name)
{
    uint32_t i;

    VG_LOG_TRACE("Find plane property: %s", name);

    for (i = 0; i < drm_dev->count_plane_props; ++i)
        if (!strcmp(drm_dev->plane_props[i]->name, name))
            return drm_dev->plane_props[i]->prop_id;

    VG_LOG_TRACE("Unknown plane property: %s", name);

    return 0;
}

static uint32_t get_crtc_property_id(drm_dev_t * drm_dev, const char * name)
{
    uint32_t i;

    VG_LOG_TRACE("Find crtc property: %s", name);

    for (i = 0; i < drm_dev->count_crtc_props; ++i)
        if (!strcmp(drm_dev->crtc_props[i]->name, name))
            return drm_dev->crtc_props[i]->prop_id;

    VG_LOG_TRACE("Unknown crtc property: %s", name);

    return 0;
}

static uint32_t get_conn_property_id(drm_dev_t * drm_dev, const char * name)
{
    uint32_t i;

    VG_LOG_TRACE("Find conn property: %s", name);

    for (i = 0; i < drm_dev->count_conn_props; ++i)
        if (!strcmp(drm_dev->conn_props[i]->name, name))
            return drm_dev->conn_props[i]->prop_id;

    VG_LOG_TRACE("Unknown conn property: %s", name);

    return 0;
}

static void page_flip_handler(int fd, unsigned int sequence,
                              unsigned int tv_sec, unsigned int tv_usec,
                              void * user_data)
{
    VG_LOG_TRACE("flip");

    drm_dev_t * drm_dev = user_data;

    if (drm_dev->req) {
        drmModeAtomicFree(drm_dev->req);
        drm_dev->req = NULL;
    }
}

static int drm_get_plane_props(drm_dev_t * drm_dev)
{
    uint32_t i;

    drmModeObjectPropertiesPtr props = drmModeObjectGetProperties(drm_dev->fd, drm_dev->plane_id, DRM_MODE_OBJECT_PLANE);
    if (!props) {
        VG_LOG_ERROR("drmModeObjectGetProperties failed");
        return -1;
    }
    VG_LOG_TRACE("Found %u plane props", props->count_props);
    drm_dev->count_plane_props = props->count_props;
    for (i = 0; i < props->count_props; i++) {
        drm_dev->plane_props[i] = drmModeGetProperty(drm_dev->fd, props->props[i]);
        VG_LOG_TRACE("Added plane prop %u:%s", drm_dev->plane_props[i]->prop_id, drm_dev->plane_props[i]->name);
    }
    drmModeFreeObjectProperties(props);

    return 0;
}

static int drm_get_crtc_props(drm_dev_t * drm_dev)
{
    uint32_t i;

    drmModeObjectPropertiesPtr props = drmModeObjectGetProperties(drm_dev->fd, drm_dev->crtc_id, DRM_MODE_OBJECT_CRTC);
    if (!props) {
        VG_LOG_ERROR("drmModeObjectGetProperties failed");
        return -1;
    }
    VG_LOG_TRACE("Found %u crtc props", props->count_props);
    drm_dev->count_crtc_props = props->count_props;
    for (i = 0; i < props->count_props; i++) {
        drm_dev->crtc_props[i] = drmModeGetProperty(drm_dev->fd, props->props[i]);
        VG_LOG_TRACE("Added crtc prop %u:%s", drm_dev->crtc_props[i]->prop_id, drm_dev->crtc_props[i]->name);
    }
    drmModeFreeObjectProperties(props);

    return 0;
}

static int drm_get_conn_props(drm_dev_t * drm_dev)
{
    uint32_t i;

    drmModeObjectPropertiesPtr props = drmModeObjectGetProperties(drm_dev->fd, drm_dev->conn_id, DRM_MODE_OBJECT_CONNECTOR);
    if (!props) {
        VG_LOG_ERROR("drmModeObjectGetProperties failed");
        return -1;
    }
    VG_LOG_TRACE("Found %u connector props", props->count_props);
    drm_dev->count_conn_props = props->count_props;
    for (i = 0; i < props->count_props; i++) {
        drm_dev->conn_props[i] = drmModeGetProperty(drm_dev->fd, props->props[i]);
        VG_LOG_TRACE("Added connector prop %u:%s", drm_dev->conn_props[i]->prop_id, drm_dev->conn_props[i]->name);
    }
    drmModeFreeObjectProperties(props);

    return 0;
}

static int drm_add_plane_property(drm_dev_t * drm_dev, const char * name, uint64_t value)
{
    int ret;
    uint32_t prop_id = get_plane_property_id(drm_dev, name);

    if (!prop_id) {
        VG_LOG_ERROR("Couldn't find plane prop %s", name);
        return -1;
    }

    ret = drmModeAtomicAddProperty(drm_dev->req, drm_dev->plane_id, get_plane_property_id(drm_dev, name), value);
    if (ret < 0) {
        VG_LOG_ERROR("drmModeAtomicAddProperty (%s:%" PRIu64 ") failed: %d", name, value, ret);
        return ret;
    }

    return 0;
}

static int drm_add_crtc_property(drm_dev_t * drm_dev, const char * name, uint64_t value)
{
    int ret;
    uint32_t prop_id = get_crtc_property_id(drm_dev, name);

    if (!prop_id) {
        VG_LOG_ERROR("Couldn't find crtc prop %s", name);
        return -1;
    }

    ret = drmModeAtomicAddProperty(drm_dev->req, drm_dev->crtc_id, get_crtc_property_id(drm_dev, name), value);
    if (ret < 0) {
        VG_LOG_ERROR("drmModeAtomicAddProperty (%s:%" PRIu64 ") failed: %d", name, value, ret);
        return ret;
    }

    return 0;
}

static int drm_add_conn_property(drm_dev_t * drm_dev, const char * name, uint64_t value)
{
    int ret;
    uint32_t prop_id = get_conn_property_id(drm_dev, name);

    if (!prop_id) {
        VG_LOG_ERROR("Couldn't find conn prop %s", name);
        return -1;
    }

    ret = drmModeAtomicAddProperty(drm_dev->req, drm_dev->conn_id, get_conn_property_id(drm_dev, name), value);
    if (ret < 0) {
        VG_LOG_ERROR("drmModeAtomicAddProperty (%s:%" PRIu64 ") failed: %d", name, value, ret);
        return ret;
    }

    return 0;
}

static int drm_atomic_set_plane(void *user_data, drm_dev_t * drm_dev, drm_buffer_t * buf)
{
    int ret;
    static int first = 1;
    uint32_t flags = DRM_MODE_PAGE_FLIP_EVENT | DRM_MODE_ATOMIC_NONBLOCK;

    /* Wait for previous request to complete before allocating new one */
    while (drm_dev->req) {
        usleep(1000);  /* Sleep 1ms */
    }

    drm_dev->req = drmModeAtomicAlloc();

    /* On first Atomic commit, do a modeset */
    if (first) {
        drm_add_conn_property(drm_dev, "CRTC_ID", drm_dev->crtc_id);

        drm_add_crtc_property(drm_dev, "MODE_ID", drm_dev->blob_id);
        drm_add_crtc_property(drm_dev, "ACTIVE", 1);

        flags |= DRM_MODE_ATOMIC_ALLOW_MODESET;

        first = 0;
    }

    drm_add_plane_property(drm_dev, "FB_ID", buf->id);
    drm_add_plane_property(drm_dev, "CRTC_ID", drm_dev->crtc_id);
    drm_add_plane_property(drm_dev, "SRC_X", 0);
    drm_add_plane_property(drm_dev, "SRC_Y", 0);
    drm_add_plane_property(drm_dev, "SRC_W", drm_dev->width << 16);
    drm_add_plane_property(drm_dev, "SRC_H", drm_dev->height << 16);
    drm_add_plane_property(drm_dev, "CRTC_X", 0);
    drm_add_plane_property(drm_dev, "CRTC_Y", 0);
    drm_add_plane_property(drm_dev, "CRTC_W", drm_dev->width);
    drm_add_plane_property(drm_dev, "CRTC_H", drm_dev->height);

    ret = drmModeAtomicCommit(drm_dev->fd, drm_dev->req, flags, user_data);
    if (ret) {
        VG_LOG_ERROR("drmModeAtomicCommit failed: %s (%d)", strerror(errno), errno);
        drmModeAtomicFree(drm_dev->req);
        drm_dev->req = NULL;
        return ret;
    }

    return 0;
}

static int find_plane(drm_dev_t * drm_dev, unsigned int fourcc, uint32_t * plane_id,
                      uint32_t crtc_id, uint32_t crtc_idx)
{
    drmModePlaneResPtr planes;
    drmModePlanePtr plane;
    drmModePlanePtr primary_plane = NULL;
    unsigned int i, j;
    int ret = 0;

    planes = drmModeGetPlaneResources(drm_dev->fd);
    if (!planes) {
        VG_LOG_ERROR("drmModeGetPlaneResources failed");
        return -1;
    }

    VG_LOG_TRACE("drm: found planes %u", planes->count_planes);

    for (i = 0; i < planes->count_planes; ++i) {
        plane = drmModeGetPlane(drm_dev->fd, planes->planes[i]);
        if (!plane) {
            VG_LOG_ERROR("drmModeGetPlane failed: %s", strerror(errno));
            continue;
        }

        if (plane->possible_crtcs & (1 << crtc_idx)) {
            drmModeObjectProperties *props = drmModeObjectGetProperties(drm_dev->fd, plane->plane_id, DRM_MODE_OBJECT_PLANE);
            if (!props) {
                VG_LOG_ERROR("Failed to get plane properties");
                drmModeFreePlane(plane);
                continue;
            }

            for (j = 0; j < props->count_props; j++) {
                drmModePropertyRes *prop = drmModeGetProperty(drm_dev->fd, props->props[j]);
                if (!prop)
                    continue;

                if (strcmp(prop->name, "type") == 0) {
                    if (props->prop_values[j] == DRM_PLANE_TYPE_PRIMARY) {
                        primary_plane = plane;
                        drmModeFreeProperty(prop);
                        break;
                    }
                }
                drmModeFreeProperty(prop);
            }

            drmModeFreeObjectProperties(props);

            if (primary_plane) {
                // Don't free the primary plane here
                break;
            } else {
                // Free planes that are not primary
                drmModeFreePlane(plane);
            }
        } else {
            // Free planes that don't match the CRTC
            drmModeFreePlane(plane);
        }
    }

    if (primary_plane) {
        *plane_id = primary_plane->plane_id;
        VG_LOG_TRACE("found plane %d", *plane_id);
        // We don't free primary_plane here because it will be freed later
    } else {
        VG_LOG_ERROR("not found primary plane");
        ret = -1;
    }

    drmModeFreePlaneResources(planes);

    return ret;
}

static int drm_find_connector(drm_dev_t * drm_dev, int64_t connector_id)
{
    drmModeConnector * conn = NULL;
    drmModeEncoder * enc = NULL;
    drmModeRes * res;
    int i;

    if ((res = drmModeGetResources(drm_dev->fd)) == NULL) {
        VG_LOG_ERROR("drmModeGetResources() failed");
        return -1;
    }

    if (res->count_crtcs <= 0) {
        VG_LOG_ERROR("no Crtcs");
        goto free_res;
    }

    /* find all available connectors */
    for (i = 0; i < res->count_connectors; i++) {
        conn = drmModeGetConnector(drm_dev->fd, res->connectors[i]);
        if (!conn)
            continue;

        if (connector_id >= 0 && conn->connector_id != connector_id) {
            drmModeFreeConnector(conn);
            continue;
        }

        if (conn->connection == DRM_MODE_CONNECTED)
            VG_LOG_TRACE("drm: connector %d: connected", conn->connector_id);
        else if (conn->connection == DRM_MODE_DISCONNECTED)
            VG_LOG_TRACE("drm: connector %d: disconnected", conn->connector_id);
        else if (conn->connection == DRM_MODE_UNKNOWNCONNECTION)
            VG_LOG_TRACE("drm: connector %d: unknownconnection", conn->connector_id);
        else
            VG_LOG_TRACE("drm: connector %d: unknown", conn->connector_id);

        if (conn->connection == DRM_MODE_CONNECTED && conn->count_modes > 0)
            break;

        drmModeFreeConnector(conn);
        conn = NULL;
    };

    if (!conn) {
        VG_LOG_ERROR("suitable connector not found");
        goto free_res;
    }

    drm_dev->conn_id = conn->connector_id;
    VG_LOG_TRACE("conn_id: %d", drm_dev->conn_id);
    drm_dev->mmWidth = conn->mmWidth;
    drm_dev->mmHeight = conn->mmHeight;

    memcpy(&drm_dev->mode, &conn->modes[0], sizeof(drmModeModeInfo));

    if (drmModeCreatePropertyBlob(drm_dev->fd, &drm_dev->mode, sizeof(drm_dev->mode),
                                 &drm_dev->blob_id)) {
        VG_LOG_ERROR("error creating mode blob");
        goto free_res;
    }

    drm_dev->width = conn->modes[0].hdisplay;
    drm_dev->height = conn->modes[0].vdisplay;

    for (i = 0 ; i < res->count_encoders; i++) {
        enc = drmModeGetEncoder(drm_dev->fd, res->encoders[i]);
        if (!enc)
            continue;

        VG_LOG_TRACE("enc%d enc_id %d conn enc_id %d", i, enc->encoder_id, conn->encoder_id);

        if (enc->encoder_id == conn->encoder_id)
            break;

        drmModeFreeEncoder(enc);
        enc = NULL;
    }

    if (enc) {
        drm_dev->enc_id = enc->encoder_id;
        VG_LOG_TRACE("enc_id: %d", drm_dev->enc_id);
        drm_dev->crtc_id = enc->crtc_id;
        VG_LOG_TRACE("crtc_id: %d", drm_dev->crtc_id);
        drmModeFreeEncoder(enc);
    } else {
        /* Encoder hasn't been associated yet, look it up */
        for (i = 0; i < conn->count_encoders; i++) {
            int crtc, crtc_id = -1;

            enc = drmModeGetEncoder(drm_dev->fd, conn->encoders[i]);
            if (!enc)
                continue;

            for (crtc = 0 ; crtc < res->count_crtcs; crtc++) {
                uint32_t crtc_mask = 1 << crtc;

                crtc_id = res->crtcs[crtc];

                VG_LOG_TRACE("enc_id %d crtc%d id %d mask %x possible %x",
                             enc->encoder_id, crtc, crtc_id, crtc_mask,
                             enc->possible_crtcs);

                if (enc->possible_crtcs & crtc_mask)
                    break;
            }

            if (crtc_id > 0) {
                drm_dev->enc_id = enc->encoder_id;
                VG_LOG_TRACE("enc_id: %d", drm_dev->enc_id);
                drm_dev->crtc_id = crtc_id;
                VG_LOG_TRACE("crtc_id: %d", drm_dev->crtc_id);
                break;
            }

            drmModeFreeEncoder(enc);
            enc = NULL;
        }

        if (!enc) {
            VG_LOG_ERROR("suitable encoder not found");
            goto free_res;
        }

        drmModeFreeEncoder(enc);
    }

    drm_dev->crtc_idx = UINT32_MAX;

    for (i = 0; i < res->count_crtcs; ++i) {
        if (drm_dev->crtc_id == res->crtcs[i]) {
            drm_dev->crtc_idx = i;
            break;
        }
    }

    if (drm_dev->crtc_idx == UINT32_MAX) {
        VG_LOG_ERROR("drm: CRTC not found");
        goto free_res;
    }

    VG_LOG_TRACE("crtc_idx: %d", drm_dev->crtc_idx);

    return 0;

free_res:
    drmModeFreeResources(res);

    return -1;
}

static int drm_open(const char * path)
{
    int fd, flags;
    uint64_t has_dumb;
    int ret;

    fd = open(path, O_RDWR);
    if (fd < 0) {
        VG_LOG_ERROR("cannot open \"%s\"", path);
        return -1;
    }

    /* set FD_CLOEXEC flag */
    if ((flags = fcntl(fd, F_GETFD)) < 0 ||
       fcntl(fd, F_SETFD, flags | FD_CLOEXEC) < 0) {
        VG_LOG_ERROR("fcntl FD_CLOEXEC failed");
        goto err;
    }

    /* check capability */
    ret = drmGetCap(fd, DRM_CAP_DUMB_BUFFER, &has_dumb);
    if (ret < 0 || has_dumb == 0) {
        VG_LOG_ERROR("drmGetCap DRM_CAP_DUMB_BUFFER failed or \"%s\" doesn't have dumb buffer",
                     path);
        goto err;
    }

    return fd;
err:
    close(fd);
    return -1;
}

static int drm_close(int fd)
{
    close(fd);
    return 0;
}

static int drm_setup(drm_dev_t * drm_dev, const char * device_path,
                     int64_t connector_id, unsigned int fourcc)
{
    int ret;

    drm_dev->fd = drm_open(device_path);
    if (drm_dev->fd < 0)
        return -1;

    ret = drmSetClientCap(drm_dev->fd, DRM_CLIENT_CAP_ATOMIC, 1);
    if (ret) {
        VG_LOG_ERROR("No atomic modesetting support: %s", strerror(errno));
        goto err;
    }

    ret = drm_find_connector(drm_dev, connector_id);
    if (ret) {
        VG_LOG_ERROR("available drm devices not found");
        goto err;
    }

    ret = find_plane(drm_dev, fourcc, &drm_dev->plane_id, drm_dev->crtc_id, drm_dev->crtc_idx);
    if (ret) {
        VG_LOG_ERROR("Cannot find plane");
        goto err;
    }

    drm_dev->plane = drmModeGetPlane(drm_dev->fd, drm_dev->plane_id);
    if (!drm_dev->plane) {
        VG_LOG_ERROR("Cannot get plane");
        goto err;
    }

    drm_dev->crtc = drmModeGetCrtc(drm_dev->fd, drm_dev->crtc_id);
    if (!drm_dev->crtc) {
        VG_LOG_ERROR("Cannot get crtc");
        goto err;
    }

    drm_dev->conn = drmModeGetConnector(drm_dev->fd, drm_dev->conn_id);
    if (!drm_dev->conn) {
        VG_LOG_ERROR("Cannot get connector");
        goto err;
    }

    ret = drm_get_plane_props(drm_dev);
    if (ret) {
        VG_LOG_ERROR("Cannot get plane props");
        goto err;
    }

    ret = drm_get_crtc_props(drm_dev);
    if (ret) {
        VG_LOG_ERROR("Cannot get crtc props");
        goto err;
    }

    ret = drm_get_conn_props(drm_dev);
    if (ret) {
        VG_LOG_ERROR("Cannot get connector props");
        goto err;
    }

    drm_dev->drm_event_ctx.version = DRM_EVENT_CONTEXT_VERSION;
    drm_dev->drm_event_ctx.page_flip_handler = page_flip_handler;
    drm_dev->fourcc = fourcc;

    VG_LOG_INFO("drm: Found plane_id: %u connector_id: %d crtc_id: %d",
            drm_dev->plane_id, drm_dev->conn_id, drm_dev->crtc_id);

    VG_LOG_INFO("drm: %dx%d (%dmm X% dmm) pixel format %c%c%c%c",
            drm_dev->width, drm_dev->height, drm_dev->mmWidth, drm_dev->mmHeight,
            (fourcc >> 0) & 0xff, (fourcc >> 8) & 0xff, (fourcc >> 16) & 0xff, (fourcc >> 24) & 0xff);

    return 0;

err:
    close(drm_dev->fd);
    return -1;
}

static int drm_allocate_dumb(drm_dev_t * drm_dev, drm_buffer_t * buf)
{
    struct drm_mode_create_dumb creq;
    struct drm_mode_map_dumb mreq;
    uint32_t handles[4] = {0}, pitches[4] = {0}, offsets[4] = {0};
    int ret;

    /* create dumb buffer */
    memset(&creq, 0, sizeof(creq));
    creq.width = drm_dev->width;
    creq.height = drm_dev->height;
    creq.bpp = 32;
    ret = drmIoctl(drm_dev->fd, DRM_IOCTL_MODE_CREATE_DUMB, &creq);
    if (ret < 0) {
        VG_LOG_ERROR("DRM_IOCTL_MODE_CREATE_DUMB fail");
        return -1;
    }

    buf->handle = creq.handle;
    buf->pitch = creq.pitch;
    buf->size = creq.size;
    buf->fourcc = drm_dev->fourcc; /* Copy pixel format from drm_dev */

    /* prepare buffer for memory mapping */
    memset(&mreq, 0, sizeof(mreq));
    mreq.handle = creq.handle;
    ret = drmIoctl(drm_dev->fd, DRM_IOCTL_MODE_MAP_DUMB, &mreq);
    if (ret) {
        VG_LOG_ERROR("DRM_IOCTL_MODE_MAP_DUMB fail");
        return -1;
    }

    buf->offset = mreq.offset;
    VG_LOG_TRACE("dump buf size %lu pitch %u offset %u",
            buf->size, buf->pitch, buf->offset);

    /* perform actual memory mapping */
    buf->map = mmap(0, creq.size, PROT_READ | PROT_WRITE, MAP_SHARED, drm_dev->fd, mreq.offset);
    if (buf->map == MAP_FAILED) {
        struct drm_mode_destroy_dumb dreq;
        VG_LOG_ERROR("mmap fail");
        /* Clean up dumb buffer and framebuffer resources on mmap failure */
        if (buf->id > 0) {
            drmModeRmFB(drm_dev->fd, buf->id);
            buf->id = 0;
        }
        memset(&dreq, 0, sizeof(dreq));
        dreq.handle = buf->handle;
        drmIoctl(drm_dev->fd, DRM_IOCTL_MODE_DESTROY_DUMB, &dreq);
        buf->handle = 0;
        return -1;
    }

    /* clear the framebuffer to 0 (= full transparency in ARGB8888) */
    memset(buf->map, 0, creq.size);

    /* create framebuffer object for the dumb-buffer */
    handles[0] = creq.handle;
    pitches[0] = creq.pitch;
    offsets[0] = 0;
    ret = drmModeAddFB2(drm_dev->fd, drm_dev->width, drm_dev->height, drm_dev->fourcc,
                        handles, pitches, offsets, &buf->id, 0);
    if (ret) {
        VG_LOG_ERROR("drmModeAddFB fail");
        return -1;
    }

    ret = drmPrimeHandleToFD(drm_dev->fd, buf->handle, 0, &buf->fd);
    if (ret) {
        VG_LOG_ERROR("drmPrimeHandleToFD fail");
        return -1;
    }

    /* Flush CPU cache to make data visible to hardware */
    dmabuf_sync(buf->fd, DMA_BUF_SYNC_END);

    return 0;
}

static int drm_setup_buffers(drm_dev_t * drm_dev)
{
    int ret;

    /*Allocate DUMB buffers*/
    ret = drm_allocate_dumb(drm_dev, &drm_dev->drm_bufs[0]);
    if (ret)
        return ret;

    ret = drm_allocate_dumb(drm_dev, &drm_dev->drm_bufs[1]);
    if (ret)
        return ret;

    return 0;
}


