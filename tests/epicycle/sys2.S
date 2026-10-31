# sys2.S: the 750 MMU: BATs, segments, the hashed page table (R/C), DSI/ISI, tlbie, user keys.
# Bare metal; the reference is tests/epicycle/ref/sys2.bin. Journal and handler as in sys1.S (clobbers only %r3, %r30).
# Physical memory: 0-8 MB mapped one-to-one by DBAT0/IBAT0 (journal RES, HTAB, code); the test pages
# A=0x200000 B=0x201000 C=0x202000 D=0x203000 (code) are mapped as pages through segments 2, 3, 5.
# %r20..: PTE addresses (to read the R/C bits). HTAB: 0x100000, 64 KB (SDR1 = 0x00100000).
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
        .macro RFITO lbl, msr          # go to lbl with MSR = msr
        LI32 %r3, \lbl
        mtsrr0 %r3
        LI32 %r3, \msr
        mtsrr1 %r3
        rfi
        .endm
        .macro PTE ea, w1, h, slot, save   # PTE: EA -> word 1 (RPN|PP), hash h (0/1), slot; the PTE address -> save
        LI32 %r4, \ea
        LI32 %r5, \w1
        li %r6, \h
        li %r7, \slot
        bl pte_set
        mr \save, %r11
        .endm
        .set DR, 0x10
        .set IR, 0x20
        .set PR, 0x4000
        .set FP, 0x2000

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
        VEC 0x900
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
        mfspr %r30, 19
        LOG %r30
        mfspr %r30, 18
        LOG %r30
        mtsrr0 %r29
        mtsrr1 %r28
        rfi
die:    b die

# pte_set: %r4 = EA, %r5 = word 1, %r6 = H, %r7 = slot -> %r11 = PTE address. Clobbers %r8-%r12. HTABMASK = 0.
pte_set:
        mfsrin %r9, %r4
        rlwinm %r9, %r9, 0, 8, 31          # VSID
        rlwinm %r10, %r4, 20, 16, 31       # page index
        rlwinm %r11, %r9, 0, 13, 31
        xor %r11, %r11, %r10                # primary hash
        cmpwi %r6, 0
        beq 1f
        not %r11, %r11                     # secondary
1:      rlwinm %r11, %r11, 6, 16, 25       # (hash & 0x3ff) << 6
        oris %r11, %r11, 0x0010            # HTABORG
        rlwinm %r12, %r7, 3, 26, 28
        add %r11, %r11, %r12
        rlwinm %r12, %r9, 7, 1, 24         # VSID << 7
        oris %r12, %r12, 0x8000            # V
        rlwimi %r12, %r6, 6, 25, 25        # H
        rlwimi %r12, %r4, 10, 26, 31       # API
        stw %r5, 4(%r11)
        sync
        stw %r12, 0(%r11)
        sync
        blr

start:  li %r4, 0
        mtmsr %r4
        isync
        MARK 1                           # setup in real mode
        LI32 %r4, 0x00100000              # clear the HTAB
        li %r5, 0
        li %r6, 0x4000
        mtctr %r6
1:      stw %r5, 0(%r4)
        addi %r4, %r4, 4
        bdnz 1b
        LI32 %r4, 0x00100000
        mtspr 25, %r4                     # SDR1
        li %r5, 0                         # SR[i] = Kp | VSID 0x100+i
        LI32 %r6, 0x20000100
2:      mtsrin %r6, %r5
        addis %r5, %r5, 0x1000
        addi %r6, %r6, 1
        cmplwi %r5, 0
        bne 2b
        li %r4, 0                         # disable all BATs
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
        li %r5, 2                         # BAT0: 0-8 MB one-to-one, RW, Vs=Vp=1
        mtspr 537, %r5
        mtspr 529, %r5
        li %r4, 0xff
        mtspr 536, %r4
        mtspr 528, %r4
        isync
        LI32 %r4, 0x00200000              # page contents
        LI32 %r5, 0xaaaa0000
        stw %r5, 0(%r4)
        LI32 %r5, 0xaaaa0ffc
        stw %r5, 0xffc(%r4)
        LI32 %r5, 0xbbbb0000
        stw %r5, 0x1000(%r4)
        LI32 %r5, 0xcccc0000
        stw %r5, 0x2000(%r4)
        LI32 %r5, 0x38a05555              # D: li %r5, 0x5555; blr
        stw %r5, 0x3000(%r4)
        LI32 %r5, 0x4e800020
        stw %r5, 0x3004(%r4)
        PTE 0x20000000, 0x00200002, 0, 0, %r20    # A, RW
        PTE 0x20001000, 0x00201003, 0, 1, %r21    # B, read-only
        PTE 0x20002000, 0x00202000, 0, 0, %r22    # C, PP=0: supervisor RW, no user access
        PTE 0x20003000, 0x00202001, 1, 3, %r23    # C, PP=1, secondary hash
        PTE 0x30000000, 0x00202002, 0, 0, %r24    # C (under DBAT1)
        PTE 0x50000000, 0x00203002, 0, 0, %r25    # D, code
        PTE 0x50001000, 0x00203000, 0, 0, %r26    # D, PP=0: the user may not execute it
        MARK 2                           # DR=1: read, R bit
        li %r4, DR
        mtmsr %r4
        isync
        LI32 %r4, 0x20000000
        lwz %r5, 0(%r4)
        LOG %r5
        lwz %r5, 4(%r20)
        LOG %r5
        MARK 3                           # store: C bit, the data is visible physically
        LI32 %r5, 0x12345678
        stw %r5, 0(%r4)
        lwz %r5, 4(%r20)
        LOG %r5
        LI32 %r6, 0x00200000
        lwz %r5, 0(%r6)
        LOG %r5
        MARK 4                           # no mapping: DSI 0x40000000 / 0x42000000
        LI32 %r4, 0x20005000
        RESUME 4f, DR
        lwz %r5, 0(%r4)
4:      RESUME 4f, DR
        stw %r5, 0(%r4)
4:      MARK 5                           # read-only
        LI32 %r4, 0x20001000
        lwz %r5, 0(%r4)
        LOG %r5
        RESUME 5f, DR
        stw %r5, 0(%r4)
5:      lwz %r5, 4(%r21)
        LOG %r5
        MARK 6                           # access across a page boundary (A | B read-only)
        LI32 %r4, 0x20000ffe
        lwz %r5, 0(%r4)
        LOG %r5
        RESUME 6f, DR
        stw %r5, 0(%r4)
6:      MARK 7                           # keys: the supervisor (Ks=0) reads PP=0, the user (Kp=1) does not
        LI32 %r4, 0x20002000
        lwz %r5, 0(%r4)
        LOG %r5
        RESUME 7f, DR
        RFITO u1, PR|DR
u1:     lwz %r5, 0(%r4)
7:      RESUME 7f, DR
        RFITO u2, PR|DR
u2:     LI32 %r4, 0x20003000              # PP=1, secondary hash: the user may read but not write
        lwz %r5, 0(%r4)
        LOG %r5
        stw %r5, 0(%r4)
7:      MARK 8                           # tlbie: A -> B
        LI32 %r5, 0x00201002
        stw %r5, 4(%r20)
        sync
        LI32 %r4, 0x20000000
        tlbie %r4
        sync
        tlbsync
        sync
        lwz %r5, 0(%r4)
        LOG %r5
        MARK 9                           # DBAT1 takes priority over the page
        LI32 %r4, 0x00200002
        mtspr 539, %r4
        LI32 %r4, 0x30000003
        mtspr 538, %r4
        isync
        LI32 %r4, 0x30000000
        lwz %r5, 0(%r4)
        LOG %r5
        MARK 10                          # DBAT1 for the user only (Vs=0): the supervisor goes through the pages
        LI32 %r5, 0x30000001
        mtspr 538, %r5
        isync
        lwz %r5, 0(%r4)
        LOG %r5
        MARK 11                          # DBAT1 read-only
        LI32 %r5, 0x00200001
        mtspr 539, %r5
        LI32 %r5, 0x30000003
        mtspr 538, %r5
        isync
        RESUME 11f, DR
        stw %r5, 0(%r4)
11:     lwz %r5, 0(%r4)
        LOG %r5
        MARK 12                          # dcbz through translation
        LI32 %r4, 0x20000040
        LI32 %r5, 0x00201040
        LI32 %r6, 0x5a5a5a5a
        stw %r6, 0(%r5)
        dcbz 0, %r4
        lwz %r6, 0(%r5)
        LOG %r6
        MARK 13                          # FP through translation
        li %r4, DR|FP
        mtmsr %r4
        isync
        LI32 %r4, 0x20000000
        lfd %f1, 0(%r4)
        stfd %f1, 0(%r31)
        addi %r31, %r31, 8
        MARK 14                          # IR=1: code under IBAT0; fetch from a page; ISI
        li %r4, IR|DR
        mtmsr %r4
        isync
        li %r5, 0x1414
        LOG %r5
        RESUME 14f, IR|DR
        LI32 %r4, 0x40000000
        mtctr %r4
        bctr
14:     LI32 %r4, 0x50000000
        mtctr %r4
        bctrl
        LOG %r5
        lwz %r5, 4(%r25)
        LOG %r5
        MARK 15                          # segment N=1: no execute
        LI32 %r6, 0x30000105
        mtsr 5, %r6
        isync
        RESUME 15f, IR|DR
        bctrl
15:     LI32 %r6, 0x20000105
        mtsr 5, %r6
        isync
        MARK 16                          # the user may not execute a PP=0 page (Kp=1)
        RESUME 16f, IR|DR
        RFITO 0x50001000, PR|IR|DR
16:     MARK 17                          # precise state on a DSI in the middle of a block: registers as before the instruction
        li %r4, DR
        mtmsr %r4
        isync
        RESUME 17f, DR
        LI32 %r9, 0x20005000              # not mapped
        li %r10, 10
        li %r11, 11
        li %r12, 12
        li %r13, 13
        li %r14, 14
        li %r15, 15
        li %r16, 16
        li %r17, 17
        li %r18, 18
        li %r19, 19
        li %r20, 20
        li %r21, 21
        add %r14, %r10, %r11
        mulli %r15, %r14, 3
        cmpwi cr3, %r15, 63
        cmpwi %r14, 21
        mtctr %r15
        xor %r16, %r16, %r15
        addic %r17, %r17, -100
        lwzu %r20, 0x10(%r9)               # DSI: %r9 and %r20 stay unchanged
17:     bl 18f
18:     LOG %r9
        LOG %r10
        LOG %r11
        LOG %r12
        LOG %r13
        LOG %r14
        LOG %r15
        LOG %r16
        LOG %r17
        LOG %r18
        LOG %r19
        LOG %r20
        LOG %r21
        mfcr %r4
        LOG %r4
        mfctr %r4
        LOG %r4
        mfxer %r4
        LOG %r4
        RESUME 19f, DR                   # store fault after a not-taken conditional branch (superblock)
        addi %r18, %r18, 100
        cmpwi %r10, 5
        beq 19f
        addi %r19, %r19, 200
        stwu %r21, -8(%r9)
19:     LOG %r9
        LOG %r18
        LOG %r19
        LOG %r21
        MARK 0xee
        b .
