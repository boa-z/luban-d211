/*
 * Copyright (C) 2020-2023 ArtInChip Technology Co., Ltd.
 *
 * Authors:  <qi.xu@artinchip.com>
 *
 *            mpp_frame
 * vedec  ---------------->   fbsink
 *
 * Usage:
 *   gst-launch-1.0 filesrc location=/sdcard/test.mp4 typefind=true ! video/quicktime ! qtdemux ! vedec ! fbsink
 */

#include <gst/gst.h>
#include <gst/video/video-info.h>
#include <gst/gstbuffer.h>
#include <linux/fb.h>
#include <video/artinchip_fb.h>
#include <sys/ioctl.h>

#include "gstfbsink.h"

#define gst_fbsink_parent_class parent_class
G_DEFINE_TYPE (Gstfbsink, gst_fbsink, GST_TYPE_VIDEO_SINK);

GST_DEBUG_CATEGORY_STATIC (gst_fbsink_debug);
#define GST_CAT_DEFAULT gst_fbsink_debug

#define VIDEO_CAPS "{ RGB, BGR, BGRx, xBGR, RGB, RGBx, xRGB, NV12, NV21, I420, YV12 }"

static GstStaticPadTemplate fbsink_template =
GST_STATIC_PAD_TEMPLATE (
	"sink",
	GST_PAD_SINK,
	GST_PAD_ALWAYS,
	GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE (VIDEO_CAPS))
);

static GstStateChangeReturn
gst_fbsink_change_state (GstElement * element,
    GstStateChange transition)
{
	GstStateChangeReturn ret = GST_STATE_CHANGE_SUCCESS;
	Gstfbsink *fbsink = GST_FBSINK (element);
	GST_DEBUG_OBJECT (fbsink, "%d -> %d",
		GST_STATE_TRANSITION_CURRENT (transition),
		GST_STATE_TRANSITION_NEXT (transition));

	switch (transition) {
	case GST_STATE_CHANGE_NULL_TO_READY:
	// open device

		break;
	case GST_STATE_CHANGE_READY_TO_PAUSED:
		break;
	case GST_STATE_CHANGE_PAUSED_TO_PLAYING:
		break;
	case GST_STATE_CHANGE_PAUSED_TO_READY:
		break;
	default:
		break;
	}

	ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

	switch (transition) {
	case GST_STATE_CHANGE_PLAYING_TO_PAUSED:
		break;
	case GST_STATE_CHANGE_PAUSED_TO_READY:
		/* Forget everything about the current stream. */
		//gst_framebuffersink_reset (framebuffersink);
		break;
	case GST_STATE_CHANGE_READY_TO_NULL:

		break;
	default:
		break;
	}
	return ret;
}

static int alloc_mpp_buf(Gstfbsink* fbsink, GstVideoInfo *info)
{
	struct mpp_buf *buf = &fbsink->buf;
	buf->size.width = info->width;
	buf->size.height = info->height;
	buf->stride[0] = info->width;

	switch (info->finfo->format) {
	case GST_VIDEO_FORMAT_I420:
		buf->format = MPP_FMT_YUV420P;
		break;
	case GST_VIDEO_FORMAT_YV12:
		buf->format = MPP_FMT_YUV420P;
		break;
	case GST_VIDEO_FORMAT_NV12:
		buf->format = MPP_FMT_NV12;
		break;
	case GST_VIDEO_FORMAT_NV21:
		buf->format = MPP_FMT_NV21;
		break;
	default:
		GST_ERROR_OBJECT(fbsink, "unkown format: %d, %s",
			info->finfo->format, info->finfo->name);
		break;
	}

	mpp_buf_alloc(fbsink->dmabuf_fd, buf);
	fbsink->alloc_flag = 1;

	return 0;
}

/*
 * the memory in GstBuffer is struct mpp_frame
 */
static GstFlowReturn gst_fbsink_show_frame (GstBaseSink * bsink, GstBuffer * buffer)
{
	Gstfbsink* fbsink = GST_FBSINK(bsink);
	GstMemory *mem = NULL;
	GstVideoInfo info;
	GstVideoFrame frame;
	GstMapInfo mapinfo;
	struct mpp_frame mframe;
	int i=0;

	mem = gst_buffer_get_memory(buffer, 0);

	if (mem->size == sizeof(struct mpp_frame)) {
		if(!gst_memory_map(mem, &mapinfo, GST_MAP_READ)) {
			GST_ERROR_OBJECT(fbsink, "memory_map failed");
			return GST_FLOW_ERROR;
		}

		// physic memory
		memcpy(&mframe, mapinfo.data, sizeof(struct mpp_frame));
		GST_DEBUG_OBJECT(fbsink, "mpp_frame id: %d, w: %d, h: %d", mframe.id,
			mframe.buf.size.width, mframe.buf.size.height);
		gst_aicfb_render(fbsink->fb_fd, &(mframe.buf));

		// send event to vedec, return this mpp_frame
		GstStructure *s = gst_structure_new ("return-frame", "id", G_TYPE_INT, mframe.id, NULL);
		GstEvent* event = gst_event_new_custom (GST_EVENT_CUSTOM_UPSTREAM, s);
		gst_pad_push_event(bsink->sinkpad, event);
	} else {
		GstCaps *caps = gst_pad_get_current_caps (GST_VIDEO_SINK_PAD (fbsink));
		gst_video_info_from_caps (&info, caps);

		gst_video_frame_map (&frame, &info, buffer, GST_MAP_READ);
		GST_DEBUG_OBJECT(fbsink, "frame width: %d, height: %d, format: %d, name: %s",
			info.width, info.height, info.finfo->format, info.finfo->name);

		if(!fbsink->alloc_flag) {
			alloc_mpp_buf(fbsink, &info);
		}

		int datasize[3] = {info.width * info.height, info.width * info.height/4, info.width * info.height/4};
		unsigned char* data[3] = {frame.data[i], frame.data[i]+ datasize[0], frame.data[i]+ datasize[0]*5/4};
		int comp = 1;
		if(info.finfo->format == GST_VIDEO_FORMAT_I420 || info.finfo->format == GST_VIDEO_FORMAT_YV12) {
			comp = 3;
		} else if(info.finfo->format == GST_VIDEO_FORMAT_NV12 || info.finfo->format == GST_VIDEO_FORMAT_NV21) {
			comp = 2;
			datasize[1] = datasize[0];
		}

		for (i=0; i<comp; i++) {
			unsigned char* vaddr = dmabuf_mmap(fbsink->buf.fd[i], datasize[i]);
			memcpy(vaddr, data[i], datasize[i]);
			dmabuf_munmap(vaddr, datasize[i]);
			dmabuf_sync(fbsink->buf.fd[i], CACHE_CLEAN);
		}

		gst_aicfb_render(fbsink->fb_fd, &fbsink->buf);
	}

	gst_memory_unref(mem);
	return 0;
}

static gboolean gst_framebuffersink_start (GstBaseSink *sink)
{
	Gstfbsink *fbsink = GST_FBSINK(sink);
	fbsink->fb_fd = gst_aicfb_open();
	fbsink->dmabuf_fd = dmabuf_device_open();

	//gst_get_screeninfo(fbsink->fb_fd, &fbsink->screen_width, &fbsink->screen_height);

	return TRUE;
}

static gboolean gst_framebuffersink_stop (GstBaseSink *sink)
{
	Gstfbsink *fbsink = GST_FBSINK(sink);

	if(fbsink->fb_fd)
		gst_aicfb_close(fbsink->fb_fd);
	if(fbsink->dmabuf_fd)
		dmabuf_device_close(fbsink->dmabuf_fd);

	return TRUE;
}

static void gst_fbsink_finalize (Gstfbsink * fbsink)
{
	if(fbsink->alloc_flag) {
		mpp_buf_free(&fbsink->buf);
	}
	G_OBJECT_CLASS (parent_class)->finalize ((GObject *) (fbsink));
}

/* initialize the fbsink's class */
static void gst_fbsink_class_init (GstfbsinkClass * klass)
{
	GObjectClass *gobject_class;
	GstElementClass *element_class;
	GstBaseSinkClass *basesink_class;

	gobject_class = (GObjectClass *) klass;
	element_class = (GstElementClass *) klass;
	basesink_class = GST_BASE_SINK_CLASS (klass);

	gobject_class->finalize = (GObjectFinalizeFunc) gst_fbsink_finalize;
	//gobject_class->set_property = gst_fbsink_set_property;
	//gobject_class->get_property = gst_fbsink_get_property;

	gst_element_class_add_pad_template (element_class,
		gst_static_pad_template_get (&fbsink_template));

	element_class->change_state = GST_DEBUG_FUNCPTR (
		gst_fbsink_change_state);
	basesink_class->render = GST_DEBUG_FUNCPTR (gst_fbsink_show_frame);
	basesink_class->start = GST_DEBUG_FUNCPTR (gst_framebuffersink_start);
	basesink_class->stop = GST_DEBUG_FUNCPTR (gst_framebuffersink_stop);

	GST_DEBUG_CATEGORY_INIT (gst_fbsink_debug, "fbsink", 0, "ArtInChip fbsink");

	gst_element_class_set_static_metadata (element_class,
		"ArtInChip FrameBuffer Sink", "Sink/Video",
		"Displays frames on ArtInChip DE device",
		"<qi.xu@artinchip.com>");
}

/* initialize the new element
 * initialize instance structure
 */
static void gst_fbsink_init (Gstfbsink *fbsink)
{

}
