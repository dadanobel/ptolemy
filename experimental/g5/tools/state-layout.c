/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "g5_cpu.h"
#include <stddef.h>
#include <stdio.h>
_Static_assert(sizeof(((PtG5Cpu *)0)->r.pc) == 8, "G5 PC must be 64-bit");
_Static_assert(sizeof(((PtG5Cpu *)0)->r.gpr[0]) == 8, "G5 GPR storage remains 64-bit in SF=0");
#define FIELD(name)                                                                          \
    printf("  \"%s\": {\"offset\": %zu, \"bytes\": %zu},\n", #name, offsetof(PtG5Cpu, name), \
           sizeof(((PtG5Cpu *)0)->name))
int main(void) {
    puts("{");
    FIELD(r.gpr);
    FIELD(r.pc);
    FIELD(r.lr);
    FIELD(r.ctr);
    FIELD(r.msr);
    FIELD(r.cr);
    FIELD(r.xer);
    FIELD(fpr);
    FIELD(vr);
    FIELD(fpscr);
    FIELD(srr0);
    FIELD(srr1);
    FIELD(dar);
    FIELD(dsisr);
    FIELD(mmu);
    printf("  \"size\": %zu, \"alignment\": %zu\n}\n", sizeof(PtG5Cpu), _Alignof(PtG5Cpu));
}
