/* SPDX-License-Identifier: GPL-2.0-or-later */
/* pt_ohci_quiet: when the owner may run the bus's frames late, in one batch.
 * cc -Iinclude tests/ohci-quiet.c src/ohci.c src/hid.c src/platform.c -pthread */
#include "ohci.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static uint8_t ram[0x8000];
static void level(void *p, int v) {
    *(int *)p = v;
}
static void td(unsigned a, unsigned flags, unsigned cbp, unsigned next, unsigned end) {
    pt_write_le(ram + a, 4, flags);
    pt_write_le(ram + a + 4, 4, cbp);
    pt_write_le(ram + a + 8, 4, next);
    pt_write_le(ram + a + 12, 4, end);
}
int main(void) {
    PtHid h[3];
    for (unsigned i = 0; i < 3; i++)
        pt_hid_init(&h[i], i);
    PtOhci o;
    int line = 0;
    pt_ohci_init(&o, (PtRam){ram, sizeof ram, NULL}, h, h + 1, h + 2, level, &line);
    o.bus_master = 1;
    pt_ohci_write(&o, 0x18, 0x1000);     /* HCCA */
    pt_ohci_write(&o, 0x10, 0x80000002); /* done-queue interrupt only */
    pt_ohci_write(&o, 4, 0x84);          /* operational, the periodic list */
    pt_ohci_write(&o, 0x54, 16);         /* port reset: ten frames that matter */
    pt_ohci_frame(&o);
    assert(!pt_ohci_quiet(&o));
    for (unsigned i = 0; i < 10; i++)
        pt_ohci_frame(&o);
    assert(pt_ohci_quiet(&o) && (pt_ohci_read(&o, 0x54) & 0x13) == 3);
    /* The keyboard's interrupt endpoint, polled in every frame, with nothing to say: still quiet. */
    h[0].address = 7;
    h[0].configuration = 1;
    td(0x2100, 0xf0000000 | (2 << 19), 0x4000, 0x2120, 0x4007);
    td(0x2000, (8 << 16) | (1 << 7) | 7, 0x2120, 0x2100, 0);
    for (unsigned i = 0; i < 32; i++)
        pt_write_le(ram + 0x1000 + 4 * i, 4, 0x2000);
    pt_ohci_frame(&o);
    assert(pt_ohci_quiet(&o) && (pt_read_le(ram + 0x2100, 4) >> 28) == 15);
    /* The same transfer waiting on the control list: a frame may complete it. A register write: not quiet at once. */
    pt_ohci_write(&o, 0x20, 0x2000);
    assert(!pt_ohci_quiet(&o));
    pt_ohci_write(&o, 4, 0x94);
    pt_ohci_frame(&o);
    assert(!pt_ohci_quiet(&o));
    pt_ohci_write(&o, 4, 0x84);
    pt_ohci_frame(&o);
    assert(pt_ohci_quiet(&o));
    /* A key: the report is retired in the frame and handed over in it, and the bus is quiet again; while the driver has
     * not taken the done queue, a second report waits for it. */
    pt_hid_key(h, 4, 1);
    pt_ohci_frame(&o);
    assert(ram[0x4002] == 4 && line && pt_ohci_quiet(&o));
    td(0x2120, 0xf0000000 | (2 << 19), 0x4000, 0x2130, 0x4007);
    td(0x2000, (8 << 16) | (1 << 7) | 7, 0x2130, 0x2120, 0);
    pt_hid_key(h, 4, 0);
    pt_ohci_frame(&o);
    assert(!pt_ohci_quiet(&o));
    pt_ohci_write(&o, 12, 2);
    pt_ohci_frame(&o);
    assert(pt_ohci_quiet(&o));
    /* A start-of-frame interrupt asked for: every frame matters. */
    pt_ohci_write(&o, 0x10, 4);
    pt_ohci_frame(&o);
    assert(!pt_ohci_quiet(&o));
    for (unsigned i = 0; i < 3; i++)
        pt_hid_destroy(&h[i]);
    puts("ohci-quiet ok");
    return 0;
}
