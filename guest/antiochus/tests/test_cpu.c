/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <stdio.h>
#include "../src/cpu.c"
void ant_text(const char *k,const char *v){(void)k;(void)v;}
void ant_number(const char *k,double v){(void)k;(void)v;}
int main(void) {
    const char *ids[]={"cpu-crc","cpu-sort","cpu-matrix","cpu-fft","cpu-stream-small","cpu-stream-large","cpu-chase","vmx-saturate","vmx-permute","vmx-madd","cpu-matrix-sticky"};
    unsigned i;
    for(i=0;i<sizeof ids/sizeof *ids;++i) {
        AntWork w;Data *d;assert(ant_cpu(ids[i],&w));d=w.data;w.run(w.data);assert(w.check(w.data));
        if(i==0)d->result^=1;else if(i==1)d->b[10]^=1;else if(i==2 || i==10)d->z[10]+=1;
        else if(i==3)d->y[127]+=1;else if(i==6)d->sum+=1;else if(i==9)((float *)d->c)[10]+=1;else d->c[10]^=0x80808080u;
        assert(!w.check(w.data));w.destroy(w.data);printf("%s: pass and corrupted-result rejection\n",ids[i]);
    }
    return 0;
}
