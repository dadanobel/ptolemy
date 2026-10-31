# fp.S: lfs/lfd/stfs/stfd (and the x, u forms), fmr/fneg/fabs/fnabs, mfxer/mtxer, JIT against the interpreter (PT_CHECK=1).
# Values: normal, denormal, +-0, +-Inf, QNaN, SNaN (single and double). The exit code is a hash of the output memory.
    .globl _start
_start:
    lis 31, src@ha
    addi 31, 31, src@l
    lis 30, dst@ha
    addi 30, 30, dst@l
    li 29, 0
    li 28, 8
1:  slwi 3, 29, 2
    lfsx 1, 31, 3
    slwi 4, 29, 3
    stfdx 1, 30, 4
    addi 29, 29, 1
    cmpw 29, 28
    blt 1b
    addi 27, 31, 32
    li 29, 0
2:  slwi 4, 29, 3
    lfdx 2, 27, 4
    fneg 3, 2
    fabs 4, 2
    fnabs 5, 2
    fmr 6, 3
    addi 5, 30, 64
    slwi 6, 29, 4
    add 5, 5, 6
    stfs 2, 0(5)
    stfs 3, 4(5)
    stfd 4, 8(5)
    addi 29, 29, 1
    cmpw 29, 28
    blt 2b
    addi 26, 31, 32
    lfdu 7, 8(26)
    lfsu 8, 4(31)
    stfdu 7, 8(30)
    stfsu 8, 4(30)
    addi 25, 31, 100
    li 24, 4
    lfsux 9, 25, 24
    stfdux 9, 30, 24
    mfxer 10
    lis 11, 0x6000
    ori 11, 11, 0x15
    mtxer 11
    mfxer 12
    addc 13, 11, 11
    mfxer 14
    mtxer 10
    lis 3, dst@ha
    addi 3, 3, dst@l
    li 4, 0
    li 5, 64
    mtctr 5
3:  lwz 6, 0(3)
    rotlwi 4, 4, 5
    xor 4, 4, 6
    addi 3, 3, 4
    bdnz 3b
    xor 4, 4, 12
    xor 4, 4, 14
    srwi 5, 4, 16
    xor 4, 4, 5
    srwi 5, 4, 8
    xor 3, 4, 5
    andi. 3, 3, 0xff
    li 0, 1
    sc
    .data
    .balign 8
src: .long 0x3f800000, 0x00000001, 0x80000000, 0x7f800000, 0xff800000, 0x7fc00001, 0x7f800001, 0xc2f6e979
     .quad 0x3ff0000000000000, 0x0000000000000001, 0x8000000000000000, 0x7ff0000000000000, 0x36a0000000000000, 0x7ff8000000000001, 0x7ff0000000000001, 0xc05edd2f1a9fbe77
     .long 0x12345678, 0x3e99999a, 0x40490fdb
dst: .space 256
