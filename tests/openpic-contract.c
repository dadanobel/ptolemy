/* SPDX-License-Identifier: GPL-2.0-or-later */
/* OpenPIC 1.2 activity, threshold, edge replay and cached output contract. */
#include "platform.h"
#include <assert.h>
#include <stdio.h>
#include <time.h>
int main(void) {
    PtPic p;pt_pic_init(&p);pt_pic_write(&p,0x20080,0);
    pt_pic_write(&p,0x10000,5u<<16|0x42);pt_pic_write(&p,0x10010,1);
    pt_pic_line(&p,0,0,1);pt_pic_line(&p,0,0,0);assert(pt_pic_output(&p));
    assert(pt_pic_read(&p,0x200a0)==0x42);assert(!pt_pic_output(&p));
    assert(pt_pic_read(&p,0x10000)&PT_ACTIVE);
    pt_pic_line(&p,0,0,1);pt_pic_line(&p,0,0,0);assert(!pt_pic_output(&p));
    pt_pic_write(&p,0x200b0,0);assert(pt_pic_output(&p));assert(pt_pic_read(&p,0x200a0)==0x42);
    pt_pic_write(&p,0x200b0,0);assert(!(pt_pic_read(&p,0x10000)&PT_ACTIVE));
    struct timespec a,b;clock_gettime(CLOCK_MONOTONIC,&a);unsigned sum=0;
    for(unsigned i=0;i<200000;i++)sum+=pt_pic_output(&p);
    clock_gettime(CLOCK_MONOTONIC,&b);assert(!sum);
    printf("PASS: OpenPIC contract; idle output %.1f ns/call\n",((b.tv_sec-a.tv_sec)*1e9+b.tv_nsec-a.tv_nsec)/200000);
    pt_pic_destroy(&p);return 0;
}
