# so.S: XER[SO]=1 in the JIT (blocks translated for SO=1), JIT against the interpreter (-i, and PT_CHECK=1; also with
# PT_CR_FUSE=1, PT_NOLINK=1, PT_NOJC=1). 16 rounds over operand pairs, SO set by mtxer on odd rounds: compares into
# several fields, record forms (one overwriting another's cr0), the OE forms, divwo/divwuo too (an overflow in an SO=0
# block switches to the SO=1 block mid-block), a compare fused with its branch in a loop (the field formed at the side
# exit), stwcx. with and without a reservation, mfcr + rlwinm., bso, an indirect call into another page from both kinds
# of block (jump cache keys), a direct jump into another page, mcrxr, an SO switch onto a target the jump cache also
# holds for the other SO. CR and XER are folded into r26 after each step; r26 goes to stdout (4 bytes), the exit code is
# the xor of its bytes.
    .include "fold.inc"
    .macro STATE
    mfcr 9
    FOLD 9
    mfxer 9
    FOLD 9
    .endm
    .macro WORLD                    # XER: SO on odd rounds, CA when round & 4
    rlwinm 3, 28, 31, 0, 0
    rlwinm 9, 28, 27, 2, 2
    or 3, 3, 9
    mtxer 3
    .endm
    .globl _start
_start:
    lis 31, data@ha
    addi 31, 31, data@l
    li 26, 0
    li 28, 0
round:
    slwi 9, 28, 3
    add 9, 9, 31
    lwz 4, 0(9)
    lwz 5, 4(9)
    WORLD
    cmpw 4, 5
    cmplw 1, 4, 5
    cmpwi 7, 4, -5
    cmplwi 6, 5, 7
    STATE
    add. 6, 4, 5
    and. 7, 4, 5
    STATE
    rlwinm. 8, 4, 0, 0, 0
    STATE
    addic. 10, 4, 1
    STATE
    andi. 10, 4, 0
    STATE
    or. 6, 4, 4
    and. 7, 5, 5
    STATE
    addo. 6, 4, 5
    FOLD 6
    STATE
    WORLD
    subfco 7, 4, 5
    FOLD 7
    STATE
    WORLD
    subfco. 7, 5, 4
    FOLD 7
    STATE
    WORLD
    addco. 6, 4, 5
    FOLD 6
    STATE
    WORLD
    nego. 8, 4
    FOLD 8
    STATE
    WORLD
    mullwo. 8, 4, 5
    FOLD 8
    STATE
    WORLD
    addeo. 6, 4, 5
    subfeo 7, 4, 5
    FOLD 6
    FOLD 7
    STATE
    WORLD
    addzeo. 6, 4
    subfzeo. 7, 5
    addmeo 8, 4
    subfmeo. 10, 5
    FOLD 6
    FOLD 7
    FOLD 8
    FOLD 10
    STATE
    WORLD
    subfo. 6, 4, 5
    FOLD 6
    STATE
    WORLD
    neg 9, 5                        # divwo: /0 (round 0), INT_MIN/-1 (round 2)
    divwo. 8, 4, 9
    FOLD 8
    STATE
    WORLD
    neg 9, 5
    divwo 9, 4, 9                   # rD = rB
    FOLD 9
    STATE
    WORLD
    divwuo. 8, 4, 5
    FOLD 8
    STATE
    WORLD
    li 12, 5
fused:
    cmpwi 12, 2
    blt fused_out
    addic. 12, 12, -1
    b fused
fused_out:
    STATE
    lwarx 6, 0, 31
    stwcx. 6, 0, 31
    STATE
    stwcx. 6, 0, 31
    STATE
    mfcr 6
    rlwinm. 6, 6, 4, 28, 31
    FOLD 6
    STATE
    cmpw 3, 4, 5
    bso 3, so_set
    addi 26, 26, 7
so_set:
    STATE
    lis 9, so_after@ha              # so_after through the jump cache with SO=0, then after an mtxer setting SO=1: the
    addi 9, 9, so_after@l           # switch must take the SO=1 block, not the SO=0 cell
    mtctr 9
    li 3, 0
    mtxer 3
    li 11, 1
    bctrl
    li 11, 0
    lis 3, 0x8000
    mtxer 3
so_after:
    cmpw 6, 4, 5
    mfcr 9
    FOLD 9
    cmpwi 7, 11, 0
    beq 7, so_done
    blr
so_done:
    STATE
    lis 9, far_call@ha
    addi 9, 9, far_call@l
    mtctr 9
    bctrl
    STATE
    b far_jump
back:
    STATE
    mcrxr 4
    STATE
    addi 28, 28, 1
    cmpwi 28, 16
    blt round
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
    .balign 4096
far_call:                           # another page, reached through the jump cache from SO=0 and SO=1 blocks
    cmpw 2, 4, 5
    add. 6, 5, 4
    mfxer 9
    FOLD 9
    mfcr 9
    FOLD 9
    blr
far_jump:
    cmplw 5, 5, 4
    subf. 7, 4, 5
    STATE
    b back
    .data
    .balign 16
data:                               # r4, r5 per round
    .long 0, 0
    .long 0x7fffffff, 1
    .long 0x80000000, 1
    .long 5, 5
    .long -5, -7
    .long 0x10000, 0x10000
    .long 0x80000000, 0x80000000
    .long 1, 2
    .long -1, 1
    .long 0, 0x80000000
    .long 0x7fffffff, 0x7fffffff
    .long 0xffff, 0x8001
    .long -2, 0x7fffffff
    .long 3, -3
    .long 0x40000000, 2
    .long 0xfffffffe, 0xffffffff
