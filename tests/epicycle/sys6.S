# sys6.S: the jump cache across a segment alias. SR2 = SR3 = VSID A, so EA 0x20000000
# and EA 0x30000000 are one virtual page. Code on that page is called through SR3 by an indirect bctrl at IR=1 (the
# JIT remembers the target in its jump cache); then the PTE moves the page to other code and one tlbie of the SR2
# address retires it: tlbie names the page within the segment, not the segment. The next call through SR3 must run
# the new code. The failure it guards against: a jump-cache cell that checks the generation of the full EA page
# (0x30000), which a tlbie of 0x20000000 does not move, keeps jumping to the old translation. The interpreter
# (ptv -i -s) is the oracle; the JIT in the VM (PT_VMRFI=2 PT_VMMSR=2) must give the same journal word for word.
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
        LI32 %r4, codeA                  # code A on page 0x200000, code B on page 0x201000
        LI32 %r6, 0x00200000
        bl copy3
        LI32 %r4, codeB
        LI32 %r6, 0x00201000
        bl copy3
        LI32 %r14, SRA
        mtsr 2, %r14                     # the same VSID in SR2 and SR3
        mtsr 3, %r14
        isync
        LI32 %r4, EA
        LI32 %r5, 0x00200002             # page 0x200000 (code A), PP=2
        li %r6, 0
        li %r7, 0
        bl pte_set
        mr %r18, %r11                    # the PTE's address
        LI32 %r16, EA3
        LI32 %r17, EA
        MARK 2                           # IR=DR=1: two calls through SR3, the second one from the jump cache
        RFITO s1, IR|DR
s1:     mtctr %r16
        bctrl
        LOG %r5
        mtctr %r16
        bctrl
        LOG %r5
        REAL
        MARK 3                           # the page moves to code B; one tlbie of the SR2 address
        LI32 %r5, 0x00201002
        stw %r5, 4(%r18)
        sync
        tlbie %r17
        sync
        RFITO s2, IR|DR
s2:     mtctr %r16
        bctrl                            # through SR3: code B
        LOG %r5
        mtctr %r17
        bctrl                            # through SR2: code B
        LOG %r5
        REAL
        MARK 0xee
        b .

# copy3: three instruction words from %r4 to %r6, made visible to instruction fetch. Clobbers %r4, %r6-%r8, CTR.
copy3:  li %r7, 3
        mtctr %r7
1:      lwz %r8, 0(%r4)
        stw %r8, 0(%r6)
        dcbst 0, %r6
        sync
        icbi 0, %r6
        addi %r4, %r4, 4
        addi %r6, %r6, 4
        bdnz 1b
        isync
        blr
codeA:  lis %r5, 0xc0de
        ori %r5, %r5, 0xa
        blr
codeB:  lis %r5, 0xc0de
        ori %r5, %r5, 0xb
        blr
