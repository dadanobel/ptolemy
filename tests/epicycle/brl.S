# brl.S: conditional bcctrl and bclrl on a CR bit (BO 12: if set, BO 4: if clear), taken and not taken: LR is the next
# instruction either way, a taken one calls its target, which returns there. JIT against the interpreter (-i, and
# PT_CHECK=1). 8 rounds compare the round number with 3 into cr2; LR, the calls and the round fold into r26; r26 goes to
# stdout (4 bytes), the exit code is the xor of its bytes.
    .include "fold.inc"
    .globl _start
_start:
    li 26, 0
    li 28, 0
    lis 30, callee@ha
    addi 30, 30, callee@l
round:
    cmpwi 2, 28, 3
    mtctr 30
    bcctrl 12, 10                   # if cr2.eq
1:  mflr 9
    FOLD 9
    mtctr 30
    bcctrl 4, 8                     # if not cr2.lt
2:  mflr 9
    FOLD 9
    mtlr 30
    bclrl 12, 9                     # if cr2.gt
3:  mflr 9
    FOLD 9
    mtlr 30
    bclrl 4, 10                     # if not cr2.eq
4:  mflr 9
    FOLD 9
    FOLD 28
    addi 28, 28, 1
    cmpwi 28, 8
    blt round
    lis 31, out@ha
    addi 31, 31, out@l
    stw 26, 0(31)
    li 0, 4
    li 3, 1
    mr 4, 31
    li 5, 4
    sc
    srwi 3, 26, 8
    xor 3, 3, 26
    srwi 4, 26, 16
    xor 3, 3, 4
    srwi 4, 26, 24
    xor 3, 3, 4
    andi. 3, 3, 255
    li 0, 1
    sc
callee:                             # called: count it, return to the instruction after the branch
    addi 26, 26, 0x101
    blr
    .data
    .balign 4
out: .long 0
