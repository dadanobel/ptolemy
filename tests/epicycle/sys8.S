# sys8.S: icbi at DR=1 in the JIT, which finds the physical page through the ARM tables (AT in the VM; outside the VM,
# the interpreter). EA 0x20000000 -> page 0x200000 (PP=2) gets "li %r5, n; blr" at IR=DR=1, dcbst/sync/icbi/isync, and a
# call, four times in supervisor and four in user mode: the first call finds no translated code on the page, every later
# one must run the new words. Then the words change through EA 0x20000000 while icbi names EA 0x30000000 (SR3 = SR2: the
# same page), which no data access has touched, so no ARM entry holds it (the interpreter translates that icbi): the next
# call must run the new words. The interpreter (ptv -i -s) is the oracle; the JIT (pt; ptv with PT_VMSC=2 PT_VMRFI=2
# PT_VMMSR=2) must give the same journal word for word.
        .include "sys.inc"
        .set DR, 0x10
        .set PR, 0x4000
        .set SRA, 0x20000102             # Kp=1, VSID 0x102
        .set EA, 0x20000000
        .set EA3, 0x30000000
        .set IR, 0x20

        .section .rom, "ax"
        RESET
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
        VEC 0x900
        VEC 0xc00
        VEC 0xd00
        VEC 0xf00
        VEC 0x1300

        .text
        COMMON msr=0

        PTE_SET

start:  li %r4, 0
        mtmsr %r4
        isync
        MARK 1                           # real mode: HTAB, SDR1, SRs, BAT0 = 0-8 MB 1:1 for both keys
        LI32 %r4, 0x00100000
        li %r5, 0
        li %r6, 0x4000
        mtctr %r6
1:      stw %r5, 0(%r4)
        addi %r4, %r4, 4
        bdnz 1b
        LI32 %r4, 0x00100000
        mtspr 25, %r4
        li %r5, 0
        LI32 %r6, 0x20000100
2:      mtsrin %r6, %r5
        addis %r5, %r5, 0x1000
        addi %r6, %r6, 1
        cmplwi %r5, 0
        bne 2b
        li %r4, 0
        mtspr 528, %r4
        mtspr 529, %r4
        mtspr 530, %r4
        mtspr 531, %r4
        mtspr 532, %r4
        mtspr 533, %r4
        mtspr 534, %r4
        mtspr 535, %r4
        mtspr 536, %r4
        mtspr 537, %r4
        mtspr 538, %r4
        mtspr 539, %r4
        mtspr 540, %r4
        mtspr 541, %r4
        mtspr 542, %r4
        mtspr 543, %r4
        li %r5, 2
        mtspr 537, %r5
        mtspr 529, %r5
        li %r4, 0xff
        mtspr 536, %r4
        mtspr 528, %r4
        isync
        LI32 %r14, SRA
        mtsr 2, %r14                     # the same VSID in SR2 and SR3
        mtsr 3, %r14
        isync
        LI32 %r4, EA
        LI32 %r5, 0x00200002             # page 0x200000, PP=2
        li %r6, 0
        li %r7, 0
        bl pte_set
        MARK 2                           # supervisor, IR=DR=1: four rounds
        RFITO s2, IR|DR
s2:     li %r14, 0x100
        bl rounds
        REAL
        MARK 3                           # user mode: four rounds, then sc (the handler logs it, back in real mode)
        RESUME s3, 0
        RFITO u3, PR|IR|DR
u3:     li %r14, 0x200
        bl rounds
        sc
s3:     MARK 4                           # icbi through EA3, an alias no data access has touched
        RFITO s4, IR|DR
s4:     li %r4, 0x300
        LI32 %r6, EA
        LI32 %r7, EA3
        bl gen
        mtctr %r6
        bctrl
        LOG %r5
        REAL
        MARK 0xee
        b .

# rounds: four times gen("li %r5, %r14 + k") at EA, icbi EA, then a call to EA; logs %r5. Clobbers %r4-%r7, %r10, %r14-%r16, CTR.
rounds: mflr %r16
        li %r15, 4
1:      mr %r4, %r14
        LI32 %r6, EA
        mr %r7, %r6
        bl gen
        mtctr %r6
        bctrl
        LOG %r5
        addi %r14, %r14, 1
        addic. %r15, %r15, -1
        bne 1b
        mtlr %r16
        blr

# gen: "li %r5, %r4; blr" at %r6 (DR=1), dcbst, sync, icbi %r7, isync. Clobbers %r10.
gen:    oris %r10, %r4, 0x38a0
        stw %r10, 0(%r6)
        LI32 %r10, 0x4e800020
        stw %r10, 4(%r6)
        dcbst 0, %r6
        sync
        icbi 0, %r7
        isync
        blr
