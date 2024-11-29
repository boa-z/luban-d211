/*
 * Copyright (C) 2020-2023 ArtInChip Technology Co., Ltd.
 *
 * Authors:  <qi.xu@artinchip.com>
 */

#ifndef __GST_FBSINK_H__
#define __GST_FBSINK_H__

#include <gst/gst.h>
#include <gst/video/gstvideosink.h>

#include "gstaicfb.h"
#include "dma_allocator.h"

G_BEGIN_DECLS

#define GST_TYPE_FBSINK \
  (gst_fbsink_get_type())
#define GST_FBSINK(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_FBSINK,Gstfbsink))
#define GST_FBSINK_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_FBSINK,GstfbsinkClass))
#define GST_IS_FBSINK(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_FBSINK))
#define GST_IS_FBSINK_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_FBSINK))

typedef struct _Gstfbsink      Gstfbsink;
typedef struct _GstfbsinkClass GstfbsinkClass;

struct _Gstfbsink {
	GstVideoSink videosink;

	int fb_fd;
	int dmabuf_fd;
	struct mpp_buf buf;
	int alloc_flag;

	int screen_width;
	int screen_height;
};

struct _GstfbsinkClass {
	GstVideoSinkClass parent_class;
};

GType gst_fbsink_get_type (void);

G_END_DECLS

#endif /* __GST_FBSINK_H__ */
