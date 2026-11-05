/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "g5_bridge.h"
#include "platform.h"
/* Registers of the U3 bridge as the 970 bring-up needs them: revision, power-management state, CPU 0 running, no
 * second processor. */
int pt_g5_bridge_access(PtG5Bridge *s, uint32_t at, uint8_t *p, unsigned n, int write) {
    if (!s || !p || n != 4 || (at & 3))
        return -1;
    uint32_t v = write ? (uint32_t)pt_read_be(p, 4) : 0;
    switch (at) {
    case 0:
        if (write)
            return -1;
        v = 0x30;
        break; /* U3 revision, big-endian. */
    case 0x20:
        if (write)
            s->clock = v;
        else
            v = s->clock;
        break;
    case 0x40:
        if (write)
            s->arbitration = v;
        else
            v = s->arbitration;
        break;
    case 0x30:
        if (write && v)
            return -1;
        break; /* Normal only; no synthetic sleep success. */
    case 0x50:
        if (write)
            return -1;
        break; /* CPU 0. */
    case 0x70:
        if (write)
            return -1;
        v = 2;
        break; /* Running. */
    case 0x23030:
        if (write && v)
            return -1;
        break; /* No second physical processor enabled. */
    default:
        return -1;
    }
    if (!write)
        pt_write_be(p, 4, v);
    return 0;
}
