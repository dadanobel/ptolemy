# sys5.S: one VSID in two segment registers. SR2 = SR3 = VSID A, so EA 0x20000000 and
# EA 0x30000000 are the same virtual page. Both are touched at DR=1 (the VM builds a mapping for each segment), then
# the PTE moves the page from 0x200000 to 0x201000 and one tlbie of EA 0x20000000 must retire it under BOTH
# segments: a later read through either EA sees the new page. The failure it guards against: the second segment's
# fault builds a second L3 table, which escapes the tlbie and keeps the old page. The interpreter (ptv -i -s) is the
# oracle; the JIT in the VM (PT_VMRFI=2 PT_VMMSR=2) must give the same journal word for word.
        .include "sys.inc"
        .set DR, 0x10
        .set PR, 0x4000
        .set SRA, 0x20000102             # Kp=1, VSID 0x102
        .set EA, 0x20000000
        .set EA3, 0x30000000

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
        LI32 %r4, 0x00200000             # page contents
        LI32 %r5, 0xaaaa0000
        stw %r5, 0(%r4)
        LI32 %r5, 0xbbbb0000
        stw %r5, 0x1000(%r4)
        LI32 %r14, SRA
        mtsr 2, %r14                     # the same VSID in SR2 and SR3
        mtsr 3, %r14
        isync
        LI32 %r4, EA
        LI32 %r5, 0x00200002             # page 0x200000, PP=2
        li %r6, 0
        li %r7, 0
        bl pte_set
        mr %r18, %r11                    # the PTE's address
        LI32 %r16, EA
        LI32 %r19, EA3
        MARK 2                           # DR=1: one read through each segment builds both mappings
        RFITO s1, DR
s1:     lwz %r5, 0(%r16)
        LOG %r5
        lwz %r5, 0(%r19)
        LOG %r5
        LI32 %r5, 0xa1a1a1a1
        stw %r5, 4(%r19)                 # written through SR3, read back through SR2
        lwz %r5, 4(%r16)
        LOG %r5
        REAL
        MARK 3                           # the page moves; one tlbie of the SR2 address
        LI32 %r5, 0x00201002
        stw %r5, 4(%r18)
        sync
        tlbie %r16
        sync
        LI32 %r5, 0xb2b2b2b2
        LI32 %r4, 0x00201000
        stw %r5, 8(%r4)
        RFITO s2, DR
s2:     lwz %r5, 0(%r16)                 # through SR2: the new page
        LOG %r5
        lwz %r5, 0(%r19)                 # through SR3: the new page
        LOG %r5
        lwz %r5, 8(%r16)
        LOG %r5
        LI32 %r5, 0xc3c3c3c3
        stw %r5, 12(%r16)                # must land in the new page
        REAL
        MARK 4                           # physical pages
        LI32 %r4, 0x00200000
        lwz %r5, 4(%r4)
        LOG %r5
        lwz %r5, 12(%r4)
        LOG %r5
        lwz %r5, 0x100c(%r4)
        LOG %r5
        MARK 0xee
        b .
