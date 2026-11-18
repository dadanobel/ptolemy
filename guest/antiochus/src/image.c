/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "image.h"
size_t ant_compare_rgba(const unsigned char *a,const unsigned char *b,size_t pixels,
    unsigned tolerance,unsigned char *difference,unsigned *maximum) {
    size_t i,bad=0;unsigned c;*maximum=0;
    for(i=0;i<pixels;++i) {
        int mismatch=0;
        for(c=0;c<4;++c) {
            int d=(int)a[4*i+c]-b[4*i+c];unsigned delta=d<0?(unsigned)-d:(unsigned)d;
            if(delta>tolerance)mismatch=1;if(delta>*maximum)*maximum=delta;
            if(difference)difference[4*i+c]=delta>63?255:delta*4;
        }
        bad+=mismatch;
    }
    return bad;
}
