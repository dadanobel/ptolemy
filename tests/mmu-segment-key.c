/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <Hypervisor/Hypervisor.h>
static hv_return_t sink_ttbr(hv_vcpu_t v, hv_sys_reg_t r, uint64_t x) {
    (void)v;
    (void)r;
    (void)x;
    return HV_SUCCESS;
}
#define hv_vcpu_set_sys_reg sink_ttbr
#define PT_NO_MAIN
#include "../epicycle/hvf.c"
#include <assert.h>
int jit_stats_on;
static uint64_t entry(unsigned segment, unsigned pr, uint32_t ea) {
    struct St *s = store(1, segment);
    uint64_t p = s->l2[pr][ea >> 21 & 127];
    return p & 1 ? l3tab(p)[ea >> 12 & 511] : 0;
}
int main(void) {
    eng = calloc(1, ENG_SIZE);
    assert(eng);
    nslot = 2;
    vm_tlbi_on = 1;
    for (unsigned s = 0; s < 2; s++) {
        slot[s].used = 1;
        slot[s].gen = clear_gen;
        slot[s].asid[0] = 4 + s * 2;
        slot[s].asid[1] = 5 + s * 2;
        for (unsigned g = 0; g < 16; g++)
            slot[s].sr[g] = 1;
    }
    uint32_t a = 0x6f755000, b = 0x1f755000;
    cur = 0;
    assert(store(1, 6) != store(1, 1));
    assert(!map_page(1, a, a, 1));
    assert(!map_page(1, b, b, 1));
    assert((entry(6, 0, a) & 0xfffff000) == a);
    assert((entry(1, 0, b) & 0xfffff000) == b);
    cur = 1;
    assert(!map_page(1, a, a, 1));
    assert(!map_page(2, b, 0x345000, 0));
    assert((entry(1, 0, b) & 0xfffff000) == b && (entry(1, 1, b) & 0xfffff000) == 0x345000);
    cur = 0;
    CPU *c = ENVC;
    vm_dtlbie(c, &a, 1);
    assert(!entry(6, 0, a) && !entry(1, 0, b) && !entry(1, 1, b));
    unsigned seen = 0;
    for (unsigned i = 0; i < c->tlbi_n; i++) {
        uint64_t va = c->tlbi_va[i] << 12;
        if (va == a)
            seen |= 1;
        if (va == b)
            seen |= 2;
        if (va == a + (1ull << 32))
            seen |= 4;
        if (va == b + (1ull << 32))
            seen |= 8;
    }
    assert(seen == 15);
    free(eng);
    puts("PASS MMU: same SR/page in different segments, PR roots, slots, invalidation aliases");
}
