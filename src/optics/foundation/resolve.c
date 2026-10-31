/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "resolve.h"
#include <math.h>
#include <stddef.h>
#include <string.h>

void pt_r2_gamma_init(PtR2Gamma *g)
{
    if(!g)return;
    for(unsigned i=0;i<256;i++)g->degamma[i]=powf(i/255.0f,2.2f);
    for(unsigned i=0;i<4096;i++)g->regamma[i]=(uint8_t)lrintf(255.0f*powf(i/4095.0f,1.0f/2.2f));
}
int pt_r2_resolve_pixel(const PtR2Gamma *g,const uint8_t (*s)[4],
                        unsigned n,uint32_t ctl,uint8_t out[4])
{
    if(!g || !s || !out || (n!=1 && n!=2 && n!=4) || !(ctl&1) || (ctl&~7u))return 0;
    if(n==1){memcpy(out,s[0],4);return 1;}
    uint8_t r[4];
    for(unsigned c=0;c<4;c++) {
        if(!(ctl&1) || (c==3 && !(ctl&4)))r[c]=s[0][c];
        else if(c!=3 && (ctl&2)) {
            float sum=0;for(unsigned k=0;k<n;k++)sum+=g->degamma[s[k][c]];
            long index=lrintf(sum/n*4095.0f);
            if(index<0 || index>4095)return 0;
            r[c]=g->regamma[index];
        } else {
            unsigned sum=n/2;for(unsigned k=0;k<n;k++)sum+=s[k][c];r[c]=(uint8_t)(sum/n);
        }
    }
    memcpy(out,r,4);return 1;
}
