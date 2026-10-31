/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Decode coverage only. Architectural results are checked by g4-system and
 * altivec-compare; accepting an opcode is not proof of every corner case. */
#include "ppc.h"
#include <stdio.h>
#include <stdlib.h>
extern int trace_unimpl;
int sys_call(CPU *c){(void)c;return 0;}
int main(void) {
    uint8_t record[8],out[12];trace_unimpl=0;
    while(fread(record,1,sizeof record,stdin)==sizeof record) {
        CPU *c=calloc(1,sizeof *c);if(!c)return 2;
        c->mem=calloc(1,0x10000);if(!c->mem){free(c);return 2;}
        c->preg[0]=(PRegion){0,0x10000,c->mem,0};c->npreg=1;c->sys=2;
        c->spr[287]=be32(record+4);c->msr=MSR_FP|MSR_VEC;c->pc=0x100;c->xer_bc=1;
        for(unsigned i=0;i<32;i++){c->gpr[i]=0x1000;c->fpr[i]=1.0;}
        wbe32(c->mem+0x100,be32(record));(void)interp_step(c);
        wbe32(out,c->pc);wbe32(out+4,c->spr[SPR_SRR1]);wbe32(out+8,c->halted);
        if(fwrite(out,1,sizeof out,stdout)!=sizeof out)return 2;
        free(c->mem);free(c);
    }
    return ferror(stdin)?2:0;
}
