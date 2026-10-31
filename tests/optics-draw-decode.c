/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Vertex stream element decode of the R300 draw builder (BYTE, D3DCOLOR, SHORT, SIGNED/NORMALIZE). */
#include "../src/optics/draw.c"
#include <assert.h>
#include <stdio.h>
int main(void) {
    float r[4]; uint32_t w[4]={0x80ff4000,0x00017fff,0,0};
    decode_stream(4|1u<<15,0,w,r);   /* BYTE, NORMALIZE: x = bits 7:0 */
    assert(r[0]==0 && fabsf(r[1]-64/255.0f)<1e-6f && r[2]==1 && fabsf(r[3]-128/255.0f)<1e-6f);
    decode_stream(5|1u<<15,0,w,r);   /* D3DCOLOR: x,y,z = bytes 2,1,0 */
    assert(r[0]==1 && fabsf(r[1]-64/255.0f)<1e-6f && r[2]==0);
    decode_stream(4|1u<<14|1u<<15,1,w,r);   /* signed byte, method 1: 0x80 -> -1 (clamped), 0xff -> -1/127 */
    assert(r[3]==-1 && fabsf(r[2]+1/127.0f)<1e-6f);
    decode_stream(4|1u<<14|1u<<15,0,w,r);   /* method 0: 0x80 -> -128/127, below -1 */
    assert(fabsf(r[3]+128/127.0f)<1e-6f && fabsf(r[0]-0)<1e-6f);
    decode_stream(7,0,w,r);   /* SHORT_4, raw integers */
    assert(r[0]==0x4000 && r[1]==0x80ff && r[2]==0x7fff && r[3]==1);
    decode_stream(6|1u<<15,0,w,r);   /* SHORT_2 normalized; z,w default */
    assert(fabsf(r[0]-0x4000/65535.0f)<1e-6f && r[2]==0 && r[3]==1);
    decode_stream(1,0,w,r); assert(r[2]==0 && r[3]==1);   /* FLOAT_2 */
    assert(vap_swap(0x11223344,0)==0x11223344 && vap_swap(0x11223344,1)==0x22114433 && vap_swap(0x11223344,2)==0x44332211 && vap_swap(0x11223344,3)==0x33441122);
    puts("optics-draw-decode: ok"); return 0;
}
