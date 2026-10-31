/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "pointer_math.h"
#include <assert.h>
#include <stdio.h>
int main(void){double x=0;int n=0;for(unsigned k=0;k<100;k++){x+=0.125;n+=pt_pointer_whole(&x);}assert(n==12&&x==0.5);
    x=0;n=0;for(unsigned k=0;k<100;k++){x+=pt_pointer_scroll(0.5,1);n+=pt_pointer_whole(&x);}assert(n>=4&&n<=5);
    assert(pt_pointer_scroll(3,0)==3);x=NAN;assert(!pt_pointer_whole(&x)&&x==0);x=1e100;assert(pt_pointer_whole(&x)==INT32_MAX&&x==0);
    x=-0.9;assert(!pt_pointer_whole(&x));x-=0.2;assert(pt_pointer_whole(&x)==-1);puts("PASS: fractional pointer motion, precise/line scroll, finite and saturation boundaries");}
