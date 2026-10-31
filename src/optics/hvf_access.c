/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "hvf_access.h"
#include <string.h>
typedef struct {hv_vcpu_t vcpu;void *user;PtR2IoResult (*memory)(void *,uint64_t,uint8_t *,unsigned,int);} Context;
static int get_x(void *p,unsigned r,int base,uint64_t *value) {
    Context *c=p;if(r==31)return base?hv_vcpu_get_sys_reg(c->vcpu,HV_SYS_REG_SP_EL1,value)==HV_SUCCESS:(*value=0,1);
    return hv_vcpu_get_reg(c->vcpu,(hv_reg_t)(HV_REG_X0+r),value)==HV_SUCCESS;
}
static int set_x(void *p,unsigned r,uint64_t value) {Context *c=p;return r<31&&hv_vcpu_set_reg(c->vcpu,(hv_reg_t)(HV_REG_X0+r),value)==HV_SUCCESS;}
static int get_q(void *p,unsigned r,uint8_t data[16]) {
    Context *c=p;hv_simd_fp_uchar16_t q;if(hv_vcpu_get_simd_fp_reg(c->vcpu,(hv_simd_fp_reg_t)(HV_SIMD_FP_REG_Q0+r),&q)!=HV_SUCCESS)return 0;memcpy(data,&q,16);return 1;
}
static int set_q(void *p,unsigned r,const uint8_t data[16]) {
    Context *c=p;hv_simd_fp_uchar16_t q;memcpy(&q,data,16);return hv_vcpu_set_simd_fp_reg(c->vcpu,(hv_simd_fp_reg_t)(HV_SIMD_FP_REG_Q0+r),q)==HV_SUCCESS;
}
static PtR2IoResult memory(void *p,uint64_t va,uint8_t *data,unsigned n,int store) {Context *c=p;return c->memory(c->user,va,data,n,store);}
OpticsCpuMemoryResult optics_hvf_guard_step(hv_vcpu_t vcpu,uint64_t pc,uint32_t in,PtR2IoResult (*access)(void *,uint64_t,uint8_t *,unsigned,int),void *user) {
    OpticsA64Memory decoded;if(!access||(pc&3)||!optics_a64_memory_decode(in,&decoded))return OPTICS_CPU_UNSUPPORTED;
    Context context={vcpu,user,access};OpticsCpuMemoryOps ops={&context,get_x,set_x,get_q,set_q,memory};
    OpticsCpuMemoryResult r=optics_a64_memory_step(&decoded,&ops);
    if(r==OPTICS_CPU_DONE&&hv_vcpu_set_reg(vcpu,HV_REG_PC,pc+4)!=HV_SUCCESS)return OPTICS_CPU_FAILED;
    return r;
}
