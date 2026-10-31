/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "pixel.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
int main(void){
    assert(optics_pixel_r300_yuv(0x00400a14)==OPTICS_PIXEL_YUYV601_UNSIGNED);
    assert(optics_pixel_r300_yuv(0x01400a14)==OPTICS_PIXEL_YUYV601_SIGNED);
    assert(optics_pixel_r300_yuv(0x00400a15)==OPTICS_PIXEL_UYVY601_UNSIGNED);
    assert(optics_pixel_r300_yuv(0x00000a14)==UINT32_MAX&&optics_pixel_r300_yuv(0x00800a14)==UINT32_MAX);
    OpticsPixelLayout l={OPTICS_PIXEL_YUYV601_UNSIGNED,{0,1,2,3}};uint8_t b[]={16,128,235,128,99,99,99,99,81,90,145,240},q[4],wrong[4];
    assert(optics_pixel_reference_texel(l,b,2,2,8,0,0,q)&&q[0]==0&&q[1]==0&&q[2]==0&&q[3]==255);assert(optics_pixel_reference_texel(l,b,2,2,8,1,0,q)&&q[0]==255&&q[1]==255&&q[2]==255);
    assert(optics_pixel_reference_texel(l,b,2,2,8,0,1,q)&&q[0]>250&&q[1]<3&&q[2]<3);
    l.format=OPTICS_PIXEL_UYVY601_UNSIGNED;assert(optics_pixel_reference_texel(l,b,2,2,8,0,1,wrong)&&memcmp(q,wrong,3));
    uint8_t uyvy[]={90,81,240,145};assert(optics_pixel_reference_texel(l,uyvy,2,1,4,0,0,wrong)&&!memcmp(q,wrong,4));
    l.format=OPTICS_PIXEL_YUYV601_SIGNED;uint8_t signed_uv[]={81,90^128,145,240^128};assert(optics_pixel_reference_texel(l,signed_uv,2,1,4,0,0,wrong)&&!memcmp(q,wrong,4));
    assert(!optics_pixel_reference(l,b,q));assert(!optics_pixel_reference_texel(l,b,1,1,2,0,0,q));assert(!optics_pixel_reference_texel(l,b,2,1,3,0,0,q));assert(!optics_pixel_reference_texel(l,b,2,1,4,2,0,q));
    puts("PASS: YUYV/UYVY pair sharing, signed/unsigned chroma MSB, black/white/red vectors, padded rows, odd width and wrong-order negative control");
}
