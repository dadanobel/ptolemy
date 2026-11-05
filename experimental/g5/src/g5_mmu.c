/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "g5.h"
#define PA_MASK ((UINT64_C(1) << 42) - 1)
#define VSID_MASK ((UINT64_C(1) << 37) - 1)

PtG5Result pt_g5_slbmte(PtG5Mmu *m, uint64_t rs, uint64_t rb, bool sf) {
    unsigned index = (unsigned)(rb & 0xfff);
    if (index >= 64 || !(rb & (1u << 27)) || (rb & UINT64_C(0x07fff000)) || (rs & 0x7f) || (rs >> 12) > VSID_MASK ||
        (!sf && rb >> 32))
        return PT_G5_BAD_CONFIG;
    uint64_t esid = rb >> 28;
    for (unsigned i = 0; i < 64; i++)
        if (i != index && m->slb[i].valid && m->slb[i].esid == esid)
            return PT_G5_BAD_CONFIG;
    m->slb[index] = (PtG5Slb){esid, rs >> 12, (uint16_t)(rs & 0xf80), true};
    m->generation++;
    return PT_G5_OK;
}
PtG5Result pt_g5_mtsr(PtG5Mmu *m, unsigned index, uint32_t sr) {
    if (index >= 16 || (sr & 0x88000000u))
        return PT_G5_BAD_CONFIG;
    uint64_t rs = ((uint64_t)(sr & 0x07ffffff) << 12) | ((sr >> 19) & 0xe00);
    return pt_g5_slbmte(m, rs, ((uint64_t)index << 28) | (1u << 27) | index, false);
}
PtG5Result pt_g5_mfsr(const PtG5Mmu *m, unsigned index, uint32_t *out) {
    if (index >= 16 || !out)
        return PT_G5_BAD_CONFIG;
    const PtG5Slb *s = &m->slb[index];
    if (!s->valid || s->esid != index || s->vsid > 0x07ffffff || (s->flags & 0x180))
        return PT_G5_UNSUPPORTED;
    *out = (uint32_t)s->vsid | ((uint32_t)(s->flags & 0xe00) << 19);
    return PT_G5_OK;
}
void pt_g5_slbie(PtG5Mmu *m, uint64_t rb) {
    for (unsigned i = 0; i < 64; i++)
        if (m->slb[i].esid == rb >> 28 && !!(m->slb[i].flags & PT_G5_SLB_C) == !!(rb & (1u << 27)))
            m->slb[i].valid = false;
    m->generation++;
}
void pt_g5_slbia(PtG5Mmu *m) {
    for (unsigned i = 1; i < 64; i++)
        m->slb[i].valid = false;
    m->generation++;
}
PtG5Result pt_g5_slbmfe(const PtG5Mmu *m, unsigned index, bool vsid, uint64_t *out) {
    if (index >= 64 || !out)
        return PT_G5_BAD_CONFIG;
    const PtG5Slb *s = &m->slb[index];
    *out = !s->valid ? 0 : vsid ? (s->vsid << 12) | s->flags : (s->esid << 28) | (1u << 27);
    return PT_G5_OK;
}
PtG5Result pt_g5_translate(PtG5Mmu *m, PtG5Memory *mem, uint64_t ea, uint64_t msr, PtG5Access access,
                           PtG5Translation *out) {
    if (!out || access > PT_G5_FETCH)
        return PT_G5_BAD_CONFIG;
    *out = (PtG5Translation){0};
    if (msr & 1)
        return PT_G5_UNSUPPORTED; /* Little-endian address swizzling is not implemented. */
    if (!(msr & PT_G5_MSR_SF))
        ea = (uint32_t)ea;
    bool pr = !!(msr & PT_G5_MSR_PR);
    bool translated = pr || (msr & (access == PT_G5_FETCH ? PT_G5_MSR_IR : PT_G5_MSR_DR));
    if (!translated) {
        if (ea > PA_MASK)
            return PT_G5_BUS;
        /* HID cache-inhibit overrides belong to the future CPU adapter. */
        *out = (PtG5Translation){ea, 0, 4096, 3, true, 0};
        return PT_G5_OK;
    }
    const PtG5Slb *s = NULL;
    for (unsigned i = 0; i < 64; i++)
        if (m->slb[i].valid && m->slb[i].esid == ea >> 28) {
            if (s)
                return PT_G5_BAD_CONFIG;
            s = &m->slb[i];
        }
    if (!s)
        return PT_G5_SEGMENT;
    PtG5Walk walk = {m->sdr1, m->prepare_pte, m->prepare_opaque};
    return pt_g5_walk_segment(&walk, mem, s, ea, pr, access, out);
}
PtG5Result pt_g5_walk_segment(const PtG5Walk *m, PtG5Memory *mem, const PtG5Slb *s, uint64_t ea, bool pr,
                              PtG5Access access, PtG5Translation *out) {
    if (!m || !mem || !s || !out || access > PT_G5_FETCH || !s->valid || s->esid != ea >> 28)
        return PT_G5_BAD_CONFIG;
    *out = (PtG5Translation){0};
    unsigned shift = s->flags & PT_G5_SLB_L ? 24 : 12;
    unsigned ht = (unsigned)(m->sdr1 & 31);
    if (ht > 24 || (m->sdr1 & UINT64_C(0x3ffe0)))
        return PT_G5_BAD_CONFIG;
    uint64_t size = UINT64_C(1) << (18 + ht), base = m->sdr1 & ~UINT64_C(0x3ffff);
    if ((base & (size - 1)) || base > PA_MASK || size > PA_MASK + 1 - base)
        return PT_G5_BAD_CONFIG;
    uint64_t page_mask = (UINT64_C(1) << shift) - 1;
    uint64_t hash = s->vsid ^ ((ea & 0x0fffffffull) >> shift);
    /* Keep VSID separate: a 970 virtual address has 65 bits. */
    uint64_t avpn = (s->vsid << 5) | ((ea & ~page_mask & 0x0fffffffull) >> 23);
    unsigned key = !!(s->flags & (pr ? PT_G5_SLB_KP : PT_G5_SLB_KS));
    for (unsigned retry = 0; retry < 8; retry++) {
        bool changed = false;
        for (unsigned h = 0; h < 2 && !changed; h++) {
            uint64_t group = base | (((h ? ~hash : hash) & ((size >> 7) - 1)) << 7);
            for (unsigned slot = 0; slot < 8; slot++) {
                uint64_t pa = group + slot * 16, pte[2];
                if (m->prepare_pte) {
                    PtG5Result r = m->prepare_pte(m->prepare_opaque, pa, 16, false);
                    if (r != PT_G5_OK)
                        return r;
                }
                PtG5Result r = pt_g5_pte_read(mem, pa, pte);
                if (r != PT_G5_OK)
                    return r;
                if ((pte[0] & 3) != (1u | (h << 1)) || pte[0] >> 7 != avpn)
                    continue;
                if (pte[0] & 4)
                    return PT_G5_BAD_CONFIG;
                unsigned pp = (unsigned)(pte[1] & 3);
                bool readable = !(key && pp == 0);
                bool writable = readable && pp != 3 && !(key && pp == 1);
                if (access == PT_G5_FETCH && ((s->flags & PT_G5_SLB_N) || (pte[1] & 0xc))) {
                    out->fault_bits = 0x10000000;
                    return PT_G5_PROTECTION;
                }
                if (!readable || (access == PT_G5_STORE && !writable)) {
                    out->fault_bits = 0x08000000;
                    return PT_G5_PROTECTION;
                }
                uint64_t rpn = pte[1] & ~UINT64_C(0xfff);
                if (rpn > PA_MASK || (rpn & page_mask))
                    return PT_G5_BAD_CONFIG;
                uint64_t updated = pte[1] | 0x100;
                if (access == PT_G5_STORE)
                    updated = (updated | 0x90) & ~UINT64_C(0x40);
                if (updated != pte[1]) {
                    if (m->prepare_pte) {
                        r = m->prepare_pte(m->prepare_opaque, pa + 8, 8, true);
                        if (r != PT_G5_OK)
                            return r;
                    }
                    r = pt_g5_pte_commit(mem, pa, pte, updated);
                    if (r == PT_G5_RETRY) {
                        changed = true;
                        break;
                    }
                    if (r != PT_G5_OK)
                        return r;
                }
                uint8_t wimg = (uint8_t)(((pte[1] >> 3) & 5) | 2);
                if (access != PT_G5_STORE && (wimg & 4))
                    wimg |= 1;
                *out = (PtG5Translation){rpn | (ea & page_mask),       pa, UINT64_C(1) << shift, wimg,
                                         writable && (updated & 0x80), 0};
                return PT_G5_OK;
            }
        }
        if (!changed) {
            out->fault_bits = 0x40000000 | (access != PT_G5_FETCH && shift == 24 ? 0x10000 : 0);
            return PT_G5_PAGE;
        }
    }
    return PT_G5_RETRY;
}
unsigned pt_g5_dcbz_size(uint64_t hid5, uint32_t instruction) {
    if (instruction & (1u << 21))
        return 128;
    if (hid5 & (1u << 6))
        return 0;
    return hid5 & (1u << 7) ? 32 : 128;
}
