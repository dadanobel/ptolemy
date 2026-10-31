/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef OPTICS_PIXEL_H
#define OPTICS_PIXEL_H
#include <stdint.h>
#include <stddef.h>
/* Normalized LINEAR upload formats, not R300 TX_FORMAT field encodings. */
enum {OPTICS_PIXEL_RGBA8,OPTICS_PIXEL_RGB565_LE,OPTICS_PIXEL_RGB565_BE,
    OPTICS_PIXEL_ARGB1555_LE,OPTICS_PIXEL_ARGB1555_BE,OPTICS_PIXEL_ARGB4444_LE,OPTICS_PIXEL_ARGB4444_BE,OPTICS_PIXEL_R8,
    OPTICS_PIXEL_RG8, /* two bytes, R then G */
    OPTICS_PIXEL_YUYV601_SIGNED,OPTICS_PIXEL_UYVY601_SIGNED,OPTICS_PIXEL_YUYV601_UNSIGNED,OPTICS_PIXEL_UYVY601_UNSIGNED,
    OPTICS_PIXEL_DXT1,OPTICS_PIXEL_DXT3,OPTICS_PIXEL_DXT5};   /* S3TC: 4x4 texel blocks of 8 or 16 bytes, rows of blocks */
/* Storage units along one axis: texels, or 4x4 blocks for the DXT layouts. */
#define OPTICS_PIXEL_UNITS(format,texels) ((format)>=OPTICS_PIXEL_DXT1?((texels)+3)/4:(texels))
typedef struct {uint32_t format;uint8_t component[4];} OpticsPixelLayout;
/* component 0..3 selects decoded RGBA; 4 is zero, 5 is one. */
unsigned optics_pixel_bytes(OpticsPixelLayout);   /* of a texel, or of a block (DXT) */
/* UINT32_MAX for a packed-YUV mode not represented by the clamped codec. */
uint32_t optics_pixel_r300_yuv(uint32_t format1);
int optics_pixel_reference(OpticsPixelLayout,const uint8_t *source,uint8_t rgba[4]);
int optics_pixel_reference_texel(OpticsPixelLayout,const uint8_t *source,unsigned width,unsigned height,size_t row,unsigned x,unsigned y,uint8_t rgba[4]);
/* Canonical little-endian R300 integer depth: 1 D16; 2 high-24 Z / low-8 S.
 * Exact round trip for every representable source value, without aliasing loads. */
unsigned optics_depth_bytes(unsigned format);
int optics_depth_unpack(unsigned format,const uint8_t *,float *depth,uint8_t *stencil);
int optics_depth_pack(unsigned format,float depth,uint8_t stencil,uint8_t *);
#endif
