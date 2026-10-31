/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Test real JIT init, not a replacement parser. No generated instruction is executed. */
/* Run by tools/build-perf.sh --run-unit; built with -Iepicycle. */
#include "jit.c"
#include <assert.h>
int sys_call(CPU *c){(void)c;abort();}
int main(void){
    uint32_t *code=calloc(1,64u<<20);JC *cache=calloc(1,sizeof(JC)*(1u<<16));assert(code&&cache);
    CPU c={0};c.mem=calloc(1,65536);assert(c.mem);c.msr=MSR_FP;
    jit_vm_buf=code;jit_vm_size=64u<<20;jit_vm_jc=cache;
    unsetenv("PT_CHECK");unsetenv("PT_MAXBLK");unsetenv("PT_INTERP_BATCH");
    const char *names[]={"PT_NOLINK","PT_NOJC","PT_FP_OLD","PT_FMA_FAST"};
    const char *values[]={NULL,"0","1"};
    for(unsigned k=0;k<3;k++){
        for(unsigned n=0;n<4;n++){if(values[k])setenv(names[n],values[k],1);else unsetenv(names[n]);}
        init(&c);int expected=k==2;
        assert(!!nolink==expected && !!nojc==expected && !!fp_old==expected && !!fma_fast==expected);
    }
    puts("CPU config: unset/0/1 through real init PASS");free(c.mem);free(cache);free(code);return 0;
}
