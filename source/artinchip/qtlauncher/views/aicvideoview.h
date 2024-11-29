/*
 * Copyright (C) 2024 ArtInChip Technology Co. Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef AICVIDEOVIEW_H
#define AICVIDEOVIEW_H

#include <QWidget>
#include <QLabel>
#include <QDebug>
#include <QPainter>
#include <QPaintEvent>

#include "../video/aicvideothread.h"
#include "../video/aicimagedecoder.h"

#include <linux/fb.h>
#include <video/artinchip_fb.h>

#ifdef QTLAUNCHER_GE_SUPPORT
#include <video/mpp_types.h>
#include <linux/dma-heap.h>
#include <dma_allocator.h>
#include <frame_allocator.h>
#include <mpp_decoder.h>
#include <mpp_ge.h>
#endif

class AiCVideoView : public QWidget
{
    Q_OBJECT

public:
    AiCVideoView(QSize size, QWidget *parent = NULL);
    ~AiCVideoView();

    void videoStop(void);

private:
    void initView(int width, int height);

#ifdef QTLAUNCHER_GE_SUPPORT
public:
    void geBgFill();
    void geBtnBlt();

protected:
    virtual void paintEvent(QPaintEvent *event);
    virtual bool eventFilter(QObject *obj, QEvent *event);

private:
    int mScreenW;
    int mScreenH;
    int mFbStride;
    mpp_pixel_format mFbFormat;
    unsigned int mFbPhy;

    QLabel *mPlayLabel;

    AiCVideoThread *mVideoThread;
    AiCImageDecoder *mImageDecoder;
#endif
};

#endif // AICVIDEOVIEW_H
