# vmsum.S: vmsumshm and vmsumshs, JIT against the interpreter (PT_CHECK=1 PT_VMX_NEON=1 PT_VMX_MHADD=1), on all
# triples of 6 vectors (halfword extremes -32768/32767, mixed signs, words near the 32-bit limits). After each
# operation the result and VSCR (SAT) are folded into r26, and SAT is cleared; the exit code is the xor of its bytes.
    .set N, 6
    .include "fold.inc"
    .globl _start
_start:
    lis 31, vals@ha
    addi 31, 31, vals@l
    lis 30, tmp@ha
    addi 30, 30, tmp@l
    li 26, 0
    vxor 5, 5, 5
    mtvscr 5
    li 28, 0
li_:
    li 27, 0
lj:
    li 25, 0
lk:
    slwi 3, 28, 4
    lvx 1, 31, 3
    slwi 4, 27, 4
    lvx 2, 31, 4
    slwi 5, 25, 4
    lvx 3, 31, 5
    vmsumshm 4, 1, 2, 3
    VFOLD 4
    vmsumshs 4, 1, 2, 3
    VFOLD 4
    vmsumshs 3, 3, 3, 3
    VFOLD 3
    addi 25, 25, 1
    cmpwi 25, N
    blt lk
    addi 27, 27, 1
    cmpwi 27, N
    blt lj
    addi 28, 28, 1
    cmpwi 28, N
    blt li_
    srwi 3, 26, 8
    xor 3, 3, 26
    srwi 4, 26, 16
    xor 3, 3, 4
    srwi 4, 26, 24
    xor 3, 3, 4
    andi. 3, 3, 255
    li 0, 1
    sc
    .data
    .balign 16
vals:
    .short -32768, -32768, 32767, 32767, -32768, 32767, 1, -1
    .short -32768, -32768, -32768, 32767, 32767, -32768, 300, -7
    .long 0x7fffffff, 0x80000000, 0x7ffffff0, 0x80000010
    .long 0xfffffffe, 0x00000002, 0x40000000, 0xc0000000
    .short 1234, -4321, 0, 32767, -1, -1, 12, 34
    .short 0x7f00, 0x00ff, 0x8001, 0x7ffe, 5, 6, -7, 8
tmp: .long 0, 0, 0, 0
