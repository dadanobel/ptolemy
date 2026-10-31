# ops2.S: CR logic, mcrf, sraw, lswi/stswi, bdnzlr, JIT against the interpreter (PT_CHECK=1). The exit code folds
# all results (the JIT and the interpreter must give the same one).
    .globl _start
_start:
    lis 31, buf@ha
    addi 31, 31, buf@l
    li 3, 5
    li 4, -7
    cmpw 0, 3, 4
    cmplw 1, 3, 4
    cmpwi 2, 3, 5
    cmpwi 7, 4, 0
    crand 12, 0, 5
    cror 13, 1, 10
    crxor 14, 2, 9
    crnand 15, 28, 1
    crnor 16, 3, 29
    creqv 17, 30, 10
    crandc 18, 1, 28
    crorc 19, 2, 31
    crxor 6, 6, 6
    creqv 7, 7, 7
    crand 0, 9, 10
    cror 31, 2, 1
    mcrf 5, 0
    mcrf 0, 7
    mfcr 5
    li 6, 0
    li 7, 1
    li 8, 31
    li 9, 32
    li 10, 63
    li 11, 40
    lis 12, 0x8000
    lis 13, 0x8765
    ori 13, 13, 0x4321
    li 14, 1
    li 15, 0
    sraw 16, 12, 6
    adde 16, 16, 15
    sraw 17, 12, 8
    adde 17, 17, 15
    sraw 18, 12, 9
    adde 18, 18, 15
    sraw. 19, 13, 7
    adde 19, 19, 15
    sraw 20, 13, 10
    adde 20, 20, 15
    sraw 21, 14, 11
    adde 21, 21, 15
    sraw 22, 13, 8
    adde 22, 22, 15
    mfcr 23
    lis 3, 0x1122
    ori 3, 3, 0x3344
    lis 4, 0x5566
    ori 4, 4, 0x7788
    lis 5, 0x99aa
    ori 5, 5, 0xbbcc
    stswi 3, 31, 11
    addi 30, 31, 1
    stswi 3, 30, 7
    lswi 24, 31, 3
    lswi 26, 30, 9
    .long 0x7D1F04AA        # lswi 8, 31, 32 (NB=0)
    .long 0x7E1E05AA        # stswi 16, 30, 32
    li 3, 3
    mtctr 3
    bl 1f
    b 2f
1:  mflr 29
    bdnzlr
    bl 1b
2:  li 4, 10                         # bdnzf: leave on CTR=0 or on equality
    mtctr 4
    li 5, 0
4:  addi 5, 5, 1
    cmpwi 5, 7
    bdnzf 2, 4b
    mfctr 6
    li 4, 3
    mtctr 4
    li 7, 0
5:  addi 7, 7, 1
    cmpwi 7, 99
    bdnzf 2, 5b
    mfctr 9
    add 15, 15, 5
    add 15, 15, 6
    add 15, 15, 7
    add 15, 15, 9
    li 3, 0
    li 0, 1
    xor 3, 3, 5
    xor 3, 3, 16
    xor 3, 3, 17
    xor 3, 3, 18
    xor 3, 3, 19
    xor 3, 3, 20
    xor 3, 3, 21
    xor 3, 3, 22
    xor 3, 3, 23
    xor 3, 3, 24
    xor 3, 3, 25
    xor 3, 3, 26
    xor 3, 3, 27
    xor 3, 3, 8
    xor 3, 3, 15
    mfctr 4
    xor 3, 3, 4
    lwz 4, 8(31)
    xor 3, 3, 4
    lwz 4, 20(31)
    xor 3, 3, 4
    srwi 4, 3, 16
    xor 3, 3, 4
    andi. 3, 3, 0xff
    sc
    .data
    .balign 64
buf: .space 128
