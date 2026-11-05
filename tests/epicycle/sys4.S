# sys4.S: the same EA under two segment sets, switched in real mode.
# A guest OS sets SRs at DR=0 and enters the new context with rfi; the JIT in the VM finishes such an rfi itself and
# leaves for a lookup (s4), and the host switches the ARM tables first. Checked here, with the TLB warm, three times:
#   EA 0x20000000 under VSID A -> page 0x200000 (PP=2, RW);  under VSID B -> page 0x201000 (PP=1, Kp=1).
#   supervisor A: reads A;  user B: reads B, a write is a DSI (Kp=1, PP=1);  supervisor B: writes B (Ks=0);
#   supervisor A again: reads A, never B's data. Physical reads in real mode show which page each write hit.
# The interpreter (ptv -i -s) is the oracle; the JIT runs with rfi/mtmsr in the VM
# (PT_VMRFI=2 PT_VMMSR=2) and must give the same journal word for word.
        .include "sys.inc"
        .set DR, 0x10
        .set PR, 0x4000
        .set SRA, 0x20000102             # Kp=1, VSID 0x102
        .set SRB, 0x20000202             # Kp=1, VSID 0x202
        .set EA, 0x20000000

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
        LI32 %r15, SRB
        LI32 %r16, EA
        mtsr 2, %r15
        isync
        LI32 %r4, EA
        LI32 %r5, 0x00201001             # B: page 0x201000, PP=1
        li %r6, 0
        li %r7, 0
        bl pte_set
        mtsr 2, %r14
        isync
        LI32 %r4, EA
        LI32 %r5, 0x00200002             # A: page 0x200000, PP=2
        li %r6, 0
        li %r7, 0
        bl pte_set
        li %r17, 3                       # rounds, TLB warm after the first
round:  MARK 2                           # supervisor, SR2 = A set at DR=0
        LOG %r17
        mtsr 2, %r14
        RFITO sa, DR
sa:     lwz %r5, 0(%r16)
        LOG %r5
        REAL
        MARK 3                           # user, SR2 = B set at DR=0: read B, a write is a DSI
        mtsr 2, %r15
        RESUME ub, 0
        RFITO ua, PR|DR
ua:     lwz %r5, 0(%r16)
        LOG %r5
        stw %r17, 0(%r16)
        sc                               # only if the write went through: a 0xc00 entry in the journal
ub:     MARK 4                           # supervisor, SR2 = B still: Ks=0 writes B
        RFITO sb, DR
sb:     lwz %r5, 0(%r16)
        LOG %r5
        oris %r5, %r17, 0xb0b0
        stw %r5, 0(%r16)
        lwz %r5, 0(%r16)
        LOG %r5
        REAL
        MARK 5                           # supervisor, SR2 = A again: A's data, B's write not seen
        mtsr 2, %r14
        RFITO sc_, DR
sc_:    lwz %r5, 0(%r16)
        LOG %r5
        oris %r5, %r17, 0xa0a0
        stw %r5, 0(%r16)
        REAL
        MARK 6                           # physical pages after the round
        LI32 %r4, 0x00200000
        lwz %r5, 0(%r4)
        LOG %r5
        lwz %r5, 0x1000(%r4)
        LOG %r5
        addic. %r17, %r17, -1
        bne round
        MARK 0xee
        b .
