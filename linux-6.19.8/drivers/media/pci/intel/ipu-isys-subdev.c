// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2013 - 2018 Intel Corporation

#include <linux/types.h>
#include <linux/slab.h>
#include <linux/videodev2.h>

#include <media/media-entity.h>

#include <uapi/linux/media-bus-format.h>

#include "ipu-isys.h"
#include "ipu-isys-video.h"
#include "ipu-isys-subdev.h"

unsigned int ipu_isys_mbus_code_to_bpp(u32 code)
{
	switch (code) {
	case MEDIA_BUS_FMT_RGB888_1X24:
		return 24;
	case MEDIA_BUS_FMT_YUYV10_1X20:
		return 20;
	case MEDIA_BUS_FMT_Y10_1X10:
	case MEDIA_BUS_FMT_RGB565_1X16:
	case MEDIA_BUS_FMT_UYVY8_1X16:
	case MEDIA_BUS_FMT_YUYV8_1X16:
		return 16;
	case MEDIA_BUS_FMT_SBGGR14_1X14:
	case MEDIA_BUS_FMT_SGBRG14_1X14:
	case MEDIA_BUS_FMT_SGRBG14_1X14:
	case MEDIA_BUS_FMT_SRGGB14_1X14:
		return 14;
	case MEDIA_BUS_FMT_SBGGR12_1X12:
	case MEDIA_BUS_FMT_SGBRG12_1X12:
	case MEDIA_BUS_FMT_SGRBG12_1X12:
	case MEDIA_BUS_FMT_SRGGB12_1X12:
		return 12;
	case MEDIA_BUS_FMT_SBGGR10_1X10:
	case MEDIA_BUS_FMT_SGBRG10_1X10:
	case MEDIA_BUS_FMT_SGRBG10_1X10:
	case MEDIA_BUS_FMT_SRGGB10_1X10:
		return 10;
	case MEDIA_BUS_FMT_SBGGR8_1X8:
	case MEDIA_BUS_FMT_SGBRG8_1X8:
	case MEDIA_BUS_FMT_SGRBG8_1X8:
	case MEDIA_BUS_FMT_SRGGB8_1X8:
	case MEDIA_BUS_FMT_SBGGR10_DPCM8_1X8:
	case MEDIA_BUS_FMT_SGBRG10_DPCM8_1X8:
	case MEDIA_BUS_FMT_SGRBG10_DPCM8_1X8:
	case MEDIA_BUS_FMT_SRGGB10_DPCM8_1X8:
		return 8;
	default:
		WARN_ON(1);
		return -EINVAL;
	}
}

unsigned int ipu_isys_mbus_code_to_mipi(u32 code)
{
	switch (code) {
	case MEDIA_BUS_FMT_RGB565_1X16:
		return IPU_ISYS_MIPI_CSI2_TYPE_RGB565;
	case MEDIA_BUS_FMT_RGB888_1X24:
		return IPU_ISYS_MIPI_CSI2_TYPE_RGB888;
	case MEDIA_BUS_FMT_YUYV10_1X20:
		return IPU_ISYS_MIPI_CSI2_TYPE_YUV422_10;
	case MEDIA_BUS_FMT_UYVY8_1X16:
	case MEDIA_BUS_FMT_YUYV8_1X16:
		return IPU_ISYS_MIPI_CSI2_TYPE_YUV422_8;
	case MEDIA_BUS_FMT_SBGGR14_1X14:
	case MEDIA_BUS_FMT_SGBRG14_1X14:
	case MEDIA_BUS_FMT_SGRBG14_1X14:
	case MEDIA_BUS_FMT_SRGGB14_1X14:
		return IPU_ISYS_MIPI_CSI2_TYPE_RAW14;
	case MEDIA_BUS_FMT_SBGGR12_1X12:
	case MEDIA_BUS_FMT_SGBRG12_1X12:
	case MEDIA_BUS_FMT_SGRBG12_1X12:
	case MEDIA_BUS_FMT_SRGGB12_1X12:
		return IPU_ISYS_MIPI_CSI2_TYPE_RAW12;
	case MEDIA_BUS_FMT_Y10_1X10:
	case MEDIA_BUS_FMT_SBGGR10_1X10:
	case MEDIA_BUS_FMT_SGBRG10_1X10:
	case MEDIA_BUS_FMT_SGRBG10_1X10:
	case MEDIA_BUS_FMT_SRGGB10_1X10:
		return IPU_ISYS_MIPI_CSI2_TYPE_RAW10;
	case MEDIA_BUS_FMT_SBGGR8_1X8:
	case MEDIA_BUS_FMT_SGBRG8_1X8:
	case MEDIA_BUS_FMT_SGRBG8_1X8:
	case MEDIA_BUS_FMT_SRGGB8_1X8:
		return IPU_ISYS_MIPI_CSI2_TYPE_RAW8;
	case MEDIA_BUS_FMT_SBGGR10_DPCM8_1X8:
	case MEDIA_BUS_FMT_SGBRG10_DPCM8_1X8:
	case MEDIA_BUS_FMT_SGRBG10_DPCM8_1X8:
	case MEDIA_BUS_FMT_SRGGB10_DPCM8_1X8:
		return IPU_ISYS_MIPI_CSI2_TYPE_USER_DEF(1);
	default:
		WARN_ON(1);
		return -EINVAL;
	}
}

enum ipu_isys_subdev_pixelorder ipu_isys_subdev_get_pixelorder(u32 code)
{
	switch (code) {
	case MEDIA_BUS_FMT_SBGGR14_1X14:
	case MEDIA_BUS_FMT_SBGGR12_1X12:
	case MEDIA_BUS_FMT_SBGGR10_1X10:
	case MEDIA_BUS_FMT_SBGGR8_1X8:
	case MEDIA_BUS_FMT_SBGGR10_DPCM8_1X8:
		return IPU_ISYS_SUBDEV_PIXELORDER_BGGR;
	case MEDIA_BUS_FMT_SGBRG14_1X14:
	case MEDIA_BUS_FMT_SGBRG12_1X12:
	case MEDIA_BUS_FMT_SGBRG10_1X10:
	case MEDIA_BUS_FMT_SGBRG8_1X8:
	case MEDIA_BUS_FMT_SGBRG10_DPCM8_1X8:
		return IPU_ISYS_SUBDEV_PIXELORDER_GBRG;
	case MEDIA_BUS_FMT_SGRBG14_1X14:
	case MEDIA_BUS_FMT_SGRBG12_1X12:
	case MEDIA_BUS_FMT_SGRBG10_1X10:
	case MEDIA_BUS_FMT_SGRBG8_1X8:
	case MEDIA_BUS_FMT_SGRBG10_DPCM8_1X8:
		return IPU_ISYS_SUBDEV_PIXELORDER_GRBG;
	case MEDIA_BUS_FMT_SRGGB14_1X14:
	case MEDIA_BUS_FMT_SRGGB12_1X12:
	case MEDIA_BUS_FMT_SRGGB10_1X10:
	case MEDIA_BUS_FMT_SRGGB8_1X8:
	case MEDIA_BUS_FMT_SRGGB10_DPCM8_1X8:
		return IPU_ISYS_SUBDEV_PIXELORDER_RGGB;
	default:
		WARN_ON(1);
		return -EINVAL;
	}
}

u32 ipu_isys_subdev_code_to_uncompressed(u32 sink_code)
{
	switch (sink_code) {
	case MEDIA_BUS_FMT_SBGGR10_DPCM8_1X8:
		return MEDIA_BUS_FMT_SBGGR10_1X10;
	case MEDIA_BUS_FMT_SGBRG10_DPCM8_1X8:
		return MEDIA_BUS_FMT_SGBRG10_1X10;
	case MEDIA_BUS_FMT_SGRBG10_DPCM8_1X8:
		return MEDIA_BUS_FMT_SGRBG10_1X10;
	case MEDIA_BUS_FMT_SRGGB10_DPCM8_1X8:
		return MEDIA_BUS_FMT_SRGGB10_1X10;
	default:
		return sink_code;
	}
}

struct v4l2_mbus_framefmt *__ipu_isys_get_ffmt(struct v4l2_subdev *sd,
			   struct v4l2_subdev_state *cfg,
					       unsigned int pad,
					       unsigned int stream,
					       unsigned int which)
{
	struct ipu_isys_subdev *asd = to_ipu_isys_subdev(sd);

	if (pad >= sd->entity.num_pads || stream >= asd->pad_stream_count[pad])
		return NULL;

	if (which == V4L2_SUBDEV_FORMAT_ACTIVE)
		return &asd->ffmt[pad][stream];

	struct v4l2_mbus_framefmt *ffmt =
		v4l2_subdev_state_get_format(cfg, pad, stream);
	if (!ffmt)
	    ffmt = &asd->ffmt[pad][stream];
	return ffmt;
}

struct v4l2_rect *__ipu_isys_get_selection(struct v4l2_subdev *sd,
		       struct v4l2_subdev_state *cfg,
					   unsigned int target,
					   unsigned int pad, unsigned int which)
{
	struct ipu_isys_subdev *asd = to_ipu_isys_subdev(sd);

	if (which == V4L2_SUBDEV_FORMAT_ACTIVE) {
		switch (target) {
		case V4L2_SEL_TGT_CROP:
			return &asd->crop[pad];
		case V4L2_SEL_TGT_COMPOSE:
			return &asd->compose[pad];
		}
	} else {
		struct v4l2_rect *rect;

		switch (target) {
			case V4L2_SEL_TGT_CROP:
			    rect = v4l2_subdev_state_get_crop(cfg, pad);
			    return rect ? rect : &asd->crop[pad];
			case V4L2_SEL_TGT_COMPOSE:
			    rect = v4l2_subdev_state_get_compose(cfg, pad);
			    return rect ? rect : &asd->compose[pad];
		}
	}
	WARN_ON(1);
	return NULL;
}

static int target_valid(struct v4l2_subdev *sd, unsigned int target,
			unsigned int pad)
{
	struct ipu_isys_subdev *asd = to_ipu_isys_subdev(sd);

	switch (target) {
	case V4L2_SEL_TGT_CROP:
		return asd->valid_tgts[pad].crop;
	case V4L2_SEL_TGT_COMPOSE:
		return asd->valid_tgts[pad].compose;
	default:
		return 0;
	}
}

int ipu_isys_subdev_fmt_propagate(struct v4l2_subdev *sd,
				  struct v4l2_subdev_state *state,
				  struct v4l2_mbus_framefmt *ffmt,
				  struct v4l2_rect *r,
				  enum isys_subdev_prop_tgt tgt,
				  unsigned int pad, unsigned int which)
{
	struct ipu_isys_subdev *asd = to_ipu_isys_subdev(sd);
	struct v4l2_mbus_framefmt **ffmts = NULL;
	struct v4l2_rect **crops = NULL;
	struct v4l2_rect **compose = NULL;
	unsigned int i;
	int rval = 0;

	if (tgt == IPU_ISYS_SUBDEV_PROP_TGT_NR_OF)
		return 0;

	if (WARN_ON(pad >= sd->entity.num_pads))
		return -EINVAL;

	ffmts = kcalloc(sd->entity.num_pads,
			sizeof(*ffmts), GFP_KERNEL);
	if (!ffmts) {
		rval = -ENOMEM;
		goto out_subdev_fmt_propagate;
	}
	crops = kcalloc(sd->entity.num_pads,
			sizeof(*crops), GFP_KERNEL);
	if (!crops) {
		rval = -ENOMEM;
		goto out_subdev_fmt_propagate;
	}
	compose = kcalloc(sd->entity.num_pads,
			  sizeof(*compose), GFP_KERNEL);
	if (!compose) {
		rval = -ENOMEM;
		goto out_subdev_fmt_propagate;
	}

	for (i = 0; i < sd->entity.num_pads; i++) {
		ffmts[i] = __ipu_isys_get_ffmt(sd, state, i, 0, which);
		crops[i] = __ipu_isys_get_selection(sd, state, V4L2_SEL_TGT_CROP,
						    i, which);
		compose[i] = __ipu_isys_get_selection(sd, state,
						      V4L2_SEL_TGT_COMPOSE,
						      i, which);
	}

	switch (tgt) {
	case IPU_ISYS_SUBDEV_PROP_TGT_SINK_FMT:
		crops[pad]->left = 0;
		crops[pad]->top = 0;
		crops[pad]->width = ffmt->width;
		crops[pad]->height = ffmt->height;
		rval = ipu_isys_subdev_fmt_propagate(sd, state, ffmt, crops[pad],
						     tgt + 1, pad, which);
		goto out_subdev_fmt_propagate;
	case IPU_ISYS_SUBDEV_PROP_TGT_SINK_CROP:
		if (WARN_ON(sd->entity.pads[pad].flags & MEDIA_PAD_FL_SOURCE))
			goto out_subdev_fmt_propagate;

		compose[pad]->left = 0;
		compose[pad]->top = 0;
		compose[pad]->width = r->width;
		compose[pad]->height = r->height;
		rval = ipu_isys_subdev_fmt_propagate(sd, state, ffmt,
						     compose[pad], tgt + 1,
						     pad, which);
		goto out_subdev_fmt_propagate;
	case IPU_ISYS_SUBDEV_PROP_TGT_SINK_COMPOSE:
		if (WARN_ON(sd->entity.pads[pad].flags & MEDIA_PAD_FL_SOURCE)) {
			rval = -EINVAL;
			goto out_subdev_fmt_propagate;
		}

		for (i = 1; i < sd->entity.num_pads; i++) {
			if (!(sd->entity.pads[i].flags &
					MEDIA_PAD_FL_SOURCE))
				continue;

			compose[i]->left = 0;
			compose[i]->top = 0;
			compose[i]->width = r->width;
			compose[i]->height = r->height;
			rval = ipu_isys_subdev_fmt_propagate(sd, state,
							     ffmt,
							     compose[i],
							     tgt + 1, i,
							     which);
			if (rval)
				goto out_subdev_fmt_propagate;
		}
		goto out_subdev_fmt_propagate;
	case IPU_ISYS_SUBDEV_PROP_TGT_SOURCE_COMPOSE:
		if (WARN_ON(sd->entity.pads[pad].flags & MEDIA_PAD_FL_SINK)) {
			rval = -EINVAL;
			goto out_subdev_fmt_propagate;
		}

		crops[pad]->left = 0;
		crops[pad]->top = 0;
		crops[pad]->width = r->width;
		crops[pad]->height = r->height;
		rval = ipu_isys_subdev_fmt_propagate(sd, state, ffmt,
						     crops[pad], tgt + 1,
						     pad, which);
		goto out_subdev_fmt_propagate;
	case IPU_ISYS_SUBDEV_PROP_TGT_SOURCE_CROP:{
			struct v4l2_subdev_format fmt = {
				.which = which,
				.pad = pad,
				.format = {
					.width = r->width,
					.height = r->height,
					/*
					 * Either use the code from sink pad
					 * or the current one.
					 */
					.code = ffmt ? ffmt->code :
						       ffmts[pad]->code,
					.field = ffmt ? ffmt->field :
							ffmts[pad]->field,
				},
			};

			asd->set_ffmt(sd, state, &fmt);
			goto out_subdev_fmt_propagate;
		}
	}

out_subdev_fmt_propagate:
	kfree(ffmts);
	kfree(crops);
	kfree(compose);
	return rval;
}

int ipu_isys_subdev_set_ffmt_default(struct v4l2_subdev *sd,
		      struct v4l2_subdev_state *cfg,
				      struct v4l2_subdev_format *fmt)
{
	struct v4l2_mbus_framefmt *ffmt =
		__ipu_isys_get_ffmt(sd, cfg, fmt->pad, fmt->stream,
					   fmt->which);

	/* No propagation for non-zero pads. */
	if (fmt->pad) {
		struct v4l2_mbus_framefmt *sink_ffmt =
			__ipu_isys_get_ffmt(sd, cfg, 0, fmt->stream,
						   fmt->which);

		ffmt->width = sink_ffmt->width;
		ffmt->height = sink_ffmt->height;
		ffmt->code = sink_ffmt->code;
		ffmt->field = sink_ffmt->field;
	}

	ffmt->width = fmt->format.width;
	ffmt->height = fmt->format.height;
	ffmt->code = fmt->format.code;
	ffmt->field = fmt->format.field;

	return ipu_isys_subdev_fmt_propagate(sd, cfg, &fmt->format, NULL,
				      IPU_ISYS_SUBDEV_PROP_TGT_SINK_FMT,
				      fmt->pad, fmt->which);
}

int __ipu_isys_subdev_set_ffmt(struct v4l2_subdev *sd,
		   struct v4l2_subdev_state *cfg,
			       struct v4l2_subdev_format *fmt)
{
	struct ipu_isys_subdev *asd = to_ipu_isys_subdev(sd);
	struct v4l2_mbus_framefmt *ffmt =
		__ipu_isys_get_ffmt(sd, cfg, fmt->pad, fmt->stream,
					   fmt->which);
	u32 code = asd->supported_codes[fmt->pad][0];
	unsigned int i;

	WARN_ON(!mutex_is_locked(&asd->mutex));

	fmt->format.width = clamp(fmt->format.width, IPU_ISYS_MIN_WIDTH,
				  IPU_ISYS_MAX_WIDTH);
	fmt->format.height = clamp(fmt->format.height,
				   IPU_ISYS_MIN_HEIGHT, IPU_ISYS_MAX_HEIGHT);

	for (i = 0; asd->supported_codes[fmt->pad][i]; i++) {
		if (asd->supported_codes[fmt->pad][i] == fmt->format.code) {
			code = asd->supported_codes[fmt->pad][i];
			break;
		}
	}

	fmt->format.code = code;

	asd->set_ffmt(sd, cfg, fmt);
	if (fmt->which == V4L2_SUBDEV_FORMAT_ACTIVE && cfg) {
		unsigned int pad, stream;

		/* Keep the V4L2 active-state formats in step with the IPU cache. */
		for (pad = 0; pad < sd->entity.num_pads; pad++) {
			for (stream = 0; stream < asd->pad_stream_count[pad];
			     stream++) {
				struct v4l2_mbus_framefmt *state_fmt =
					v4l2_subdev_state_get_format(cfg, pad,
								     stream);
				struct v4l2_mbus_framefmt *cached =
					&asd->ffmt[pad][stream];

				if (!cached->width || !cached->height)
					cached = &asd->ffmt[pad][0];
				if (state_fmt)
					*state_fmt = *cached;
			}
		}
	}

	fmt->format = *ffmt;

	return 0;
}

int ipu_isys_subdev_set_ffmt(struct v4l2_subdev *sd,
		 struct v4l2_subdev_state *cfg,
			     struct v4l2_subdev_format *fmt)
{
	struct ipu_isys_subdev *asd = to_ipu_isys_subdev(sd);
	int rval;

	if (fmt->pad >= sd->entity.num_pads ||
	    fmt->stream >= asd->pad_stream_count[fmt->pad])
		return -EINVAL;

	mutex_lock(&asd->mutex);
	rval = __ipu_isys_subdev_set_ffmt(sd, cfg, fmt);
	mutex_unlock(&asd->mutex);

	return rval;
}

int ipu_isys_subdev_get_ffmt(struct v4l2_subdev *sd,
		 struct v4l2_subdev_state *cfg,
			     struct v4l2_subdev_format *fmt)
{
	struct ipu_isys_subdev *asd = to_ipu_isys_subdev(sd);

	if (fmt->pad >= sd->entity.num_pads ||
	    fmt->stream >= asd->pad_stream_count[fmt->pad])
		return -EINVAL;

	mutex_lock(&asd->mutex);
	fmt->format = *__ipu_isys_get_ffmt(sd, cfg, fmt->pad,
					   fmt->stream,
					   fmt->which);
	mutex_unlock(&asd->mutex);

	return 0;
}

int ipu_isys_subdev_get_frame_desc(struct v4l2_subdev *sd,
				   struct v4l2_mbus_frame_desc *desc)
{
	int i, rval = 0;

	for (i = 0; i < sd->entity.num_pads; i++) {
		if (!(sd->entity.pads[i].flags & MEDIA_PAD_FL_SOURCE))
			continue;

		rval = v4l2_subdev_call(sd, pad, get_frame_desc, i, desc);
		if (!rval)
			return rval;
	}

	if (i == sd->entity.num_pads)
		rval = -EINVAL;

	return rval;
}

u32 ipu_isys_get_src_stream_by_src_pad(struct v4l2_subdev *sd, u32 pad)
{
	struct v4l2_subdev_state *state;
	struct v4l2_subdev_route *routes;
	unsigned int i;
	u32 source_stream = 0;

	state = v4l2_subdev_lock_and_get_active_state(sd);
	if (!state)
		return 0;

	routes = state->routing.routes;
	for (i = 0; i < state->routing.num_routes; i++) {
		if ((routes[i].flags & V4L2_SUBDEV_ROUTE_FL_ACTIVE) &&
		    routes[i].source_pad == pad) {
			source_stream = routes[i].source_stream;
			break;
		}
	}

	v4l2_subdev_unlock_state(state);

	return source_stream;
}

bool ipu_isys_subdev_has_route(struct media_entity *entity,
			       unsigned int pad0, unsigned int pad1, int *stream)
{
	struct ipu_isys_subdev *asd;
	int i;

	if (!entity) {
		WARN_ON(1);
		return false;
	}
	asd = to_ipu_isys_subdev(media_entity_to_v4l2_subdev(entity));

	/* Two sinks are never connected together. */
	if (pad0 < asd->nsinks && pad1 < asd->nsinks)
		return false;

	for (i = 0; i < asd->nstreams; i++) {
		if ((asd->route[i].flags & V4L2_SUBDEV_ROUTE_FL_ACTIVE) &&
		    ((asd->route[i].sink == pad0 &&
		      asd->route[i].source == pad1) ||
		     (asd->route[i].sink == pad1 &&
			  asd->route[i].source == pad0))) {
			if (stream)
				*stream = i;
			return true;
		}
	}

	return false;
}

static unsigned int ipu_isys_route_sink_slot(
		const struct ipu_isys_subdev *asd,
		const struct v4l2_subdev_route *route)
{
	if (asd->pad_stream_count[route->sink_pad] > 1)
		return route->source_pad - asd->nsinks;
	return 0;
}

static unsigned int ipu_isys_route_source_slot(
		const struct ipu_isys_subdev *asd,
		const struct v4l2_subdev_route *route)
{
	if (asd->pad_stream_count[route->source_pad] > 1)
		return route->sink_pad;
	return 0;
}

static int ipu_isys_subdev_sync_routes(struct ipu_isys_subdev *asd,
				      const struct v4l2_subdev_krouting *routing)
{
	unsigned int i, j;

	for (i = 0; i < asd->sd.entity.num_pads; i++)
		bitmap_zero(asd->stream[i].streams_stat, 32);

	for (i = 0; i < routing->num_routes; i++) {
		const struct v4l2_subdev_route *route = &routing->routes[i];
		unsigned int sink_slot = ipu_isys_route_sink_slot(asd, route);
		unsigned int source_slot = ipu_isys_route_source_slot(asd, route);

		for (j = 0; j < asd->nstreams; j++)
			if (asd->route[j].sink == route->sink_pad &&
			    asd->route[j].source == route->source_pad)
				break;
		if (j == asd->nstreams)
			return -EINVAL;

		if (asd->pad_stream_count[route->sink_pad] > 1)
			asd->stream[route->sink_pad].stream_id[sink_slot] =
				route->sink_stream;
		if (asd->pad_stream_count[route->source_pad] > 1)
			asd->stream[route->source_pad].stream_id[source_slot] =
				route->source_stream;
		else
			asd->stream[route->source_pad].stream_id[0] =
				route->source_stream;

		asd->route[j].flags = route->flags;
		if (route->flags & V4L2_SUBDEV_ROUTE_FL_ACTIVE) {
			bitmap_set(asd->stream[route->sink_pad].streams_stat,
				   route->sink_stream, 1);
			bitmap_set(asd->stream[route->source_pad].streams_stat,
				   route->source_stream, 1);
		}
	}

	return 0;
}

int ipu_isys_subdev_set_routing(struct v4l2_subdev *sd,
			   struct v4l2_subdev_state *state,
			   enum v4l2_subdev_format_whence which,
			   struct v4l2_subdev_krouting *route)
{
	struct ipu_isys_subdev *asd = to_ipu_isys_subdev(sd);
	struct v4l2_mbus_framefmt initial_fmt = asd->ffmt[0][0];
	struct v4l2_mbus_framefmt *old_formats;
	bool *old_format_valid;
	size_t format_count;
	unsigned int i, j;
	int ret;

	if (!state || !route || route->num_routes != asd->nstreams ||
	    (route->num_routes && !route->routes))
		return -EINVAL;

	for (i = 0; i < route->num_routes; i++) {
		const struct v4l2_subdev_route *candidate = &route->routes[i];

		if (candidate->flags & ~V4L2_SUBDEV_ROUTE_FL_ACTIVE ||
		    candidate->sink_pad >= sd->entity.num_pads ||
		    candidate->source_pad >= sd->entity.num_pads ||
		    candidate->sink_stream >= asd->pad_stream_count[candidate->sink_pad] ||
		    candidate->source_stream >= asd->pad_stream_count[candidate->source_pad])
			return -EINVAL;
	}

	ret = v4l2_subdev_routing_validate(sd, route,
					   V4L2_SUBDEV_ROUTING_ONLY_1_TO_1);
	if (ret)
		return ret;

	for (i = 0; i < route->num_routes; i++) {
		struct v4l2_subdev_route *candidate = &route->routes[i];
		bool found = false;

		for (j = 0; j < asd->nstreams; j++) {
			if (candidate->sink_pad == asd->route[j].sink &&
			    candidate->source_pad == asd->route[j].source) {
				if (asd->route[j].immutable) {
					unsigned int sink_slot =
						ipu_isys_route_sink_slot(asd, candidate);
					unsigned int source_slot =
						ipu_isys_route_source_slot(asd, candidate);
					unsigned int sink_stream = 0, source_stream = 0;

					if (asd->pad_stream_count[candidate->sink_pad] > 1)
						sink_stream = asd->stream[candidate->sink_pad].stream_id[sink_slot];
					if (asd->pad_stream_count[candidate->source_pad] > 1)
						source_stream = asd->stream[candidate->source_pad].stream_id[source_slot];
					else
						source_stream = asd->stream[candidate->source_pad].stream_id[0];

					if (!(candidate->flags & V4L2_SUBDEV_ROUTE_FL_ACTIVE) ||
					    candidate->sink_stream != sink_stream ||
					    candidate->source_stream != source_stream)
						return -EINVAL;
				}
				found = true;
				break;
			}
		}
		if (!found)
			return -EINVAL;
		for (j = 0; j < i; j++)
			if (route->routes[j].sink_pad == candidate->sink_pad &&
			    route->routes[j].source_pad == candidate->source_pad)
				return -EINVAL;
	}

	if (which != V4L2_SUBDEV_FORMAT_ACTIVE &&
	    which != V4L2_SUBDEV_FORMAT_TRY)
		return -EINVAL;
	format_count = (size_t)sd->entity.num_pads * asd->nstreams;
	old_formats = kcalloc(format_count, sizeof(*old_formats), GFP_KERNEL);
	old_format_valid = kcalloc(format_count, sizeof(*old_format_valid),
				   GFP_KERNEL);
	if (!old_formats || !old_format_valid) {
		kfree(old_formats);
		kfree(old_format_valid);
		return -ENOMEM;
	}
	if (which == V4L2_SUBDEV_FORMAT_TRY) {
		for (i = 0; i < sd->entity.num_pads; i++) {
			for (j = 0; j < asd->pad_stream_count[i]; j++) {
				struct v4l2_mbus_framefmt *fmt =
					v4l2_subdev_state_get_format(state, i, j);
				size_t idx = (size_t)i * asd->nstreams + j;

				if (fmt) {
					old_formats[idx] = *fmt;
					old_format_valid[idx] = true;
				}
			}
		}
	}

	ret = v4l2_subdev_set_routing_with_fmt(sd, state, route, &initial_fmt);
	if (!ret && which == V4L2_SUBDEV_FORMAT_ACTIVE) {
		mutex_lock(&asd->mutex);
		ret = ipu_isys_subdev_sync_routes(asd, route);
		for (i = 0; !ret && i < sd->entity.num_pads; i++) {
			for (j = 0; j < asd->pad_stream_count[i]; j++) {
				struct v4l2_mbus_framefmt *fmt =
					v4l2_subdev_state_get_format(state, i, j);
				struct v4l2_mbus_framefmt *cached =
					&asd->ffmt[i][j];

				if (!fmt)
					continue;
				if (!cached->width || !cached->height)
					cached = &asd->ffmt[i][0];
				*fmt = *cached;
			}
		}
		mutex_unlock(&asd->mutex);
	} else if (!ret) {
		for (i = 0; i < sd->entity.num_pads; i++) {
			for (j = 0; j < asd->pad_stream_count[i]; j++) {
				struct v4l2_mbus_framefmt *fmt =
					v4l2_subdev_state_get_format(state, i, j);
				size_t idx = (size_t)i * asd->nstreams + j;

				if (fmt && old_format_valid[idx])
					*fmt = old_formats[idx];
			}
		}
	}
	kfree(old_formats);
	kfree(old_format_valid);
	return ret;
}

int ipu_isys_subdev_init_state(struct v4l2_subdev *sd,
			       struct v4l2_subdev_state *state)
{
	struct ipu_isys_subdev *asd = to_ipu_isys_subdev(sd);
	struct v4l2_subdev_route *routes;
	struct v4l2_subdev_krouting routing;
	unsigned int i;
	int ret;

	routes = kcalloc(asd->nstreams, sizeof(*routes), GFP_KERNEL);
	if (!routes)
		return -ENOMEM;

	mutex_lock(&asd->mutex);
	for (i = 0; i < asd->nstreams; i++) {
		struct v4l2_subdev_route *route = &routes[i];
		unsigned int sink_slot, source_slot;

		route->sink_pad = asd->route[i].sink;
		route->source_pad = asd->route[i].source;
		sink_slot = ipu_isys_route_sink_slot(asd, route);
		source_slot = ipu_isys_route_source_slot(asd, route);
		if (asd->pad_stream_count[route->sink_pad] > 1)
			route->sink_stream = asd->stream[route->sink_pad].stream_id[sink_slot];
		if (asd->pad_stream_count[route->source_pad] > 1)
			route->source_stream = asd->stream[route->source_pad].stream_id[source_slot];
		else
			route->source_stream = asd->stream[route->source_pad].stream_id[0];
		route->flags = asd->route[i].flags & V4L2_SUBDEV_ROUTE_FL_ACTIVE;
	}
	mutex_unlock(&asd->mutex);

	routing.num_routes = asd->nstreams;
	routing.routes = routes;
	ret = v4l2_subdev_set_routing_with_fmt(sd, state, &routing,
						&asd->ffmt[0][0]);
	if (!ret) {
		unsigned int pad, stream;

		mutex_lock(&asd->mutex);
		for (pad = 0; pad < sd->entity.num_pads; pad++) {
			for (stream = 0; stream < asd->pad_stream_count[pad];
			     stream++) {
				struct v4l2_mbus_framefmt *fmt =
					v4l2_subdev_state_get_format(state, pad,
								     stream);

				if (fmt) {
					struct v4l2_mbus_framefmt *cached =
						&asd->ffmt[pad][stream];

					if (!cached->width || !cached->height)
						cached = &asd->ffmt[pad][0];
					*fmt = *cached;
				}
			}
		}
		mutex_unlock(&asd->mutex);
	}
	kfree(routes);
	return ret;
}

int ipu_isys_subdev_init_finalize(struct ipu_isys_subdev *asd)
{
	unsigned int pad, stream;
	int ret;

	ret = v4l2_subdev_init_finalize(&asd->sd);
	if (ret)
		return ret;

	mutex_lock(&asd->mutex);
	for (pad = 0; pad < asd->sd.entity.num_pads; pad++) {
		for (stream = 0; stream < asd->pad_stream_count[pad]; stream++) {
			struct v4l2_mbus_framefmt *fmt =
				v4l2_subdev_state_get_format(asd->sd.active_state,
							     pad, stream);

			if (fmt)
				*fmt = (asd->ffmt[pad][stream].width &&
					asd->ffmt[pad][stream].height) ?
					asd->ffmt[pad][stream] : asd->ffmt[pad][0];
		}
	}
	mutex_unlock(&asd->mutex);

	return 0;
}

int ipu_isys_subdev_set_sel(struct v4l2_subdev *sd,
		struct v4l2_subdev_state *cfg,
			    struct v4l2_subdev_selection *sel)
{
	struct ipu_isys_subdev *asd = to_ipu_isys_subdev(sd);
	struct media_pad *pad = &asd->sd.entity.pads[sel->pad];
	struct v4l2_rect *r, __r = { 0 };
	unsigned int tgt;

	if (!target_valid(sd, sel->target, sel->pad))
		return -EINVAL;

	switch (sel->target) {
	case V4L2_SEL_TGT_CROP:
		if (pad->flags & MEDIA_PAD_FL_SINK) {
			struct v4l2_mbus_framefmt *ffmt =
				__ipu_isys_get_ffmt(sd, cfg, sel->pad, 0,
							   sel->which);

			__r.width = ffmt->width;
			__r.height = ffmt->height;
			r = &__r;
			tgt = IPU_ISYS_SUBDEV_PROP_TGT_SINK_CROP;
		} else {
			/* 0 is the sink pad. */
			r = __ipu_isys_get_selection(sd, cfg, sel->target, 0,
						     sel->which);
			tgt = IPU_ISYS_SUBDEV_PROP_TGT_SOURCE_CROP;
		}

		break;
	case V4L2_SEL_TGT_COMPOSE:
		if (pad->flags & MEDIA_PAD_FL_SINK) {
			r = __ipu_isys_get_selection(sd, cfg, V4L2_SEL_TGT_CROP,
						     sel->pad, sel->which);
			tgt = IPU_ISYS_SUBDEV_PROP_TGT_SINK_COMPOSE;
		} else {
			r = __ipu_isys_get_selection(sd, cfg,
						     V4L2_SEL_TGT_COMPOSE, 0,
						     sel->which);
			tgt = IPU_ISYS_SUBDEV_PROP_TGT_SOURCE_COMPOSE;
		}
		break;
	default:
		return -EINVAL;
	}

	sel->r.width = clamp(sel->r.width, IPU_ISYS_MIN_WIDTH, r->width);
	sel->r.height = clamp(sel->r.height, IPU_ISYS_MIN_HEIGHT, r->height);
	*__ipu_isys_get_selection(sd, cfg, sel->target, sel->pad,
				  sel->which) = sel->r;
	return ipu_isys_subdev_fmt_propagate(sd, cfg, NULL, &sel->r, tgt,
				      sel->pad, sel->which);
}

int ipu_isys_subdev_get_sel(struct v4l2_subdev *sd,
		struct v4l2_subdev_state *cfg,
			    struct v4l2_subdev_selection *sel)
{
	if (!target_valid(sd, sel->target, sel->pad))
		return -EINVAL;

	sel->r = *__ipu_isys_get_selection(sd, cfg, sel->target,
					   sel->pad, sel->which);

	return 0;
}

int ipu_isys_subdev_enum_mbus_code(struct v4l2_subdev *sd,
		   struct v4l2_subdev_state *cfg,
				   struct v4l2_subdev_mbus_code_enum *code)
{
	struct ipu_isys_subdev *asd = to_ipu_isys_subdev(sd);
	const u32 *supported_codes;
	u32 index;

	if (code->pad >= sd->entity.num_pads)
		return -EINVAL;
	supported_codes = asd->supported_codes[code->pad];

	if (code->stream >= asd->pad_stream_count[code->pad])
		return -EINVAL;

	for (index = 0; supported_codes[index]; index++) {
		if (index == code->index) {
			code->code = supported_codes[index];
			return 0;
		}
	}

	return -EINVAL;
}

#if !defined(CONFIG_VIDEO_INTEL_IPU4) && !defined(CONFIG_VIDEO_INTEL_IPU4P)
/*
 * IPU private link validation
 * In advanced IPU and special case, there will be format change between
 * sink/source pads in ISYS.
 * Format code checking is not necessary for these features.
 */
static int ipu_isys_subdev_link_validate_private(
					struct v4l2_subdev *sd,
					struct media_link *link,
					struct v4l2_subdev_format *source_fmt,
					struct v4l2_subdev_format *sink_fmt)
{
	struct ipu_isys_subdev *asd = to_ipu_isys_subdev(sd);

	/* The width and height must match. */
	if (source_fmt->format.width != sink_fmt->format.width
	    || source_fmt->format.height != sink_fmt->format.height)
		return -EPIPE;

	/*
	 * The field order must match, or the sink field order must be NONE
	 * to support interlaced hardware connected to bridges that support
	 * progressive formats only.
	 */
	if (source_fmt->format.field != sink_fmt->format.field &&
	    sink_fmt->format.field != V4L2_FIELD_NONE)
		return -EPIPE;

	if (source_fmt->stream != sink_fmt->stream)
		return -EINVAL;

	/*
	 * For new IPU special case, YUV format changing in BE-SOC,
	 * from YUV422 to I420, which is used to adapt multiple
	 * YUV sensors and provide I420 to BB for partial processing.
	 * If this entity doing format convert, ignore format check
	 */
	if (source_fmt->format.code != sink_fmt->format.code) {
		if (source_fmt->format.code == MEDIA_BUS_FMT_UYVY8_2X8 &&
			(sink_fmt->format.code == MEDIA_BUS_FMT_YUYV8_1X16 ||
			sink_fmt->format.code == MEDIA_BUS_FMT_UYVY8_1X16))
			dev_warn(&asd->isys->adev->dev,
				"YUV format change, ignore code check\n");
		else
			return -EINVAL;
	}

	return 0;
}
#endif

/*
 * Besides validating the link, figure out the external pad and the
 * ISYS FW ABI source.
 */
int ipu_isys_subdev_link_validate(struct v4l2_subdev *sd,
				  struct media_link *link,
				  struct v4l2_subdev_format *source_fmt,
				  struct v4l2_subdev_format *sink_fmt)
{
	struct v4l2_subdev *source_sd =
	    media_entity_to_v4l2_subdev(link->source->entity);
	struct ipu_isys_pipeline *ip = container_of(media_entity_pipeline(&sd->entity),
						    struct ipu_isys_pipeline,
						    pipe);
	struct ipu_isys_subdev *asd = to_ipu_isys_subdev(sd);

	if (!source_sd)
		return -ENODEV;
	if (strncmp(source_sd->name, IPU_ISYS_ENTITY_PREFIX,
		strlen(IPU_ISYS_ENTITY_PREFIX)) != 0) {
		/*
		 * source_sd isn't ours --- sd must be the external
		 * sub-device.
		 */
		ip->external = link->source;
		if (!strncmp(sd->entity.name,
			     IPU_ISYS_ENTITY_PREFIX " CSI-2 ",
			     strlen(IPU_ISYS_ENTITY_PREFIX " CSI-2 ")))
			ip->source = ipu_isys_csi2_get_fw_source(sd);
		else
			ip->source = to_ipu_isys_subdev(sd)->source;
		dev_dbg(&asd->isys->adev->dev, "%s: using source %d\n",
			sd->entity.name, ip->source);
	} else if (source_sd->entity.num_pads == 1) {
		/* All internal sources have a single pad. */
		ip->external = link->source;
		ip->source = to_ipu_isys_subdev(source_sd)->source;

		dev_dbg(&asd->isys->adev->dev, "%s: using source %d\n",
			sd->entity.name, ip->source);
	}

	if (asd->isl_mode != IPU_ISL_OFF)
		ip->isl_mode = asd->isl_mode;

#if !defined(CONFIG_VIDEO_INTEL_IPU4) && !defined(CONFIG_VIDEO_INTEL_IPU4P)
	return ipu_isys_subdev_link_validate_private(sd, link, source_fmt,
						    sink_fmt);
#else
	return v4l2_subdev_link_validate_default(sd, link, source_fmt,
						 sink_fmt);
#endif
}

int ipu_isys_subdev_open(struct v4l2_subdev *sd, struct v4l2_subdev_fh *fh)
{
	struct ipu_isys_subdev *asd = to_ipu_isys_subdev(sd);
	unsigned int i;

	mutex_lock(&asd->mutex);

	for (i = 0; i < asd->sd.entity.num_pads; i++) {
		struct v4l2_mbus_framefmt *try_fmt =
			v4l2_subdev_state_get_format(fh->state, i);
		struct v4l2_rect *try_crop =
			v4l2_subdev_state_get_crop(fh->state, i);
		struct v4l2_rect *try_compose =
			v4l2_subdev_state_get_compose(fh->state, i);

		if (try_fmt)
			*try_fmt = asd->ffmt[i][0];
		if (try_crop)
			*try_crop = asd->crop[i];
		if (try_compose)
			*try_compose = asd->compose[i];
	}

	mutex_unlock(&asd->mutex);

	return 0;
}

int ipu_isys_subdev_close(struct v4l2_subdev *sd, struct v4l2_subdev_fh *fh)
{
	return 0;
}

int ipu_isys_subdev_init(struct ipu_isys_subdev *asd,
			 struct v4l2_subdev_ops *ops,
			 unsigned int nr_ctrls,
			 unsigned int num_pads,
			 unsigned int num_streams,
			 unsigned int num_source,
			 unsigned int num_sink,
			 unsigned int sd_flags)
{
	int i;
	int rval = -EINVAL;

	mutex_init(&asd->mutex);

	v4l2_subdev_init(&asd->sd, ops);

	asd->sd.flags |= V4L2_SUBDEV_FL_HAS_DEVNODE | sd_flags;
	asd->sd.owner = THIS_MODULE;

	asd->nstreams = num_streams;
	asd->nsources = num_source;
	asd->nsinks = num_sink;

	asd->pad = devm_kcalloc(&asd->isys->adev->dev, num_pads,
				sizeof(*asd->pad), GFP_KERNEL);

	asd->ffmt = (struct v4l2_mbus_framefmt **)
			devm_kcalloc(&asd->isys->adev->dev, num_pads,
				     sizeof(struct v4l2_mbus_framefmt *),
				     GFP_KERNEL);

	asd->crop = devm_kcalloc(&asd->isys->adev->dev, num_pads,
				 sizeof(*asd->crop), GFP_KERNEL);

	asd->compose = devm_kcalloc(&asd->isys->adev->dev, num_pads,
				    sizeof(*asd->compose), GFP_KERNEL);

	asd->valid_tgts = devm_kcalloc(&asd->isys->adev->dev, num_pads,
				       sizeof(*asd->valid_tgts), GFP_KERNEL);
	asd->route = devm_kcalloc(&asd->isys->adev->dev, num_streams,
				  sizeof(*asd->route), GFP_KERNEL);

	asd->stream = devm_kcalloc(&asd->isys->adev->dev, num_pads,
				   sizeof(*asd->stream), GFP_KERNEL);
	asd->pad_stream_count = devm_kcalloc(&asd->isys->adev->dev, num_pads,
					     sizeof(*asd->pad_stream_count),
					     GFP_KERNEL);

	if (!asd->pad || !asd->ffmt || !asd->crop || !asd->compose ||
	    !asd->valid_tgts || !asd->route || !asd->stream ||
	    !asd->pad_stream_count)
		return -ENOMEM;

	for (i = 0; i < num_pads; i++) {
		asd->pad_stream_count[i] = 1;
		asd->ffmt[i] = (struct v4l2_mbus_framefmt *)
		    devm_kcalloc(&asd->isys->adev->dev, num_streams,
				 sizeof(struct v4l2_mbus_framefmt), GFP_KERNEL);
		if (!asd->ffmt[i])
			return -ENOMEM;

		asd->stream[i].stream_id =
		    devm_kcalloc(&asd->isys->adev->dev, num_source,
				 sizeof(*asd->stream[i].stream_id), GFP_KERNEL);
		if (!asd->stream[i].stream_id)
			return -ENOMEM;
	}

	for (i = 0; i < num_sink; i++)
		asd->pad[i].flags = MEDIA_PAD_FL_SINK;
	for (i = num_sink; i < num_pads; i++)
		asd->pad[i].flags = MEDIA_PAD_FL_SOURCE;

	rval = media_entity_pads_init(&asd->sd.entity, num_pads, asd->pad);
	if (rval)
		goto out_mutex_destroy;

	if (asd->ctrl_init) {
		rval = v4l2_ctrl_handler_init(&asd->ctrl_handler, nr_ctrls);
		if (rval)
			goto out_media_entity_cleanup;

		asd->ctrl_init(&asd->sd);
		if (asd->ctrl_handler.error) {
			rval = asd->ctrl_handler.error;
			goto out_v4l2_ctrl_handler_free;
		}

		asd->sd.ctrl_handler = &asd->ctrl_handler;
	}

	asd->source = -1;

	return 0;

out_v4l2_ctrl_handler_free:
	v4l2_ctrl_handler_free(&asd->ctrl_handler);

out_media_entity_cleanup:
	media_entity_cleanup(&asd->sd.entity);

out_mutex_destroy:
	mutex_destroy(&asd->mutex);

	return rval;
}

void ipu_isys_subdev_cleanup(struct ipu_isys_subdev *asd)
{
	if (asd->sd.active_state)
		v4l2_subdev_cleanup(&asd->sd);
	media_entity_cleanup(&asd->sd.entity);
	v4l2_ctrl_handler_free(&asd->ctrl_handler);
	mutex_destroy(&asd->mutex);
}
