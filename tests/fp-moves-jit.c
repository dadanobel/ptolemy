/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "jit-test.h"

static unsigned emit(uint32_t *code, unsigned xo, unsigned cache, unsigned single, unsigned alias) {
    pthread_jit_write_protect_np(0);
    cp = code;
    E(0xa9bf7bfb);
    E(0xaa0003fb); /* stp x27,x30; mov x27,x0 */
    exit_common = code + 512;
    memset(cmap, -1, sizeof cmap);
    cwr = 0;
    memset(fmap, -1, sizeof fmap);
    fmap[1] = (cache & 1) ? 25 : -1;
    fmap[2] = (cache & 2) ? 26 : -1;
    fcache_active = cache != 0;
    fsf = single ? 2 : 0;
    fvalid = ((cache & 1) ? 2 : 0) | ((cache & 2) ? 4 : 0);
    E(0xFD400000u | (OFF(fpr) / 8 + 1u) << 10 | ENV << 5 | 25u);
    E(0xFD400000u | (OFF(fpr) / 8 + 2u) << 10 | ENV << 5 | 26u);
    uint32_t *start = cp;
    assert(!tr(63u << 26 | (alias ? 1u : 2u) << 21 | 1u << 11 | xo << 1, 0x1000));
    unsigned words = (unsigned)(cp - start);
    E(0xFD000000u | 1u << 5 | 25u);
    E(0xFD000000u | 1u << 10 | 1u << 5 | 26u); /* report cached bits */
    b_to(exit_common);
    cp = exit_common;
    E(0xa8c17bfb);
    E(0xd65f03c0);
    __builtin___clear_cache((char *)code, (char *)cp);
    pthread_jit_write_protect_np(1);
    return words;
}
int main(void) {
    uint32_t *code = mmap(0, 16384, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANON | MAP_JIT, -1, 0);
    assert(code != MAP_FAILED);
    jsys = 0;
    fp_hoist = 0;
    const unsigned ops[] = {72, 40, 264, 136};
    const uint64_t edge[] = {0,
                             1,
                             UINT64_C(0x8000000000000000),
                             UINT64_C(0x7ff0000000000000),
                             UINT64_C(0xfff0000000000000),
                             UINT64_C(0x7ff0000000000001),
                             UINT64_C(0xfff8123456789abc)};
    unsigned checks = 0, old_words = 0, new_words = 0;
    for (unsigned op = 0; op < 4; op++)
        for (unsigned cache = 0; cache < 4; cache++)
            for (unsigned single = 0; single < 2; single++)
                for (unsigned alias = 0; alias < 2; alias++) {
                    /* A destination written by a bit move is classified double in the real dry pass. */
                    if (alias && single)
                        continue;
                    fp_moves_enabled = 0;
                    unsigned old = emit(code, ops[op], cache, single, alias);
                    old_words += old;
                    fp_moves_enabled = 1;
                    unsigned next = emit(code, ops[op], cache, single, alias);
                    new_words += next;
                    assert(next <= old);
                    for (unsigned i = 0; i < 4096; i++) {
                        CPU c = {0};
                        uint64_t u = i < sizeof edge / sizeof *edge ? edge[i] : ((uint64_t)rnd() << 32 | rnd());
                        memcpy(&c.fpr[1], &u, 8);
                        c.fpscr = rnd();
                        c.fs_kind = 29;
                        c.fs_x[0] = 3;
                        c.fs_x[1] = 4;
                        c.fs_x[2] = 5;
                        CPU before = c;
                        uint64_t expected = u;
                        if (ops[op] == 40)
                            expected ^= UINT64_C(1) << 63;
                        if (ops[op] == 264)
                            expected &= ~(UINT64_C(1) << 63);
                        if (ops[op] == 136)
                            expected |= UINT64_C(1) << 63;
                        uint64_t cached_bits[2];
                        ((void (*)(CPU *, uint64_t *))(void *)code)(&c, cached_bits);
                        memcpy(&before.fpr[alias ? 1 : 2], &expected, 8);
                        assert(!memcmp(c.fpr, before.fpr, sizeof c.fpr) && c.fpscr == before.fpscr &&
                               c.fs_kind == before.fs_kind);
                        assert(!memcmp(c.fs_x, before.fs_x, sizeof c.fs_x));
                        for (unsigned f = 1; f <= 2; f++)
                            if (fcache_active && fmap[f] >= 0)
                                assert(!memcmp(&cached_bits[f - 1], &before.fpr[f], 8));
                        checks++;
                    }
                }
    assert(new_words < old_words);
    printf("FP moves: %u exact bit/debt checks, emitted words %u -> %u PASS\n", checks, old_words, new_words);
    return 0;
}
