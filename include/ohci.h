/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef PT_OHCI_H
#define PT_OHCI_H
#include "dma.h"
#include "hid.h"
typedef struct {
    PtRam ram; PtHid *hid[3];PtUsbDevice device[4];
    uint32_t reg[24],ports[4],done;
    unsigned reset_ticks[4],done_delay;
    int default_port,bus_master; /* board updates PCI command bit 2 */
    uint64_t budget_exhaustions;
    void (*irq)(void *,int); void *opaque;
} PtOhci;
void pt_ohci_init(PtOhci *,PtRam,PtHid *keyboard,PtHid *mouse,PtHid *tablet,void (*)(void *,int),void *);
/* CPU owner; NULL disconnects port 4. Backend must remain alive after unplug. */
void pt_ohci_storage(PtOhci *,const PtUsbDevice *);
uint32_t pt_ohci_read(PtOhci *,unsigned offset);
void pt_ohci_write(PtOhci *,unsigned offset,uint32_t);
int pt_ohci_needs_clock(const PtOhci *);
/* CPU owner calls once per elapsed 1 ms, with an outer catch-up budget. */
void pt_ohci_frame(PtOhci *);
#endif
