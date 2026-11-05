# fold.inc: the r26 hash of the user-mode tests (FOLD: brl.S, so.S; VFOLD: vmsum.S, vmxops.S).
    .macro FOLD r
    add 26, 26, \r
    rotlwi 26, 26, 5
    xor 26, 26, \r
    .endm
    .macro VFOLD vr, keep           # \vr and VSCR through tmp (r30); then VSCR = \keep, or 0 if keep is blank
    stvx \vr, 0, 30
    lwz 9, 0(30)
    add 26, 26, 9
    rotlwi 26, 26, 5
    lwz 9, 4(30)
    xor 26, 26, 9
    lwz 9, 8(30)
    add 26, 26, 9
    rotlwi 26, 26, 3
    lwz 9, 12(30)
    xor 26, 26, 9
    mfvscr 5
    stvx 5, 0, 30
    lwz 9, 12(30)
    add 26, 26, 9
    rotlwi 26, 26, 7
    .ifb \keep
    vxor 5, 5, 5
    mtvscr 5
    .else
    mtvscr \keep
    .endif
    .endm
