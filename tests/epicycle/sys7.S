# sys7.S: twi, the trap in the VM (the JIT raises it without leaving the VM, as sc): every TO bit, true and false, signed
# and unsigned, negative immediates, TO 0, a trap loop, the vector at 0xFFF00700 (IP=1) and a trap from user mode.
# Bare metal; the reference is the journal tests/epicycle/ref/sys7.bin (the interpreter, ptv -i -s); the JIT on ptv must give it word for word.
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
        MARK 0x70                        # TO 31 in a loop: a trap every time, back into the same block
        li %r20, 0
loop1:  RESUME l1n, 0
        twi 31, %r0, 0x10
l1n:    addi %r20, %r20, 1
        cmplwi %r20, 4
        blt loop1
        LOG %r20
        MARK 0x71                        # each TO bit, true then false
        LI32 %r5, -7
        RESUME a1, 0
        twi 4, %r5, -7                   # equal -> trap
a1:     twi 4, %r5, 7                    # not equal: no trap
        RESUME a2, 0
        twi 16, %r5, -6                  # -7 < -6 signed -> trap
a2:     twi 8, %r5, -6                   # -7 > -6 signed: no trap
        RESUME a3, 0
        twi 1, %r5, 5                    # 0xFFFFFFF9 > 5 unsigned -> trap
a3:     twi 2, %r5, 5                    # 0xFFFFFFF9 < 5 unsigned: no trap
        li %r6, 3
        RESUME a4, 0
        twi 2, %r6, -1                   # 3 < 0xFFFFFFFF unsigned -> trap
a4:     twi 24, %r6, 3                   # less or greater (not equal): no trap
        RESUME a5, 0
        twi 24, %r6, 4                   # 3 < 4 -> trap
a5:     twi 0, %r6, 3                    # TO 0: never
        LI32 %r4, 0x5a5a
        LOG %r4
        MARK 0x72                        # IP=1: the trap's vector is 0xFFF00700
        li %r4, 0x40
        mtmsr %r4
        isync
        RESUME a6, 0x40
        twi 31, %r0, 1
a6:     li %r4, 0
        mtmsr %r4
        isync
        MARK 0x73                        # a trap from user mode (PR=1): SRR1 keeps PR
        RESUME a7, 0
        USER u7
u7:     twi 31, %r1, 2
a7:     MARK 0xee                        # end
        b .
