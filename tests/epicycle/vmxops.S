# vmxops.S: vmsumubm vmsummbm vmsumuhm vmsumuhs, vsum4ubs vsum4sbs vsum4shs vsum2sws vsumsws, vrlb vrlh vrlw, vpkpx, and
# vctuxs vctsxs at UIMM 0/1/5/16/31 with VSCR.NJ clear and set: JIT against the interpreter (PT_G4=1 PT_VMX_NEON=1
# PT_VMX_INT2=1, with PT_CHECK=1) on all pairs and triples of 12 vectors (byte, halfword and word extremes, float
# specials), and the conversions also on 52 vectors with one lane at or next to a saturation threshold, or a NaN,
# infinity, zero or denormal, among lanes that never saturate (SAT is one bit for all four lanes). As vmsum.S: each
# result and VSCR are folded into r26, then VSCR is reset; r26 goes to stdout (4 bytes), the exit code is the xor of
# its bytes.
    .set N, 12
    .set M, 52
    .include "fold.inc"
    .macro CVT k
    vctuxs 4, 1, \k
    VFOLD 4, 7
    vctsxs 4, 1, \k
    VFOLD 4, 7
    mtvscr 8
    vctuxs 4, 1, \k
    VFOLD 4, 8
    vctsxs 4, 1, \k
    VFOLD 4, 7
    .endm
    .macro LOAD vr, idx
    slwi 3, \idx, 4
    lvx \vr, 31, 3
    .endm
    .globl _start
_start:
    lis 31, vals@ha
    addi 31, 31, vals@l
    lis 30, tmp@ha
    addi 30, 30, tmp@l
    lis 3, nj@ha
    addi 3, 3, nj@l
    lvx 8, 0, 3
    vxor 7, 7, 7
    mtvscr 7
    li 26, 0
    li 28, 0
li_:
    LOAD 1, 28
    CVT 0
    CVT 1
    CVT 5
    CVT 16
    CVT 31
    vctuxs 1, 1, 3
    VFOLD 1, 7
    li 27, 0
lj:
    LOAD 1, 28
    LOAD 2, 27
    vsum4ubs 4, 1, 2
    VFOLD 4, 7
    vsum4sbs 4, 1, 2
    VFOLD 4, 7
    vsum4shs 4, 1, 2
    VFOLD 4, 7
    vsum2sws 4, 1, 2
    VFOLD 4, 7
    vsumsws 4, 1, 2
    VFOLD 4, 7
    vrlb 4, 1, 2
    VFOLD 4, 7
    vrlh 4, 1, 2
    VFOLD 4, 7
    vrlw 4, 1, 2
    VFOLD 4, 7
    vpkpx 4, 1, 2
    VFOLD 4, 7
    vpkpx 2, 2, 1
    VFOLD 2, 7
    LOAD 2, 27
    li 25, 0
lk:
    LOAD 1, 28
    LOAD 2, 27
    LOAD 3, 25
    vmsumubm 4, 1, 2, 3
    VFOLD 4, 7
    vmsummbm 4, 1, 2, 3
    VFOLD 4, 7
    vmsumuhm 4, 1, 2, 3
    VFOLD 4, 7
    vmsumuhs 4, 1, 2, 3
    VFOLD 4, 7
    vmsumubm 3, 3, 3, 3
    VFOLD 3, 7
    addi 25, 25, 1
    cmpwi 25, N
    blt lk
    vsum2sws 2, 2, 2
    VFOLD 2, 7
    vrlh 1, 1, 1
    VFOLD 1, 7
    vsumsws 1, 2, 1
    VFOLD 1, 7
    addi 27, 27, 1
    cmpwi 27, N
    blt lj
    addi 28, 28, 1
    cmpwi 28, N
    blt li_
    lis 29, cvts@ha
    addi 29, 29, cvts@l
    li 28, 0
lc:
    slwi 3, 28, 4
    lvx 1, 29, 3
    CVT 0
    CVT 1
    CVT 5
    CVT 16
    CVT 31
    addi 28, 28, 1
    cmpwi 28, M
    blt lc
    stw 26, 0(30)
    li 0, 4
    li 3, 1
    mr 4, 30
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
    .data
    .balign 16
vals:
    .long 0x7fffffff, 0x80000000, 0xffffffff, 0x00000000
    .long 0x7f800000, 0xff800000, 0x7fc00000, 0x7f800001
    .long 0x00000001, 0x807fffff, 0x4f000000, 0xcf000000
    .long 0x4f800000, 0x4effffff, 0x4f7fffff, 0xcf000001
    .long 0xbf800000, 0xbf000000, 0x3f000000, 0xbfc00000
    .long 0xbd000000, 0xbcffffff, 0x4c800000, 0x4d000000
    .long 0x47000000, 0x47800000, 0xb7800000, 0xb0000000
    .long 0x40000000, 0x3f800000, 0x7f7fffff, 0xff7fffff
    .long 0x4e800000, 0xce800000, 0xce800001, 0xbeffffff
    .long 0xcc800000, 0xcc800001, 0x4e7fffff, 0x80000001
    .long 0x12345678, 0x9abcdef0, 0x0f1e2d3c, 0xa5c3e1f7
    .long 0x3fa00000, 0xc2f6e979, 0x4e6e6b28, 0xceffffff
cvts:   # vctuxs/vctsxs at UIMM 0 1 5 16 31: 2^(32-k), 2^(31-k), -2^-k, -2^(31-k) and their neighbours; specials
    .long 0x4f800000, 0x2b800000, 0x3e800000, 0x00000000
    .long 0x2b800000, 0x4f7fffff, 0x00000000, 0x2b800000
    .long 0x3e800000, 0x00000000, 0x4f000000, 0x3e800000
    .long 0x00000000, 0x2b800000, 0x3e800000, 0x4effffff
    .long 0xbf800000, 0x3e800000, 0x00000000, 0x2b800000
    .long 0x3e800000, 0xbf7fffff, 0x2b800000, 0x3e800000
    .long 0x00000000, 0x2b800000, 0xcf000000, 0x00000000
    .long 0x2b800000, 0x3e800000, 0x00000000, 0xcf000001
    .long 0x4e800000, 0x00000000, 0x2b800000, 0x3e800000
    .long 0x00000000, 0x4e7fffff, 0x3e800000, 0x00000000
    .long 0x2b800000, 0x3e800000, 0xbf000000, 0x2b800000
    .long 0x3e800000, 0x00000000, 0x2b800000, 0xbeffffff
    .long 0xce800000, 0x2b800000, 0x3e800000, 0x00000000
    .long 0x2b800000, 0xce800001, 0x00000000, 0x2b800000
    .long 0x3e800000, 0x00000000, 0x4d000000, 0x3e800000
    .long 0x00000000, 0x2b800000, 0x3e800000, 0x4cffffff
    .long 0x4c800000, 0x3e800000, 0x00000000, 0x2b800000
    .long 0x3e800000, 0x4c7fffff, 0x2b800000, 0x3e800000
    .long 0x00000000, 0x2b800000, 0xbd000000, 0x00000000
    .long 0x2b800000, 0x3e800000, 0x00000000, 0xbcffffff
    .long 0xcc800000, 0x00000000, 0x2b800000, 0x3e800000
    .long 0x00000000, 0xcc800001, 0x3e800000, 0x00000000
    .long 0x2b800000, 0x3e800000, 0x47800000, 0x2b800000
    .long 0x3e800000, 0x00000000, 0x2b800000, 0x477fffff
    .long 0x47000000, 0x2b800000, 0x3e800000, 0x00000000
    .long 0x2b800000, 0x46ffffff, 0x00000000, 0x2b800000
    .long 0x3e800000, 0x00000000, 0xb7800000, 0x3e800000
    .long 0x00000000, 0x2b800000, 0x3e800000, 0xb77fffff
    .long 0xc7000000, 0x3e800000, 0x00000000, 0x2b800000
    .long 0x3e800000, 0xc7000001, 0x2b800000, 0x3e800000
    .long 0x00000000, 0x2b800000, 0x40000000, 0x00000000
    .long 0x2b800000, 0x3e800000, 0x00000000, 0x3fffffff
    .long 0x3f800000, 0x00000000, 0x2b800000, 0x3e800000
    .long 0x00000000, 0x3f7fffff, 0x3e800000, 0x00000000
    .long 0x2b800000, 0x3e800000, 0xb0000000, 0x2b800000
    .long 0x3e800000, 0x00000000, 0x2b800000, 0xafffffff
    .long 0xbf800001, 0x2b800000, 0x3e800000, 0x00000000
    .long 0x2b800000, 0x00000000, 0x00000000, 0x2b800000
    .long 0x3e800000, 0x00000000, 0x80000000, 0x3e800000
    .long 0x00000000, 0x2b800000, 0x3e800000, 0x00000001
    .long 0x80000001, 0x3e800000, 0x00000000, 0x2b800000
    .long 0x3e800000, 0x007fffff, 0x2b800000, 0x3e800000
    .long 0x00000000, 0x2b800000, 0x807fffff, 0x00000000
    .long 0x2b800000, 0x3e800000, 0x00000000, 0x00800000
    .long 0x80800000, 0x00000000, 0x2b800000, 0x3e800000
    .long 0x00000000, 0x7fc00000, 0x3e800000, 0x00000000
    .long 0x2b800000, 0x3e800000, 0x7f800001, 0x2b800000
    .long 0x3e800000, 0x00000000, 0x2b800000, 0xffc00000
    .long 0x7f800000, 0x2b800000, 0x3e800000, 0x00000000
    .long 0x2b800000, 0xff800000, 0x00000000, 0x2b800000
    .long 0x3e800000, 0x00000000, 0x7f7fffff, 0x3e800000
    .long 0x00000000, 0x2b800000, 0x3e800000, 0xff7fffff
nj: .long 0, 0, 0, 0x10000
tmp: .long 0, 0, 0, 0
