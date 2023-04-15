#ifndef AVFILTER_VIPSUTILS_H
#define AVFILTER_VIPSUTILS_H

#include "avfilter.h"
#include "libavutil/frame.h"
#include <vips/vips.h>

/**
 * Creates a new VipsImage using the data in an AVFrame with the AV_PIX_FMT_RGB24 pixel format and frees the AVFrame once it's not needed.
 * 
 * @param[out] image A pointer to where the pointer to the VipsImage should be stored
 * @param[in] frame A pointer to the AVFrame
 * @return 0 on success, otherwise a negative error code
 */
int ff_frame_to_vipsimage(VipsImage **image, AVFrame *frame);

/**
 * Allocates a new AVFrame with buffer, writes the data from the image to it and frees the VipsImage.
 * 
 * @param[out] frame A pointer to where the pointer to the AVFrame should be stored
 * @param[in] image A pointer to the VipsImage
 * @param[in] outlink The outlink to use for ff_get_video_buffer()
 * @return 0 on success, otherwise a negative error code
 */
int ff_vipsimage_to_frame(AVFrame **frame, VipsImage *image, AVFilterLink *outlink);

#endif /* AVFILTER_VIPSUTILS_H */
