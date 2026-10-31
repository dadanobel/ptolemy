# fpa.S: FP arithmetic, JIT against the interpreter (PT_CHECK=1): fadd fsub fmul fdiv fmadd fmsub fnmadd fnmsub
# (double and single), frsp, fctiw(z), fsel, fcmpu/fcmpo on all pairs of 16 values (ordinary, +-0, +-Inf, QNaN, SNaN,
# double and single denormals, overflow edges), three passes: RN=0; RN=1; VE=1 (special cases go to the interpreter).
# The results and FPSCR after every operation are folded into r26; the exit code is the xor of its bytes.
    .set N, 16
    .macro CK fr
    stfd \fr, 0(30)
    lwz 9, 0(30)
    lwz 10, 4(30)
    add 26, 26, 9
    rotlwi 26, 26, 5
    xor 26, 26, 10
    mffs 0
    stfd 0, 0(30)
    lwz 9, 4(30)
    add 26, 26, 9
    rotlwi 26, 26, 3
    .endm
    .macro CR
    mfcr 9
    add 26, 26, 9
    rotlwi 26, 26, 7
    .endm
    .globl _start
_start:
    lis 31, vals@ha
    addi 31, 31, vals@l
    lis 30, tmp@ha
    addi 30, 30, tmp@l
    li 26, 0
    li 29, 0
pass:
    li 28, 0
li_:
    li 27, 0
lj:
    slwi 3, 28, 3
    lfdx 1, 31, 3
    slwi 4, 27, 3
    lfdx 2, 31, 4
    add 5, 28, 27
    andi. 5, 5, N-1
    slwi 5, 5, 3
    lfdx 3, 31, 5
    fadd 4, 1, 2
    CK 4
    fsub 4, 1, 2
    CK 4
    fmul 4, 1, 2
    CK 4
    fdiv 4, 1, 2
    CK 4
    fmadd 4, 1, 2, 3
    CK 4
    fmsub 4, 1, 2, 3
    CK 4
    fnmadd 4, 1, 2, 3
    CK 4
    fnmsub 4, 1, 2, 3
    CK 4
    fadds 4, 1, 2
    CK 4
    fsubs 4, 1, 2
    CK 4
    fmuls 4, 1, 2
    CK 4
    fdivs 4, 1, 2
    CK 4
    fmadds 4, 1, 2, 3
    CK 4
    fmsubs 4, 1, 2, 3
    CK 4
    fnmadds 4, 1, 2, 3
    CK 4
    fnmsubs 4, 1, 2, 3
    CK 4
    frsp 4, 1
    CK 4
    fctiwz 4, 2
    CK 4
    fctiw 4, 2
    CK 4
    fsel 4, 1, 2, 3
    CK 4
    fcmpu 1, 1, 2
    CR
    fcmpo 6, 2, 3
    CR
    addi 27, 27, 1
    cmpwi 27, N
    blt lj
    addi 28, 28, 1
    cmpwi 28, N
    blt li_
    addi 29, 29, 1
    cmpwi 29, 1
    beq 1f
    cmpwi 29, 2
    beq 2f
    srwi 3, 26, 8
    xor 3, 3, 26
    srwi 4, 26, 16
    xor 3, 3, 4
    srwi 4, 26, 24
    xor 3, 3, 4
    andi. 3, 3, 255
    li 0, 1
    sc
1:  mtfsfi 7, 1
    b pass
2:  mtfsfi 7, 0
    mtfsfi 6, 8
    b pass
    .data
    .balign 8
vals:
    .quad 0x3FF0000000000000, 0xC004000000000000, 0x4008000000000000, 0x3FB999999999999A
    .quad 0x3FD5555555555555, 0x0000000000000000, 0x8000000000000000, 0x7FF0000000000000
    .quad 0xFFF0000000000000, 0x7FF8000000000000, 0x7FF4000000000000, 0x0000000000000001
    .quad 0x7FE1CCF385EBC8A0, 0x47EFFFFFE0000000, 0x36A0000000000000, 0x3FF8000000000000
tmp: .quad 0
