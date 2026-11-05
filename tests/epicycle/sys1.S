# sys1.S: 750 supervisor mode without the MMU (IR=DR=0). Bare metal; the reference is the journal tests/epicycle/ref/sys1.bin (750 behaviour per the PEM).
# Journal: %r31 -> RES, LOG writes a word. Before each expected exception: %r29 = where to return, %r28 = with which MSR.
# The handler clobbers only %r3 and %r30. `common` logs: the vector number (odd: the 0xFFF0xxxx table), SRR0, SRR1, the MSR in the handler, DAR, DSISR.
# Build (zig = clang/lld): python3 -m ziglang cc -target powerpc-freestanding-none -mcpu=750 -c -o sys1.o sys1.S
#                          python3 -m ziglang ld.lld -N -e reset -T sys.ld -o sys1.elf sys1.o  (GNU as/ld work too)
        .include "sys.inc"

        .section .rom, "ax"              # 0xFFF00000: reset and the vectors for MSR.IP=1
        RESET
        MARK 0
        mfmsr %r4
        LOG %r4                           # MSR after reset
        MARK 1                           # sc with IP=1 -> 0xFFF00C00
        RESUME t1, 0x40
        sc
t1:     LI32 %r4, start
        mtctr %r4
        bctr
        TABLE 1

        .section .vec, "ax"              # 0x00000000: the vectors for MSR.IP=0
        TABLE 0

        .text
        COMMON

start:  MARK 2                           # clear IP
        li %r4, 0
        mtmsr %r4
        isync
        mfmsr %r4
        LOG %r4
        MARK 3                           # sc in supervisor mode -> 0xC00, SRR0 = the next instruction
        RESUME t3, 0
        sc
t3:     MARK 4                           # illegal: 0, op 4 (AltiVec on the G4), fsqrt (not on the 750)
        RESUME t4a, 0
        .long 0
t4a:    RESUME t4b, 0
        .long 0x10000000
t4b:    li %r4, 0x2000
        mtmsr %r4
        isync
        RESUME t4c, 0x2000
        .long 0xfc20082c                 # fsqrt %f1, %f1
t4c:    RESUME t4d, 0
        mfspr %r4, 1000                   # an SPR the 750 does not have
t4d:    MARK 5                           # traps
        RESUME t5a, 0
        tw 4, %r0, %r0                     # equal -> trap
t5a:    li %r4, 5
        RESUME t5b, 0
        twi 8, %r4, 3                     # 5 > 3 -> trap
t5b:    twi 16, %r4, 3                    # 5 < 3: no trap
        li %r4, 0x5a5a
        LOG %r4
        MARK 6                           # user mode
        RESUME t6a, 0
        USER u1
u1:     mfspr %r4, 8                      # LR: allowed
        mftb %r5                          # TB: allowed
        mfmsr %r4                         # privileged
t6a:    RESUME t6b, 0
        USER u2
u2:     sc                               # from user mode: SRR1.PR=1, PR=0 in the handler
t6b:    RESUME t6c, 0
        USER u3
u3:     mfsprg %r4, 0                     # SPRG0 is a privileged SPR
t6c:    RESUME t6d, 0
        USER u4
u4:     mtmsr %r4
t6d:    RESUME t6e, 0
        USER u5
u5:     mfspr %r4, 1016                   # no such SPR, a privileged number
t6e:    MARK 7                           # FP unavailable (MSR.FP=0)
        li %r4, 0
        mtmsr %r4
        isync
        RESUME t7a, 0
        fadd %f1, %f1, %f1
t7a:    RESUME t7b, 0
        lfd %f1, 0(%r31)
t7b:    RESUME t7c, 0
        stfiwx %f1, 0, %r31
t7c:    li %r4, 0x2000
        mtmsr %r4
        isync
        fadd %f1, %f1, %f1                  # with FP=1: no exception
        li %r4, 0x7777
        LOG %r4
        MARK 8                           # rfi: which SRR1 bits reach the MSR
        LI32 %r4, 0xffff3946
        mtsrr1 %r4
        LI32 %r4, t8
        mtsrr0 %r4
        rfi
t8:     mfmsr %r4
        LOG %r4
        MARK 9                           # mtmsr: which bits are written (the value avoids POW, EE, PR, SE, BE, IR, DR, LE)
        LI32 %r4, 0xfffb3946
        mtmsr %r4
        isync
        mfmsr %r4
        LOG %r4
        li %r4, 0
        mtmsr %r4
        isync
        MARK 10                          # SPRs: write and read
        LI32 %r4, 0x11111111
        mtsprg 0, %r4
        LI32 %r4, 0x22222222
        mtsprg 1, %r4
        LI32 %r4, 0x33333333
        mtsprg 2, %r4
        LI32 %r4, 0x44444444
        mtsprg 3, %r4
        LI32 %r4, 0x55555555
        mtsrr0 %r4
        LI32 %r4, 0x66666666
        mtsrr1 %r4
        LI32 %r4, 0x77777777
        mtspr 19, %r4
        LI32 %r4, 0x88888888
        mtspr 18, %r4
        LI32 %r4, 0x00ff0000
        mtspr 25, %r4
        LI32 %r4, 0x8000000a
        mtspr 282, %r4
        mfsprg %r4, 0
        LOG %r4
        mfsprg %r4, 1
        LOG %r4
        mfsprg %r4, 2
        LOG %r4
        mfsprg %r4, 3
        LOG %r4
        mfsrr0 %r4
        LOG %r4
        mfsrr1 %r4
        LOG %r4
        mfspr %r4, 19
        LOG %r4
        mfspr %r4, 18
        LOG %r4
        mfspr %r4, 25
        LOG %r4
        mfspr %r4, 282
        LOG %r4
        MARK 11                          # segment registers: mtsrin / mfsrin / mfsr
        li %r5, 0
        LI32 %r6, 0x60000111
1:      mtsrin %r6, %r5
        addis %r5, %r5, 0x1000
        addi %r6, %r6, 0x111
        cmplwi %r5, 0
        bne 1b
        li %r5, 0
2:      mfsrin %r4, %r5
        LOG %r4
        addis %r5, %r5, 0x1000
        cmplwi %r5, 0
        bne 2b
        mfsr %r4, 7
        LOG %r4
        MARK 0xee                        # end
        b .
