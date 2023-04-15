#include "vipsutils.h"
#include "avfilter.h"
#include "video.h"
#include "libavutil/avassert.h"
#include "libavutil/frame.h"

#include <vips/vips.h>
#include <glib-object.h>

static void free_buffer_handler(VipsImage *image, uint8_t *buffer)
{
    av_free(buffer);
}

static void free_avframe_handler(VipsImage *image, AVFrame *frame)
{
    av_frame_free(&frame);
}

int ff_frame_to_vipsimage(VipsImage **image, AVFrame *frame)
{
    size_t bytes_per_line = 3*frame->width;
    size_t buffer_size = bytes_per_line*frame->height;
    uint8_t *buffer;
    int direct;

    // TODO: Transparency support?
    av_assert1(frame->format == AV_PIX_FMT_RGB24);

    if (av_frame_is_writable(frame) && frame->linesize[0] == bytes_per_line) {
        direct = 1;
        buffer = frame->data[0];

    } else {
        direct = 0;

        // Copy the lines to a contiguous memory region
        buffer = av_malloc(buffer_size);
        if (!buffer) {
            return AVERROR(ENOMEM);
        } else {
            uint8_t *dst = buffer;
            uint8_t *src = frame->data[0];
            uint8_t *src_end = src + frame->height * frame->linesize[0];
            for (; src < src_end; dst += bytes_per_line, src += frame->linesize[0]) {
                memcpy(dst, src, bytes_per_line);
            }
        }
    }

    *image = vips_image_new_from_memory(buffer, buffer_size, frame->width, frame->height, 3, VIPS_FORMAT_UCHAR);

    if (direct) {
        // Free the frame when the image is freed
        g_signal_connect(G_OBJECT(*image), "close", G_CALLBACK(free_avframe_handler), (gpointer) frame);
    } else {
        // Free the created buffer when the image is freed
        av_frame_unref(frame);
        g_signal_connect(G_OBJECT(*image), "close", G_CALLBACK(free_buffer_handler), (gpointer) buffer);
    }

    return 0;
}

int ff_vipsimage_to_frame(AVFrame **frame, VipsImage *image, AVFilterLink *outlink)
{
    int width;
    int height;
    int bands;
    VipsImage *new_image;
    uint8_t *out;
    size_t out_size;
    int direct;

    width = vips_image_get_width(image);
    height = vips_image_get_height(image);
    av_assert1(outlink->w == width && outlink->h == height);
    av_assert1(vips_image_get_format(image) == VIPS_FORMAT_UCHAR);
    bands = vips_image_get_bands(image);
    av_assert1((bands == 3 && outlink->format == AV_PIX_FMT_RGB24) || (bands == 4 && outlink->format == AV_PIX_FMT_RGBA));
    out_size = bands*width*height;

    *frame = ff_get_video_buffer(outlink, width, height);
    if (!*frame) {
        g_object_unref(image);
        return AVERROR(ENOMEM);
    }

    // Copy the image data to the frame's buffer
    if ((*frame)->linesize[0] == bands*width) {
        out = (*frame)->data[0];
        direct = 1;
    } else {
        // Unfortunately, libvips does not provide a way to access the image data directly, so it
        // needs to be copied to a temporary buffer.
        out = av_malloc(out_size);
        direct = 0;
        if (!out) {
            g_object_unref(image);
            return AVERROR(ENOMEM);
        }
    }

    new_image = vips_image_new_from_memory(out, out_size, width, height, bands, VIPS_FORMAT_UCHAR);
    if (vips_image_write(image, new_image)) {
        g_object_unref(image);
        g_object_unref(new_image);
        av_frame_free(frame);
        return AVERROR_EXTERNAL;
    }
    g_object_unref(image);
    g_object_unref(new_image);

    if (!direct) {
        uint8_t *dst = (*frame)->data[0];
        uint8_t *src = out;
        uint8_t *src_end = src + out_size;
        size_t bytes_per_line = bands*width;
        for (; src < src_end; dst += (*frame)->linesize[0], src += bytes_per_line) {
            memcpy(dst, src, bytes_per_line);
        }
        av_free(out);
    }

    return 0;
}
