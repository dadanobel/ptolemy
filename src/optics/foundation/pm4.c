/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "pm4.h"

static uint32_t le32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 |
           (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

PtR2Pm4Status pt_r2_pm4_decode(const void *data, size_t bytes,
                             PtR2Pm4Packet *out, size_t *needed)
{
    if (needed) *needed = 4;
    if (!out || (!data && bytes)) return PT_R2_PM4_BAD_ARGUMENT;
    if (bytes < 4) return PT_R2_PM4_SHORT;
    uint32_t h = le32(data);
    PtR2Pm4Packet p = {.header = h, .type = h >> 30};
    switch (p.type) {
    case 0:
        /* R300 uses 13 address bits; bits 13/14 are not a wider MMIO address. */
        if (h & 0x6000) return PT_R2_PM4_BAD_HEADER;
        p.words = ((h >> 16) & 0x3fff) + 1;
        p.reg[0] = (h & 0x1fff) * 4;
        p.one_reg = (h >> 15) & 1;
        if (!p.one_reg && p.words > 0x2000u - (h & 0x1fff))
            return PT_R2_PM4_BAD_HEADER;
        break;
    case 1:
        /* Unlike types 0/3, type 1 has exactly two payload words. */
        if (h & 0x3fc00000) return PT_R2_PM4_BAD_HEADER;
        p.words = 2;
        p.reg[0] = (h & 0x7ff) * 4;
        p.reg[1] = ((h >> 11) & 0x7ff) * 4;
        break;
    case 2:
        break;
    case 3:
        p.words = ((h >> 16) & 0x3fff) + 1;
        p.opcode = (h >> 8) & 0xff;
        break;
    }
    p.bytes = (p.words + 1) * 4;
    if (needed) *needed = p.bytes;
    if (bytes < p.bytes) return PT_R2_PM4_SHORT;
    p.payload = (const uint8_t *)data + 4;
    *out = p;
    return PT_R2_PM4_PACKET;
}

int pt_r2_pm4_word(const PtR2Pm4Packet *p, uint32_t i, uint32_t *value)
{
    if (!p || !value || i >= p->words) return 0;
    *value = le32(p->payload + (size_t)i * 4);
    return 1;
}

int pt_r2_pm4_write(const PtR2Pm4Packet *p, uint32_t i,
                    uint32_t *reg, uint32_t *value)
{
    if (!p || !reg || !value || p->type > 1 || i >= p->words) return 0;
    uint32_t v;
    if (!pt_r2_pm4_word(p, i, &v)) return 0;
    *reg = p->type == 1 ? p->reg[i] : p->reg[0] + (p->one_reg ? 0 : i * 4);
    *value = v;
    return 1;
}

const char *pt_r2_pm4_opcode_name(uint8_t op)
{
    switch (op) {
    case 0x10: return "NOP";
    case 0x26: return "WAIT_FOR_IDLE";
    case 0x28: return "DRAW_VBUF";
    case 0x29: return "DRAW_IMMD";
    case 0x2a: return "DRAW_INDX";
    case 0x2f: return "LOAD_VBPNTR";
    case 0x32: return "CLEAR_ZMASK";
    case 0x33: return "INDX_BUFFER";
    case 0x34: return "DRAW_VBUF_2";
    case 0x35: return "DRAW_IMMD_2";
    case 0x36: return "DRAW_INDX_2";
    case 0x38: return "3D_CLEAR_CMASK";
    case 0x37: return "CLEAR_HIZ";
    case 0x91: return "PAINT";
    case 0x92: return "BITBLT";
    case 0x94: return "HOSTDATA_BLT";
    case 0x9b: return "BITBLT_MULTI";
    default: return "UNKNOWN";
    }
}
