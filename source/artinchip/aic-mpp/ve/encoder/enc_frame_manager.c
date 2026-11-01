/*
 * Copyright (C) 2020-2026 ArtInChip Technology Co. Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 *  author: <qi.xu@artinchip.com>
 *  Desc:  encoder frame buffer manager
 */

#define LOG_TAG "enc_frame_manager"

#include <string.h>
#include <pthread.h>

#include "mpp_mem.h"
#include "mpp_list.h"
#include "mpp_log.h"
#include "enc_frame_manager.h"

struct enc_fm_frame_node {
    struct mpp_frame frame;
    struct mpp_list list;
};

struct enc_frame_manager {
    pthread_mutex_t lock;
    struct mpp_list ready_list;
    int frame_count;

    enc_fm_release_callback release_cb;
    void *user_data;
};

struct enc_frame_manager* enc_frame_manager_create(void)
{
    struct enc_frame_manager *fm;

    logd("create encoder frame manager");

    fm = (struct enc_frame_manager *)mpp_alloc(sizeof(struct enc_frame_manager));
    if (!fm)
        return NULL;

    memset(fm, 0, sizeof(struct enc_frame_manager));

    pthread_mutex_init(&fm->lock, NULL);
    mpp_list_init(&fm->ready_list);
    fm->frame_count = 0;
    fm->release_cb = NULL;
    fm->user_data = NULL;

    logd("create encoder frame manager successful! (%p)", fm);

    return fm;
}

int enc_frame_manager_destroy(struct enc_frame_manager *fm)
{
    struct enc_fm_frame_node *node;
    struct enc_fm_frame_node *next;

    logd("destroy encoder frame manager");

    if (!fm)
        return -1;

    pthread_mutex_lock(&fm->lock);

    mpp_list_for_each_entry_safe(node, next, &fm->ready_list, list) {
        mpp_list_del(&node->list);
        mpp_free(node);
    }

    pthread_mutex_unlock(&fm->lock);
    pthread_mutex_destroy(&fm->lock);

    mpp_free(fm);

    return 0;
}

int enc_frame_buffer_put(struct enc_frame_manager *fm, struct mpp_frame* frame)
{
    struct enc_fm_frame_node *node;

    logd("encoder frame manager put frame");

    if (!fm || !frame)
        return -1;

    node = (struct enc_fm_frame_node *)mpp_alloc(sizeof(struct enc_fm_frame_node));
    if (!node) {
        loge("alloc frame node failed!");
        return -1;
    }

    memset(node, 0, sizeof(struct enc_fm_frame_node));
    memcpy(&node->frame, frame, sizeof(struct mpp_frame));

    pthread_mutex_lock(&fm->lock);
    mpp_list_add_tail(&node->list, &fm->ready_list);
    fm->frame_count++;
    pthread_mutex_unlock(&fm->lock);

    logd("encoder frame manager put frame successful, frame_count: %d", fm->frame_count);

    return 0;
}

struct mpp_frame* enc_frame_buffer_get(struct enc_frame_manager *fm)
{
    struct enc_fm_frame_node *node;
    struct mpp_frame *frame = NULL;

    logd("encoder frame manager get frame");

    if (!fm)
        return NULL;

    pthread_mutex_lock(&fm->lock);

    node = mpp_list_first_entry_or_null(&fm->ready_list, struct enc_fm_frame_node, list);
    if (!node) {
        logd("encoder frame manager get frame failed, no frame available!");
        pthread_mutex_unlock(&fm->lock);
        return NULL;
    }

    frame = &node->frame;
    mpp_list_del(&node->list);
    fm->frame_count--;

    pthread_mutex_unlock(&fm->lock);

    logd("encoder frame manager get frame successful, frame_count: %d", fm->frame_count);

    return frame;
}

int enc_frame_buffer_return(struct enc_frame_manager *fm, struct mpp_frame* frame)
{
    struct enc_fm_frame_node *node;

    logd("encoder frame manager return frame");

    if (!fm || !frame)
        return -1;

    if (fm->release_cb) {
        fm->release_cb(frame, fm->user_data);
    }

    node = (struct enc_fm_frame_node *)frame;
    mpp_free(node);

    return 0;
}

void enc_frame_set_release_callback(struct enc_frame_manager *fm,
    enc_fm_release_callback callback, void* user_data)
{
    logd("encoder frame manager set release callback");

    if (!fm)
        return;

    fm->release_cb = callback;
    fm->user_data = user_data;
}
