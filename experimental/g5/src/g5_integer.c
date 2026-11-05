/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "g5.h"
#include <limits.h>

uint64_t pt_g5_mask(unsigned first, unsigned last) {
    if (first > 63 || last > 63)
        return 0;
    uint64_t left = UINT64_MAX >> first, right = UINT64_MAX << (63 - last);
    return first <= last ? left & right : left | right;
}
static uint64_t rotate(uint64_t a, unsigned n) {
    n &= 63;
    return n ? (a << n) | (a >> (64 - n)) : a;
}
uint8_t pt_g5_compare(uint64_t a, uint64_t b, bool wide, bool sign, bool so) {
    if (!wide) {
        a = (uint32_t)a;
        b = (uint32_t)b;
    }
    if (sign) {
        uint64_t bias = UINT64_C(1) << (wide ? 63 : 31);
        a ^= bias;
        b ^= bias;
    }
    return (a < b ? 8 : a > b ? 4 : 2) | (so ? 1 : 0);
}
static void crfield(PtG5Integer *c, unsigned bf, uint8_t v) {
    unsigned shift = 28 - 4 * bf;
    c->cr = (c->cr & ~(15u << shift)) | ((uint32_t)v << shift);
}
PtG5Result pt_g5_integer(PtG5Integer *c, uint32_t in) {
    unsigned op = in >> 26, t = (in >> 21) & 31, a = (in >> 16) & 31, b = (in >> 11) & 31, xo = (in >> 1) & 1023;
    unsigned dst = t, ca = 0;
    bool set_ca = false, oe = false, ov = false, record = in & 1;
    uint64_t av = c->gpr[a], bv = c->gpr[b], sv = c->gpr[t], r = 0;
    bool wide = !!(c->msr & PT_G5_MSR_SF);
    if (op == 10 || op == 11 || (op == 31 && (xo == 0 || xo == 32))) {
        bool d = !!(in & (1u << 21)), sign = op == 11 || (op == 31 && xo == 0);
        if ((in & (1u << 22)) || (op == 31 && (in & 1)))
            return PT_G5_BAD_CONFIG;
        uint64_t right = op == 31 ? bv : sign ? (uint64_t)(int64_t)(int16_t)in : (uint16_t)in;
        crfield(c, (in >> 23) & 7, pt_g5_compare(av, right, d, sign, c->xer >> 31));
        goto done;
    }
    switch (op) {
    case 7:
        r = av * (uint64_t)(int64_t)(int16_t)in;
        record = false;
        break;
    case 8:
    case 12:
    case 13: {
        uint64_t left = op == 8 ? ~av : av, right = (uint64_t)(int64_t)(int16_t)in, carry = op == 8;
        __uint128_t sum = (__uint128_t)left + right + carry;
        r = (uint64_t)sum;
        ca = wide ? (unsigned)(sum >> 64) : (unsigned)(((uint64_t)(uint32_t)left + (uint32_t)right + carry) >> 32);
        set_ca = true;
        record = op == 13;
        break;
    }
    case 14:
        r = (a ? av : 0) + (uint64_t)(int64_t)(int16_t)in;
        record = false;
        break;
    case 15:
        r = (a ? av : 0) + (uint64_t)((int64_t)(int16_t)in * 65536);
        record = false;
        break;
    case 24:
        r = sv | (uint16_t)in;
        dst = a;
        record = false;
        break;
    case 25:
        r = sv | ((uint64_t)(uint16_t)in << 16);
        dst = a;
        record = false;
        break;
    case 26:
        r = sv ^ (uint16_t)in;
        dst = a;
        record = false;
        break;
    case 27:
        r = sv ^ ((uint64_t)(uint16_t)in << 16);
        dst = a;
        record = false;
        break;
    case 28:
        r = sv & (uint16_t)in;
        dst = a;
        record = true;
        break;
    case 29:
        r = sv & ((uint64_t)(uint16_t)in << 16);
        dst = a;
        record = true;
        break;
    case 20:
    case 21:
    case 23: {
        unsigned n = op == 23 ? (unsigned)bv & 31 : b;
        uint32_t word = (uint32_t)sv;
        word = n ? (word << n) | (word >> (32 - n)) : word;
        uint64_t m = pt_g5_mask(32 + (in >> 6 & 31), 32 + (in >> 1 & 31));
        r = (((uint64_t)word << 32) | word) & m;
        if (op == 20)
            r |= av & ~m;
        dst = a;
        break;
    }
    case 30: {
        unsigned kind = (in >> 2) & 7, n = b | ((in & 2) << 4), edge = ((in >> 6) & 31) | (in & 32);
        uint64_t mask;
        if (kind <= 3)
            mask = kind == 0 ? pt_g5_mask(edge, 63) : kind == 1 ? pt_g5_mask(0, edge) : pt_g5_mask(edge, 63 - n);
        else if ((in & 0x1e) == 0x10 || (in & 0x1e) == 0x12) {
            n = (unsigned)bv & 63;
            mask = (in & 2) ? pt_g5_mask(0, edge) : pt_g5_mask(edge, 63);
        } else
            return PT_G5_UNSUPPORTED;
        r = rotate(sv, n) & mask;
        if (kind == 3)
            r |= av & ~mask;
        dst = a;
        break;
    }
    case 31:
        switch (xo) {
        case 27:
            r = (bv & 127) >= 64 ? 0 : sv << (bv & 63);
            dst = a;
            break;
        case 539:
            r = (bv & 127) >= 64 ? 0 : sv >> (bv & 63);
            dst = a;
            break;
        case 58:
            if (b)
                return PT_G5_BAD_CONFIG;
            r = sv ? (unsigned)__builtin_clzll(sv) : 64;
            dst = a;
            break;
        case 986:
            if (b)
                return PT_G5_BAD_CONFIG;
            r = (uint64_t)(int64_t)(int32_t)sv;
            dst = a;
            break;
        case 954:
            if (b)
                return PT_G5_BAD_CONFIG;
            r = (uint64_t)(int64_t)(int8_t)sv;
            dst = a;
            break;
        case 922:
            if (b)
                return PT_G5_BAD_CONFIG;
            r = (uint64_t)(int64_t)(int16_t)sv;
            dst = a;
            break;
        case 26:
            if (b)
                return PT_G5_BAD_CONFIG;
            r = (uint32_t)sv ? (unsigned)__builtin_clz((uint32_t)sv) : 32;
            dst = a;
            break;
        case 24:
            r = (bv & 63) >= 32 ? 0 : (uint32_t)sv << (bv & 31);
            r = (uint32_t)r;
            dst = a;
            break;
        case 536:
            r = (bv & 63) >= 32 ? 0 : (uint32_t)sv >> (bv & 31);
            dst = a;
            break;
        case 792:
        case 824: {
            unsigned n = xo == 792 ? (unsigned)bv & 63 : b;
            int32_t v = (int32_t)sv;
            r = (uint64_t)(int64_t)(n >= 32 ? v >> 31 : v >> n);
            ca = v < 0 && (n >= 32 ? true : n ? !!((uint32_t)v & ((1u << n) - 1)) : false);
            set_ca = true;
            dst = a;
            break;
        }
        case 28:
            r = sv & bv;
            dst = a;
            break;
        case 60:
            r = sv & ~bv;
            dst = a;
            break;
        case 316:
            r = sv ^ bv;
            dst = a;
            break;
        case 444:
            r = sv | bv;
            dst = a;
            break;
        case 124:
            r = ~(sv | bv);
            dst = a;
            break;
        case 412:
            r = sv | ~bv;
            dst = a;
            break;
        case 476:
            r = ~(sv & bv);
            dst = a;
            break;
        case 284:
            r = ~(sv ^ bv);
            dst = a;
            break;
        case 11:
            r = ((uint64_t)(uint32_t)av * (uint32_t)bv) >> 32;
            break;
        case 75:
            r = (uint64_t)(((int64_t)(int32_t)av * (int32_t)bv) >> 32);
            break;
        case 9:
            r = (uint64_t)(((__uint128_t)av * bv) >> 64);
            break;
        case 73:
            r = (uint64_t)(((__int128)(int64_t)av * (int64_t)bv) >> 64);
            break;
        case 794:
        case 826:
        case 827: {
            unsigned n = xo == 794 ? (unsigned)bv & 127 : b | ((in & 2) << 4);
            bool negative = sv >> 63;
            r = n >= 64 ? (negative ? UINT64_MAX : 0) : n ? (sv >> n) | (negative ? (UINT64_MAX << (64 - n)) : 0) : sv;
            ca = negative && (n >= 64 ? sv != 0 : n ? !!(sv & ((UINT64_C(1) << n) - 1)) : false);
            set_ca = true;
            dst = a;
            break;
        }
        default:
            oe = !!(in & 0x400);
            switch (xo & 511) {
            case 104:
                r = 0 - av;
                ov = wide ? av == UINT64_C(0x8000000000000000) : (uint32_t)av == 0x80000000u;
                break;
            case 200:
            case 202:
            case 232:
            case 234: {
                unsigned k = xo & 511;
                uint64_t left = (k == 200 || k == 232) ? ~av : av;
                uint64_t right = k >= 232 ? UINT64_MAX : 0, carry = c->xer >> 29 & 1;
                __uint128_t sum = (__uint128_t)left + right + carry;
                r = (uint64_t)sum;
                ca = wide ? (unsigned)(sum >> 64)
                          : (unsigned)(((uint64_t)(uint32_t)left + (uint32_t)right + carry) >> 32);
                set_ca = true;
                ov = !!((~(left ^ right) & (left ^ r)) & (UINT64_C(1) << (wide ? 63 : 31)));
                break;
            }
            case 8:
            case 40:
            case 136:
            case 266:
            case 10:
            case 138: {
                unsigned k = xo & 511;
                bool sub = k == 8 || k == 40 || k == 136;
                uint64_t left = sub ? ~av : av, carry = sub ? 1 : 0;
                if (k == 136 || k == 138)
                    carry = (c->xer >> 29) & 1;
                __uint128_t sum = (__uint128_t)left + bv + carry;
                r = (uint64_t)sum;
                ca = wide ? (unsigned)(sum >> 64) : (unsigned)(((uint64_t)(uint32_t)left + (uint32_t)bv + carry) >> 32);
                set_ca = k != 40 && k != 266;
                uint64_t signbit = UINT64_C(1) << (wide ? 63 : 31);
                ov = !!((~(left ^ bv) & (left ^ r)) & signbit);
                break;
            }
            case 233: {
                __int128 product = (__int128)(int64_t)av * (int64_t)bv;
                r = (uint64_t)product;
                ov = product > (__int128)INT64_MAX || product < (__int128)INT64_MIN;
                break;
            }
            case 235: {
                int64_t product = (int64_t)(int32_t)av * (int32_t)bv;
                r = (uint64_t)product;
                ov = product > INT32_MAX || product < INT32_MIN;
                break;
            }
            case 459:
                ov = (uint32_t)bv == 0;
                r = ov ? 0 : (uint32_t)av / (uint32_t)bv;
                break;
            case 491:
                ov = (uint32_t)bv == 0 || ((uint32_t)av == 0x80000000u && (uint32_t)bv == UINT32_MAX);
                r = ov ? 0 : (uint64_t)(int64_t)((int32_t)av / (int32_t)bv);
                break;
            case 457:
                ov = bv == 0;
                r = ov ? 0 : av / bv;
                break;
            case 489:
                ov = bv == 0 || (av == UINT64_C(0x8000000000000000) && bv == UINT64_MAX);
                r = ov ? 0 : (uint64_t)((int64_t)av / (int64_t)bv);
                break;
            default:
                return PT_G5_UNSUPPORTED;
            }
        }
        break;
    default:
        return PT_G5_UNSUPPORTED;
    }
    /* GPR writes remain 64-bit even with MSR.SF=0. Only EA/CR/CA/OV have mode rules. */
    c->gpr[dst] = r;
    if (set_ca)
        c->xer = (c->xer & ~(1u << 29)) | (ca << 29);
    if (oe) {
        c->xer = (c->xer & ~(1u << 30)) | ((uint32_t)ov << 30);
        if (ov)
            c->xer |= 1u << 31;
    }
    if (record)
        crfield(c, 0, pt_g5_compare(r, 0, wide, true, c->xer >> 31));
done:
    c->pc += 4;
    if (!wide)
        c->pc = (uint32_t)c->pc;
    return PT_G5_OK;
}
