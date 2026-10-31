/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "pointer_calibration.h"
#include "pt_agent.h"
#include <assert.h>
#include <stdio.h>
static int ptagent_call(uint32_t op,uint32_t t,uint32_t job,uint32_t b,uint32_t n,uint32_t r[3]){(void)op;(void)t;(void)job;(void)b;(void)n;(void)r;return 0;}
#include "../guest/agent/input_builtin.h"
static void *selected;static int ambiguous;
static int32_t count(CFIndex *n){*n=ambiguous?3:2;return 0;}
static int32_t at(CFIndex i,void **p){*p=(void *)(i+1);return 0;}
static int32_t property(void *p,uint32_t which,const void **v){*v=which==5?(p==(void *)1?CFSTR("U.S."):CFSTR("Russian")):(p==(void *)1?CFSTR("en_US"):CFSTR("ru_RU"));return 0;}
static int32_t choose(void *p){selected=p;return 0;}
int main(void){
    PtPointerCalibration p;pt_pointer_calibration_reset(&p);assert(!p.valid&&pt_pointer_calibration_apply(&p,0,0.5)==0.5);
    for(unsigned mode=0;mode<2;mode++){pt_pointer_calibration_reset(&p);for(unsigned i=0;i<8;i++){double x=.15+i*.09,y=.8-i*.08;pt_pointer_calibration_add(&p,x,y,mode?x*1.18-.09:x,mode?y*1.18-.09:y,1024,768);}assert(p.valid);for(unsigned j=0;j<3;j++){double raw=.2+j*.2,screen=mode?raw*1.18-.09:raw;assert(fabs(pt_pointer_calibration_apply(&p,0,screen)-raw)<1e-8);}}
    pt_pointer_calibration_reset(&p);for(unsigned i=0;i<8;i++)pt_pointer_calibration_add(&p,.4,.4,.4,.4,1024,768);assert(!p.valid);
    pt_pointer_calibration_reset(&p);for(unsigned i=0;i<8;i++)pt_pointer_calibration_add(&p,.15+i*.09,.8-i*.08,.1+i*.1,.7-(i%2)*.2,1024,768);assert(!p.valid);
    pt_input_api.count=count;pt_input_api.at=at;pt_input_api.property=property;pt_input_api.select=choose;
    char *us[]={"/Ptolemy/input","layout","com.apple.keylayout.ABC","en",NULL},*ru[]={"/Ptolemy/input","layout","CustomRussian","ru",NULL};unsigned status;
    assert(pt_input_builtin(1,1,us,&status)&&!status&&selected==(void *)1);assert(pt_input_builtin(1,1,ru,&status)&&!status&&selected==(void *)2);
    ambiguous=1;selected=NULL;assert(pt_input_builtin(1,1,ru,&status)&&status&&!selected);
    us[2]="Missing";us[3]="zz";assert(pt_input_builtin(1,1,us,&status)&&status&&!selected);
    pt_input_guest_begin(0);pt_input_guest_tick(1,0);
    puts("PASS: both Tiger affine mappings, insufficient spread and nonlinear negatives, exact KL name, unique language and ambiguous/missing refusal");
}
