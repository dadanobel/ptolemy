/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "ppc.h"
#include <assert.h>
#include <stdio.h>
static unsigned writes;
static void mmio(CPU *c,uint32_t a,uint8_t *b,int n,int write) {
    (void)c;
    assert(a==0xfff00100 && n==4 && write && be32(b)==0x12345678);
    writes++; /* ROM store is ignored, as by the firmware board. */
}
void ppc_exception(CPU *c,uint32_t vector,uint32_t pc,uint32_t flags) {
    (void)c; (void)vector; (void)pc; (void)flags; assert(!"unexpected fault");
}
int main(void) {
    CPU c={0}; uint8_t ram[4096]={0}, rom[4096]={0};
    c.preg[0]=(PRegion){0,4096,ram,0};
    c.preg[1]=(PRegion){0xfff00000,4096,rom,1};
    c.npreg=2; c.env_mmio=mmio;
    assert(!ppc_st(&c,0x100,4,0x12345678) && be32(ram+0x100)==0x12345678);
    assert(!ppc_st(&c,0xfff00100,4,0x12345678) && !be32(rom+0x100) && writes==1);
    uint64_t value=0;
    wbe32(rom+0x100,0x87654321);
    assert(!ppc_ld(&c,0xfff00100,4,&value) && value==0x87654321);
    assert(!pmem(&c,0xffffffff,4) && !pmem_write(&c,4095,4));
    assert(!pmem(&c,0xffeffffc,4) && !pmem_write(&c,0xffeffffc,4));
    puts("PASS: RAM stores, ROM read-only interpreter path, physical bounds");
}
