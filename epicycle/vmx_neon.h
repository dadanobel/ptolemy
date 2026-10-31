/* SPDX-License-Identifier: GPL-2.0-or-later
 * Integer VMX -> AArch64. VR memory contains canonical big-endian bytes.
 * Only v0-v3 are scratch; no guest-memory access, FPCR/FPSR or VSCR changes.
 * VRs stay memory backed: there is no vector register cache (worth adding only if spills measure costly).
 */
#ifndef PT_VMX_NEON_H
#define PT_VMX_NEON_H
#include <stdint.h>

enum { PT_VMX_NEON_WORDS = 12 };

/* Returns 0 without writing out for an unsupported/invalid encoding.
 * Caller checks CPU capabilities and MSR.VEC before executing the body.
 * base is the A64 CPU-state register, not a guest effective address.
 */
static unsigned pt_vmx_neon_emit(uint32_t out[PT_VMX_NEON_WORDS], uint32_t in,
                                unsigned vr_offset, unsigned base)
{
    if (in >> 26 != 4 || base > 30 || (vr_offset & 15) || vr_offset > 65536 - 512)
        return 0;
    unsigned d = in >> 21 & 31, a = in >> 16 & 31, b = in >> 11 & 31;
    unsigned c = in >> 6 & 31, xo = in & 2047;
    /* Rc is CR6 bookkeeping, emitted by jit.c after the vector result. */
    unsigned cmp = xo & ~0x400u, cb = cmp & ~0xc0u;
    if ((cmp & 63) == 6 && ((cmp >> 6) & 3) < 3 && (cb == 6 || cb == 0x206 || cb == 0x306)) xo = cmp;
    unsigned low = xo & 63;
    unsigned lane = xo >> 6 & 3, family = xo & ~0xc0u;
    uint32_t op = 0, reverse = 0;
    int special = 0;
    if (low == 42 || low == 43) special = (int)low;
    else if (low == 44 && c < 16) special = 44;
    else if (family == 0x20c && lane < 3 && a < (16u >> lane)) special = 1;
    else if (family == 0x30c && lane < 3 && b == 0) special = 2;
    else if (xo == 0x00e || xo == 0x04e) special = 7; /* modulo pack: select low BE bytes/halves */
    else if ((xo == 0x20e || xo == 0x24e || xo == 0x28e || xo == 0x2ce) && a == 0) special = 8;
    else if (low == 8 && lane < 2 && family <= 0x308) special = 9;
    else {
        switch (xo) {
        case 0x404: op = 0x4e211c00; break; /* vand */
        case 0x444: op = 0x4e611c00; break; /* vandc */
        case 0x484: op = 0x4ea11c00; break; /* vor */
        case 0x4c4: op = 0x6e211c00; break; /* vxor */
        case 0x504: op = 0x4ea11c00; special = 3; break; /* vnor */
        default:
            if (lane > 2) return 0;
            switch (family) {
            case 0x000: op = 0x4e218400; break; /* vaddu*m */
            case 0x400: op = 0x6e218400; break; /* vsubu*m */
            case 0x002: op = 0x6e216400; break; /* vmaxu* */
            case 0x102: op = 0x4e216400; break; /* vmaxs* */
            case 0x202: op = 0x6e216c00; break; /* vminu* */
            case 0x302: op = 0x4e216c00; break; /* vmins* */
            case 0x402: op = 0x6e211400; break; /* vavgu*: rounded halving add */
            case 0x502: op = 0x4e211400; break; /* vavgs* */
            case 0x104: op = 0x6e214400; special = 4; break; /* vsl* */
            case 0x204: op = 0x6e214400; special = 5; break; /* vsr* */
            case 0x304: op = 0x4e214400; special = 6; break; /* vsra* */
            case 0x006: op = 0x6e218c00; break; /* vcmpequ*, no Rc */
            case 0x206: op = 0x6e213400; break; /* vcmpgtu* */
            case 0x306: op = 0x4e213400; break; /* vcmpgts* */
            case 0x00c: op = 0x4e013800; break; /* vmrgh* */
            case 0x10c: op = 0x4e017800; break; /* vmrgl* */
            default: return 0;
            }
            op |= lane << 22;
            /* Compare masks and zip operate on raw element bytes unchanged. */
            if (family != 0x006 && family != 0x00c && family != 0x10c)
                reverse = lane == 1 ? 0x4e201800 : lane == 2 ? 0x6e200800 : 0;
        }
    }

    uint32_t *p = out;
#define PT_QLOAD(v, g) (*p++ = 0x3dc00000u | (vr_offset / 16 + (g)) << 10 | base << 5 | (v))
#define PT_QSTORE(v) (*p++ = 0x3d800000u | (vr_offset / 16 + d) << 10 | base << 5 | (v))
    if (special == 1) {
        PT_QLOAD(1, b);
        *p++ = 0x4e000420u | ((1u << lane) | a << (lane + 1)) << 16;
    } else if (special == 2) {
        int imm = a < 16 ? (int)a : (int)a - 32;
        unsigned u = (unsigned)(lane && imm < 0 ? ~imm : imm) & 255;
        uint32_t ins = lane == 0 ? 0x4f00e400u : lane == 1 ? 0x4f00a400u : 0x4f006400u;
        if (lane && imm < 0) ins |= 1u << 29; /* MVNI, canonical BE lanes. */
        *p++ = ins | (u & 0xe0) << 11 | (u & 31) << 5;
    } else if (special == 8) {
        PT_QLOAD(1, b);
        unsigned half = !!(xo & 0x40), second = !!(xo & 0x80);
        if (half) *p++ = 0x4e201821; /* rev16 v1.16b,v1.16b */
        *p++ = (half ? 0x0f10a420u : 0x0f08a420u) | second << 30;
        *p++ = half ? 0x6e200800u : 0x4e201800u;
    } else {
        PT_QLOAD(0, a);
        PT_QLOAD(1, b);
        if (special == 7) {
            *p++ = 0x4e015800u | (xo == 0x04e ? 1u << 22 : 0);
        } else if (special == 9) {
            if (lane) { *p++ = 0x4e201800u; *p++ = 0x4e201821u; }
            uint32_t odd = family & 0x200 ? 0 : 0x4000u;
            *p++ = 0x4e001800u | lane << 22 | odd;
            *p++ = 0x4e011821u | lane << 22 | odd;
            *p++ = (family & 0x100 ? 0x0e21c000u : 0x2e21c000u) | lane << 22;
            *p++ = lane ? 0x6e200800u : 0x4e201800u;
        } else if (special == 42 || special == 43) {
            PT_QLOAD(2, c);
            if (special == 42) {
                *p++ = 0x6e601c22; /* bsl v2.16b, v1.16b, v0.16b */
                PT_QSTORE(2);
                return (unsigned)(p - out);
            }
            *p++ = 0x4f00e7e3; /* movi v3.16b, #31 */
            *p++ = 0x4e231c42; /* PPC ignores high three index bits; TBL does not. */
            *p++ = 0x4e022000; /* tbl v0.16b, {v0.16b,v1.16b}, v2.16b */
        } else if (special == 44) {
            *p++ = 0x6e010000u | c << 11;
        } else {
            if (reverse) { *p++ = reverse; *p++ = reverse | 1u << 5 | 1; }
            if (special >= 4) {
                *p++ = lane == 0 ? 0x4f00e4e2 : lane == 1 ? 0x4f0085e2 : 0x4f0007e2;
                *p++ = 0x4e221c21; /* AND shift counts with PPC lane-width mask. */
                if (special != 4) *p++ = 0x6e20b821u | lane << 22;
            }
            *p++ = op;
            if (special == 3) *p++ = 0x6e205800; /* NOT after OR. */
            if (reverse) *p++ = reverse;
        }
    }
    PT_QSTORE(0);
#undef PT_QLOAD
#undef PT_QSTORE
    return (unsigned)(p - out);
}
#endif
