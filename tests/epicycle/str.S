# str.S: lswx/stswx (n from XER, wrap from r31 to r0, n = 0), mffs/mtfsf/mtfsfi, JIT against the interpreter (PT_CHECK=1).
# lswx/stswx are .long words: the LLVM assembler does not know them. Exit code 63: +1 lswx 7 bytes, +2 lswx 13 bytes with wrap, +4 stswx 9 bytes, +8 n = 0 touches nothing,
# +16 mtfsf/mffs (summary VX/FEX computed), +32 mtfsfi
    .globl _start
_start:
    lis 3, src@ha
    addi 3, 3, src@l
    lis 4, dst@ha
    addi 4, 4, dst@l
    li 9, 0
    li 10, 0
    li 11, 7
    mtxer 11
    li 6, -1
    .long 0x7CA3542A          # lswx 5, 3, 10
    lis 7, 0x0102
    ori 7, 7, 0x0304
    cmpw 5, 7
    bne 1f
    lis 7, 0x0506
    ori 7, 7, 0x0700
    cmpw 6, 7
    bne 1f
    ori 9, 9, 1
1:  li 11, 13
    mtxer 11
    li 12, 1
    .long 0x7FC3642A          # lswx 30, 3, 12
    lis 7, 0x0e00
    cmpw 1, 7
    bne 2f
    lis 7, 0x0a0b
    ori 7, 7, 0x0c0d
    cmpw 0, 7
    bne 2f
    lis 7, 0x0607
    ori 7, 7, 0x0809
    cmpw 31, 7
    bne 2f
    ori 9, 9, 2
2:  li 11, 9
    mtxer 11
    lis 5, 0x1112
    ori 5, 5, 0x1314
    lis 6, 0x1516
    ori 6, 6, 0x1718
    lis 7, 0x191a
    ori 7, 7, 0x1b1c
    li 12, 3
    .long 0x7CA4652A          # stswx 5, 4, 12
    lbz 8, 3(4)
    cmpwi 8, 0x11
    bne 3f
    lbz 8, 11(4)
    cmpwi 8, 0x19
    bne 3f
    lbz 8, 12(4)
    cmpwi 8, 0xee
    bne 3f
    lbz 8, 2(4)
    cmpwi 8, 0xee
    bne 3f
    ori 9, 9, 4
3:  li 11, 0
    mtxer 11
    li 5, 77
    .long 0x7CA3542A          # lswx 5, 3, 10
    .long 0x7CA4552A          # stswx 5, 4, 10
    cmpwi 5, 77
    bne 4f
    lbz 8, 0(4)
    cmpwi 8, 0xee
    bne 4f
    ori 9, 9, 8
4:  lis 5, fpv@ha
    addi 5, 5, fpv@l
    lfd 1, 0(5)             # 0x0100_00F8: VXSNAN + all enables
    mtfsf 0xff, 1
    mffs 2
    stfd 2, 8(5)
    lwz 8, 12(5)
    lis 7, 0x6100           # FEX | VX | VXSNAN
    ori 7, 7, 0x00f8
    cmpw 8, 7
    bne 5f
    lwz 8, 8(5)
    lis 7, 0xfff8
    cmpw 8, 7
    bne 5f
    ori 9, 9, 16
5:  lfd 1, 16(5)            # zero
    mtfsf 0xff, 1
    mtfsfi 7, 1             # RN = 1
    mtfsfi 6, 8             # VE
    mffs 2
    stfd 2, 8(5)
    lwz 8, 12(5)
    cmpwi 8, 0x81
    bne 6f
    mtfsfi 7, 0
    mtfsfi 6, 0
    ori 9, 9, 32
6:  mr 3, 9
    li 0, 1
    sc
    .data
    .balign 8
fpv: .long 0, 0x010000f8, 0, 0, 0, 0
src: .byte 1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20
    .balign 4
dst: .byte 0xee,0xee,0xee,0xee,0xee,0xee,0xee,0xee,0xee,0xee,0xee,0xee,0xee,0xee,0xee,0xee
