// SPDX-License-Identifier: Apache-2.0
/*
 * Copyright (C) 2025 ArtInChip Technology Co., Ltd.
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Authors:  Matteo <duanmt@artinchip.com>
 */

#include <artinchip/sample_base.h>

#include "mediactl.h"
#include "v4l2subdev.h"

#include "test_vin.h"

struct flag_name {
	__u32 flag;
	char *name;
};

#define pr_dbg(string, args...) \
		do { \
			if (g_verbose) { \
				printf(string, ##args); \
			} \
		} while(0)

static void print_flags(const struct flag_name *flag_names, unsigned int num_entries, __u32 flags)
{
	bool first = true;
	unsigned int i;

	for (i = 0; i < num_entries; i++) {
		if (!(flags & flag_names[i].flag))
			continue;
		if (!first)
			pr_dbg(", ");
		pr_dbg("%s", flag_names[i].name);
		flags &= ~flag_names[i].flag;
		first = false;
	}

	if (flags) {
		if (!first)
			pr_dbg(", ");
		pr_dbg("0x%x", flags);
	}
}

static void v4l2_subdev_print_routes(struct media_entity *entity,
				     struct v4l2_subdev_route *routes,
				     unsigned int num_routes)
{
	unsigned int i;

	if (num_routes)
		pr_dbg("\troutes:\n");

	for (i = 0; i < num_routes; i++) {
		const struct v4l2_subdev_route *route = &routes[i];

		pr_dbg("\t\t%u/%u -> %u/%u [%s]\n",
		       route->sink_pad, route->sink_stream,
		       route->source_pad, route->source_stream,
		       route->flags & V4L2_SUBDEV_ROUTE_FL_ACTIVE ? "ACTIVE" : "INACTIVE");
	}
}

static void v4l2_subdev_print_format(struct media_entity *entity,
	unsigned int pad, unsigned int stream,
	enum v4l2_subdev_format_whence which)
{
	struct v4l2_mbus_framefmt format;
	struct v4l2_fract interval = { 0, 0 };
	struct v4l2_rect rect;
	int ret;

	ret = v4l2_subdev_get_format(entity, &format, pad, stream, which);
	if (ret != 0)
		return;

	ret = v4l2_subdev_get_frame_interval(entity, &interval, pad, stream,
					     which);
	if (ret != 0 && ret != -ENOTTY && ret != -EINVAL)
		return;

	pr_dbg("\t\t[stream:%u fmt:%s/%ux%u", stream,
	       v4l2_subdev_pixelcode_to_string(format.code),
	       format.width, format.height);

	if (interval.numerator || interval.denominator)
		pr_dbg("@%u/%u", interval.numerator, interval.denominator);

	if (format.field)
		pr_dbg(" field:%s", v4l2_subdev_field_to_string(format.field));

	if (format.colorspace) {
		pr_dbg(" colorspace:%s",
		       v4l2_subdev_colorspace_to_string(format.colorspace));

		if (format.xfer_func)
			pr_dbg(" xfer:%s",
			       v4l2_subdev_xfer_func_to_string(format.xfer_func));

		if (format.ycbcr_enc)
			pr_dbg(" ycbcr:%s",
			       v4l2_subdev_ycbcr_encoding_to_string(format.ycbcr_enc));

		if (format.quantization)
			pr_dbg(" quantization:%s",
			       v4l2_subdev_quantization_to_string(format.quantization));
	}

	ret = v4l2_subdev_get_selection(entity, &rect, pad, stream,
					V4L2_SEL_TGT_CROP_BOUNDS,
					which);
	if (ret == 0)
		pr_dbg("\n\t\t crop.bounds:(%u,%u)/%ux%u", rect.left, rect.top,
		       rect.width, rect.height);

	ret = v4l2_subdev_get_selection(entity, &rect, pad, stream,
					V4L2_SEL_TGT_CROP,
					which);
	if (ret == 0)
		pr_dbg("\n\t\t crop:(%u,%u)/%ux%u", rect.left, rect.top,
		       rect.width, rect.height);

	ret = v4l2_subdev_get_selection(entity, &rect, pad, stream,
					V4L2_SEL_TGT_COMPOSE_BOUNDS,
					which);
	if (ret == 0)
		pr_dbg("\n\t\t compose.bounds:(%u,%u)/%ux%u",
		       rect.left, rect.top, rect.width, rect.height);

	ret = v4l2_subdev_get_selection(entity, &rect, pad, stream,
					V4L2_SEL_TGT_COMPOSE,
					which);
	if (ret == 0)
		pr_dbg("\n\t\t compose:(%u,%u)/%ux%u",
		       rect.left, rect.top, rect.width, rect.height);

	pr_dbg("]\n");
}

static const char *v4l2_dv_type_to_string(unsigned int type)
{
	static const struct {
		__u32 type;
		const char *name;
	} types[] = {
		{ V4L2_DV_BT_656_1120, "BT.656/1120" },
	};

	static char unknown[21] = "";
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(types); i++) {
		if (types[i].type == type)
			return types[i].name;
	}

	snprintf(unknown, 21, "Unknown (%u)", type);
	return unknown;
}

static const struct flag_name bt_standards[] = {
	{ V4L2_DV_BT_STD_CEA861, "CEA-861" },
	{ V4L2_DV_BT_STD_DMT, "DMT" },
	{ V4L2_DV_BT_STD_CVT, "CVT" },
	{ V4L2_DV_BT_STD_GTF, "GTF" },
	{ V4L2_DV_BT_STD_SDI, "SDI" },
};

static const struct flag_name bt_capabilities[] = {
	{ V4L2_DV_BT_CAP_INTERLACED, "interlaced" },
	{ V4L2_DV_BT_CAP_PROGRESSIVE, "progressive" },
	{ V4L2_DV_BT_CAP_REDUCED_BLANKING, "reduced-blanking" },
	{ V4L2_DV_BT_CAP_CUSTOM, "custom" },
};

static const struct flag_name bt_flags[] = {
	{ V4L2_DV_FL_REDUCED_BLANKING, "reduced-blanking" },
	{ V4L2_DV_FL_CAN_REDUCE_FPS, "can-reduce-fps" },
	{ V4L2_DV_FL_REDUCED_FPS, "reduced-fps" },
	{ V4L2_DV_FL_HALF_LINE, "half-line" },
	{ V4L2_DV_FL_IS_CE_VIDEO, "CE-video" },
	{ V4L2_DV_FL_FIRST_FIELD_EXTRA_LINE, "first-field-extra-line" },
	{ V4L2_DV_FL_HAS_PICTURE_ASPECT, "has-picture-aspect" },
	{ V4L2_DV_FL_HAS_CEA861_VIC, "has-cea861-vic" },
	{ V4L2_DV_FL_HAS_HDMI_VIC, "has-hdmi-vic" },
	{ V4L2_DV_FL_CAN_DETECT_REDUCED_FPS, "can-detect-reduced-fps" },
};

static void v4l2_subdev_print_dv_timings(const struct v4l2_dv_timings *timings,
					 const char *name)
{
	pr_dbg("\t\t[dv.%s:%s", name, v4l2_dv_type_to_string(timings->type));

	switch (timings->type) {
	case V4L2_DV_BT_656_1120: {
		const struct v4l2_bt_timings *bt = &timings->bt;
		unsigned int htotal, vtotal;

		htotal = V4L2_DV_BT_FRAME_WIDTH(bt);
		vtotal = V4L2_DV_BT_FRAME_HEIGHT(bt);

		pr_dbg(" %ux%u%s%llu (%ux%u)",
		       bt->width, bt->height, bt->interlaced ? "i" : "p",
		       (htotal * vtotal) > 0 ? (bt->pixelclock / (htotal * vtotal)) : 0ULL,
		       htotal, vtotal);

		pr_dbg(" stds:");
		print_flags(bt_standards, ARRAY_SIZE(bt_standards),
			    bt->standards);
		pr_dbg(" flags:");
		print_flags(bt_flags, ARRAY_SIZE(bt_flags),
			    bt->flags);

		break;
	}
	}

	pr_dbg("]\n");
}

static void v4l2_subdev_print_pad_dv(struct media_entity *entity,
	unsigned int pad, enum v4l2_subdev_format_whence which)
{
	struct v4l2_dv_timings_cap caps;
	int ret;

	caps.pad = pad;
	ret = v4l2_subdev_get_dv_timings_caps(entity, &caps);
	if (ret != 0)
		return;

	pr_dbg("\t\t[dv.caps:%s", v4l2_dv_type_to_string(caps.type));

	switch (caps.type) {
	case V4L2_DV_BT_656_1120:
		pr_dbg(" min:%ux%u@%llu max:%ux%u@%llu",
		       caps.bt.min_width, caps.bt.min_height, caps.bt.min_pixelclock,
		       caps.bt.max_width, caps.bt.max_height, caps.bt.max_pixelclock);

		pr_dbg(" stds:");
		print_flags(bt_standards, ARRAY_SIZE(bt_standards),
			    caps.bt.standards);
		pr_dbg(" caps:");
		print_flags(bt_capabilities, ARRAY_SIZE(bt_capabilities),
			    caps.bt.capabilities);

		break;
	}

	pr_dbg("]\n");
}

static void v4l2_subdev_print_subdev_dv(struct media_entity *entity)
{
	struct v4l2_dv_timings timings;
	int ret;

	ret = v4l2_subdev_query_dv_timings(entity, &timings);
	switch (ret) {
	case -ENOLINK:
		pr_dbg("\t\t[dv.query:no-link]\n");
		break;
	case -ENOLCK:
		pr_dbg("\t\t[dv.query:no-lock]\n");
		break;
	case -ERANGE:
		pr_dbg("\t\t[dv.query:out-of-range]\n");
		break;
	case 0:
		v4l2_subdev_print_dv_timings(&timings, "detect");
		break;
	default:
		return;
	}

	ret = v4l2_subdev_get_dv_timings(entity, &timings);
	if (ret == 0)
		v4l2_subdev_print_dv_timings(&timings, "current");
}

static const char *media_entity_type_to_string(unsigned type)
{
	static const struct {
		__u32 type;
		const char *name;
	} types[] = {
		{ MEDIA_ENT_T_DEVNODE, "Node" },
		{ MEDIA_ENT_T_V4L2_SUBDEV, "V4L2 subdev" },
	};

	unsigned int i;

	type &= MEDIA_ENT_TYPE_MASK;

	for (i = 0; i < ARRAY_SIZE(types); i++) {
		if (types[i].type == type)
			return types[i].name;
	}

	return "Unknown";
}

static const char *media_entity_subtype_to_string(unsigned type)
{
	static const char *node_types[] = {
		"Unknown",
		"V4L",
		"FB",
		"ALSA",
		"DVB",
	};
	static const char *subdev_types[] = {
		"Unknown",
		"Sensor",
		"Flash",
		"Lens",
		"Decoder",
		"Tuner",
	};

	unsigned int subtype = type & MEDIA_ENT_SUBTYPE_MASK;

	switch (type & MEDIA_ENT_TYPE_MASK) {
	case MEDIA_ENT_T_DEVNODE:
		if (subtype >= ARRAY_SIZE(node_types))
			subtype = 0;
		return node_types[subtype];

	case MEDIA_ENT_T_V4L2_SUBDEV:
		if (subtype >= ARRAY_SIZE(subdev_types))
			subtype = 0;
		return subdev_types[subtype];
	default:
		return node_types[0];
	}
}

static void media_print_pad_text(struct media_entity *entity,
				 const struct media_pad *pad,
				 struct v4l2_subdev_route *routes,
				 unsigned int num_routes,
				 enum v4l2_subdev_format_whence which)
{
	u64 printed_streams_mask = 0;
	unsigned int i;

	if (media_entity_type(entity) != MEDIA_ENT_T_V4L2_SUBDEV)
		return;

	if (!routes) {
		v4l2_subdev_print_format(entity, pad->index, 0, which);
	} else {
		for (i = 0; i < num_routes; ++i) {
			const struct v4l2_subdev_route *route = &routes[i];
			unsigned int stream;

			if (!(route->flags & V4L2_SUBDEV_ROUTE_FL_ACTIVE))
				continue;

			if (pad->flags & MEDIA_PAD_FL_SINK) {
				if (route->sink_pad != pad->index)
					continue;

				stream = route->sink_stream;
			} else {
				if (route->source_pad != pad->index)
					continue;

				stream = route->source_stream;
			}

			if (printed_streams_mask & (1ULL << stream))
				continue;

			v4l2_subdev_print_format(entity, pad->index, stream,
						 which);

			printed_streams_mask |= (1ULL << stream);
		}
	}

	v4l2_subdev_print_pad_dv(entity, pad->index, which);

	if (pad->flags & MEDIA_PAD_FL_SOURCE)
		v4l2_subdev_print_subdev_dv(entity);
}

static void media_print_topology_text_entity(struct media_device *media,
					     struct media_entity *entity,
					     enum v4l2_subdev_format_whence which)
{
	static const struct flag_name link_flags[] = {
		{ MEDIA_LNK_FL_ENABLED, "ENABLED" },
		{ MEDIA_LNK_FL_IMMUTABLE, "IMMUTABLE" },
		{ MEDIA_LNK_FL_DYNAMIC, "DYNAMIC" },
	};
	static const struct flag_name pad_flags[] = {
		{ MEDIA_PAD_FL_SINK, "SINK" },
		{ MEDIA_PAD_FL_SOURCE, "SOURCE" },
		{ MEDIA_PAD_FL_MUST_CONNECT, "MUST_CONNECT" },
	};
	const struct media_entity_desc *info = media_entity_get_info(entity);
	const char *devname = media_entity_get_devname(entity);
	unsigned int num_links = media_entity_get_links_count(entity);
	struct v4l2_subdev_route *routes = NULL;
	unsigned int num_routes = 0, vin_ctrl_cnt = 0;
	unsigned int j, k;
	unsigned int padding = 16;
	bool is_subdev = false;

	if (media_entity_type(entity) == MEDIA_ENT_T_V4L2_SUBDEV) {
		v4l2_subdev_get_routing(entity, &routes, &num_routes, which);
		is_subdev = true;
	}

	pr_dbg("- entity %u:\n", info->id);
	pr_dbg("\t%s (%u pad%s, %u link%s", info->name,
	       info->pads, info->pads > 1 ? "s" : "",
	       num_links, num_links > 1 ? "s" : "");

	if (is_subdev)
		pr_dbg(", %u route%s", num_routes, num_routes != 1 ? "s" : "");

	pr_dbg(")\n");

	pr_dbg("%*cType: %s - %s, flags: %#x\n", padding, ' ',
	       media_entity_type_to_string(info->type),
	       media_entity_subtype_to_string(info->type),
	       info->flags);
	if (devname)
		pr_dbg("%*cName: %s\n", padding, ' ', devname);

	if (is_subdev)
		v4l2_subdev_print_routes(entity, routes, num_routes);

	for (j = 0; j < info->pads; j++) {
		const struct media_pad *pad = media_entity_get_pad(entity, j);

		pr_dbg("\tpad%u: ", j);
		print_flags(pad_flags, ARRAY_SIZE(pad_flags), pad->flags);
		pr_dbg("\n");
		media_print_pad_text(entity, pad, routes, num_routes, which);

		for (k = 0; k < num_links; k++) {
			const struct media_link *link = media_entity_get_link(entity, k);
			const struct media_pad *source = link->source;
			const struct media_pad *sink = link->sink;

			if (source->entity == entity && source->index == j) {
				pr_dbg("\t\t-> \"%s\":%u [",
					   media_entity_get_info(sink->entity)->name, sink->index);
				memset(g_vin_dev_info[vin_ctrl_cnt].vin_ctrl_name, 0, DEV_NAME_LEN);
				strncpy(g_vin_dev_info[vin_ctrl_cnt++].vin_ctrl_name,
						media_entity_get_info(sink->entity)->name,
						strlen(media_entity_get_info(sink->entity)->name));
			}
			else if (sink->entity == entity && sink->index == j)
				pr_dbg("\t\t<- \"%s\":%u [",
					   media_entity_get_info(source->entity)->name,
					   source->index);
			else
				continue;

			print_flags(link_flags, ARRAY_SIZE(link_flags), link->flags);

			pr_dbg("]\n");
		}
	}
	pr_dbg("\n");

	free(routes);

	if (is_subdev) {
		if (info->pads > 1)
			strncpy(g_dst_subdev_name, devname, DEV_NAME_LEN - 1);
		else
			strncpy(g_src_subdev_name, devname, DEV_NAME_LEN - 1);
	} else {
		if (strstr(devname, "video")) {
			strncpy(g_vin_dev_info[g_vin_dev_num].video_name, devname, DEV_NAME_LEN - 1);
			g_vin_dev_info[g_vin_dev_num].id = g_vin_dev_num;
			g_vin_dev_num++;
		}
	}
}

static void media_print_topology_text(struct media_device *media,
									  enum v4l2_subdev_format_whence which)
{
	unsigned int nents = media_get_entities_count(media);
	unsigned int i;

	pr_dbg("Device topology:\n"
		   "-------------------------------------------------------------\n");
	for (i = 0; i < nents; ++i)
		media_print_topology_text_entity(
			media, media_get_entity(media, i), which);
}

int media_dev_parse(char *dev)
{
	struct media_device *media = NULL;
	int ret = 0;

	media = media_device_new(dev);
	if (media == NULL) {
		ERR("Failed to create media device\n");
		goto out;
	}

	/* Enumerate entities, pads and links. */
	ret = media_device_enumerate(media);
	if (ret < 0) {
		ERR("Failed to enumerate %s (%d)\n", dev, ret);
		goto out;
	}

	if (g_verbose) {
		const struct media_device_info *info = media_get_info(media);

		printf("Media device information:\n"
		       "-------------------------------------------------------------\n"
		       "driver          %s\n"
		       "model           %s\n"
		       "serial          %s\n"
		       "bus info        %s\n"
		       "hw revision     0x%x\n"
		       "driver version  %u.%u.%u\n\n",
		       info->driver, info->model,
		       info->serial, info->bus_info,
		       info->hw_revision,
		       (info->driver_version >> 16) & 0xff,
		       (info->driver_version >> 8) & 0xff,
		       (info->driver_version >> 0) & 0xff);
	}

	media_print_topology_text(media, V4L2_SUBDEV_FORMAT_ACTIVE);

out:
	if (media)
		media_device_unref(media);

	return ret;
}
