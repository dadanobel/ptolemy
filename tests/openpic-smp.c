/* SPDX-License-Identifier: GPL-2.0-or-later */
/* OpenPIC with two CPUs, driven the way AppleMPIC drives it (disassembly of AppleMPIC.kext 10.5: start, initVector,
 * enableVector, dispatchIPI, handleInterrupt): per-CPU task priority, acknowledge and end of interrupt, a source
 * routed by its destination mask, interprocessor interrupts by channel. */
#include "platform.h"
#include <assert.h>
#include <stdio.h>
static int kicks[2];
static void kick0(void *p) {
    (void)p;
    kicks[0]++;
}
static void kick1(void *p) {
    (void)p;
    kicks[1]++;
}
enum { CPU1 = 0x1000, IACK = 0x200a0, EOI = 0x200b0, CTPR = 0x20080, IPI_VP = 0x10a0, IPI_GO = 0x20040 };
int main(void) {
    PtPic p;
    pt_pic_init(&p);
    pt_pic_set_notify(&p, kick0, NULL);
    assert((pt_pic_read(&p, 0x1000) >> 8 & 31) == 0); /* one CPU: the feature register as before */
    pt_pic_write(&p, CTPR + CPU1, 0);                 /* and no second register bank */
    assert(pt_pic_read(&p, IACK + CPU1) == 0 && pt_pic_read(&p, CTPR + CPU1) == 0);
    pt_pic_write(&p, IPI_GO, 2); /* nobody there */
    pt_pic_add_cpu(&p, kick1, NULL);
    uint32_t frr = pt_pic_read(&p, 0x1000);
    assert((frr >> 16 & 0x7ff) + 1 == 64 && (frr >> 8 & 31) + 1 == 2);
    /* AppleMPIC::start: reset, mask sources and the two entries past them, spurious vector, priorities. */
    pt_pic_write(&p, 0x1020, 0x80000000u);
    assert(!(pt_pic_read(&p, 0x1020) & 0x80000000u));
    for (unsigned i = 0; i < 66; i++)
        pt_pic_write(&p, 0x10000 + i * 32, 0x80000000u);
    pt_pic_write(&p, 0x10e0, 0xff);
    pt_pic_write(&p, 0x1020, 0x20000000u);
    for (unsigned c = 0; c < 2; c++) {
        assert(pt_pic_read(&p, CTPR + c * CPU1) == 15);
        pt_pic_write(&p, CTPR + c * CPU1, 0);
        assert(pt_pic_read(&p, IACK + c * CPU1) == 0xff);
    }
    /* initVector + enableVector: source 25 to CPU 0, priority 8, level; IPI n: vector 64 + n, priority 14. */
    pt_pic_write(&p, 0x10000 + 25 * 32 + 16, 1);
    pt_pic_write(&p, 0x10000 + 25 * 32, 0x80480000u | 25);
    pt_pic_write(&p, 0x10000 + 25 * 32, 0x00480000u | 25);
    for (unsigned n = 0; n < 2; n++) {
        pt_pic_write(&p, IPI_VP + n * 16, 0x800e0000u | (64 + n));
        assert(pt_pic_read(&p, IPI_VP + n * 16) == (0x800e0000u | (64 + n)));
    }
    /* A masked channel stays pending and is delivered when enabled. */
    kicks[0] = kicks[1] = 0;
    pt_pic_write(&p, IPI_GO + 16, 2); /* CPU 0 signals CPU 1: channel 1, mask 1 << 1 */
    assert(!pt_pic_output_cpu(&p, 1) && !kicks[1]);
    for (unsigned n = 0; n < 2; n++)
        pt_pic_write(&p, IPI_VP + n * 16, 0x000e0000u | (64 + n));
    assert(pt_pic_output_cpu(&p, 1) && kicks[1] == 1 && !pt_pic_output(&p) && !kicks[0]);
    assert(pt_pic_read(&p, IACK) == 0xff); /* not CPU 0's */
    assert(pt_pic_read(&p, IACK + CPU1) == 65);
    assert(!pt_pic_output_cpu(&p, 1) && kicks[1] == 2);
    /* In service on CPU 1: a second dispatch waits for the end of the first. */
    pt_pic_write(&p, IPI_GO + 16, 2);
    assert(!pt_pic_output_cpu(&p, 1));
    assert(pt_pic_read(&p, IACK + CPU1) == 0xff);
    pt_pic_write(&p, EOI + CPU1, 0);
    assert(pt_pic_output_cpu(&p, 1));
    assert(pt_pic_read(&p, IACK + CPU1) == 65);
    pt_pic_write(&p, EOI + CPU1, 0);
    assert(!pt_pic_output_cpu(&p, 1) && pt_pic_read(&p, IACK + CPU1) == 0xff);
    /* CPU 1 signals CPU 0 from its own bank; a device interrupt in service there is preempted (14 > 8). */
    pt_pic_line(&p, 25, 0, 1);
    assert(pt_pic_output(&p) && !pt_pic_output_cpu(&p, 1));
    assert(pt_pic_read(&p, IACK + CPU1) == 0xff); /* the source is CPU 0's */
    assert(pt_pic_read(&p, IACK) == 25);
    pt_pic_write(&p, IPI_GO + CPU1, 1);
    assert(pt_pic_output(&p));
    assert(pt_pic_read(&p, IACK) == 64);
    pt_pic_write(&p, EOI, 0); /* ends the channel, the higher of the two */
    assert(!pt_pic_output(&p)); /* the level source is still in service */
    pt_pic_line(&p, 25, 0, 0);
    pt_pic_write(&p, EOI, 0);
    assert(!pt_pic_output(&p) && pt_pic_read(&p, IACK) == 0xff);
    /* Both CPUs at once, each by its own channel; task priority 15 holds one back. */
    pt_pic_write(&p, CTPR + CPU1, 15);
    pt_pic_write(&p, IPI_GO, 3);      /* channel 0 to both */
    assert(pt_pic_output(&p) && !pt_pic_output_cpu(&p, 1));
    pt_pic_write(&p, CTPR + CPU1, 0);
    assert(pt_pic_output_cpu(&p, 1));
    assert(pt_pic_read(&p, IACK) == 64 && pt_pic_read(&p, IACK + CPU1) == 64);
    pt_pic_write(&p, EOI, 0);
    pt_pic_write(&p, EOI + CPU1, 0);
    assert(!pt_pic_output(&p) && !pt_pic_output_cpu(&p, 1));
    /* A source moved to CPU 1 by its destination register. */
    pt_pic_write(&p, 0x10000 + 25 * 32 + 16, 2);
    pt_pic_line(&p, 25, 0, 1);
    assert(!pt_pic_output(&p) && pt_pic_output_cpu(&p, 1));
    assert(pt_pic_read(&p, IACK + CPU1) == 25);
    pt_pic_line(&p, 25, 0, 0);
    pt_pic_write(&p, EOI + CPU1, 0);
    assert(!pt_pic_output_cpu(&p, 1));
    pt_pic_destroy(&p);
    puts("PASS: OpenPIC, two CPUs");
    return 0;
}
