/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <stdio.h>
#include "../src/veclib.c"
void ant_text(const char *k,const char *v){(void)k;(void)v;}
void ant_number(const char *k,double v){(void)k;(void)v;}
int main(void) {
    AntWork w;LibraryFFT *d;assert(ant_veclib(&w));d=w.data;
    w.run(d);assert(w.check(d));d->output.realp[127]+=1;assert(!w.check(d));
    w.run(d);d->output.imagp[731]=NAN;assert(!w.check(d));w.destroy(d);
    puts("vecLib FFT: pass, corrupt-bin and NaN rejection");return 0;
}
