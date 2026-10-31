# Live HVF check: NULL arithmetic, lwz r3,-4(r9) with r9 = 0 (EA 0xFFFFFFFC). On a real Mac only this process dies
# (SIGBUS/SIGSEGV); the JIT must not form a 64-bit VA below 0, which made the whole emulator exit(3).
# Build (host): zig cc -target powerpc-freestanding-none -mcpu=750 -c null-arith.S -o na.o, then link it statically
#   (zig ld.lld -static -nostdlib) and convert the ELF to a Mach-O executable; that linker script and converter are
#   not part of this tree.
    .text
    .globl _start
_start:
    li 9,0
    lwz 3,-4(9)
    li 0,1
    li 3,0
    sc
    trap
    .section .rodata
    .long 0
    .data
    .long 0
    .bss
    .space 4
