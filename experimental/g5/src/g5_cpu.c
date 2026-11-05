/* SPDX-License-Identifier: GPL-2.0-or-later
 * 970MP/PEM64 execution boundary. Reference bring-up backend, not the production JIT. */
#include "g5_cpu.h"
#include <stdatomic.h>
#include <string.h>

static uint64_t ea(const PtG5Cpu *c, uint64_t address) {
    return c->r.msr & PT_G5_MSR_SF ? address : (uint32_t)address;
}
static uint64_t be(const uint8_t *b, unsigned n) {
    uint64_t v = 0;
    while (n--)
        v = v << 8 | *b++;
    return v;
}
static void put(uint8_t *b, unsigned n, uint64_t v) {
    while (n) {
        b[--n] = (uint8_t)v;
        v >>= 8;
    }
}
static bool ram_address(const PtG5Memory *m, uint64_t pa) {
    for (unsigned i = 0; i < m->count; i++)
        if (pa >= m->bank[i].range.base && pa - m->bank[i].range.base < m->bank[i].range.size)
            return true;
    return false;
}
void pt_g5_cpu_init(PtG5Cpu *c, unsigned pir, uint64_t pc, uint64_t msr, uint64_t ticks) {
    memset(c, 0, sizeof *c);
    c->pir = pir;
    c->pvr = 0x00440100;
    c->r.msr = (msr & PT_G5_MSR_MASK) | PT_G5_MSR_HV;
    c->r.pc = ea(c, pc) & ~UINT64_C(3);
    pt_g5_dec_write(&c->dec, ticks, 0x7fffffff);
}
PtG5Result pt_g5_cpu_exception(PtG5Cpu *c, unsigned vector, uint64_t pc, uint32_t reason) {
    switch (vector) {
    case 0x100:
    case 0x200:
    case 0x300:
    case 0x380:
    case 0x400:
    case 0x480:
    case 0x500:
    case 0x600:
    case 0x700:
    case 0x800:
    case 0x900:
    case 0xc00:
    case 0xf20:
        break;
    default:
        return PT_G5_UNSUPPORTED;
    }
    if (vector == 0x200 && !(c->r.msr & PT_G5_MSR_ME)) {
        c->checkstop = true;
        return PT_G5_BUS;
    }
    c->srr0 = ea(c, pc) & ~UINT64_C(3);
    c->srr1 = (c->r.msr & ~UINT64_C(0x783f0000)) | reason;
    c->r.msr = PT_G5_MSR_SF | PT_G5_MSR_HV | (vector == 0x200 ? 0 : c->r.msr & PT_G5_MSR_ME);
    c->r.pc = c->hior + vector;
    c->exception = vector;
    c->mmu.generation++;
    /* Exceptions preserve reservations (PEM64 6.2.2); context-switch code clears them. */
    return PT_G5_OK;
}
static PtG5Result fault(PtG5Cpu *c, PtG5Result r, uint64_t address, PtG5Access access, uint32_t bits) {
    if (r == PT_G5_RETRY || r == PT_G5_UNSUPPORTED || r == PT_G5_BAD_CONFIG)
        return r;
    bool fetch = access == PT_G5_FETCH, store = access == PT_G5_STORE;
    if (!fetch) {
        c->dar = ea(c, address);
        c->dsisr = r == PT_G5_SEGMENT
                           ? 0
                           : (r == PT_G5_ALIGNMENT ? c->dsisr & ~0x02000000u : bits) | (store ? 0x02000000 : 0);
    }
    if (r == PT_G5_BUS)
        return pt_g5_cpu_exception(c, 0x200, c->r.pc, 0);
    unsigned vector = r == PT_G5_ALIGNMENT ? 0x600
                      : r == PT_G5_SEGMENT ? (fetch ? 0x480 : 0x380)
                                           : (fetch ? 0x400 : 0x300);
    return pt_g5_cpu_exception(c, vector, c->r.pc, fetch ? bits : 0);
}
PtG5Result pt_g5_cpu_access(PtG5Cpu *c, const PtG5CpuBus *b, uint64_t address, void *data, unsigned n,
                            PtG5Access access) {
    if (!b || !b->memory || !data || !n || n > 128 || (n & (n - 1)) || access > PT_G5_FETCH)
        return PT_G5_BAD_CONFIG;
    /* Translate both pieces before any store: a page fault cannot write half an operand. */
    PtG5Translation t[2];
    unsigned sizes[2], count = 0, off = 0;
    while (off < n) {
        uint64_t a = ea(c, address + off);
        unsigned size = 4096 - (unsigned)(a & 4095);
        if (size > n - off)
            size = n - off;
        PtG5Result r = pt_g5_translate(&c->mmu, b->memory, a, c->r.msr, access, &t[count]);
        if (r != PT_G5_OK)
            return fault(c, r, a, access, t[count].fault_bits);
        if ((t[count].wimg & 4) && (a & (n - 1)))
            return fault(c, PT_G5_ALIGNMENT, a, access, 0);
        sizes[count++] = size;
        off += size;
    }
    off = 0;
    for (unsigned i = 0; i < count; i++) {
        PtG5Result r = access == PT_G5_STORE ? pt_g5_write(b->memory, t[i].pa, (uint8_t *)data + off, sizes[i])
                                             : pt_g5_read(b->memory, t[i].pa, (uint8_t *)data + off, sizes[i]);
        if (r == PT_G5_BUS && b->mmio && count == 1 && access != PT_G5_FETCH && !ram_address(b->memory, t[i].pa))
            r = b->mmio(b->user, c->pir, t[i].pa, (uint8_t *)data + off, sizes[i], access == PT_G5_STORE);
        if (r != PT_G5_OK)
            return fault(c, r, ea(c, address + off), access, r == PT_G5_PROTECTION ? 0x08000000 : 0);
        off += sizes[i];
    }
    return PT_G5_OK;
}
static PtG5Result privileged(PtG5Cpu *c) {
    return c->r.msr & PT_G5_MSR_PR ? pt_g5_cpu_exception(c, 0x700, c->r.pc, 0x40000) : PT_G5_OK;
}
static PtG5Result invalid(PtG5Cpu *c) {
    return pt_g5_cpu_exception(c, 0x700, c->r.pc, 0x80000);
}
static PtG5Result invalidate(PtG5Cpu *c, const PtG5CpuBus *b) {
    c->mmu.generation++;
    return b->invalidate ? b->invalidate(b->user, c->pir, &c->tlb_ticket) : PT_G5_OK;
}
static PtG5Result synchronized(PtG5Cpu *c, const PtG5CpuBus *b) {
    if (c->tlb_ticket) {
        if (!b->synced)
            return PT_G5_BAD_CONFIG;
        PtG5Result r = b->synced(b->user, c->tlb_ticket);
        if (r != PT_G5_OK)
            return r;
        c->tlb_ticket = 0;
    }
    atomic_thread_fence(memory_order_seq_cst);
    return PT_G5_OK;
}
static PtG5Result spr(PtG5Cpu *c, const PtG5CpuBus *b, unsigned n, bool write, uint64_t *v) {
    if (n != 1 && n != 8 && n != 9 && n != 256 && n != 268 && n != 269) {
        PtG5Result r = privileged(c);
        if (r != PT_G5_OK || c->exception)
            return r;
    }
    uint64_t *slot = NULL;
    switch (n) {
    case 1:
        if (write)
            c->r.xer = (uint32_t)*v;
        else
            *v = c->r.xer;
        return PT_G5_OK;
    case 8:
        slot = &c->r.lr;
        break;
    case 9:
        slot = &c->r.ctr;
        break;
    case 18:
        if (write)
            c->dsisr = (uint32_t)*v;
        else
            *v = c->dsisr;
        return PT_G5_OK;
    case 19:
        slot = &c->dar;
        break;
    case 26:
        slot = &c->srr0;
        break;
    case 27:
        slot = &c->srr1;
        break;
    case 25:
        if (write) {
            c->mmu.sdr1 = *v;
            c->mmu.generation++;
        } else
            *v = c->mmu.sdr1;
        return PT_G5_OK;
    case 22: {
        if (!b->ticks)
            return PT_G5_BAD_CONFIG;
        uint64_t now = b->ticks(b->user, c->pir);
        uint32_t d;
        if (write)
            pt_g5_dec_write(&c->dec, now, (uint32_t)*v);
        else {
            PtG5Result r = pt_g5_dec_read(&c->dec, now, &d);
            if (r != PT_G5_OK)
                return r;
            *v = d;
        }
        return PT_G5_OK;
    }
    case 256:
        if (write)
            c->vrsave = (uint32_t)*v;
        else
            *v = c->vrsave;
        return PT_G5_OK;
    case 268:
    case 269:
    case 284:
    case 285: {
        if (!b->ticks)
            return PT_G5_BAD_CONFIG;
        if ((write && n < 284) || (!write && n >= 284))
            return invalid(c);
        uint64_t ticks = b->ticks(b->user, c->pir), tb = ticks + c->tb_offset;
        if (write) {
            tb = n == 284 ? (tb & UINT64_C(0xffffffff00000000)) | (uint32_t)*v
                          : (tb & 0xffffffff) | ((uint64_t)(uint32_t)*v << 32);
            c->tb_offset = tb - ticks;
        } else
            *v = n == 268 ? tb : (uint32_t)(tb >> 32);
        return PT_G5_OK;
    }
    case 272:
    case 273:
    case 274:
    case 275:
        slot = &c->sprg[n - 272];
        break;
    case 287:
    case 1023:
        if (write)
            return invalid(c);
        *v = n == 287 ? c->pvr : c->pir;
        return PT_G5_OK;
    case 311:
        if (write)
            c->hior = *v & UINT64_C(0x3fffff00000);
        else
            *v = c->hior;
        return PT_G5_OK;
    /* HID side effects, SCOM and performance facilities are still integration gates. */
    default:
        return PT_G5_UNSUPPORTED;
    }
    if (write)
        *slot = *v;
    else
        *v = *slot;
    return PT_G5_OK;
}
static PtG5Result memory_op(PtG5Cpu *c, const PtG5CpuBus *b, uint32_t in, unsigned n, bool write, bool update,
                            bool indexed, bool sign, bool reverse) {
    unsigned t = in >> 21 & 31, a = in >> 16 & 31, rb = in >> 11 & 31;
    if (indexed && (in & 1))
        return invalid(c);
    if (update && (!a || (!write && a == t)))
        return invalid(c);
    uint64_t address =
            ea(c, (a ? c->r.gpr[a] : 0) +
                          (indexed ? c->r.gpr[rb] : (uint64_t)(int64_t)(int16_t)(in & ((in >> 26 >= 58) ? ~3u : ~0u))));
    uint8_t bytes[8];
    if (write)
        put(bytes, n, c->r.gpr[t]);
    if (write && reverse)
        for (unsigned i = 0; i < n / 2; i++) {
            uint8_t x = bytes[i];
            bytes[i] = bytes[n - i - 1];
            bytes[n - i - 1] = x;
        }
    PtG5Result r = pt_g5_cpu_access(c, b, address, bytes, n, write ? PT_G5_STORE : PT_G5_LOAD);
    if (r != PT_G5_OK || c->exception)
        return r;
    if (!write) {
        if (reverse)
            for (unsigned i = 0; i < n / 2; i++) {
                uint8_t x = bytes[i];
                bytes[i] = bytes[n - i - 1];
                bytes[n - i - 1] = x;
            }
        uint64_t v = be(bytes, n);
        if (sign)
            v = n == 2 ? (uint64_t)(int64_t)(int16_t)v : (uint64_t)(int64_t)(int32_t)v;
        c->r.gpr[t] = v;
    }
    if (update)
        c->r.gpr[a] = address;
    return PT_G5_OK;
}
static PtG5Result reserve_op(PtG5Cpu *c, const PtG5CpuBus *b, uint32_t in, unsigned n, bool write) {
    unsigned t = in >> 21 & 31, a = in >> 16 & 31, rb = in >> 11 & 31;
    uint64_t address = ea(c, (a ? c->r.gpr[a] : 0) + c->r.gpr[rb]);
    if ((in & 1) != (unsigned)write)
        return invalid(c);
    if (address & (n - 1))
        return fault(c, PT_G5_ALIGNMENT, address, write ? PT_G5_STORE : PT_G5_LOAD, 0);
    PtG5Translation tr;
    PtG5Access access = write ? PT_G5_STORE : PT_G5_LOAD;
    PtG5Result r = pt_g5_translate(&c->mmu, b->memory, address, c->r.msr, access, &tr);
    if (r != PT_G5_OK)
        return fault(c, r, address, access, tr.fault_bits);
    if (tr.wimg & 12)
        return fault(c, PT_G5_PROTECTION, address, access, 0x04000000);
    bool success = false;
    uint64_t value;
    r = write ? pt_g5_conditional(b->memory, c->pir, tr.pa, n, c->r.gpr[t], &success)
              : pt_g5_reserve(b->memory, c->pir, tr.pa, n, &value);
    if (r != PT_G5_OK)
        return fault(c, r, address, access, 0);
    if (write)
        c->r.cr = (c->r.cr & 0x0fffffff) | ((uint32_t)(success ? 2 : 0) | (c->r.xer >> 31)) << 28;
    else
        c->r.gpr[t] = value;
    return PT_G5_OK;
}
PtG5Result pt_g5_cpu_execute(PtG5Cpu *c, const PtG5CpuBus *b, uint32_t in) {
    if (!c || !b || !b->memory || c->pir >= PT_G5_CPUS)
        return PT_G5_BAD_CONFIG;
    if (c->checkstop)
        return PT_G5_BUS;
    c->exception = 0;
    c->last_instruction = in;
    if (c->r.msr & 0x600)
        return PT_G5_UNSUPPORTED; /* SE/BE require the trace-status contract. */
    unsigned op = in >> 26, xo = in >> 1 & 1023, t = in >> 21 & 31, a = in >> 16 & 31, rb = in >> 11 & 31;
    uint64_t next = ea(c, c->r.pc + 4);
    PtG5Result r = PT_G5_OK;
    if (op == 18 || op == 16 || (op == 19 && (xo == 16 || xo == 528))) {
        uint64_t target;
        bool take = true;
        if (op == 18)
            target = (in & 2 ? 0 : c->r.pc) + (uint64_t)(int64_t)((int32_t)(in << 6) >> 6 & ~3);
        else {
            unsigned bo = t, bi = a;
            uint64_t ctr = c->r.ctr;
            if (xo == 528 && op == 19 && !(bo & 4))
                return invalid(c);
            if (!(bo & 4)) {
                c->r.ctr--;
                take = ((c->r.msr & PT_G5_MSR_SF ? c->r.ctr : (uint32_t)c->r.ctr) != 0) ^ !!(bo & 2);
            }
            take = take && ((bo & 16) || ((c->r.cr >> (31 - bi) & 1) == !!(bo & 8)));
            target = op == 16   ? (in & 2 ? 0 : c->r.pc) + (uint64_t)(int64_t)(int16_t)(in & ~3u)
                     : xo == 16 ? c->r.lr
                                : ctr;
        }
        if (in & 1)
            c->r.lr = next;
        c->r.pc = take ? ea(c, target) & ~UINT64_C(3) : next;
        c->retired++;
        return PT_G5_OK;
    }
    if (op == 17) {
        if (in != 0x44000002)
            return PT_G5_UNSUPPORTED;
        c->retired++;
        return pt_g5_cpu_exception(c, 0xc00, next, 0);
    }
    if (op == 19 && xo == 18) {
        if (in != 0x4c000024)
            return invalid(c);
        r = privileged(c);
        if (r != PT_G5_OK || c->exception)
            return r;
        r = synchronized(c, b);
        if (r != PT_G5_OK)
            return r;
        c->r.msr = (c->srr1 & PT_G5_MSR_MASK & ~PT_G5_MSR_POW) | PT_G5_MSR_HV;
        if (c->r.msr & PT_G5_MSR_PR)
            c->r.msr |= PT_G5_MSR_IR | PT_G5_MSR_DR;
        c->r.pc = ea(c, c->srr0) & ~UINT64_C(3);
        c->mmu.generation++;
        c->retired++;
        return PT_G5_OK;
    }
    if (op >= 32 && op <= 45) {
        bool write = (op >= 36 && op <= 39) || op >= 44;
        unsigned n = op < 40 ? ((op & 2) ? 1 : 4) : 2;
        r = memory_op(c, b, in, n, write, op & 1, false, op == 42 || op == 43, false);
    } else if (op == 58 || op == 62) {
        unsigned form = in & 3;
        if (form == 3 || (op == 62 && form > 1))
            return invalid(c);
        r = memory_op(c, b, in, form == 2 ? 4 : 8, op == 62, form == 1, false, form == 2, false);
    } else if (op == 19 && xo == 150) {
        if (in != 0x4c00012c)
            return invalid(c);
        r = synchronized(c, b);
    } else if (op == 31) {
        switch (xo) {
        case 20:
        case 84:
        case 150:
        case 214:
            r = reserve_op(c, b, in, (xo == 84 || xo == 214) ? 8 : 4, xo == 150 || xo == 214);
            break;
        case 21:
        case 53:
        case 149:
        case 181:
            r = memory_op(c, b, in, 8, xo >= 149, xo == 53 || xo == 181, true, false, false);
            break;
        case 23:
        case 55:
        case 151:
        case 183:
        case 341:
        case 373:
            r = memory_op(c, b, in, 4, xo == 151 || xo == 183, xo == 55 || xo == 183 || xo == 373, true, xo >= 341,
                          false);
            break;
        case 87:
        case 119:
        case 215:
        case 247:
            r = memory_op(c, b, in, 1, xo >= 215, xo == 119 || xo == 247, true, false, false);
            break;
        case 279:
        case 311:
        case 343:
        case 375:
        case 407:
        case 439:
            r = memory_op(c, b, in, 2, xo >= 407, xo == 311 || xo == 375 || xo == 439, true, xo == 343 || xo == 375,
                          false);
            break;
        case 534:
        case 662:
        case 790:
        case 918:
            r = memory_op(c, b, in, xo >= 790 ? 2 : 4, xo == 662 || xo == 918, false, true, false, true);
            break;
        case 339:
        case 371:
        case 467: {
            unsigned n = (in >> 16 & 31) | (in >> 6 & 0x3e0);
            if (in & 1)
                return invalid(c);
            if (xo == 371 && n != 268 && n != 269)
                return invalid(c);
            uint64_t v = c->r.gpr[t];
            r = spr(c, b, n, xo == 467, &v);
            if (r == PT_G5_OK && !c->exception && xo != 467)
                c->r.gpr[t] = v;
            break;
        }
        case 83:
            r = privileged(c);
            if (r == PT_G5_OK && !c->exception)
                c->r.gpr[t] = c->r.msr;
            break;
        case 146:
        case 178: {
            r = privileged(c);
            if (r != PT_G5_OK || c->exception)
                break;
            if ((in & 0x001ef801) || (c->r.gpr[t] & PT_G5_MSR_POW))
                return PT_G5_UNSUPPORTED;
            uint64_t mask = in & 0x10000
                                    ? PT_G5_MSR_EE | PT_G5_MSR_RI
                                    : PT_G5_MSR_MASK & ~(PT_G5_MSR_ME | PT_G5_MSR_HV | (xo == 146 ? PT_G5_MSR_SF : 0));
            uint64_t v = (c->r.msr & ~mask) | (c->r.gpr[t] & mask);
            if (v & PT_G5_MSR_PR)
                v |= PT_G5_MSR_IR | PT_G5_MSR_DR;
            c->r.msr = v;
            c->mmu.generation++;
            next = ea(c, next);
            break;
        }
        case 402:
        case 434:
        case 498:
        case 851:
        case 915:
        case 210:
        case 242:
        case 595:
        case 659:
            r = privileged(c);
            if (r != PT_G5_OK || c->exception)
                break;
            if (xo == 402)
                r = pt_g5_slbmte(&c->mmu, c->r.gpr[t], c->r.gpr[rb], !!(c->r.msr & PT_G5_MSR_SF));
            else if (xo == 434)
                pt_g5_slbie(&c->mmu, c->r.gpr[rb]);
            else if (xo == 498)
                pt_g5_slbia(&c->mmu);
            else if (xo == 851 || xo == 915)
                r = pt_g5_slbmfe(&c->mmu, (unsigned)c->r.gpr[rb] & 4095, xo == 851, &c->r.gpr[t]);
            else {
                if (c->r.msr & PT_G5_MSR_SF)
                    return PT_G5_BAD_CONFIG;
                unsigned index = (xo == 210 || xo == 595) ? a & 15 : (unsigned)(c->r.gpr[rb] >> 28) & 15;
                if (xo == 210 || xo == 242)
                    r = pt_g5_mtsr(&c->mmu, index, (uint32_t)c->r.gpr[t]);
                else {
                    uint32_t v;
                    r = pt_g5_mfsr(&c->mmu, index, &v);
                    if (r == PT_G5_OK)
                        c->r.gpr[t] = v;
                }
            }
            break;
        case 306:
        case 274:
        case 566:
            r = privileged(c);
            if (r != PT_G5_OK || c->exception)
                break;
            if (xo == 566)
                r = synchronized(c, b);
            else if (xo == 274)
                c->mmu.generation++;
            else
                r = invalidate(c, b);
            break;
        case 598:
            r = synchronized(c, b);
            break;
        case 854:
            atomic_thread_fence(memory_order_seq_cst);
            break;
        case 19:
            if (in & 0x00100000)
                return PT_G5_UNSUPPORTED;
            c->r.gpr[t] = c->r.cr;
            break;
        case 144: {
            unsigned mask = in >> 12 & 255;
            for (unsigned f = 0; f < 8; f++)
                if (mask >> (7 - f) & 1) {
                    uint32_t m = 15u << (28 - 4 * f);
                    c->r.cr = (c->r.cr & ~m) | ((uint32_t)c->r.gpr[t] & m);
                }
            break;
        }
        default:
            r = pt_g5_integer(&c->r, in);
            if (r == PT_G5_OK) {
                c->retired++;
                return r;
            }
            break;
        }
    } else if (op == 59 || op == 63 || (op >= 48 && op <= 55)) {
        return !(c->r.msr & PT_G5_MSR_FP) ? pt_g5_cpu_exception(c, 0x800, c->r.pc, 0) : PT_G5_UNSUPPORTED;
    } else if (op == 4) {
        return !(c->r.msr & PT_G5_MSR_VEC) ? pt_g5_cpu_exception(c, 0xf20, c->r.pc, 0) : PT_G5_UNSUPPORTED;
    } else {
        r = pt_g5_integer(&c->r, in);
        if (r == PT_G5_OK) {
            c->retired++;
            return r;
        }
    }
    if (r != PT_G5_OK || c->exception)
        return r;
    c->r.pc = next;
    c->retired++;
    return PT_G5_OK;
}
PtG5Result pt_g5_cpu_step(PtG5Cpu *c, const PtG5CpuBus *b, bool external_irq) {
    if (!c || !b || !b->ticks)
        return PT_G5_BAD_CONFIG;
    c->exception = 0;
    if (c->checkstop)
        return PT_G5_BUS;
    if (c->r.msr & PT_G5_MSR_EE) {
        if (external_irq)
            return pt_g5_cpu_exception(c, 0x500, c->r.pc, 0);
        bool pending;
        PtG5Result r = pt_g5_dec_pending(&c->dec, b->ticks(b->user, c->pir), true, &pending);
        if (r != PT_G5_OK)
            return r;
        if (pending)
            return pt_g5_cpu_exception(c, 0x900, c->r.pc, 0);
    }
    if (c->r.pc & 3)
        return PT_G5_BAD_CONFIG;
    uint8_t bytes[4];
    PtG5Result r = pt_g5_cpu_access(c, b, c->r.pc, bytes, 4, PT_G5_FETCH);
    return r != PT_G5_OK || c->exception ? r : pt_g5_cpu_execute(c, b, (uint32_t)be(bytes, 4));
}
