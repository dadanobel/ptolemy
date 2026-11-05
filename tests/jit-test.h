/* SPDX-License-Identifier: GPL-2.0-or-later
 * Shared by the JIT fixtures of tools/build-perf.sh: the engine (jit.c), rnd(), run1() and the native trampoline. */
#include "../epicycle/jit.c"
#include <assert.h>
#include <pthread.h>
int sys_call(CPU *c) {
    (void)c;
    abort();
}

static uint64_t rng = 0x9e3779b97f4a7c15u; /* xorshift64 state; a fixture may seed it first */
static inline uint32_t rnd(void) {
    rng ^= rng << 13;
    rng ^= rng >> 7;
    rng ^= rng << 17;
    return (uint32_t)rng;
}

/* Translate the one instruction `in` (next pc 0x2000) at code, between stp/mov and ldp/ret, and run it on c. */
static inline void run1(uint32_t *code, uint32_t in, CPU *c) {
    memset(cmap, -1, sizeof cmap);
    cwr = 0;
    pthread_jit_write_protect_np(0);
    cp = code;
    E(0xa9bf7bfb);
    E(0xaa0003fb); /* stp x27, x30, [sp, #-16]!; mov x27, x0 */
    exit_common = code + 4096;
    assert(tr(in, 0x2000) == 0);
    b_to(exit_common);
    cp = exit_common;
    E(0xa8c17bfb);
    E(0xd65f03c0);
    pthread_jit_write_protect_np(1);
    __builtin___clear_cache((char *)code, (char *)(exit_common + 2));
    ((void (*)(CPU *))(void *)code)(c);
}

/* The engine's native trampoline at code, for enter(); leaves the JIT region writable. It preserves the host ABI:
 * x18 is macOS-reserved; the VM may use it, a native fixture must not. */
static inline void trampoline(uint32_t *code) {
    NPOOL = 8;
    memcpy(POOL, (int8_t[]){15, 19, 20, 21, 22, 23, 24, 25}, 8);
    pthread_jit_write_protect_np(0);
    cp = code;
    E(0xA9800000u | (unsigned)(-12 & 0x7f) << 15 | 30u << 10 | 31u << 5 | 29);
    for (unsigned i = 0; i < 5; i++)
        E(0xA9000000u | (2 + 2 * i) << 15 | (20 + 2 * i) << 10 | 31u << 5 | (19 + 2 * i));
    E(0xAA0003FBu);
    E(0xAA0103F1u);
    ldrx(MEM, ENV, OFF(vbase));
    load_guest();
    E(0xD61F0220u);
    exit_common = cp;
    store_guest();
    ldrw(0, ENV, OFF(jit_exit));
    for (unsigned i = 0; i < 5; i++)
        E(0xA9400000u | (2 + 2 * i) << 15 | (20 + 2 * i) << 10 | 31u << 5 | (19 + 2 * i));
    E(0xA8C00000u | 12u << 15 | 30u << 10 | 31u << 5 | 29);
    E(0xD65F03C0u);
    enter = (int (*)(CPU *, void *))(void *)code;
}
