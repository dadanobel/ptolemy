/* SPDX-License-Identifier: GPL-2.0-or-later */
.text
.p2align 4
.globl _pt_g5_native_start,_pt_g5_native_end
_pt_g5_native_start:
    clrex
1:  ldxr x2,[x0]
    rev x2,x2
    add x2,x2,#1
    rev x2,x2
    stxr w3,x2,[x0]
    cbnz w3,1b
    subs x1,x1,#1
    b.ne 1b
    dmb ish
    brk #1
_pt_g5_native_end:
