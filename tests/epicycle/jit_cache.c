// Cache invalidation regression: collisions, generation wrap and the real X_LOOKUP path.
#include "jit.c"
#include <assert.h>
#include <limits.h>
#include <time.h>

int sys_call(CPU *c) { (void)c; return 0; }
static int phase; static uint64_t old5;
static int fake_enter(CPU *c, void *code) {
    (void)code;
    switch (phase++) {
    case 0: assert(jc[0].pc == ~0ull); c->pc = 0x20; return X_LOOKUP;
    case 1: assert(jc[8].pc == 0x20 && jc[8].code); c->pc = 0x40020; return X_LOOKUP;
    case 2: assert(jc[8].pc == 0x40020 && jc[8].code); c->pc = 0x20; return X_LOOKUP;
    case 3: assert(jc[8].pc == 0x20 && jc[8].code); c->xgen++; return X_IRQ;
    case 4: assert(jc[8].pc == ~0ull && !jc[8].code); c->pc = 0x40020; return X_LOOKUP;
    case 5: assert(jc[8].pc == 0x40020 && jc[8].code); old5 = jc[8].code; c->icbi_ea[0] = 0x40020; c->icbi_n = 1; c->cgen++; return X_IRQ;   // icbi+isync: this page's block is dropped
    case 6: { int dead = 0; for (int i = 0; i < nblk; i++) if ((uint64_t)blk[i].code == old5) dead = blk[i].isdead;   // the old translation is dropped:
              assert(dead && lookup(0x40020, 0x40) != (uint32_t *)old5); c->halted = 1; return X_IRQ; }   // its cache cell leads to the "dead" exit, the table holds the new one
    default: abort();
    }
}
static double now(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec + t.tv_nsec * 1e-9; }
int main(int argc, char **argv) {
    (void)argv;
    uint32_t scratch[256] = {0}; exit_common = scratch;
    flush_all();
    uint32_t keys[] = {0, 1, 0x400000, 0x800000, 0xfffffffc};
    for (unsigned i = 0; i < sizeof keys/sizeof keys[0]; i++) insert(keys[i], 0, scratch+i);
    for (unsigned i = 0; i < sizeof keys/sizeof keys[0]; i++) assert(lookup(keys[i], 0) == scratch+i);
    flush_all();
    for (unsigned i = 0; i < sizeof keys/sizeof keys[0]; i++) assert(!lookup(keys[i], 0));
    insert(0x400000, 0, scratch);
    assert(lookup(0x400000, 0) == scratch && !lookup(0, 0));
    insert(0x1000, 7, scratch + 1);                             // the same pc on another physical page is another block
    assert(lookup(0x1000, 7) == scratch + 1 && !lookup(0x1000, 8) && !lookup(0x1000, 0));
    gen = UINT_MAX; flush_all();
    for (unsigned i = 0; i < sizeof keys/sizeof keys[0]; i++) assert(!lookup(keys[i], 0));
    if (argc > 1) {
        double start = now();
        for (unsigned i = 0; i < 20000; i++) {
            insert(0x1000, 0, scratch);
            __asm__ volatile("" : : "r"(ht) : "memory");
            flush_all();
        }
        printf("20000 complete invalidations: %.6f s\n", now()-start);
    }
    CPU c = {0}; c.mem = calloc(1, 0x41000); c.preg[0] = (PRegion){0, 0x41000, c.mem}; c.npreg = 1;
    wbe32(c.mem, 0x48000000); wbe32(c.mem+0x20, 0x48000000); wbe32(c.mem+0x40020, 0x48000000);
    jit_vm_buf = calloc(1, 64 << 20); jit_vm_size = 64 << 20;
    jit_vm_jc = calloc(1 << 16, sizeof(JC)); jit_vm_jc_va = (uint64_t)jit_vm_jc;
    jit_vm_enter = fake_enter;
    jit_run(&c);
    assert(phase == 7 && !c.exit_code);
    puts("JIT cache invalidation: PASS");
    free(c.mem); free(jit_vm_buf); free(jit_vm_jc);
    return 0;
}
