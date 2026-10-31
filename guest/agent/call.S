/* SPDX-License-Identifier: GPL-2.0-or-later
 * op, token, id/ptr, ptr/cap/status, n, reply[3]. Only r0,r3..r6 modified. */
    .text
    .align 2
    .globl _ptagent_call
_ptagent_call:
    lis r0,0x5054
    ori r0,r0,0x4147
    sync
    .long 0x00005041
    stw r4,0(r8)
    stw r5,4(r8)
    stw r6,8(r8)
    blr
