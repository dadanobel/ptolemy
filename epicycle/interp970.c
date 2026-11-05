/* SPDX-License-Identifier: GPL-2.0-or-later
 * Interpreter bring-up path. The G4 JIT must never execute this 64-bit state. */
#include "ppc.h"
#include "../experimental/g5/src/g5_cpu.h"
#include <math.h>
#include <stdatomic.h>
#include <stdio.h>
#include <time.h>
#define R(n) ppc_gpr64(c, n)
#define RA(n) ((n) ? R(n) : 0)
#define SET(n, v) ppc_set_gpr64(c, n, v)
#define PRIV()                                       \
    do {                                             \
        if (c->msr & MSR_PR) {                       \
            ppc970_exception(c, 0x700, pc, 0x40000); \
            return 0;                                \
        }                                            \
    } while (0)
#define LOAD(n, addr, v)               \
    do {                               \
        if (ppc970_ld(c, addr, n, &v)) \
            return c->halted;          \
    } while (0)
#define STORE(n, addr, v)             \
    do {                              \
        if (ppc970_st(c, addr, n, v)) \
            return c->halted;         \
    } while (0)
typedef struct {
    uint64_t pc, lr, r1, r0, r3, msr;
    uint32_t in;
} Trace970;
enum {
    TRACE970_COUNT = 1024
};
static PT_CPU_LOCAL Trace970 recent[TRACE970_COUNT];
static PT_CPU_LOCAL unsigned recent_at, recent_count;
static PT_CPU_LOCAL int trace970 = -1;
/* PT_970_TRACE: logs each executed 970 instruction. */
static void record970(CPU *c, uint32_t in) {
    if (trace970 < 0) {
        const char *s = getenv("PT_970_TRACE");
        trace970 = s && strcmp(s, "0");
    }
    if (!trace970)
        return;
    recent[recent_at++ & (TRACE970_COUNT - 1)] = (Trace970){ppc_pc64(c),
                                                            (uint64_t)c->lr_hi << 32 | c->lr,
                                                            ppc_gpr64(c, 1),
                                                            ppc_gpr64(c, 0),
                                                            ppc_gpr64(c, 3),
                                                            ppc_msr64(c),
                                                            in};
    if (recent_count < TRACE970_COUNT)
        recent_count++;
}
static uint64_t clock970(CPU *c) {
    return c->env_ticks970(c);
}
void ppc970_clock_init(CPU *c) {
    c->dec0 = clock970(c);
    c->decv = 0x7fffffff;
    c->tb_off = 0;
}
/* Stops the 970 at an instruction the bring-up does not implement and reports it. */
static int unsupported(CPU *c, uint32_t in) {
    c->unsupported970 = in;
    c->halted = 1;
    c->exit_code = 78;
    if (trace970 > 0)
        for (unsigned i = recent_at - recent_count; i != recent_at; i++) {
            const Trace970 *t = &recent[i & (TRACE970_COUNT - 1)];
            fprintf(stderr, "970 recent pc=%016llx in=%08x lr=%016llx r1=%016llx r0=%016llx r3=%016llx msr=%016llx\n",
                    (unsigned long long)t->pc, t->in, (unsigned long long)t->lr, (unsigned long long)t->r1,
                    (unsigned long long)t->r0, (unsigned long long)t->r3, (unsigned long long)t->msr);
        }
    if (trace970 > 0) {
        for (unsigned i = 0; i < 32; i++)
            fprintf(stderr, "970 gpr %u=%016llx\n", i, (unsigned long long)ppc_gpr64(c, i));
        /* Physical, 32-bit backchains only: never translate or fault while diagnosing a stop. */
        if (!(c->msr & (MSR_DR | MSR_PR)) && !(c->msr_hi & 0x80000000)) {
            uint64_t frame = ppc_gpr64(c, 1);
            uint8_t bytes[12];
            for (unsigned i = 0; i < 16 && frame && !(frame & 15); i++) {
                if (ppc970_bus_read(c->bus970, frame, bytes, sizeof bytes))
                    break;
                uint32_t next = be32(bytes);
                fprintf(stderr, "970 physical frame=%08llx next=%08x saved_lr=%08x\n", (unsigned long long)frame, next,
                        be32(bytes + 8));
                if (next <= frame)
                    break;
                frame = next;
            }
        }
    }
    if (trace_unimpl)
        fprintf(stderr, "PPC970 unsupported pc=%016llx instruction=%08x msr=%016llx fpscr=%08x\n",
                (unsigned long long)ppc_pc64(c), in, (unsigned long long)ppc_msr64(c), c->fpscr);
    return 1;
}
static int illegal(CPU *c) {
    ppc970_exception(c, 0x700, ppc_pc64(c), 0x80000);
    return 0;
}
static void set_msr(CPU *c, uint64_t v) {
    v = (v & PT_G5_MSR_MASK) | PT970_HV;
    if (v & MSR_PR)
        v |= MSR_IR | MSR_DR;
    c->msr = (uint32_t)v;
    c->msr_hi = (uint32_t)(v >> 32);
    c->xgen++;
    c->irq = 1;
}
/* Takes a 970 exception: SRR0/SRR1, the new MSR (64-bit mode) and the vector address under HIOR. */
void ppc970_exception(CPU *c, uint32_t vector, uint64_t pc, uint32_t reason) {
    static PT_CPU_LOCAL unsigned shown;
    static PT_CPU_LOCAL int trace = -1;
    if (trace < 0) {
        const char *v = getenv("PT_970_TRACE");
        trace = v && strcmp(v, "0");
    }
    if (trace && (shown++ < 32 || !(shown & 0xffff)))
        fprintf(stderr, "970 exception %x pc=%016llx msr=%016llx reason=%x r1=%016llx dar=%016llx dsisr=%x\n", vector,
                (unsigned long long)pc, (unsigned long long)ppc_msr64(c), reason, (unsigned long long)ppc_gpr64(c, 1),
                (unsigned long long)ppc_spr64(c, SPR_DAR), c->spr[SPR_DSISR]);
    if (vector == 0x200 && !(c->msr & MSR_ME)) {
        c->halted = 1;
        c->exit_code = 71;
        return;
    }
    ppc_set_spr64(c, SPR_SRR0, ppc_ea64(c, pc) & ~UINT64_C(3));
    ppc_set_spr64(c, SPR_SRR1, (ppc_msr64(c) & ~UINT64_C(0x783f0000)) | reason);
    uint64_t msr = PT970_SF | PT970_HV | (vector == 0x200 ? 0 : c->msr & MSR_ME);
    c->msr = (uint32_t)msr;
    c->msr_hi = (uint32_t)(msr >> 32);
    ppc_set_pc64(c, (ppc_spr64(c, 311) & UINT64_C(0x3fffff00000)) + vector);
    c->exception970 = vector;
    c->xgen++;
}
void ppc970_irq(CPU *c) {
    c->irq = 0;
    uint32_t p = c->env_irq ? c->env_irq(c, 0) & PEND_EXT : 0;
    if ((int32_t)(c->decv - (uint32_t)(clock970(c) - c->dec0)) < 0)
        p |= PEND_DEC;
    c->pend_masked = p && !(c->msr & MSR_EE);
    if ((c->msr & MSR_EE) && p) {
        c->msr &= ~MSR_POW;
        ppc970_exception(c, p & PEND_EXT ? 0x500 : 0x900, ppc_pc64(c), 0);
    }
}
static uint64_t bits(CPU *c, unsigned r) {
    uint64_t v;
    memcpy(&v, &c->fpr[r], 8);
    return v;
}
static void fset(CPU *c, unsigned r, uint64_t v) {
    memcpy(&c->fpr[r], &v, 8);
}
static uint64_t widen(uint32_t v) {
    unsigned e = v >> 23 & 255;
    uint64_t s = (uint64_t)(v >> 31) << 63, m = v & 0x7fffff;
    if (e == 255)
        return s | UINT64_C(0x7ff0000000000000) | (m << 29);
    if (!e) {
        if (!m)
            return s;
        unsigned shift = __builtin_clz((uint32_t)m) - 8;
        m <<= shift;
        return s | ((uint64_t)(897 - shift) << 52) | ((m & 0x7fffff) << 29);
    }
    return s | ((uint64_t)(e + 896) << 52) | (m << 29);
}
static uint32_t narrow(uint64_t v) {
    /* stfs stores the single encoding, without an arithmetic rounding step. */
    unsigned e = v >> 52 & 2047;
    uint32_t s = (uint32_t)(v >> 32) & 0x80000000, m = (uint32_t)(v >> 29) & 0x7fffff;
    if (e > 896)
        return s | ((uint32_t)(e == 2047 ? 255 : e - 896) << 23) | m;
    if (e >= 874)
        return s | ((0x800000 | m) >> (897 - e));
    return s;
}
static int trap(unsigned to, uint64_t a, uint64_t b, int wide) {
    if (!wide) {
        a = (uint64_t)(int64_t)(int32_t)a;
        b = (uint64_t)(int64_t)(int32_t)b;
    }
    return ((to & 16) && (int64_t)a < (int64_t)b) || ((to & 8) && (int64_t)a > (int64_t)b) || ((to & 4) && a == b) ||
           ((to & 2) && a < b) || ((to & 1) && a > b);
}
static int fp_alignment(CPU *c, uint64_t ea, unsigned n, int store) {
    if ((ea & 3) && ((store && (ea & 4095) > 4096 - n) || (!store && n == 4 && (ea & 31) > 28))) {
        ppc970_alignment(c, ea, store);
        return 1;
    }
    return 0;
}
/* Segment-register instructions on the 970 (mtsr/mfsr, mtsrin/mfsrin), mapped onto the SLB. */
static int sr_op(CPU *c, unsigned xo, unsigned d, unsigned a, unsigned b) {
    PtG5Mmu m = {0};
    for (unsigned i = 0; i < 64; i++)
        m.slb[i] = (PtG5Slb){c->slb[i].esid & ~(UINT64_C(1) << 63), c->slb[i].vsid >> 12,
                             (uint16_t)(c->slb[i].vsid & 0xf80), !!(c->slb[i].esid >> 63)};
    PtG5Result r = PT_G5_OK;
    uint64_t value = 0;
    /* The 970 profile retains ESID/V and its six-bit selector. XNU passes an
     * unmasked EA in RB; keep this compatibility choice outside the strict walker. */
    if (xo == 402)
        r = pt_g5_slbmte(&m, R(d), R(b) & ~UINT64_C(0x07ffffc0), !!(c->msr_hi & 0x80000000));
    else if (xo == 434)
        pt_g5_slbie(&m, R(b));
    else if (xo == 498)
        pt_g5_slbia(&m);
    else if (xo == 851 || xo == 915) {
        r = pt_g5_slbmfe(&m, (unsigned)R(b) & 63, xo == 851, &value);
        if (r == PT_G5_OK)
            SET(d, value);
    } else {
        unsigned index = (xo == 210 || xo == 595) ? a & 15 : (unsigned)(R(b) >> 28) & 15;
        if (c->msr_hi & 0x80000000)
            return -1;
        if (xo == 210 || xo == 242)
            r = pt_g5_mtsr(&m, index, (uint32_t)R(d));
        else {
            uint32_t v;
            r = pt_g5_mfsr(&m, index, &v);
            if (r == PT_G5_OK)
                SET(d, v);
        }
    }
    if (r != PT_G5_OK)
        return -1;
    for (unsigned i = 0; i < 64; i++) {
        c->slb[i].esid = m.slb[i].esid | ((uint64_t)m.slb[i].valid << 63);
        c->slb[i].vsid = m.slb[i].vsid << 12 | m.slb[i].flags;
    }
    c->xgen++;
    return 0;
}
/* mfspr/mtspr for the 970's 64-bit and implementation registers (HID, HIOR, PIR, timebase and the rest). */
static int spr(CPU *c, unsigned n, int write, uint64_t *v) {
    switch (n) {
    case 276:
        if (!write) {
            *v = ppc_spr64(c, 276);
            return 0;
        }
        /* 970MP UM 12.1.3: RW=1 reads, despite the reversed prose in 12.1.2. */
        if (*v & ~UINT64_C(0xffff8000))
            return -1;
        if ((*v >> 16) != 0x0430) {
            ppc_set_spr64(c, 276, 0x21);
            return 0;
        }
        if (*v & 0x8000)
            ppc_set_spr64(c, 277, c->biu_mode970);
        else {
            uint64_t mode = ppc_spr64(c, 277);
            /* No cache or store gatherer: direct-map and timer-off affect timing only. */
            if (mode & ~UINT64_C(0x8004))
                return -1;
            c->biu_mode970 = mode;
        }
        ppc_set_spr64(c, 276, 0);
        return 0;
    case 277:
        break;
    case 259:
        if (write)
            return -1;
        *v = ppc_spr64(c, 275);
        return 0; /* Read-only user alias of SPRG3 (970MP UM 2.1.1). */
    case 1:
        if (write)
            xer_set(c, (uint32_t)*v);
        else
            *v = xer_get(c);
        return 0;
    case 8:
        if (write) {
            c->lr = (uint32_t)*v;
            c->lr_hi = (uint32_t)(*v >> 32);
        } else
            *v = (uint64_t)c->lr_hi << 32 | c->lr;
        return 0;
    case 9:
        if (write) {
            c->ctr = (uint32_t)*v;
            c->ctr_hi = (uint32_t)(*v >> 32);
        } else
            *v = (uint64_t)c->ctr_hi << 32 | c->ctr;
        return 0;
    case 22: {
        uint64_t now = clock970(c);
        if (write) {
            c->dec0 = now;
            c->decv = (uint32_t)*v;
            c->irq = 1;
        } else
            *v = c->decv - (uint32_t)(now - c->dec0);
        return 0;
    }
    case 268:
    case 269:
    case 284:
    case 285: {
        if ((write && n < 284) || (!write && n >= 284))
            return -1;
        uint64_t now = clock970(c), tb = now + c->tb_off;
        if (write) {
            tb = n == 284 ? (tb & UINT64_C(0xffffffff00000000)) | (uint32_t)*v
                          : (tb & 0xffffffff) | ((uint64_t)(uint32_t)*v << 32);
            c->tb_off = tb - now;
        } else
            *v = n == 268 ? tb : tb >> 32;
        return 0;
    }
    case 287:
    case 1023:
        if (write)
            return -1;
        *v = n == 287 ? PVR_970MP : c->pir970;
        return 0;
    case 311:
        if (write)
            *v &= UINT64_C(0x3fffff00000);
        break;
    case 18:
    case 256:
        if (write)
            *v = (uint32_t)*v;
        break;
    case 19:
    case 25:
    case 26:
    case 27:
    case 272:
    case 273:
    case 274:
    case 275:
    case 280:
        break;
    case 304:
    case 305:
        break; /* 970MP UM 2.1.2.11: 64-bit hypervisor scratch, no side effects. */
    case 1008:
    case 1009:
    case 1012:
    case 1014:
        break; /* Cache timing is not modeled; dcbz and POW use HID state. */
    case 1013:
    case 1015:
        if (write && *v)
            return -1;
        break; /* Disabled watchpoints only. */
    default:
        return -1;
    }
    if (write) {
        ppc_set_spr64(c, n, *v);
        if (n == 25)
            c->xgen++;
    } else
        *v = ppc_spr64(c, n);
    return 0;
}
/* VMX loads and stores for the 970 (lvx/stvx and the element and lvsl/lvsr forms). */
static int vector_mem(CPU *c, uint32_t in) {
    unsigned xo = in >> 1 & 1023, d = in >> 21 & 31, a = in >> 16 & 31, b = in >> 11 & 31;
    switch (xo) {
    case 6:
    case 38:
    case 7:
    case 39:
    case 71:
    case 103:
    case 359:
    case 135:
    case 167:
    case 199:
    case 231:
    case 487:
    case 342:
    case 374:
    case 822:
    case 758:
        break;
    default:
        return -1;
    }
    if (xo == 342 || xo == 374 || xo == 822 || xo == 758)
        return 0;
    if (!(c->msr & MSR_VEC)) {
        ppc970_exception(c, 0xf20, ppc_pc64(c), 0);
        return 0;
    }
    uint64_t ea = ppc_ea64(c, RA(a) + R(b));
    if (xo == 6 || xo == 38) {
        for (unsigned i = 0; i < 16; i++)
            c->vr[d][i] = (xo == 6 ? (ea & 15) : 16 - (ea & 15)) + i;
        return 0;
    }
    unsigned n = xo == 7 || xo == 135 ? 1 : xo == 39 || xo == 167 ? 2 : xo == 71 || xo == 199 ? 4 : 16;
    ea &= ~(uint64_t)(n - 1);
    unsigned off = n == 16 ? 0 : (unsigned)ea & 15;
    int store = xo == 135 || xo == 167 || xo == 199 || xo == 231 || xo == 487;
    uint8_t bytes[16];
    memcpy(bytes, c->vr[d], 16);
    if (ppc970_bytes(c, ea, bytes + off, n, store))
        return 0;
    if (!store)
        memcpy(c->vr[d], bytes, 16);
    return 0;
}
/* Executes one 970 instruction: the 64-bit integer, branch, load/store, FP and system instructions of the bring-up
 * subset. */
static int execute(CPU *c, uint32_t in) {
    record970(c, in);
    if (!c->is970 || !c->bus970)
        return unsupported(c, in);
    c->exception970 = 0;
    uint64_t pc = ppc_pc64(c), next = ppc_ea64(c, pc + 4), address, v = 0;
    unsigned op = in >> 26, d = in >> 21 & 31, a = in >> 16 & 31, b = in >> 11 & 31, xo = in >> 1 & 1023;
    if (c->msr & (MSR_SE | MSR_BE | MSR_LE))
        return unsupported(c, in);
    if (op == 0) {
        if (!c->env_hypercall || !c->env_hypercall(c, in))
            return unsupported(c, in);
        goto done;
    }
    if (op == 18 || op == 16 || (op == 19 && (xo == 16 || xo == 528))) {
        uint64_t target;
        int take = 1;
        if (op == 18)
            target = (in & 2 ? 0 : pc) + (int64_t)((int32_t)(in << 6) >> 6 & ~3);
        else {
            unsigned bo = d;
            uint64_t ctr = (uint64_t)c->ctr_hi << 32 | c->ctr;
            if (op == 19 && xo == 528 && !(bo & 4))
                return illegal(c);
            target = op == 16   ? (in & 2 ? 0 : pc) + (int64_t)(int16_t)(in & ~3u)
                     : xo == 16 ? (uint64_t)c->lr_hi << 32 | c->lr
                                : ctr;
            if (!(bo & 4)) {
                ctr--;
                c->ctr = (uint32_t)ctr;
                c->ctr_hi = (uint32_t)(ctr >> 32);
                take = (ppc_ea64(c, ctr) != 0) ^ !!(bo & 2);
            }
            take = take && ((bo & 16) || cr_bit(c, a) == !!(bo & 8));
        }
        if (in & 1) {
            c->lr = (uint32_t)next;
            c->lr_hi = (uint32_t)(next >> 32);
        }
        if (take)
            next = ppc_ea64(c, target) & ~UINT64_C(3);
        goto done;
    }
    if (op == 17) {
        if (in != 0x44000002)
            return unsupported(c, in);
        ppc970_exception(c, 0xc00, next, 0);
        return 0;
    }
    if (op == 19) {
        if (xo == 18) {
            PRIV();
            if (in != 0x4c000024)
                return illegal(c);
            set_msr(c, ppc_spr64(c, SPR_SRR1) & ~MSR_POW);
            next = ppc_ea64(c, ppc_spr64(c, SPR_SRR0)) & ~UINT64_C(3);
            goto done;
        }
        if (xo == 50)
            return illegal(c); /* rfi is not a 970 instruction. */
        if (xo == 150) {
            atomic_thread_fence(memory_order_seq_cst);
            goto done;
        }
        if (xo == 0) {
            cr_set_field(c, d >> 2, cr_field(c, a >> 2));
            goto done;
        }
        int x = cr_bit(c, a), y = cr_bit(c, b), r;
        switch (xo) {
        case 257:
            r = x & y;
            break;
        case 449:
            r = x | y;
            break;
        case 193:
            r = x ^ y;
            break;
        case 225:
            r = !(x & y);
            break;
        case 33:
            r = !(x | y);
            break;
        case 289:
            r = x == y;
            break;
        case 129:
            r = x & !y;
            break;
        case 417:
            r = x | !y;
            break;
        default:
            return unsupported(c, in);
        }
        cr_set_bit(c, d, r);
        goto done;
    }
    if (op == 2 || op == 3 || (op == 31 && (xo == 4 || xo == 68))) {
        v = op == 31 ? R(b) : (uint64_t)(int64_t)(int16_t)in;
        if (trap(d, R(a), v, op == 2 || (op == 31 && xo == 68))) {
            ppc970_exception(c, 0x700, pc, 0x20000);
            return 0;
        }
        goto done;
    }
    if (op == 4) {
        if (!(c->msr & MSR_VEC)) {
            ppc970_exception(c, 0xf20, pc, 0);
            return 0;
        }
        if (!ppc_altivec(c, in))
            return unsupported(c, in);
        goto done;
    }
    if (op == 59 || op == 63) {
        if (!(c->msr & MSR_FP)) {
            ppc970_exception(c, 0x800, pc, 0);
            return 0;
        }
        /* shortcut: NI and enabled precise FP traps need the remaining FP contract. */
        if ((c->fpscr & 4) || ((c->msr & (MSR_FE0 | MSR_FE1)) && (c->fpscr & 0xf8)))
            return unsupported(c, in);
        ppc_fp_settle(c);
        if (ppc_fp_execute(c, in))
            return unsupported(c, in);
        goto done;
    }
    if (op >= 32 && op <= 55) {
        address = ppc_ea64(c, RA(a) + (int64_t)(int16_t)in);
        unsigned n = op < 40 ? (op & 2 ? 1 : 4) : op <= 45 ? 2 : 4;
        int store = (op >= 36 && op <= 39) || op == 44 || op == 45 || op == 47 || op >= 52, fp = op >= 48,
            update = (op & 1) && op != 47;
        if (update && (!a || (!store && !fp && d == a)))
            return illegal(c);
        if (fp) {
            if (!(c->msr & MSR_FP)) {
                ppc970_exception(c, 0x800, pc, 0);
                return 0;
            }
            n = (op & 2) ? 8 : 4;
        }
        if (op == 46 || op == 47) {
            if (address & 3) {
                ppc970_alignment(c, address, store);
                return 0;
            }
            if (ppc970_data_check(c, address, 4 * (32 - d), store, 1))
                return c->halted;
            for (unsigned reg = d; reg < 32; reg++) {
                if (store) {
                    STORE(4, address, R(reg));
                } else {
                    LOAD(4, address, v);
                    SET(reg, v);
                }
                address = ppc_ea64(c, address + 4);
            }
            goto done;
        }
        if (fp && fp_alignment(c, address, n, store))
            return 0;
        if (store) {
            v = fp ? bits(c, d) : R(d);
            if (fp && n == 4)
                v = narrow(v);
            STORE(n, address, v);
        } else {
            LOAD(n, address, v);
            if (fp)
                fset(c, d, n == 4 ? widen((uint32_t)v) : v);
            else
                SET(d, op == 42 || op == 43 ? (uint64_t)(int64_t)(int16_t)v : v);
        }
        if (update)
            SET(a, address);
        goto done;
    }
    if (op == 58 || op == 62) {
        unsigned form = in & 3;
        if (form == 3 || (op == 62 && form > 1))
            return illegal(c);
        if (form == 1 && (!a || (op == 58 && a == d)))
            return illegal(c);
        address = ppc_ea64(c, RA(a) + (int64_t)(int16_t)(in & ~3u));
        if (op == 62) {
            STORE(8, address, R(d));
        } else {
            LOAD(form == 2 ? 4 : 8, address, v);
            SET(d, form == 2 ? (uint64_t)(int64_t)(int32_t)v : v);
        }
        if (form == 1)
            SET(a, address);
        goto done;
    }
    if (op == 31) {
        if (vector_mem(c, in) >= 0) {
            if (c->exception970 || c->halted)
                return c->halted;
            goto done;
        }
        int update = 0, store = 0, sign = 0, reverse = 0, fp = 0;
        unsigned n = 0;
        switch (xo) {
        case 21:
        case 53:
        case 149:
        case 181:
            n = 8;
            update = xo == 53 || xo == 181;
            store = xo >= 149;
            break;
        case 23:
        case 55:
        case 151:
        case 183:
        case 341:
        case 373:
            n = 4;
            update = xo == 55 || xo == 183 || xo == 373;
            store = xo == 151 || xo == 183;
            sign = xo >= 341;
            break;
        case 87:
        case 119:
        case 215:
        case 247:
            n = 1;
            update = xo == 119 || xo == 247;
            store = xo >= 215;
            break;
        case 279:
        case 311:
        case 343:
        case 375:
        case 407:
        case 439:
            n = 2;
            update = xo == 311 || xo == 375 || xo == 439;
            store = xo >= 407;
            sign = xo == 343 || xo == 375;
            break;
        case 534:
        case 662:
        case 790:
        case 918:
            n = xo >= 790 ? 2 : 4;
            store = xo == 662 || xo == 918;
            reverse = 1;
            break;
        case 535:
        case 567:
        case 599:
        case 631:
        case 663:
        case 695:
        case 727:
        case 759:
        case 983:
            fp = 1;
            n = (xo == 599 || xo == 631 || xo == 727 || xo == 759) ? 8 : 4;
            store = xo >= 663;
            update = xo != 983 && !!(xo & 32);
            break;
        }
        if (n) {
            if (in & 1)
                return illegal(c);
            if (update && (!a || (!store && !fp && a == d)))
                return illegal(c);
            if (fp && !(c->msr & MSR_FP)) {
                ppc970_exception(c, 0x800, pc, 0);
                return 0;
            }
            address = ppc_ea64(c, RA(a) + R(b));
            if (fp && fp_alignment(c, address, n, store))
                return 0;
            if (store) {
                v = fp ? bits(c, d) : R(d);
                if (fp && n == 4 && xo != 983)
                    v = narrow(v);
                if (reverse)
                    v = n == 2 ? __builtin_bswap16(v) : __builtin_bswap32(v);
                STORE(n, address, v);
            } else {
                LOAD(n, address, v);
                if (reverse)
                    v = n == 2 ? __builtin_bswap16(v) : __builtin_bswap32(v);
                if (sign)
                    v = n == 2 ? (uint64_t)(int64_t)(int16_t)v : (uint64_t)(int64_t)(int32_t)v;
                if (fp)
                    fset(c, d, n == 4 ? widen(v) : v);
                else
                    SET(d, v);
            }
            if (update)
                SET(a, address);
            goto done;
        }
        switch (xo) {
        case 20:
        case 84:
        case 150:
        case 214: {
            int write = xo == 150 || xo == 214, ok;
            v = R(d);
            if ((in & 1) != (unsigned)write)
                return illegal(c);
            if (ppc970_reservation(c, RA(a) + R(b), xo == 84 || xo == 214 ? 8 : 4, write, &v, &ok))
                return c->halted;
            if (write)
                cr_set_field(c, 0, (ok ? 2 : 0) | c->so);
            else
                SET(d, v);
            goto done;
        }
        case 597:
        case 533:
        case 725:
        case 661: {
            int store = xo == 725 || xo == 661;
            unsigned count = (xo == 597 || xo == 725) ? (b ? b : 32) : c->xer_bc;
            address = ppc_ea64(c, RA(a) + ((xo == 533 || xo == 661) ? R(b) : 0));
            uint8_t bytes[128];
            if (ppc970_data_check(c, address, count, store, 1))
                return c->halted;
            if (store)
                for (unsigned i = 0; i < count; i++)
                    bytes[i] = (uint8_t)(R((d + i / 4) & 31) >> (24 - 8 * (i & 3)));
            if (count && ppc970_bytes(c, address, bytes, count, store))
                return c->halted;
            if (!store)
                for (unsigned i = 0; i < count; i += 4) {
                    v = 0;
                    for (unsigned k = 0; k < 4; k++)
                        v = v << 8 | (i + k < count ? bytes[i + k] : 0);
                    SET((d + i / 4) & 31, v);
                }
            goto done;
        }
        case 83:
            PRIV();
            SET(d, ppc_msr64(c));
            goto done;
        case 146:
        case 178: {
            PRIV();
            if (in & 0x001ef801)
                return illegal(c);
            uint64_t mask =
                    in & 0x10000 ? MSR_EE | MSR_RI : PT_G5_MSR_MASK & ~(PT970_HV | MSR_ME | (xo == 146 ? PT970_SF : 0));
            set_msr(c, (ppc_msr64(c) & ~mask) | (R(d) & mask));
            next = ppc_ea64(c, next);
            goto done;
        }
        case 210:
        case 242:
        case 595:
        case 659:
        case 402:
        case 434:
        case 498:
        case 851:
        case 915:
            PRIV();
            if (sr_op(c, xo, d, a, b))
                return unsupported(c, in);
            goto done;
        case 339:
        case 371:
        case 467: {
            unsigned number = a | (b << 5);
            int write = xo == 467;
            if (in & 1)
                return illegal(c);
            if (number == 259 && write)
                return illegal(c);
            if (number != 1 && number != 8 && number != 9 && number != 256 && number != 259 && number != 268 &&
                number != 269) {
                PRIV();
            }
            if (xo == 371 && number != 268 && number != 269)
                return illegal(c);
            v = R(d);
            if (spr(c, number, write, &v))
                return unsupported(c, in);
            if (!write)
                SET(d, v);
            goto done;
        }
        case 19: {
            unsigned fields = in >> 12 & 255;
            uint32_t value = cr_get(c);
            if (in & 0x00100000) {
                if (!fields || (fields & (fields - 1)))
                    return illegal(c);
                uint32_t mask = 0;
                for (unsigned f = 0; f < 8; f++)
                    if (fields & (0x80 >> f))
                        mask |= 15u << (28 - 4 * f);
                value &= mask;
            }
            SET(d, value);
            goto done;
        }
        case 144: {
            unsigned fields = in >> 12 & 255;
            if ((in & 0x00100000) && (!fields || (fields & (fields - 1))))
                return illegal(c);
            for (unsigned f = 0; f < 8; f++)
                if (fields & (0x80 >> f))
                    cr_set_field(c, f, (uint32_t)R(d) >> (28 - 4 * f) & 15);
            goto done;
        }
        case 512:
            cr_set_field(c, d >> 2, xer_get(c) >> 28);
            c->ca = c->ov = c->so = 0;
            goto done;
        case 306:
        case 274:
        case 566:
            PRIV();
            c->xgen++;
            atomic_thread_fence(memory_order_seq_cst);
            goto done;
        case 598:
        case 854:
            atomic_thread_fence(memory_order_seq_cst);
            goto done;
        case 246:
        case 278:
            goto done; /* Cache hints may be ignored. */
        case 54:
        case 86:
        case 470:
        case 982:
            if (xo == 470) {
                PRIV();
            }
            if (ppc970_data_check(c, RA(a) + R(b), 1, xo == 470, 0))
                return c->halted;
            if (xo == 982)
                c->cgen++;
            atomic_thread_fence(memory_order_seq_cst);
            goto done; /* No cached code or data in the reference path. */
        case 1014: {
            unsigned count = pt_g5_dcbz_size(ppc_spr64(c, 1014), in);
            if (!count)
                return illegal(c);
            if (ppc970_zero(c, RA(a) + R(b), count))
                return c->halted;
            goto done;
        }
        }
    }
    {
        PtG5Integer s = {.pc = pc, .msr = ppc_msr64(c), .cr = cr_get(c), .xer = xer_get(c)};
        for (unsigned i = 0; i < 32; i++)
            s.gpr[i] = R(i);
        PtG5Result r = pt_g5_integer(&s, in);
        if (r == PT_G5_BAD_CONFIG)
            return illegal(c);
        if (r != PT_G5_OK)
            return unsupported(c, in);
        for (unsigned i = 0; i < 32; i++)
            SET(i, s.gpr[i]);
        for (unsigned f = 0; f < 8; f++)
            cr_set_field(c, f, s.cr >> (28 - 4 * f) & 15);
        xer_set(c, s.xer);
    }
done:
    c->icount++;
    ppc_set_pc64(c, next);
    return c->halted;
}
int ppc970_execute(CPU *c, uint32_t in) {
    if (!c->is970 || !c->bus970)
        return unsupported(c, in);
    unsigned xo = in >> 1 & 1023;
    /* An uncached walker can still hold a translated PA. Drain complete older
     * instructions before global invalidation completes; keep ordinary CPUs concurrent. */
    int exclusive = (in >> 26) == 31 && (xo == 306 || xo == 566 || xo == 982);
    ppc970_bus_enter(c->bus970, exclusive);
    int result = execute(c, in);
    ppc970_bus_leave(c->bus970);
    return result;
}
/* One 970 step: delivers a pending interrupt, fetches and executes one instruction. */
int interp970_step(CPU *c) {
    if (c->halted)
        return 1;
    c->exception970 = 0;
    ppc970_irq(c);
    if (c->halted || c->exception970)
        return c->halted;
    if (c->msr & MSR_POW) {
        uint32_t hid = (uint32_t)(ppc_spr64(c, 1008) >> 32);
        if (hid & 0x01000000u)
            return unsupported(c, 0); /* Deep nap needs a board wake/reset protocol. */
        if (!(hid & 0x00c00000u))
            return unsupported(c, 0);
        if (c->env_idle)
            c->env_idle(c);
        else {
            struct timespec delay = {0, 1000000};
            nanosleep(&delay, NULL);
        }
        return c->halted;
    }
    uint32_t in;
    ppc970_bus_enter(c->bus970, 0);
    int fault = ppc970_ifetch(c, ppc_pc64(c), &in);
    ppc970_bus_leave(c->bus970);
    if (fault)
        return c->halted;
    return ppc970_execute(c, in);
}
