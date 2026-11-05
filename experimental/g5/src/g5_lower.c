/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "g5_lower.h"
PtG5Result pt_g5_lower_leaf(uint32_t in, uint32_t *word) {
    if (!word || in >> 26 != 31 || (in & 1))
        return PT_G5_UNSUPPORTED;
    unsigned xo = (in >> 1) & 1023;
    uint32_t op;
    switch (xo) {
    case 266:
        op = 0x8b000000;
        break; /* add x11,x9,x10 */
    case 40:
        *word = 0xcb000000 | 9u << 16 | 10u << 5 | 11;
        return PT_G5_OK; /* subf */
    case 233:
        op = 0x9b007c00;
        break; /* mul */
    case 9:
        op = 0x9bc07c00;
        break;
    case 73:
        op = 0x9b407c00;
        break;
    case 28:
        op = 0x8a000000;
        break;
    case 60:
        op = 0x8a200000;
        break;
    case 316:
        op = 0xca000000;
        break;
    case 444:
        op = 0xaa000000;
        break;
    case 986:
        if (in & (31u << 11))
            return PT_G5_BAD_CONFIG;
        *word = 0x93407c00 | 9u << 5 | 11;
        return PT_G5_OK;
    case 58:
        if (in & (31u << 11))
            return PT_G5_BAD_CONFIG;
        *word = 0xdac01000 | 9u << 5 | 11;
        return PT_G5_OK;
    default:
        return PT_G5_UNSUPPORTED;
    }
    *word = op | 10u << 16 | 9u << 5 | 11;
    return PT_G5_OK;
}
