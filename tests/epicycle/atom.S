# atom.S: lwarx/stwcx., dcbz, isync in the JIT (Linux ABI). Exit code 7: +1 atomic increment, +2 stwcx. without a reservation does not store, +4 dcbz zeroed 32 bytes
    .globl _start
_start:
    lis 3, buf@ha
    addi 3, 3, buf@l
    li 4, 5
    stw 4, 0(3)
1:  lwarx 5, 0, 3
    addi 5, 5, 1
    stwcx. 5, 0, 3
    bne- 1b
    li 9, 0
    lwz 6, 0(3)
    cmpwi 6, 6
    bne 2f
    ori 9, 9, 1
2:  li 5, 99
    stwcx. 5, 0, 3
    beq 3f
    lwz 6, 0(3)
    cmpwi 6, 6
    bne 3f
    ori 9, 9, 2
3:  addi 7, 3, 64
    li 8, -1
    stw 8, 60(3)
    stw 8, 64(3)
    stw 8, 92(3)
    stw 8, 96(3)
    dcbz 0, 7
    isync
    lwz 8, 64(3)
    lwz 10, 92(3)
    or. 8, 8, 10
    bne 4f
    lwz 8, 60(3)
    lwz 10, 96(3)
    and 8, 8, 10
    cmpwi 8, -1
    bne 4f
    ori 9, 9, 4
4:  mr 3, 9
    li 0, 1
    sc
    .data
    .balign 64
buf: .space 192
