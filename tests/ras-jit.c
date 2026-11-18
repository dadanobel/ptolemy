/* SPDX-License-Identifier: GPL-2.0-or-later
 * Calls and returns of JIT code (the jump cache and its first level): through the real block translator, the
 * links and the jump-cache fills jit_run makes, against the interpreter - registers, LR, CTR, CR, XER and the stack's
 * memory. Programs: calls across pages with frames and cached registers alive around them; recursion 100 deep
 * with a conditional return; mtlr and blrl; a return to another frame (longjmp); bctrl; `bcl 20,31,.+4`; the
 * caller's code changed while the callee runs; the code buffer flushed while the callee runs; tlbie and mtsr inside
 * the callee; XER.SO set inside the callee; interrupts requested by another thread all along. Then the count of blr
 * lookups the first level did not answer: a few.
 * Arguments: `off` - without the
 * first-level jump cache (PT_JC_L1=0): the same results; `bench` - nanoseconds per call and return natively, with it
 * and without. */
#include "jit-test.h"
#include <time.h>
#include <unistd.h>
static uint8_t *memory;
static uint32_t *code;
enum { GUEST = 1 << 20, END = 0xf000, STACK = 0x80000 };
#define OP(o) ((uint32_t)(o) << 26)
#define ADDI(d, a, i) (OP(14) | (d) << 21 | (a) << 16 | ((i) & 0xffff))
#define ADDIS(d, a, i) (OP(15) | (d) << 21 | (a) << 16 | ((i) & 0xffff))
#define ORI(a, s, i) (OP(24) | (s) << 21 | (a) << 16 | ((i) & 0xffff))
#define LWZ(d, a, i) (OP(32) | (d) << 21 | (a) << 16 | ((i) & 0xffff))
#define STW(s, a, i) (OP(36) | (s) << 21 | (a) << 16 | ((i) & 0xffff))
#define STWU(s, a, i) (OP(37) | (s) << 21 | (a) << 16 | ((i) & 0xffff))
#define XO(d, a, b, xo) (OP(31) | (d) << 21 | (a) << 16 | (b) << 11 | (xo) << 1)
#define ADD(d, a, b) XO(d, a, b, 266)
#define ADDO(d, a, b) (XO(d, a, b, 266) | 1 << 10)
#define ADD_(d, a, b) (XO(d, a, b, 266) | 1)
#define MULLW(d, a, b) XO(d, a, b, 235)
#define CMPWI(a, i) (OP(11) | (a) << 16 | ((i) & 0xffff))
#define BC(bo, bi, to, at) (OP(16) | (bo) << 21 | (bi) << 16 | (((to) - (at)) & 0xfffc))
#define BR(to, at, lk) (OP(18) | (((to) - (at)) & 0x3fffffc) | (lk))
#define BLR 0x4e800020u
#define BLRL 0x4e800021u
#define BEQLR 0x4d820020u
#define BCTRL 0x4e800421u
#define MFLR(r) (0x7c0802a6u | (r) << 21)
#define MTLR(r) (0x7c0803a6u | (r) << 21)
#define MTCTR(r) (0x7c0903a6u | (r) << 21)
#define MFCR(r) (0x7c000026u | (r) << 21)
#define MTDEC(r) (0x7c1603a6u | (r) << 21) /* not translated: leaves JIT code for the interpreter */
#define ANDI_(a, s, i) (OP(28) | (s) << 21 | (a) << 16 | ((i) & 0xffff))
#define MTXER(r) (0x7c0103a6u | (r) << 21)
#define TLBIE(b) (0x7c000264u | (b) << 11)
#define MTSR(sr, s) (0x7c0001a4u | (s) << 21 | (sr) << 16)
static void words(uint32_t at, unsigned n, const uint32_t *w) {
    for (unsigned i = 0; i < n; i++)
        wbe32(memory + at + 4 * i, w[i]);
}
#define AT(a, ...) words((a), sizeof((uint32_t[]){__VA_ARGS__}) / 4, (uint32_t[]){__VA_ARGS__})
static CPU initial(void) {
    CPU c = {0};
    c.pc = 0x1000;
    c.mem = c.vbase = memory;
    c.jit_jc = jc;
    c.npreg = 1;
    c.preg[0] = (PRegion){0, GUEST, memory, 0};
    for (unsigned i = 0; i < 16; i++)
        c.crtab[i] = cr_make(i >> 3 & 1, i >> 2 & 1, i >> 1 & 1, i & 1);
    c.gpr[1] = STACK;
    c.gpr[4] = 1;
    return c;
}
static void (*hook)(CPU *); /* once, when the guest leaves JIT code at hook_pc with r31 = hook_r31 */
static uint32_t hook_pc, hook_r31;
static CPU *running; /* the interrupter's target */
static volatile int interrupts;
static uint64_t lookup_exits; /* lookups the jump cache did not answer either: the guest left JIT code */
/* As jit_run: translate, link, fill the jump cache after a lookup exit, step the interpreter where the translator asks. */
static void run(CPU *c) {
    uint32_t *t = NULL;
    int fill = 0;
    while (c->pc != END) {
        pthread_jit_write_protect_np(0);
        kso = c->so & 1; /* as jit_run: the variant follows XER.SO */
        if (!t)
            t = block(c);
        assert(t);
        if (fill)
            jc_fill(c, t, 0);
        pthread_jit_write_protect_np(1);
        int why = enter(c, t);
        t = NULL;
        fill = why == X_LOOKUP;
        lookup_exits += fill;
        if (hook && c->pc == hook_pc && c->gpr[31] == hook_r31) {
            hook(c);
            hook = NULL;
        }
        if (why == X_LINK) {
            uint32_t *site = (uint32_t *)c->jit_link;
            pthread_jit_write_protect_np(0);
            kso = c->so & 1;
            t = block(c);
            assert(t);
            b_at(site, t);
            __builtin___clear_cache((char *)site, (char *)(site + 1));
            pthread_jit_write_protect_np(1);
        } else if (why == X_IRQ) {
            c->irq = 0;
            interrupts++;
        } else if (why != X_LOOKUP && c->pc != END) /* the interpreter's instruction, a mode exit, XER.SO's other variant */
            assert(interp_step(c) == 0);
    }
}
static uint64_t lookups; /* blr lookups of the last equal(): the PT_STATS counter of exit_ind */
static void equal(const char *name) { /* the program in memory, from a fresh state, against the interpreter */
    static uint8_t *before, *after;
    if (!before)
        assert((before = malloc(GUEST)) && (after = malloc(GUEST)));
    memcpy(before, memory, GUEST);
    CPU c = initial(), r = c;
    void (*h)(CPU *) = hook;
    kso = 0;
    running = &c;
    lookup_exits = 0;
    run(&c);
    running = NULL;
    lookups = c.st_ind[1];
    memcpy(after, memory, GUEST);
    memcpy(memory, before, GUEST);
    for (hook = h; r.pc != END;) {
        if (hook && r.pc == hook_pc && r.gpr[31] == hook_r31) {
            hook(&r);
            hook = NULL;
        }
        assert(interp_step(&r) == 0);
    }
    int same = !memcmp(c.gpr, r.gpr, sizeof c.gpr) && c.lr == r.lr && c.ctr == r.ctr && !memcmp(c.cr, r.cr, sizeof c.cr) &&
               c.so == r.so && c.ov == r.ov && c.ca == r.ca && !memcmp(after + STACK - 0x4000, memory + STACK - 0x4000, 0x4100);
    if (getenv("RAS_TRACE"))
        fprintf(stderr, "ras-jit: %s: %d interrupts so far, %llu blr lookups\n", name, interrupts, (unsigned long long)lookups);
    if (!same)
        fprintf(stderr, "ras-jit: %s differs from the interpreter (r3 %08x/%08x r6 %08x/%08x r10 %08x/%08x lr %08x/%08x)\n", name,
                c.gpr[3], r.gpr[3], c.gpr[6], r.gpr[6], c.gpr[10], r.gpr[10], c.lr, r.lr);
    assert(same);
    pthread_jit_write_protect_np(0);
    kir = kpr = kso = 0;
    flush_all(); /* each program translates from nothing */
    pthread_jit_write_protect_np(1);
}
static void new_code(CPU *c) { /* the caller's code after the call changes while the callee runs (as icbi drops it) */
    (void)c;
    wbe32(memory + 0x1008, ADDI(6, 6, 2));
    pthread_jit_write_protect_np(0);
    for (int b = 0; b < nblk; b++)
        if (!blk[b].isdead && blk[b].pc >> 12 == 1)
            kill_block(b);
    pthread_jit_write_protect_np(1);
}
static void flush(CPU *c) { /* the code buffer starts over while the callee runs: the caller's code is gone */
    (void)c;
    pthread_jit_write_protect_np(0);
    flush_all();
    for (int i = 0; i < 2000; i++) /* other code where the callers were */
        E(0xD4200000);
    pthread_jit_write_protect_np(1);
}
static volatile int stop;
static void *interrupter(void *o) { /* 0.3 ms after a program starts, then twice as long each time: an emulated host
                                      * needs longer than that to get anywhere between two interrupts */
    (void)o;
    CPU *last = NULL;
    for (useconds_t wait = 300; !stop; usleep(running ? wait : 200)) {
        CPU *c = running;
        if (c != last)
            wait = 300;
        else if (c) {
            c->irq = 1;
            wait = wait < 100000 ? wait * 2 : wait;
        }
        last = c;
    }
    return NULL;
}
/* The bench programs: from 0x0ff0, n times { call at 0x1000 }, the loop's own state in r31 (not CTR: bctrl uses it). */
#define LOOP(n, call)                                                                                                   \
    AT(0x0ff8, ADDI(31, 0, n), ADDI(0, 0, 0)), AT(0x1000, call, ADDI(6, 6, 1), ADDI(31, 31, -1), CMPWI(31, 0),         \
                                                  BC(4, 2, 0x1000, 0x1010), BR(END, 0x1014, 0))
static double bench(void (*program)(void), unsigned calls) {
    program();
    CPU c = initial();
    c.pc = 0x0ff0;
    run(&c); /* translate and link */
    c = initial();
    c.pc = 0x0ff0;
    struct timespec a, b;
    clock_gettime(CLOCK_MONOTONIC, &a);
    run(&c);
    clock_gettime(CLOCK_MONOTONIC, &b);
    pthread_jit_write_protect_np(0);
    flush_all();
    pthread_jit_write_protect_np(1);
    return ((double)(b.tv_sec - a.tv_sec) * 1e9 + (double)(b.tv_nsec - a.tv_nsec)) / calls;
}
enum { BENCH = 20000 }; /* loop count of the bench programs: 16 bits of addi */
static void b_leaf(void) {
    AT(0x0ff0, ADDI(0, 0, 0), ADDI(0, 0, 0));
    LOOP(BENCH, BR(0x3000, 0x1000, 1));
    AT(0x3000, ADDI(7, 7, 1), BLR);
}
static void b_nest(void) { /* three frames deep across three pages, at different offsets in them: the first level's
                            * cells are chosen by the offset */
    AT(0x0ff0, ADDI(0, 0, 0), ADDI(0, 0, 0));
    LOOP(BENCH, BR(0x3040, 0x1000, 1));
    AT(0x3040, MFLR(0), STW(0, 1, 8), STWU(1, 1, -64), BR(0x5080, 0x304c, 1), ADDI(1, 1, 64), LWZ(0, 1, 8), MTLR(0), BLR);
    AT(0x5080, MFLR(0), STW(0, 1, 8), STWU(1, 1, -64), BR(0x70c0, 0x508c, 1), ADDI(1, 1, 64), LWZ(0, 1, 8), MTLR(0), BLR);
    AT(0x70c0, ADDI(7, 7, 1), BLR);
}
static void b_bctrl(void) {
    LOOP(BENCH, BCTRL);
    AT(0x0ff0, ADDI(12, 0, 0x3000), MTCTR(12));
    AT(0x3000, ADDI(7, 7, 1), BLR);
}
int main(int argc, char **argv) {
    const char *mode = argc > 1 ? argv[1] : "on";
    int off = !strcmp(mode, "off"), timing = !strcmp(mode, "bench");
    l1_on = !off;
    if (getenv("PT_JC_BITS")) /* as init(): the jump cache's size; the emitted index and jc_fill() must agree at any */
        jc_bits = (unsigned)atoi(getenv("PT_JC_BITS"));
    assert(jc_bits >= 16 && jc_bits <= JC_MAX);
    jit_stats_on = 1; /* the counters of exit_ind */
    code = mmap(NULL, 1 << 24, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANONYMOUS | MAP_JIT, -1, 0);
    memory = mmap(NULL, GUEST, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    assert(code != MAP_FAILED && memory != MAP_FAILED);
    trampoline(code);
    buf = code;
    bufend = code + (1 << 22);
    jmsr = MSR_750;
    zbase = 0;
    maxblk = 128;
    settle_gen();
    flush_all();
    __builtin___clear_cache((char *)code, (char *)cp);
    pthread_jit_write_protect_np(1);
    if (timing) {
        jit_stats_on = 0;
        static const struct {
            const char *name;
            void (*program)(void);
            unsigned calls;
        } b[] = {{"leaf call to another page", b_leaf, BENCH}, {"three frames deep", b_nest, 3 * BENCH}, {"bctrl", b_bctrl, BENCH}};
        for (unsigned k = 0; k < 3; k++) {
            double ns[2];
            for (int on = 0; on < 2; on++) {
                l1_on = on;
                double best = 1e9;
                for (int i = 0; i < 9; i++) {
                    double t = bench(b[k].program, b[k].calls);
                    best = t < best ? t : best;
                }
                ns[on] = best;
            }
            printf("ras-bench %-26s ns a call and return: %.2f as before, %.2f first-level cache (%+.0f%%)\n", b[k].name, ns[0],
                   ns[1], (ns[1] / ns[0] - 1) * 100);
        }
        return 0;
    }
    alarm(300);
    pthread_t t;
    if (getenv("RAS_NO_IRQ"))
        stop = 1;
    assert(!pthread_create(&t, NULL, interrupter, NULL));
    uint64_t hits[4];
    /* 1. A leaf on another page, 200 calls. Every program starts at 0x1000. */
    AT(0x1000, ADDI(31, 0, 200), BR(0x2000, 0x1004, 1), ADDI(6, 6, 1), ADDI(31, 31, -1), CMPWI(31, 0), BC(4, 2, 0x1004, 0x1014),
       BR(END, 0x1018, 0));
    AT(0x2000, ADDI(7, 7, 1), BLR); /* the callee on another page */
    equal("leaf on another page");
    hits[0] = lookups;
    assert(lookup_exits <= 20); /* 200 calls and 200 returns: the cache answers all but the first of each */
    /* 2. Frames three deep over three pages; r29, r30 (the block's register pool) written before each call and read
     * after it, the callees using the pool for other registers. */
    AT(0x1000, ADDI(31, 0, 200), ADDI(30, 30, 3), ADDI(29, 29, 5), BR(0x2000, 0x100c, 1), ADD(6, 30, 29), ADD(3, 3, 6),
       ADDI(31, 31, -1), CMPWI(31, 0), BC(4, 2, 0x1004, 0x1020), BR(END, 0x1024, 0));
    AT(0x2000, MFLR(0), STW(0, 1, 8), STWU(1, 1, -64), ADDI(28, 28, 7), ADDI(27, 3, 1), BR(0x4000, 0x2014, 1), ADD(3, 3, 28),
       ADD(3, 3, 27), ADDI(1, 1, 64), LWZ(0, 1, 8), MTLR(0), BLR);
    AT(0x4000, MFLR(0), STW(0, 1, 8), STWU(1, 1, -64), ADDI(26, 26, 9), BR(0x6000, 0x4010, 1), ADD(3, 3, 26), ADDI(1, 1, 64),
       LWZ(0, 1, 8), MTLR(0), BLR);
    AT(0x6000, ADDI(25, 3, 11), MULLW(3, 25, 25), BLR);
    equal("three frames over three pages");
    hits[1] = lookups;
    /* 3. Recursion 100 deep with a conditional return, twice. */
    AT(0x1000, ADDI(3, 0, 100), BR(0x2000, 0x1004, 1), ADDI(6, 6, 1), ADDI(3, 0, 100), BR(0x2000, 0x1010, 1), ADDI(6, 6, 1),
       BR(END, 0x1018, 0));
    AT(0x2000, CMPWI(3, 0), BEQLR, MFLR(0), STW(0, 1, 8), STWU(1, 1, -16), ADDI(3, 3, -1), BR(0x2000, 0x2018, 1), ADDI(4, 4, 3),
       ADDI(1, 1, 16), LWZ(0, 1, 8), MTLR(0), BLR);
    equal("recursion 100 deep");
    hits[2] = lookups;
    /* 4. mtlr and blrl: a call through LR, whose return has no entry; then a blr to an address no call left. */
    AT(0x1000, ADDI(5, 0, 0x3000), MTLR(5), BLRL, ADDI(6, 6, 1), ADDI(5, 0, 0x1020), MTLR(5), BLR, ADDI(6, 6, 100),
       /* 0x1020 */ ADDI(6, 6, 2), BR(END, 0x1024, 0));
    AT(0x3000, ADDI(7, 7, 1), BLR);
    equal("mtlr and blrl");
    /* 5. A return to an outer frame: B returns to main's address, past A's; then calls go on over the stale entry. */
    AT(0x1000, ADDI(20, 0, 0x1008), BR(0x3000, 0x1004, 1), /* 0x1008 */ ADDI(6, 6, 1), BR(0x5000, 0x100c, 1),
       BR(0x5000, 0x1010, 1), BR(END, 0x1014, 0));
    AT(0x3000, MFLR(0), STW(0, 1, 8), BR(0x4000, 0x3008, 1), ADDI(7, 7, 100), LWZ(0, 1, 8), MTLR(0), BLR);
    AT(0x4000, MTLR(20), BLR);
    AT(0x5000, ADDI(7, 7, 1), BLR);
    equal("return to an outer frame");
    /* 6. bctrl, and `bcl 20,31,.+4` (reads the pc, calls nothing) inside the callee. */
    AT(0x1000, ADDI(31, 0, 200), ADDI(12, 0, 0x3000), MTCTR(12), BCTRL, ADDI(6, 6, 1), ADDI(31, 31, -1), CMPWI(31, 0),
       BC(4, 2, 0x1004, 0x101c), BR(END, 0x1020, 0));
    AT(0x3000, MFLR(0), BC(20, 31, 0x3008, 0x3004) | 1, MFLR(9), ADD(7, 7, 9), MTLR(0), BLR);
    equal("bctrl and bcl .+4");
    hits[3] = lookups;
    assert(lookup_exits <= 20);
    /* 7. The caller's code after the call changes while the callee runs its second call (the jump cache holds the
     * old code by then). */
    AT(0x1000, ADDI(31, 0, 3), BR(0x3000, 0x1004, 1), ADDI(6, 6, 1), ADDI(31, 31, -1), CMPWI(31, 0), BC(4, 2, 0x1004, 0x1014),
       BR(END, 0x1018, 0));
    AT(0x3000, ADDI(7, 7, 1), MTDEC(7), ADDI(7, 7, 1), BLR);
    hook = new_code, hook_pc = 0x3004, hook_r31 = 2;
    equal("the caller's code changed inside the callee");
    /* 8. The code buffer flushed while the callee runs its third call: the return must not reach the code that was there. */
    AT(0x1000, ADDI(31, 0, 5));
    hook = flush, hook_pc = 0x3004, hook_r31 = 3;
    equal("flush inside the callee");
    /* 9. tlbie and mtsr inside the callee: both leave JIT code, and the stack starts over. */
    AT(0x1008, ADDI(6, 6, 1));
    AT(0x3000, ADDI(7, 7, 1), TLBIE(7), MTSR(3, 7), ADDI(7, 7, 1), BLR);
    equal("tlbie and mtsr inside the callee");
    /* 10. XER.SO cleared before each call and set inside the callee every other time, 20 times: once the jump cache
     * knows both variants no exit lies between the call and the return, and the return must go on in the SO variant of
     * the caller that matches, where add. copies SO into CR0. */
    AT(0x1000, ADDI(31, 0, 20), ADDI(9, 0, 0), MTXER(9), BR(0x3000, 0x100c, 1), ADD_(3, 3, 4), MFCR(10), ADD(6, 6, 10),
       ADDI(31, 31, -1), CMPWI(31, 0), BC(4, 2, 0x1004, 0x1024), BR(END, 0x1028, 0));
    AT(0x3000, MFLR(0), ANDI_(11, 31, 1), BC(12, 2, 0x3014, 0x3008), ADDIS(8, 0, 0x7fff), ADDO(8, 8, 8),
       /* 0x3014 */ BR(0x3100, 0x3014, 1), MTLR(0), BLR);
    AT(0x3100, ADDI(7, 7, 1), BLR); /* a call and return inside the SO variant */
    equal("XER.SO set inside the callee");
    /* 11. The three frames again, 30 000 times: long enough for the other thread's interrupts to land inside calls. */
    AT(0x1000, ADDI(31, 0, 30000), ADDI(30, 30, 3), ADDI(29, 29, 5), BR(0x2000, 0x100c, 1), ADD(6, 30, 29), ADD(3, 3, 6),
       ADDI(31, 31, -1), CMPWI(31, 0), BC(4, 2, 0x1004, 0x1020), BR(END, 0x1024, 0));
    AT(0x2000, MFLR(0), STW(0, 1, 8), STWU(1, 1, -64), ADDI(28, 28, 7), ADDI(27, 3, 1), BR(0x4000, 0x2014, 1), ADD(3, 3, 28),
       ADD(3, 3, 27), ADDI(1, 1, 64), LWZ(0, 1, 8), MTLR(0), BLR);
    AT(0x4000, MFLR(0), STW(0, 1, 8), STWU(1, 1, -64), ADDI(26, 26, 9), BR(0x6000, 0x4010, 1), ADD(3, 3, 26), ADDI(1, 1, 64),
       LWZ(0, 1, 8), MTLR(0), BLR);
    AT(0x6000, ADDI(25, 3, 11), MULLW(3, 25, 25), BLR);
    equal("interrupts inside calls");
    stop = 1;
    pthread_join(t, NULL);
    /* The first level answers all but the first lookups of a run of JIT code. */
    if (off)
        assert(hits[0] >= 200 && hits[1] >= 600 && hits[3] >= 200);
    else
        assert(hits[0] <= 20 && hits[1] <= 60 && hits[2] <= 172 && hits[3] <= 20);
    printf("ras-jit (%s): 11 programs equal to the interpreter, %d interrupts inside them; blr lookups: leaf %llu of 200 "
           "returns, three frames %llu of 600, recursion %llu of 202, bctrl %llu of 200 (lookups the first level did not answer)\n",
           mode, interrupts, (unsigned long long)hits[0], (unsigned long long)hits[1],
           (unsigned long long)hits[2], (unsigned long long)hits[3]);
    return 0;
}
