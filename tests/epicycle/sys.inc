# sys.inc: the harness of the bare-metal tests sys1.S .. sys9.S (journal and handler: see sys1.S).
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
        .macro VEC off, hi=0
        .org \off
        li %r3, \off + \hi
        ba common
        .endm
        .macro TABLE hi, vmx=0           # the vectors 0x200-0x1700; vmx=1 adds 0xF20
        VEC 0x200, \hi
        VEC 0x300, \hi
        VEC 0x400, \hi
        VEC 0x500, \hi
        VEC 0x600, \hi
        VEC 0x700, \hi
        VEC 0x800, \hi
        VEC 0x900, \hi
        VEC 0xa00, \hi
        VEC 0xb00, \hi
        VEC 0xc00, \hi
        VEC 0xd00, \hi
        VEC 0xe00, \hi
        VEC 0xf00, \hi
        .if \vmx
        VEC 0xf20, \hi                 # VMX unavailable
        .endif
        VEC 0x1000, \hi
        VEC 0x1100, \hi
        VEC 0x1200, \hi
        VEC 0x1300, \hi
        VEC 0x1400, \hi
        VEC 0x1500, \hi
        VEC 0x1600, \hi
        VEC 0x1700, \hi
        .endm
        .macro USER lbl                 # enter user mode (PR=1, IR=DR=0) at lbl
        LI32 %r4, \lbl
        mtsrr0 %r4
        li %r4, 0x4000
        mtsrr1 %r4
        rfi
        .endm
        .macro RFITO lbl, msr          # go to lbl with MSR = msr
        LI32 %r3, \lbl
        mtsrr0 %r3
        LI32 %r3, \msr
        mtsrr1 %r3
        rfi
        .endm
        .macro REAL                      # back to DR=0 from supervisor code
        li %r3, 0
        mtmsr %r3
        isync
        .endm
        .macro RESET                     # in .rom: reset at 0xFFF00100 copies .vec and .text from ROM to RAM at 0, %r31 = RES
        .globl reset
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
        .endm
        .macro COMMON msr=1              # the handler: logs the vector, SRR0, SRR1, the MSR in the handler (msr=1), DAR, DSISR
common: srwi %r30, %r31, 12                # loop guard: the journal stops at RES+4K
        cmplwi %r30, RES >> 12
        bgt die
        LOG %r3
        mfsrr0 %r30
        LOG %r30
        mfsrr1 %r30
        LOG %r30
        .if \msr
        mfmsr %r30
        LOG %r30
        .endif
        mfspr %r30, 19
        LOG %r30
        mfspr %r30, 18
        LOG %r30
        mtsrr0 %r29
        mtsrr1 %r28
        rfi
die:    b die
        .endm
# pte_set: %r4 = EA, %r5 = word 1, %r6 = H, %r7 = slot -> %r11 = PTE address. Clobbers %r8-%r12. HTABMASK = 0.
        .macro PTE_SET
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
        .endm
