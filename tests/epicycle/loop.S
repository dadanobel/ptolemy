# loop.S: a loop inside one JIT block whose side exit (beq) comes before the write to the cached r29.
# The exit must also store what earlier iterations wrote after it. Linux ABI; exit code = r29 = 3.
    .globl _start
_start:
    li 29, 0
1:  cmpwi 29, 3
    beq 2f
    addi 29, 29, 1
    b 1b
2:  mr 3, 29
    li 0, 1
    sc
