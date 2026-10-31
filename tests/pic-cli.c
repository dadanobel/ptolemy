/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "platform.h"
#include <stdio.h>
int main(void) {
    PtPic p; pt_pic_init(&p); char op; unsigned a,b;
    while(scanf(" %c",&op)==1) {
        if(op=='r' && scanf("%x",&a)==1) printf("%08x\n",pt_pic_read(&p,a));
        else if(op=='w' && scanf("%x %x",&a,&b)==2) pt_pic_write(&p,a,b);
        else if(op=='l' && scanf("%x %x",&a,&b)==2) pt_pic_line(&p,a,0,b);
        else return 2;
        fflush(stdout);
    }
    pt_pic_destroy(&p);
}
