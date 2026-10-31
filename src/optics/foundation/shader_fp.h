/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef PT_R2_SHADER_FP_H
#define PT_R2_SHADER_FP_H
#include "shader_ir.h"
#include <math.h>
#include <string.h>
/* FTZ is an operation-level contract, not a final-output comparison rule. */
static inline float pt_r2_fp_in(float x,unsigned profile)
{
    uint32_t b; memcpy(&b,&x,4);
    if(profile==PT_R2_SHADER_FP32_FTZ0 && (b&0x7f800000u)==0 && (b&0x007fffffu)) b=0;
    memcpy(&x,&b,4);return x;
}
static inline float pt_r2_fp_out(float x,unsigned profile)
{
    uint32_t b;memcpy(&b,&x,4);
    if(profile==PT_R2_SHADER_FP32_FTZ0 && (b&0x7f800000u)==0) b=0;
    memcpy(&x,&b,4);return x;
}
static inline float pt_r2_fp_add(float a,float b,unsigned p){return pt_r2_fp_out(pt_r2_fp_in(a,p)+pt_r2_fp_in(b,p),p);}
static inline float pt_r2_fp_sub(float a,float b,unsigned p){return pt_r2_fp_out(pt_r2_fp_in(a,p)-pt_r2_fp_in(b,p),p);}
static inline float pt_r2_fp_mul(float a,float b,unsigned p){return pt_r2_fp_out(pt_r2_fp_in(a,p)*pt_r2_fp_in(b,p),p);}
static inline float pt_r2_fp_div(float a,float b,unsigned p){return pt_r2_fp_out(pt_r2_fp_in(a,p)/pt_r2_fp_in(b,p),p);}
static inline float pt_r2_fp_floor(float a,unsigned p){return pt_r2_fp_out(floorf(pt_r2_fp_in(a,p)),p);}
static inline float pt_r2_fp_sqrt(float a,unsigned p){return pt_r2_fp_out(sqrtf(pt_r2_fp_in(a,p)),p);}
static inline float pt_r2_fp_exp2(float a,unsigned p){return pt_r2_fp_out(exp2f(pt_r2_fp_in(a,p)),p);}
static inline float pt_r2_fp_log2(float a,unsigned p){return pt_r2_fp_out(log2f(pt_r2_fp_in(a,p)),p);}
#endif
