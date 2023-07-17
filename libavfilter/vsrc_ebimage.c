#include "config_components.h"

#include <limits.h>

#include <vips/vips.h>
#include "../../natives/c-bindings/image.h"

#include "libavutil/opt.h"
#include "libavutil/pixfmt.h"
#include "avfilter.h"
#include "filters.h"
#include "internal.h"
#include "vipsutils.h"

typedef struct EBImageSourceContext {
    const AVClass *class;

    // Filter-specific constants
    VipsImage* (*generate_image)(struct EBImageSourceContext *imgsrc);
    enum AVPixelFormat output_format;

    int width, height;

    int pts;

    char *text0, *text1;
    char *font;

    AVFrame *frame;
} EBImageSourceContext;

#define OFFSET(x) offsetof(EBImageSourceContext, x)
#define FLAGS AV_OPT_FLAG_VIDEO_PARAM|AV_OPT_FLAG_FILTERING_PARAM

#define WIDTH_OPTION { "width", "set output width", OFFSET(width), AV_OPT_TYPE_INT, {.i64 = 100}, 0, INT_MAX, FLAGS }
#define SIZE_OPTION { "size", "set output dimensions", OFFSET(width), AV_OPT_TYPE_IMAGE_SIZE, {.str = "hd720"}, 0, 0, FLAGS }

static av_cold int init(AVFilterContext *ctx)
{
    EBImageSourceContext *imgsrc = ctx->priv;
    imgsrc->pts = 0;
    imgsrc->frame = NULL;
    return 0;
}

static int query_formats(AVFilterContext *ctx)
{
    EBImageSourceContext *imgsrc = ctx->priv;
    AVFilterFormats *formats = NULL;
    int ret;

    if ((ret = ff_add_format(&formats, imgsrc->output_format)) < 0) {
        return ret;
    }
    if ((ret = ff_formats_ref(formats, &ctx->outputs[0]->incfg.formats)) < 0) {
        return ret;
    }

    // The pixel format for the reference input is not set. This is not an oversight; it simply
    // means that this filter supports any input pixel format.

    return 0;
}

static int config_ref_input_props(AVFilterLink *inlink)
{
    AVFilterContext *ctx = inlink->dst;
    EBImageSourceContext *imgsrc = ctx->priv;

    imgsrc->width = inlink->w;
    imgsrc->height = inlink->h;

    return 0;
}

static int config_output_props(AVFilterLink *outlink)
{
    AVFilterContext *ctx = outlink->src;
    EBImageSourceContext *imgsrc = ctx->priv;
    int ret;
    VipsImage *img;

    img = imgsrc->generate_image(imgsrc);

    outlink->w = vips_image_get_width(img);
    outlink->h = vips_image_get_height(img);
    outlink->sample_aspect_ratio = (AVRational) { 1, 1 };
    // Setting time_base to and/or incementing pts by a high value causes the filter to output
    // frames less often, reducing the amount of pixel format conversions performed, as every
    // frame is converted if pixel conversion is necessary.
    outlink->frame_rate = (AVRational) { 1, INT_MAX/256 };
    outlink->time_base = (AVRational) { INT_MAX/256, 1 };

    if ((ret = ff_vipsimage_to_frame(&imgsrc->frame, img, outlink)) < 0)
        return ret;

    return 0;
}

static int activate(AVFilterContext *ctx)
{
    AVFilterLink *outlink = ctx->outputs[0];
    EBImageSourceContext *imgsrc = ctx->priv;
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
        frame = av_frame_clone(imgsrc->frame);
        if (!frame)
            return AVERROR(ENOMEM);
        frame->pts = imgsrc->pts;
#if FF_API_PKT_DURATION
FF_DISABLE_DEPRECATION_WARNINGS
        frame->key_frame = 1;
FF_ENABLE_DEPRECATION_WARNINGS
#endif
        frame->flags |= AV_FRAME_FLAG_KEY;
#if FF_API_INTERLACED_FRAME
FF_DISABLE_DEPRECATION_WARNINGS
        frame->interlaced_frame = 0;
FF_ENABLE_DEPRECATION_WARNINGS
#endif
        frame->flags &= ~AV_FRAME_FLAG_INTERLACED;
        frame->pict_type = AV_PICTURE_TYPE_I;
        frame->sample_aspect_ratio = (AVRational) { 1, 1 };

        imgsrc->pts++;

        return ff_filter_frame(outlink, frame);
    }

    return FFERROR_NOT_READY;
}

static av_cold void uninit(AVFilterContext *ctx)
{
    EBImageSourceContext *imgsrc = ctx->priv;
    av_frame_free(&imgsrc->frame);
}

static const AVFilterPad ref_inputs[] = {
    {
        .name          = "ref",
        .type          = AVMEDIA_TYPE_VIDEO,
        .config_props  = config_ref_input_props,
    },
};

static const AVFilterPad outputs[] = {
    {
        .name          = "default",
        .type          = AVMEDIA_TYPE_VIDEO,
        .config_props  = config_output_props,
    },
};


#if CONFIG_EBCAPTION_FILTER

#define EBCAPTIONREF_OPTIONS \
{ "text", "set caption text", OFFSET(text0), AV_OPT_TYPE_STRING, {.str = "get real"}, 0, 0, FLAGS },\
{ "font", "set font name", OFFSET(font), AV_OPT_TYPE_STRING, {.str = "futura"}, 0, 0, FLAGS }
static const AVOption ebcaption_options[] = {
    EBCAPTIONREF_OPTIONS,
    WIDTH_OPTION,
    { NULL }
};
static const AVOption ebcaptionref_options[] = {
    EBCAPTIONREF_OPTIONS,
    { NULL }
};

AVFILTER_DEFINE_CLASS(ebcaption);

static VipsImage *caption_generate_image(EBImageSourceContext *imgsrc)
{
    return esmbot_generate_caption(imgsrc->width, imgsrc->text0, "", imgsrc->font);
}

static av_cold int caption_init(AVFilterContext *ctx)
{
    EBImageSourceContext *imgsrc = ctx->priv;
    imgsrc->generate_image = caption_generate_image;
    imgsrc->output_format = AV_PIX_FMT_RGB24;
    return init(ctx);
}

const AVFilter ff_vsrc_ebcaption = {
    .name            = "ebcaption",
    .description     = NULL_IF_CONFIG_SMALL("Render an esmBot caption."),
    .priv_class      = &ebcaption_class,
    .priv_size       = sizeof(EBImageSourceContext),
    .init            = caption_init,
    .uninit          = uninit,
    .activate        = activate,
    .inputs          = NULL,
    FILTER_OUTPUTS(outputs),
    FILTER_QUERY_FUNC(query_formats),
};
const AVFilter ff_vsrc_ebcaptionref = {
    .name            = "ebcaptionref",
    .description     = NULL_IF_CONFIG_SMALL("Render an esmBot caption with the same width as the input video."),
    .priv_class      = &ebcaption_class,
    .priv_size       = sizeof(EBImageSourceContext),
    .init            = caption_init,
    .uninit          = uninit,
    .activate        = activate,
    FILTER_INPUTS(ref_inputs),
    FILTER_OUTPUTS(outputs),
    FILTER_QUERY_FUNC(query_formats),
};

#endif


#if CONFIG_EBCAPTIONTWO_FILTER

#define EBCAPTIONTWOREF_OPTIONS \
{ "text", "set caption text", OFFSET(text0), AV_OPT_TYPE_STRING, {.str = "get real"}, 0, 0, FLAGS },\
{ "font", "set font name", OFFSET(font), AV_OPT_TYPE_STRING, {.str = "futura"}, 0, 0, FLAGS }
static const AVOption ebcaptiontwo_options[] = {
    EBCAPTIONTWOREF_OPTIONS,
    WIDTH_OPTION,
    { NULL }
};
static const AVOption ebcaptiontworef_options[] = {
    EBCAPTIONTWOREF_OPTIONS,
    { NULL }
};

AVFILTER_DEFINE_CLASS(ebcaptiontwo);

static VipsImage *caption_two_generate_image(EBImageSourceContext *imgsrc)
{
    return esmbot_generate_caption_two(imgsrc->width, imgsrc->text0, "", imgsrc->font);
}

static av_cold int caption_two_init(AVFilterContext *ctx)
{
    EBImageSourceContext *imgsrc = ctx->priv;
    imgsrc->generate_image = caption_two_generate_image;
    imgsrc->output_format = AV_PIX_FMT_RGB24;
    return init(ctx);
}

const AVFilter ff_vsrc_ebcaptiontwo = {
    .name            = "ebcaptiontwo",
    .description     = NULL_IF_CONFIG_SMALL("Render an esmBot caption2."),
    .priv_class      = &ebcaptiontwo_class,
    .priv_size       = sizeof(EBImageSourceContext),
    .init            = caption_two_init,
    .uninit          = uninit,
    .activate        = activate,
    .inputs          = NULL,
    FILTER_OUTPUTS(outputs),
    FILTER_QUERY_FUNC(query_formats),
};
const AVFilter ff_vsrc_ebcaptiontworef = {
    .name            = "ebcaptiontworef",
    .description     = NULL_IF_CONFIG_SMALL("Render an esmBot caption2 with the same width as the input video."),
    .priv_class      = &ebcaptiontwo_class,
    .priv_size       = sizeof(EBImageSourceContext),
    .init            = caption_two_init,
    .uninit          = uninit,
    .activate        = activate,
    FILTER_INPUTS(ref_inputs),
    FILTER_OUTPUTS(outputs),
    FILTER_QUERY_FUNC(query_formats),
};

#endif


#if CONFIG_EBSNAPCHAT_FILTER

#define EBSNAPCHATREF_OPTIONS \
{ "text", "set caption text", OFFSET(text0), AV_OPT_TYPE_STRING, {.str = "get real"}, 0, 0, FLAGS },\
{ "font", "set font name", OFFSET(font), AV_OPT_TYPE_STRING, {.str = "futura"}, 0, 0, FLAGS }
static const AVOption ebsnapchat_options[] = {
    EBSNAPCHATREF_OPTIONS,
    SIZE_OPTION,
    { NULL }
};
static const AVOption ebsnapchatref_options[] = {
    EBSNAPCHATREF_OPTIONS,
    { NULL }
};

AVFILTER_DEFINE_CLASS(ebsnapchat);

static VipsImage *snapchat_generate_image(EBImageSourceContext *imgsrc)
{
    return esmbot_generate_snapchat_overlay(imgsrc->width, imgsrc->text0, "", imgsrc->font);
}

static av_cold int snapchat_init(AVFilterContext *ctx)
{
    EBImageSourceContext *imgsrc = ctx->priv;
    imgsrc->generate_image = snapchat_generate_image;
    imgsrc->output_format = AV_PIX_FMT_RGBA;
    return init(ctx);
}

const AVFilter ff_vsrc_ebsnapchat = {
    .name            = "ebsnapchat",
    .description     = NULL_IF_CONFIG_SMALL("Render an esmBot Snapchat caption overlay."),
    .priv_class      = &ebsnapchat_class,
    .priv_size       = sizeof(EBImageSourceContext),
    .init            = snapchat_init,
    .uninit          = uninit,
    .activate        = activate,
    .inputs          = NULL,
    FILTER_OUTPUTS(outputs),
    FILTER_QUERY_FUNC(query_formats),
};
const AVFilter ff_vsrc_ebsnapchatref = {
    .name            = "ebsnapchatref",
    .description     = NULL_IF_CONFIG_SMALL("Render an esmBot Snapchat caption overlay with the same width as the input video."),
    .priv_class      = &ebsnapchat_class,
    .priv_size       = sizeof(EBImageSourceContext),
    .init            = snapchat_init,
    .uninit          = uninit,
    .activate        = activate,
    FILTER_INPUTS(ref_inputs),
    FILTER_OUTPUTS(outputs),
    FILTER_QUERY_FUNC(query_formats),
};

#endif


#if CONFIG_EBMEME_FILTER

#define EBMEMEREF_OPTIONS \
{ "top", "set top text", OFFSET(text0), AV_OPT_TYPE_STRING, {.str = ""}, 0, 0, FLAGS },\
{ "bottom", "set bottom text", OFFSET(text1), AV_OPT_TYPE_STRING, {.str = ""}, 0, 0, FLAGS },\
{ "font", "set font name", OFFSET(font), AV_OPT_TYPE_STRING, {.str = "impact"}, 0, 0, FLAGS }
static const AVOption ebmeme_options[] = {
    EBMEMEREF_OPTIONS,
    SIZE_OPTION,
    { NULL }
};
static const AVOption ebmemeref_options[] = {
    EBMEMEREF_OPTIONS,
    { NULL }
};

AVFILTER_DEFINE_CLASS(ebmeme);

static VipsImage *meme_generate_image(EBImageSourceContext *imgsrc)
{
    return esmbot_generate_meme_overlay(imgsrc->width, imgsrc->height, imgsrc->text0, imgsrc->text1, "", "futura");
}

static av_cold int meme_init(AVFilterContext *ctx)
{
    EBImageSourceContext *imgsrc = ctx->priv;
    imgsrc->generate_image = meme_generate_image;
    imgsrc->output_format = AV_PIX_FMT_RGBA;
    return init(ctx);
}

const AVFilter ff_vsrc_ebmeme = {
    .name            = "ebmeme",
    .description     = NULL_IF_CONFIG_SMALL("Render an esmBot meme overlay."),
    .priv_class      = &ebmeme_class,
    .priv_size       = sizeof(EBImageSourceContext),
    .init            = meme_init,
    .uninit          = uninit,
    .activate        = activate,
    .inputs          = NULL,
    FILTER_OUTPUTS(outputs),
    FILTER_QUERY_FUNC(query_formats),
};
const AVFilter ff_vsrc_ebmemeref = {
    .name            = "ebmemeref",
    .description     = NULL_IF_CONFIG_SMALL("Render an esmBot meme overlay with the same dimensions as the input video."),
    .priv_class      = &ebmeme_class,
    .priv_size       = sizeof(EBImageSourceContext),
    .init            = meme_init,
    .uninit          = uninit,
    .activate        = activate,
    FILTER_INPUTS(ref_inputs),
    FILTER_OUTPUTS(outputs),
    FILTER_QUERY_FUNC(query_formats),
};

#endif
