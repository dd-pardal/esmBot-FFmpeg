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

/**
 * @file Utilities for filters that process frames using libvips
 */

#ifndef AVUTIL_LIBVIPS_H
#define AVUTIL_LIBVIPS_H

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

#endif /* AVUTIL_LIBVIPS_H */
