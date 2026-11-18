/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "presentation.h"
unsigned ant_tile(unsigned x,unsigned y,unsigned frame) {
    unsigned cell=y*16+x;
    return ((frame>>(cell%16))^cell^(cell>>4))&1 ? 255 : 0;
}
void ant_screen_reference(unsigned char *p,unsigned frame) {
    unsigned x,y;
    for(y=0;y<ANT_SCREEN_H;++y)for(x=0;x<ANT_SCREEN_W;++x) {
        unsigned at=4*(y*ANT_SCREEN_W+x),v=ant_tile(x/32,y/16,frame);
        p[at]=p[at+1]=p[at+2]=(unsigned char)v;p[at+3]=255;
    }
}
