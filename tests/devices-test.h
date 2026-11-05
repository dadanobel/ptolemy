/* SPDX-License-Identifier: GPL-2.0-or-later
 * Shared by the device tests: DBDMA descriptors, ATA registers, SCSI completion and USB control requests. */
#include "devices.h"
#include <assert.h>
#include <string.h>
#include <unistd.h>
static inline void desc(uint8_t *ram, unsigned at, unsigned n, unsigned cmd, unsigned addr, unsigned dep) {
    memset(ram + at, 0, 16);
    pt_write_le(ram + at, 2, n);
    pt_write_le(ram + at + 2, 2, cmd);
    pt_write_le(ram + at + 4, 4, addr);
    pt_write_le(ram + at + 8, 4, dep);
}
static inline void start(PtDma *d, unsigned at) {
    pt_dma_write(d, 0, (uint32_t)PT_DMA_RUN << 16);
    pt_dma_write(d, 12, at);
    pt_dma_write(d, 0, (uint32_t)PT_DMA_RUN << 16 | PT_DMA_RUN);
}
static inline void aw(PtAta *a, unsigned off, uint8_t v) {
    assert(!pt_ata_mmio(a, off, &v, 1, 1));
}
static inline void ata_command(PtAta *a, unsigned cmd, unsigned count, unsigned lba) {
    aw(a, 0x60, 0xe0);
    aw(a, 0x20, count);
    aw(a, 0x30, lba);
    aw(a, 0x40, lba >> 8);
    aw(a, 0x50, 0);
    aw(a, 0x70, cmd);
}
static inline void scsi_drain(PtScsi *s) {
    for (unsigned i = 0; i < 50000; i++) {
        pt_scsi_service(s);
        if (!s->pending && !s->retry)
            return;
        usleep(100);
    }
    assert(!"SCSI timeout");
}
static inline void usb_control(PtUsbDevice *d, unsigned type, unsigned req, unsigned value, unsigned index) {
    uint8_t p[8] = {type, req, value, value >> 8, index, index >> 8, 0, 0};
    assert(d->packet(d->opaque, 0, PT_USB_SETUP, p, 8, 0) == 8);
    assert(d->packet(d->opaque, 0, PT_USB_IN, p, 0, 0) == 0);
}
