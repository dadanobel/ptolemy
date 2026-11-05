/* SPDX-License-Identifier: GPL-2.0-or-later
 * 970MP UM 5.1-5.2: full-width physical bus and uncached SLB/HPT reference walk. */
#include "ppc.h"
#include "../experimental/g5/src/g5.h"
#define PA_MASK ((UINT64_C(1) << 42) - 1)
struct Pt970Bus {
    PtG5Memory memory;
    CPU *cpu[4];
    pthread_rwlock_t instructions;
};
/* 970 bring-up: the shared physical bus of up to 4 CPUs (RAM banks and device regions). */
Pt970Bus *ppc970_bus_create(const Pt970Region *regions, unsigned count) {
    if (!regions || !count || count > 8)
        return NULL;
    Pt970Bus *b = calloc(1, sizeof *b);
    if (!b)
        return NULL;
    PtG5Bank banks[8];
    for (unsigned i = 0; i < count; i++)
        banks[i] = (PtG5Bank){{regions[i].base, regions[i].size}, regions[i].host, !!regions[i].read_only};
    if (pt_g5_memory_init(&b->memory, banks, count) != PT_G5_OK) {
        free(b);
        return NULL;
    }
    if (pthread_rwlock_init(&b->instructions, NULL)) {
        pt_g5_memory_destroy(&b->memory);
        free(b);
        return NULL;
    }
    return b;
}
void ppc970_bus_destroy(Pt970Bus *b) {
    if (b) {
        for (unsigned i = 0; i < 4; i++)
            if (b->cpu[i])
                b->cpu[i]->bus970 = NULL;
        pthread_rwlock_destroy(&b->instructions);
        pt_g5_memory_destroy(&b->memory);
        free(b);
    }
}
void ppc970_bus_enter(Pt970Bus *b, int exclusive) {
    if (exclusive)
        pthread_rwlock_wrlock(&b->instructions);
    else
        pthread_rwlock_rdlock(&b->instructions);
}
void ppc970_bus_leave(Pt970Bus *b) {
    pthread_rwlock_unlock(&b->instructions);
}
/* Attaches one 970 CPU to the bus with its processor id and boot HIOR. */
int ppc970_attach(CPU *c, Pt970Bus *b, unsigned pir, uint64_t boot_hior) {
    if (!c || !b || !c->env_ticks970 || pir >= 4 || b->cpu[pir] || c->bus970 || (boot_hior & ~UINT64_C(0x3fffff00000)))
        return -1;
    c->is970 = 1;
    c->bus970 = b;
    c->pir970 = pir;
    b->cpu[pir] = c;
    c->spr[287] = PVR_970MP;
    c->spr[1023] = pir;
    c->msr = 0;
    c->msr_hi = 0x90000000;
    ppc_set_pc64(c, boot_hior + 0x100);
    ppc_set_spr64(c, 311, boot_hior);
    c->reserve = 0;
    c->halted = 0;
    c->exception970 = 0;
    c->unsupported970 = 0;
    ppc970_clock_init(c);
    return 0;
}
int ppc970_bus_read(Pt970Bus *b, uint64_t pa, void *p, unsigned n) {
    return !b || pt_g5_read(&b->memory, pa, p, n) != PT_G5_OK;
}
int ppc970_bus_write(Pt970Bus *b, uint64_t pa, const void *p, unsigned n) {
    return !b || pt_g5_write(&b->memory, pa, p, n) != PT_G5_OK;
}
/* A store to physical memory that other CPUs may cache translations or code for: notifies those owners. */
int ppc970_owner_write(Pt970Bus *b, uint64_t pa, size_t n) {
    if (!b || !n)
        return -1;
    unsigned owners = 0;
    for (unsigned i = 0; i < 4; i++)
        owners += b->cpu[i] != NULL;
    if (owners != 1)
        return -1;
    PtG5Memory *m = &b->memory;
    int valid = 0;
    for (unsigned i = 0; i < m->count; i++) {
        PtG5Bank *k = &m->bank[i];
        if (!k->readonly && pa >= k->range.base && pa - k->range.base < k->range.size &&
            n <= k->range.size - (pa - k->range.base))
            valid = 1;
    }
    if (!valid)
        return -1;
    pthread_mutex_lock(&m->lock);
    /* Conservative invalidation is permitted; the byte store follows on this
     * same owner before any further guest instruction or reservation. */
    for (unsigned i = 0; i < 4; i++)
        m->reservation[i].valid = 0;
    pthread_mutex_unlock(&m->lock);
    return 0;
}
/* The low-RAM agent copies only on the sole CPU owner. Synchronize before a
 * host command is consumed; future SMP needs bus-locked copies, not raw pages. */
int ppc970_agent_prepare(CPU *c, uint64_t pa, unsigned n, int write) {
    if (!c || !c->bus970 || !n || (write != 0 && write != 1))
        return -1;
    Pt970Bus *b = c->bus970;
    unsigned owners = 0;
    for (unsigned i = 0; i < 4; i++)
        owners += b->cpu[i] != NULL;
    if (owners != 1)
        return -1;
    int valid = 0;
    for (unsigned i = 0; i < b->memory.count; i++) {
        PtG5Bank *k = &b->memory.bank[i];
        if (!k->readonly && pa >= k->range.base && pa - k->range.base < k->range.size &&
            n <= k->range.size - (pa - k->range.base))
            valid = 1;
    }
    if (!valid || (c->env_ram970 && c->env_ram970(c, pa, n, write)))
        return -1;
    return write ? ppc970_owner_write(b, pa, n) : 0;
}
void ppc970_mmu_fault(CPU *c, uint64_t ea, int acc, uint32_t why) {
    if (acc != 2) {
        ppc_set_spr64(c, SPR_DAR, ea); /* DSEG leaves DSISR undefined (PEM 6.4.4). Zero avoids passing an old
         * page-miss reason to XNU, which uses it to decide whether to fill the SLB. */
        c->spr[SPR_DSISR] = why == PT970_SEGMENT ? 0 : why;
    }
    ppc970_exception(c, why == PT970_SEGMENT ? (acc == 2 ? 0x480 : 0x380) : (acc == 2 ? 0x400 : 0x300), ppc_pc64(c),
                     acc == 2 && why != PT970_SEGMENT ? why : 0);
}
static PtG5Result prepare_pte(void *opaque, uint64_t pa, unsigned n, bool write) {
    CPU *c = opaque;
    if (c->env_ram970 && c->env_ram970(c, pa, n, write)) {
        c->halted = 1;
        c->exit_code = 74;
        return PT_G5_BUS;
    }
    return PT_G5_OK;
}
/* 970 address translation: SLB lookup, then the hashed page table (with R/C updates), giving the real address and
 * its WIMG bits. */
static uint32_t walk(CPU *c, uint64_t ea, int acc, int pr, uint64_t *ra, int *wr, uint8_t *wimg) {
    if (!c->bus970 || !ra || !wr || acc < 0 || acc > 2)
        return 0x40000000u;
    PtG5Slb segment = {0};
    for (unsigned i = 0; i < 64; i++)
        if (c->slb[i].esid == ((ea >> 28) | (UINT64_C(1) << 63))) {
            if (segment.valid) {
                c->halted = 1;
                c->exit_code = 70;
                return 0x40000000u;
            }
            segment = (PtG5Slb){ea >> 28, c->slb[i].vsid >> 12, (uint16_t)(c->slb[i].vsid & 0xf80), true};
        }
    if (!segment.valid)
        return PT970_SEGMENT;
    PtG5Walk mmu = {.sdr1 = ppc_spr64(c, SPR_SDR1), .prepare_pte = prepare_pte, .prepare_opaque = c};
    PtG5Translation t;
    PtG5Result r = pt_g5_walk_segment(&mmu, &c->bus970->memory, &segment, ea, pr, (PtG5Access)acc, &t);
    if (r == PT_G5_OK) {
        *ra = t.pa;
        *wr = t.writable;
        if (wimg)
            *wimg = t.wimg;
        return 0;
    }
    if (r == PT_G5_SEGMENT)
        return PT970_SEGMENT;
    /* Corrupt/reserved table state is a stop, not a silently fabricated mapping. */
    if (r != PT_G5_PAGE && r != PT_G5_PROTECTION) {
        if (!c->halted) {
            c->halted = 1;
            c->exit_code = 70;
        }
        return 0x40000000u;
    }
    return t.fault_bits | (acc == 1 ? 0x02000000u : 0);
}
uint32_t pt_xlate970(CPU *c, uint64_t ea, int acc, int pr, uint64_t *ra, int *wr) {
    return walk(c, ea, acc, pr, ra, wr, NULL);
}
static int translate(CPU *c, uint64_t address, int access, uint64_t *ra, uint8_t *wimg) {
    *ra = address & PA_MASK;
    if (wimg)
        *wimg = 3 | ((ppc_spr64(c, 1012) & (UINT64_C(1) << 40)) ? 4 : 0);
    if ((c->msr & MSR_PR) || (c->msr & (access == 2 ? MSR_IR : MSR_DR))) {
        int wr;
        uint32_t why = walk(c, address, access, !!(c->msr & MSR_PR), ra, &wr, wimg);
        if (why) {
            if (!c->halted)
                ppc970_mmu_fault(c, address, access, why);
            return 1;
        }
    }
    return 0;
}
/* Reads or writes real memory on the 970 bus: RAM banks directly, everything else as MMIO. */
static int physical(CPU *c, uint64_t ra, uint8_t *p, unsigned n, int write) {
    PtG5Memory *m = &c->bus970->memory;
    int ram = 0;
    for (unsigned i = 0; i < m->count; i++)
        if (ra >= m->bank[i].range.base && ra - m->bank[i].range.base < m->bank[i].range.size) {
            ram = 1;
            break;
        }
    if (ram) {
        if (c->env_ram970 && c->env_ram970(c, ra, n, write)) {
            c->halted = 1;
            c->exit_code = 74;
            return 1;
        }
        if (!(write ? ppc970_bus_write(c->bus970, ra, p, n) : ppc970_bus_read(c->bus970, ra, p, n)))
            return 0;
    } else if (c->env_mmio970) {
        if (!c->env_mmio970(c, ra, p, (int)n, write))
            return 0;
    } else if (!(ra >> 32) && c->env_mmio) {
        c->env_mmio(c, (uint32_t)ra, p, (int)n, write);
        return 0;
    }
    ppc970_exception(c, 0x200, ppc_pc64(c), 0);
    return 1;
}
void ppc970_alignment(CPU *c, uint64_t ea, int write) {
    ppc_set_spr64(c, SPR_DAR, ppc_ea64(c, ea));
    c->spr[SPR_DSISR] = (c->spr[SPR_DSISR] & ~0x02000000u) | (write ? 0x02000000u : 0);
    ppc970_exception(c, 0x600, ppc_pc64(c), 0);
}
int ppc970_data_check(CPU *c, uint64_t ea, unsigned n, int write, int forbid_ci) {
    for (unsigned off = 0; off < n;) {
        uint64_t ra, a = ppc_ea64(c, ea + off);
        uint8_t wimg;
        if (translate(c, a, write, &ra, &wimg))
            return 1;
        if (forbid_ci && (wimg & 4)) {
            ppc970_alignment(c, a, write);
            return 1;
        }
        unsigned k = 4096 - (unsigned)(a & 4095);
        off += k < n - off ? k : n - off;
    }
    return 0;
}
/* dcbz/dcbzl: zeroes the 32- or 128-byte cache block at ea. */
int ppc970_zero(CPU *c, uint64_t ea, unsigned n) {
    if (n != 32 && n != 128) {
        c->halted = 1;
        c->exit_code = 70;
        return 1;
    }
    uint64_t ra;
    uint8_t wimg;
    ea = ppc_ea64(c, ea);
    if (translate(c, ea, 1, &ra, &wimg))
        return 1;
    /* Reference profile: cache-inhibited dcbz faults; the mode-ring no-op alternative is unsupported. */
    if (wimg & 4) {
        ppc970_alignment(c, ea, 1);
        return 1;
    }
    uint8_t zero[128] = {0};
    return physical(c, ra & ~(uint64_t)(n - 1), zero, n, 1);
}
/* Translated data access of up to 128 bytes for the 970 interpreter (raises the DSI or alignment exception). */
int ppc970_bytes(CPU *c, uint64_t address, uint8_t *p, unsigned n, int write) {
    if (!c->bus970 || !p || !n || n > 128 || (write != 0 && write != 1)) {
        c->halted = 1;
        c->exit_code = 70;
        return 1;
    }
    uint64_t ra[2];
    unsigned size[2], count = 0, off = 0;
    while (off < n) {
        uint64_t a = ppc_ea64(c, address + off);
        unsigned k = 4096 - (unsigned)(a & 4095);
        if (k > n - off)
            k = n - off;
        uint8_t wimg;
        if (translate(c, a, write, &ra[count], &wimg))
            return 1;
        if (((wimg & 4) || (ppc_spr64(c, 1012) & (UINT64_C(1) << 39))) && (n == 2 || n == 4 || n == 8) &&
            (a & (n - 1))) {
            ppc970_alignment(c, a, write);
            return 1;
        }
        size[count++] = k;
        off += k;
    }
    off = 0;
    for (unsigned i = 0; i < count; i++) {
        if (physical(c, ra[i], p + off, size[i], write))
            return 1;
        off += size[i];
    }
    return 0;
}
int ppc970_ld(CPU *c, uint64_t ea, int n, uint64_t *out) {
    if (n != 1 && n != 2 && n != 4 && n != 8) {
        c->halted = 1;
        c->exit_code = 70;
        return 1;
    }
    uint8_t p[8];
    if (ppc970_bytes(c, ea, p, n, 0))
        return 1;
    uint64_t v = 0;
    for (int i = 0; i < n; i++)
        v = v << 8 | p[i];
    *out = v;
    return 0;
}
int ppc970_st(CPU *c, uint64_t ea, int n, uint64_t v) {
    if (n != 1 && n != 2 && n != 4 && n != 8) {
        c->halted = 1;
        c->exit_code = 70;
        return 1;
    }
    uint8_t p[8];
    for (int i = n; i; i--) {
        p[i - 1] = (uint8_t)v;
        v >>= 8;
    }
    return ppc970_bytes(c, ea, p, n, 1);
}
/* Instruction fetch with 970 translation (raises the ISI exception). */
int ppc970_ifetch(CPU *c, uint64_t pc, uint32_t *in) {
    uint64_t ra;
    pc = ppc_ea64(c, pc);
    if (pc & 3) {
        c->halted = 1;
        c->exit_code = 70;
        return 1;
    }
    if (translate(c, pc, 2, &ra, NULL))
        return 1;
    uint8_t p[4];
    if (c->env_ram970 && c->env_ram970(c, ra, 4, 0)) {
        c->halted = 1;
        c->exit_code = 74;
        return 1;
    }
    if (ppc970_bus_read(c->bus970, ra, p, 4)) {
        ppc970_exception(c, 0x200, pc, 0);
        return 1;
    }
    *in = be32(p);
    return 0;
}
/* lwarx/ldarx and stwcx./stdcx.: the reservation on the 128-byte granule, shared with the other CPUs. */
int ppc970_reservation(CPU *c, uint64_t address, unsigned n, int write, uint64_t *v, int *success) {
    uint64_t ra;
    address = ppc_ea64(c, address);
    if (address & (n - 1)) {
        ppc970_alignment(c, address, write);
        return 1;
    }
    uint8_t wimg;
    if (translate(c, address, write, &ra, &wimg))
        return 1;
    if (wimg & 4) {
        ppc970_mmu_fault(c, address, write, 0x04000000u | (write ? 0x02000000u : 0));
        return 1;
    }
    if (c->env_ram970 && c->env_ram970(c, ra, n, write)) {
        c->halted = 1;
        c->exit_code = 74;
        return 1;
    }
    bool ok = false;
    PtG5Result r = write ? pt_g5_conditional(&c->bus970->memory, c->pir970, ra, n, *v, &ok)
                         : pt_g5_reserve(&c->bus970->memory, c->pir970, ra, n, v);
    if (r != PT_G5_OK) {
        ppc970_exception(c, 0x200, ppc_pc64(c), 0);
        return 1;
    }
    *success = ok;
    return 0;
}
