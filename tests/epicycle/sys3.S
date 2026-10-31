# sys3.S: the decrementer and MSR.POW sleep, self-modifying code (icbi/isync), the device path; EA beyond 2^32; string instructions.
# Bare metal; the reference is tests/epicycle/ref/sys3.bin. Journal as in sys1.S. The DEC handler does not log SRR0 (where
# the interrupt lands depends on timing) but sets %r27 = 1. The window at 0x00F00000 (16 KB) behaves as RAM, but the stands
# serve it as a device (env_mmio): JIT-code accesses to it go through a trap, and the journal must still match.
        .set RES, 0x10000
        .macro LOG r
        stw \r, 0(%r31)
        addi %r31, %r31, 4
        .endm
        .macro MARK n
        lis %r3, 0x7e57
        ori %r3, %r3, \n
        LOG %r3
        .endm
        .macro LI32 r, v
        lis \r, (\v)@h
        ori \r, \r, (\v)@l
        .endm
        .macro RESUME lbl, msr
        LI32 %r29, \lbl
        LI32 %r28, \msr
        .endm
        .macro VEC off
        .org \off
        li %r3, \off
        ba common
        .endm
        .macro SPIN                      # wait for %r27 != 0, at most 2^26 iterations; log whether it happened
        lis %r5, 0x400
        mtctr %r5
7:      cmpwi %r27, 0
        bne 8f
        bdnz 7b
8:      mfctr %r5
        cmpwi %r5, 0
        li %r6, 0
        beq 9f
        li %r6, 1
9:      LOG %r6
        .endm
        .set EE, 0x8000
        .set POW, 0x40000
        .set DR, 0x10

        .globl reset
        .section .rom, "ax"
        .org 0x100
reset:  LI32 %r3, 0xfff04000
        li %r4, 0
        LI32 %r5, ram_end
0:      lwz %r6, 0(%r3)
        stw %r6, 0(%r4)
        addi %r3, %r3, 4
        addi %r4, %r4, 4
        cmplw %r4, %r5
        blt 0b
        sync
        isync
        lis %r31, RES@h
        LI32 %r4, start
        mtctr %r4
        bctr

        .section .vec, "ax"
        VEC 0x200
        VEC 0x300
        VEC 0x400
        VEC 0x500
        VEC 0x600
        VEC 0x700
        VEC 0x800
        .org 0x900
        li %r3, 0x900
        ba decint
        VEC 0xc00
        VEC 0xd00
        VEC 0xf00
        VEC 0x1300

        .text
common: srwi %r30, %r31, 12
        cmplwi %r30, RES >> 12
        bgt die
        LOG %r3
        mfsrr0 %r30
        LOG %r30
        mfsrr1 %r30
        LOG %r30
        mfmsr %r30
        LOG %r30
        mtsrr0 %r29
        mtsrr1 %r28
        rfi
decint: LOG %r3                          # DEC: vector, SRR1, MSR in the handler; SRR0 is not logged
        mfsrr1 %r30
        LOG %r30
        mfmsr %r30
        LOG %r30
        li %r27, 1
        mtsrr0 %r29
        mtsrr1 %r28
        rfi
die:    b die

start:  li %r4, 0
        mtmsr %r4
        isync
        MARK 1                           # DEC with EE=1 interrupts the wait
        li %r27, 0
        RESUME 1f, 0
        li %r4, 2000
        mtdec %r4
        LI32 %r4, EE
        mtmsr %r4
        SPIN
1:      LOG %r27
        MARK 2                           # with EE=0 the DEC waits; mtmsr EE=1 interrupts at once
        li %r27, 0
        li %r4, 100
        mtdec %r4
        lis %r5, 0x40
        mtctr %r5
2:      bdnz 2b
        mfdec %r5
        srwi %r5, %r5, 31                # DEC is already negative
        LOG %r5
        LOG %r27                         # and no interrupt yet
        RESUME 2f, 0
        LI32 %r4, EE
        mtmsr %r4
        li %r6, 0x66                     # never reached: the interrupt comes before this instruction
        LOG %r6
2:      LOG %r27
        MARK 3                           # MSR.POW with HID0.DOZE: sleep until DEC
        LI32 %r4, 0x00800000
        mtspr 1008, %r4
        li %r27, 0
        li %r4, 2000
        mtdec %r4
        RESUME 3f, 0
        LI32 %r4, POW|EE
        mtmsr %r4
        isync
        li %r6, 0x77                     # not executed: sleep, then the interrupt
        LOG %r6
3:      LOG %r27
        li %r4, 0                        # without DOZE/NAP/SLEEP the POW bit does not sleep
        mtspr 1008, %r4
        lis %r4, 0x7fff
        mtdec %r4
        LI32 %r4, POW
        mtmsr %r4
        isync
        li %r6, 0x78
        LOG %r6
        li %r4, 0
        mtmsr %r4
        isync
        MARK 4                           # self-modifying code: dcbst, sync, icbi, isync
        LI32 %r7, 0x00204000
        li %r8, 0
        li %r9, 5
        mtctr %r9
4:      addis %r10, %r8, 0x38a0          # li %r5, %r8+1
        addi %r10, %r10, 1
        stw %r10, 0(%r7)
        LI32 %r10, 0x4e800020            # blr
        stw %r10, 4(%r7)
        dcbst 0, %r7
        sync
        icbi 0, %r7
        isync
        mfctr %r11
        mtctr %r7
        bctrl
        mtctr %r11
        LOG %r5
        addi %r8, %r8, 1
        bdnz 4b
        MARK 5                           # device window 0x00F00000: sizes, byte order, byte reversal, lmw/stmw, FP, dcbz
        li %r4, 0x2000
        mtmsr %r4
        isync
        lis %r7, 0x00f0
        LI32 %r10, 0x11223344
        stw %r10, 0(%r7)
        lwz %r11, 0(%r7)
        LOG %r11
        lhz %r11, 2(%r7)
        LOG %r11
        lbz %r11, 1(%r7)
        LOG %r11
        li %r10, 0x5566
        sth %r10, 4(%r7)
        li %r10, 0x77
        stb %r10, 6(%r7)
        lwz %r11, 4(%r7)
        LOG %r11
        lwbrx %r11, 0, %r7
        LOG %r11
        LI32 %r10, 0xa1b2c3d4
        li %r12, 8
        stwbrx %r10, %r12, %r7
        lwz %r11, 8(%r7)
        LOG %r11
        lha %r11, 8(%r7)
        LOG %r11
        lwzu %r11, 0x100(%r7)            # update form: %r7 moves
        LOG %r7
        lis %r7, 0x00f0
        LI32 %r20, 0x20202020
        LI32 %r21, 0x21212121
        stmw %r20, 0x200(%r7)            # %r20..%r31 there and back (%r31 is the journal pointer)
        li %r20, 0
        li %r21, 0
        lmw %r20, 0x200(%r7)
        LOG %r20
        LOG %r21
        lfd %f1, 0(%r7)
        stfd %f1, 0x300(%r7)
        lwz %r11, 0x304(%r7)
        LOG %r11
        dcbz 0, %r7
        lwz %r11, 0(%r7)
        LOG %r11
        lwz %r11, 0x300(%r7)
        LOG %r11
        MARK 6                           # the same window with DR=1 through DBAT0 (0-16 MB one-to-one)
        li %r4, 0
        mtspr 536, %r4
        li %r4, 2
        mtspr 537, %r4
        li %r4, 0x1ff
        mtspr 536, %r4
        li %r4, DR
        mtmsr %r4
        isync
        LI32 %r10, 0xcafef00d
        stw %r10, 0x400(%r7)
        lwz %r11, 0x400(%r7)
        LOG %r11
        lbz %r11, 0x403(%r7)
        LOG %r11
        li %r4, 0
        mtmsr %r4
        isync
        lwz %r11, 0x400(%r7)
        LOG %r11
        MARK 7                           # EA beyond 2^32: base 0xFFFFFFF0 + offset (the JIT in the VM forms the address in 64 bits), DR=0 and DR=1
        LI32 %r7, 0xfffffff0
        li %r8, 0
        LI32 %r10, 0x0badcafe
        stw %r10, 0x14(%r7)              # EA 0x00000004
        lwz %r11, 4(%r8)
        LOG %r11
        lwz %r11, 0x14(%r7)
        LOG %r11
        li %r4, DR
        mtmsr %r4
        isync
        LI32 %r10, 0x5eed1234
        stw %r10, 0x18(%r7)              # EA 0x00000008 through DBAT0
        lwz %r11, 0x18(%r7)
        LOG %r11
        lhz %r11, 0x16(%r7)              # EA 6: the low half of 0x0badcafe
        LOG %r11
        li %r4, 0
        mtmsr %r4
        isync
        lwz %r11, 8(%r8)
        LOG %r11
        MARK 8                           # string instructions lswi/stswi/lswx/stswx (BootX copies structures with them)
        LI32 %r20, 0x01020304
        LI32 %r21, 0x05060708
        LI32 %r22, 0x090a0b0c
        LI32 %r7, 0x00200000
        stswi %r20, %r7, 11              # 11 bytes: r20, r21 and the three high bytes of r22
        lswi %r24, %r7, 11               # r26: three bytes and a zero
        LOG %r24
        LOG %r25
        LOG %r26
        li %r8, 5
        mtxer %r8                        # XER: byte count 5
        li %r9, 1
        .long 0x7f674c2a                 # lswx %r27, %r7, %r9: 5 bytes from address +1 (LLVM/zig do not know lswx/stswx)
        LOG %r27
        LOG %r28
        .long 0x7ea74d2a                 # stswx %r21, %r7, %r9: 5 bytes of r21, r22 to address +1
        lwz %r11, 0(%r7)
        LOG %r11
        lwz %r11, 4(%r7)
        LOG %r11
        li %r0, 0x0
        stswi %r31, %r7, 8               # r0 follows r31
        lwz %r11, 4(%r7)
        LOG %r11
        li %r8, 0
        mtxer %r8
        MARK 0xee
        b .
