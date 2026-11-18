/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef PT_OHCI_H
#define PT_OHCI_H
#include "dma.h"
#include "hid.h"
typedef struct {
    PtRam ram;
    PtUsbDevice device[5];
    uint32_t reg[24], ports[5], done;
    unsigned reset_ticks[5], done_delay, nports; /* nports: 4, or 5 once a camera is attached */
    int default_port, bus_master; /* board updates PCI command bit 2 */
    int quiet, waiting; /* quiet: see pt_ohci_quiet; waiting: a control or bulk transfer got no answer in this frame */
    void (*irq)(void *, int);
    void *opaque;
} PtOhci;
void pt_ohci_init(PtOhci *, PtRam, PtHid *keyboard, PtHid *mouse, PtHid *tablet, void (*)(void *, int), void *);
/* CPU owner; NULL disconnects port 4. Backend must remain alive after unplug. */
void pt_ohci_storage(PtOhci *, const PtUsbDevice *);
/* Before the CPU runs: a fifth root hub port with this device on it. Without the call the hub has four ports. */
void pt_ohci_camera(PtOhci *, const PtUsbDevice *);
uint32_t pt_ohci_read(PtOhci *, unsigned offset);
void pt_ohci_write(PtOhci *, unsigned offset, uint32_t);
int pt_ohci_needs_clock(const PtOhci *);
/* After a frame: the next frames can complete nothing by themselves - no port in reset, no done queue to hand over, no
 * start-of-frame interrupt asked for, no transfer waiting on the control or bulk list (a silent interrupt endpoint is a
 * device with nothing to say). The owner may then run them late, in one batch. A register write ends it. */
int pt_ohci_quiet(const PtOhci *);
/* CPU owner calls once per elapsed 1 ms, with an outer catch-up budget. */
void pt_ohci_frame(PtOhci *);
#endif
