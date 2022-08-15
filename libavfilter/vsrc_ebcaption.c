/*
 * Copyright (c) 2022 D. Pardal <dd_pardal@outlook.pt>
 *
 * This file is part of a modified version of FFmpeg.
 *
 * FFmpeg is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * FFmpeg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with FFmpeg; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include <limits.h>

#include "avfilter.h"
#include "filters.h"
#include "internal.h"
#include "libvips_common.h"
#include "libavutil/opt.h"

#include "../../processing/processing.h"
#include <vips/vips.h>

typedef struct EBCaptionContext {
    const AVClass *class;

	char *text;
	int width;

    int pts;

    AVFrame *frame;
} EBCaptionContext;

#define OFFSET(x) offsetof(EBCaptionContext, x)
#define FLAGS AV_OPT_FLAG_VIDEO_PARAM|AV_OPT_FLAG_FILTERING_PARAM
static const AVOption ebcaption_options[] = {
    { "text", "set caption text", OFFSET(text), AV_OPT_TYPE_STRING, {.str = "get real"}, 0, 0, FLAGS },
    { "width", "set output width", OFFSET(width), AV_OPT_TYPE_INT, {.i64 = 100}, 0, INT_MAX, FLAGS },
    { NULL }
};
static const AVOption ebcaptionref_options[] = {
    { "text", "set caption text", OFFSET(text), AV_OPT_TYPE_STRING, {.str = "get real"}, 0, 0, FLAGS },
    { NULL }
};

AVFILTER_DEFINE_CLASS(ebcaption);

static av_cold int init(AVFilterContext *ctx)
{
    EBCaptionContext *caption = ctx->priv;
    caption->pts = 0;
    caption->frame = NULL;
    return 0;
}

static int query_formats(AVFilterContext *ctx)
{
    AVFilterFormats *formats = NULL;
    int ret;

    if ((ret = ff_add_format(&formats, AV_PIX_FMT_RGB24)) < 0) {
        return ret;
    }
    if ((ret = ff_formats_ref(formats, &ctx->outputs[0]->incfg.formats)) < 0) {
        return ret;
    }

    // The pixel format for ebcaptionref's input is not set. This is not an oversight; it simply
    // means that this filter supports any input pixel format.

    return 0;
}

static int config_props_ref_input(AVFilterLink *inlink)
{
    AVFilterContext *ctx = inlink->dst;
    EBCaptionContext *caption = ctx->priv;

    caption->width = inlink->w;

    return 0;
}

static int config_props_output(AVFilterLink *outlink)
{
    AVFilterContext *ctx = outlink->src;
    EBCaptionContext *caption = ctx->priv;
    int ret;
    VipsImage *img;

    av_log(ctx, AV_LOG_VERBOSE, "Using %i as the width for the caption generation.\n", caption->width);
    img = esmbot_generate_caption_img(caption->width, "futura", caption->text);

    outlink->w = vips_image_get_width(img);
    outlink->h = vips_image_get_height(img);
    outlink->sample_aspect_ratio = (AVRational) { 1, 1 };
    // Setting time_base to and/or incementing pts by a high value causes the filter to output
    // frames less often, reducing the amount of pixel format conversions performed, as every
    // frame is converted if pixel conversion is necessary.
    outlink->frame_rate = (AVRational) { 1, INT_MAX/256 };
    outlink->time_base = (AVRational) { INT_MAX/256, 1 };

    if ((ret = ff_vipsimage_to_frame(&caption->frame, img, outlink)) < 0)
        return ret;

    return 0;
}

static int activate(AVFilterContext *ctx)
{
    AVFilterLink *outlink = ctx->outputs[0];
    EBCaptionContext *caption = ctx->priv;
    AVFrame *frame;
    int ret;

    if (ctx->nb_inputs > 0) {
        // Code for ebcaptionref

        AVFilterLink *inlink = ctx->inputs[0];

        FF_FILTER_FORWARD_STATUS_BACK(outlink, inlink);

        // Consume and ignore all input frames
        ret = ff_inlink_consume_frame(inlink, &frame);
        if (ret < 0) {
            return ret;
        } else if (ret > 0) {
            av_frame_free(&frame);
        }

        FF_FILTER_FORWARD_STATUS(inlink, outlink);
    }

    if (ff_outlink_frame_wanted(outlink)) {
        av_log(ctx, AV_LOG_VERBOSE, "Frame wanted from the filter. (This should only happen twice unless the input properties have changed.)\n");

        frame = av_frame_clone(caption->frame);
        if (!frame)
            return AVERROR(ENOMEM);
        frame->pts = caption->pts;
        frame->key_frame           = 1;
        frame->interlaced_frame    = 0;
        frame->pict_type           = AV_PICTURE_TYPE_I;
        frame->sample_aspect_ratio = (AVRational) { 1, 1 };

        caption->pts++;

        return ff_filter_frame(outlink, frame);
    }

    return FFERROR_NOT_READY;
}

static av_cold void uninit(AVFilterContext *ctx)
{
    EBCaptionContext *caption = ctx->priv;
    av_frame_free(&caption->frame);
}

static const AVFilterPad ebcaptionref_inputs[] = {
    {
        .name          = "ref",
        .type          = AVMEDIA_TYPE_VIDEO,
        .config_props  = config_props_ref_input,
    },
};

// Also used by ebcaptionref
static const AVFilterPad ebcaption_outputs[] = {
    {
        .name          = "default",
        .type          = AVMEDIA_TYPE_VIDEO,
        .config_props  = config_props_output,
    },
};

const AVFilter ff_vsrc_ebcaption = {
    .name            = "ebcaption",
    .description     = NULL_IF_CONFIG_SMALL("Render an esmBot caption."),
    .priv_class      = &ebcaption_class,
    .priv_size       = sizeof(EBCaptionContext),
    .init            = init,
    .uninit          = uninit,
    .activate        = activate,
    .inputs          = NULL,
    FILTER_OUTPUTS(ebcaption_outputs),
    FILTER_QUERY_FUNC(query_formats),
};
const AVFilter ff_vsrc_ebcaptionref = {
    .name            = "ebcaptionref",
    .description     = NULL_IF_CONFIG_SMALL("Render an esmBot caption with the same width as the input video."),
    .priv_class      = &ebcaption_class,
    .priv_size       = sizeof(EBCaptionContext),
    .init            = init,
    .uninit          = uninit,
    .activate        = activate,
    FILTER_INPUTS(ebcaptionref_inputs),
    FILTER_OUTPUTS(ebcaption_outputs),
    FILTER_QUERY_FUNC(query_formats),
};
