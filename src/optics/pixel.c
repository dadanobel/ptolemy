/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "pixel.h"
#include <math.h>
unsigned optics_pixel_bytes(OpticsPixelLayout p) {
    if(p.format>OPTICS_PIXEL_DXT5)return 0;for(unsigned i=0;i<4;i++)if(p.component[i]>5)return 0;
    return p.format==OPTICS_PIXEL_RGBA8?4:p.format==OPTICS_PIXEL_R8?1:p.format==OPTICS_PIXEL_DXT1?8:p.format>OPTICS_PIXEL_DXT1?16:2;   /* RG8 and the 16-bit layouts: 2 */
}
static uint8_t scale(unsigned x,unsigned max) {return (uint8_t)((x*255+max/2)/max);}
int optics_pixel_reference(OpticsPixelLayout p,const uint8_t *src,uint8_t out[4]) {
    if(!src||!out||!optics_pixel_bytes(p)||p.format>=OPTICS_PIXEL_YUYV601_SIGNED)return 0;uint8_t color[4]={0,0,0,255};
    if(p.format==OPTICS_PIXEL_RGBA8)for(unsigned i=0;i<4;i++)color[i]=src[i];
    else if(p.format==OPTICS_PIXEL_R8)color[0]=src[0];
    else if(p.format==OPTICS_PIXEL_RG8){color[0]=src[0];color[1]=src[1];}
    else {
        unsigned v=src[0]|(unsigned)src[1]<<8;if(!(p.format&1))v=(v>>8)|((v&255)<<8);
        if(p.format<=OPTICS_PIXEL_RGB565_BE){color[0]=scale(v>>11,31);color[1]=scale(v>>5&63,63);color[2]=scale(v&31,31);}
        else if(p.format<=OPTICS_PIXEL_ARGB1555_BE){color[0]=scale(v>>10&31,31);color[1]=scale(v>>5&31,31);color[2]=scale(v&31,31);color[3]=(v&0x8000)?255:0;}
        else {color[0]=scale(v>>8&15,15);color[1]=scale(v>>4&15,15);color[2]=scale(v&15,15);color[3]=scale(v>>12,15);}
    }
    for(unsigned i=0;i<4;i++)out[i]=p.component[i]<4?color[p.component[i]]:p.component[i]==4?0:255;return 1;
}
unsigned optics_depth_bytes(unsigned format) {return format==1?2:format==2?4:0;}
int optics_depth_unpack(unsigned format,const uint8_t *p,float *z,uint8_t *s) {
    if(!p||!z||!s||!optics_depth_bytes(format))return 0;
    uint32_t v=p[0]|(uint32_t)p[1]<<8;
    if(format==2)v|=(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;
    *s=format==2?(uint8_t)v:0;
    *z=(float)((format==2?v>>8:v)/(format==2?16777215.0:65535.0));return 1;
}
int optics_depth_pack(unsigned format,float z,uint8_t s,uint8_t *p) {
    if(!p||!optics_depth_bytes(format)||!isfinite(z)||z<0||z>1)return 0;
    uint32_t v=(uint32_t)floor((double)z*(format==2?16777215.0:65535.0)+0.5);
    if(format==2)v=v<<8|s;
    for(unsigned i=0;i<optics_depth_bytes(format);i++)p[i]=(uint8_t)(v>>(8*i));return 1;
}

static uint8_t yuv_byte(double v){return (uint8_t)fmax(0,fmin(255,floor(v+0.5)));}
int optics_pixel_reference_texel(OpticsPixelLayout p,const uint8_t *src,unsigned w,unsigned h,size_t row,unsigned x,unsigned y,uint8_t out[4]){
    unsigned bpp=optics_pixel_bytes(p);if(!src||!out||!bpp||x>=w||y>=h||row<(size_t)OPTICS_PIXEL_UNITS(p.format,w)*bpp||row>SIZE_MAX/h)return 0;
    if(p.format<OPTICS_PIXEL_YUYV601_SIGNED)return optics_pixel_reference(p,src+(size_t)y*row+(size_t)x*bpp,out);
    if(p.format>=OPTICS_PIXEL_DXT1){   /* S3TC (little-endian words): colour block, after an alpha block in DXT3/5 */
        const uint8_t *b=src+(size_t)(y/4)*row+(size_t)(x/4)*bpp,*q=bpp==16?b+8:b;unsigned i=(y&3)*4+(x&3),e[2][3],c[4]={0,0,0,255};
        unsigned c0=q[0]|(unsigned)q[1]<<8,c1=q[2]|(unsigned)q[3]<<8,s=q[4+i/4]>>(2*(i&3))&3;
        for(unsigned k=0;k<2;k++){unsigned v=k?c1:c0;e[k][0]=scale(v>>11,31);e[k][1]=scale(v>>5&63,63);e[k][2]=scale(v&31,31);}
        for(unsigned k=0;k<3;k++)c[k]=s<2?e[s][k]:p.format!=OPTICS_PIXEL_DXT1||c0>c1?(s==2?(2*e[0][k]+e[1][k]+1)/3:(e[0][k]+2*e[1][k]+1)/3):s==2?(e[0][k]+e[1][k]+1)/2:0;
        if(p.format==OPTICS_PIXEL_DXT1&&c0<=c1&&s==3)c[3]=0;
        if(p.format==OPTICS_PIXEL_DXT3)c[3]=(b[i/2]>>(4*(i&1))&15)*17;
        if(p.format==OPTICS_PIXEL_DXT5){unsigned a0=b[0],a1=b[1],bit=3*i,k=(b[2+bit/8]|(unsigned)b[3+bit/8]<<8)>>(bit&7)&7;
            c[3]=k==0?a0:k==1?a1:a0>a1?((8-k)*a0+(k-1)*a1+3)/7:k==6?0:k==7?255:((6-k)*a0+(k-1)*a1+2)/5;}
        for(unsigned k=0;k<4;k++)out[k]=p.component[k]<4?(uint8_t)c[p.component[k]]:p.component[k]==4?0:255;return 1;
    }
    if(w&1)return 0;unsigned uyvy=(p.format-OPTICS_PIXEL_YUYV601_SIGNED)&1,swap=p.format>=OPTICS_PIXEL_YUYV601_UNSIGNED?128:0;
    const uint8_t *q=src+(size_t)y*row+(size_t)(x&~1u)*2;
    double yy=q[uyvy?1+(x&1)*2:(x&1)*2]-16;
    double u=(int8_t)(q[uyvy?0:1]^swap),v=(int8_t)(q[uyvy?2:3]^swap);
    uint8_t c[4]={yuv_byte(1.1643835616*yy+1.5960267857*v),yuv_byte(1.1643835616*yy-0.3917622901*u-0.8129676472*v),yuv_byte(1.1643835616*yy+2.0172321429*u),255};
    for(unsigned i=0;i<4;i++)out[i]=p.component[i]<4?c[p.component[i]]:p.component[i]==4?0:255;return 1;
}

uint32_t optics_pixel_r300_yuv(uint32_t f){
    unsigned format=f&31;if((format!=0x14&&format!=0x15)||(f>>22&3)!=1)return UINT32_MAX;
    /* Hardware consumes unsigned chroma by default; SWAP inverts its MSB. */
    return OPTICS_PIXEL_YUYV601_UNSIGNED+(format==0x15)-((f&(1u<<24))?2:0);
}
