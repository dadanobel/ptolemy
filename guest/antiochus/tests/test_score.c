/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "../src/score.h"
#include "../src/image.h"
#include "../src/presentation.h"
#include <stdlib.h>
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
int main(void) {
    AntResult r[ANT_CASE_COUNT];AntScore s;unsigned i,maximum;unsigned char a[16],b[16],diff[16];double before;
    for(i=0;i<ANT_CASE_COUNT;++i) {r[i].attempted=1;r[i].passed=1;r[i].unsupported=0;r[i].seconds=i>=29?1.0/60:0.001;}
    s=ant_score(r);assert(s.complete && fabs(s.cpu-1000)<1e-8 && fabs(s.gpu-1000)<1e-8 && fabs(s.total-1000)<1e-8);
    r[11].passed=0;r[11].seconds=1e-99;s=ant_score(r);assert(s.gpu<1000 && s.gpu>0);before=s.gpu;
    r[11].seconds=100;s=ant_score(r);assert(s.gpu==before && fabs(s.gpu_speed-1000)<1e-8);
    r[26].passed=0;r[27].passed=0;s=ant_score(r);assert(s.gpu==before && s.diagnostic_failed==2);
    r[17].passed=0;r[17].unsupported=1;s=ant_score(r);assert(s.gpu==before);
    r[17].unsupported=0;s=ant_score(r);assert(fabs(s.gpu-before*7.0/8.0)<1e-8);
    r[0].passed=0;s=ant_score(r);assert(s.cpu==0 && s.total==0);
    r[0].passed=1;r[1].seconds=NAN;s=ant_score(r);assert(isfinite(s.cpu) && s.cpu<1000);
    r[33].passed=0;s=ant_score(r);assert(s.total==0 && s.combined==0 && s.optional_failed==2);r[33].passed=1;
    r[23].attempted=0;s=ant_score(r);assert(!s.complete && s.total==0);
    memset(a,0,sizeof a);memset(b,0,sizeof b);assert(!ant_compare_rgba(a,b,4,1,diff,&maximum));
    a[3]=2;assert(ant_compare_rgba(a,b,4,1,diff,&maximum)==1 && maximum==2);
    a[3]=1;assert(!ant_compare_rgba(a,b,4,1,diff,&maximum));
    a[4]=255;b[8]=255;assert(ant_compare_rgba(a,b,4,1,diff,&maximum)==2);
    {
        size_t bytes=ANT_SCREEN_W*ANT_SCREEN_H*4;
        unsigned char *old=malloc(bytes),*current=malloc(bytes),*delta=malloc(bytes);
        assert(old && current && delta);ant_screen_reference(old,300);ant_screen_reference(current,301);
        assert(ant_compare_rgba(old,current,ANT_SCREEN_W*ANT_SCREEN_H,1,delta,&maximum)>0);
        memcpy(old,current,bytes);assert(!ant_compare_rgba(old,current,ANT_SCREEN_W*ANT_SCREEN_H,1,delta,&maximum));
        old[400]^=255;assert(ant_compare_rgba(old,current,ANT_SCREEN_W*ANT_SCREEN_H,1,delta,&maximum)==1);
        free(old);free(current);free(delta);
    }
    puts("Score, image, stale-frame and corrupt-pixel controls pass.");return 0;
}
