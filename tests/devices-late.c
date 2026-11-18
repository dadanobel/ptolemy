/* SPDX-License-Identifier: GPL-2.0-or-later */
/* pt_devices_deadline on a quiet USB bus: the frames run late, in one batch - also with the camera attached, as the
 * product has it, until its stream is open. */
#include "devices.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
int main(void) {
    setenv("PT_CAMERA", "1", 1);
    unsetenv("PT_DEVICE_LATE");
    PtDevices *d = calloc(1, sizeof *d);
    static uint8_t ram[65536];
    PtBus bus = {0};
    PtPic pic;
    PtEvent event;
    _Atomic uint32_t kick = 0;
    pt_pic_init(&pic);
    pt_event_init(&event);
    assert(!pt_devices_init(d, (PtRam){ram, sizeof ram, NULL}, &bus, &pic, &event, &kick, NULL, 0, 0, 0));
    assert(d->camera_on);
    d->usb.bus_master = 1;
    pt_ohci_write(&d->usb, 0x18, 0x1000); /* HCCA */
    pt_ohci_write(&d->usb, 4, 0x80);      /* operational, no list enabled */
    uint64_t now = 1000000000;
    pt_devices_service(d, now); /* the bus's clock starts: the first frame in a millisecond */
    assert(!pt_ohci_quiet(&d->usb) && pt_devices_deadline(d) <= now + 1000000);
    now += 1000000;
    pt_devices_service(d, now); /* a frame with nothing in it */
    assert(pt_ohci_quiet(&d->usb) && d->usb_at == now + 1000000 && d->camera.ticks == 1);
    assert(pt_devices_deadline(d) == d->usb_at + 7000000); /* the camera is attached and not streaming */
    now += 8000000;
    pt_devices_service(d, now); /* eight frames in one service, the camera's clock with them */
    assert(d->camera.ticks == 9 && d->usb_at == now + 1000000 && pt_devices_deadline(d) == d->usb_at + 7000000);
    d->camera.open = 1; /* a stream: every frame in its millisecond */
    assert(pt_devices_deadline(d) == d->usb_at);
    d->camera.open = 0;
    pt_ohci_write(&d->usb, 0x10, 0x80000000); /* a register write: not quiet until a frame says so again */
    assert(pt_devices_deadline(d) <= d->usb_at);
    assert(!pt_devices_close(d));
    free(d);
    pt_event_destroy(&event);
    puts("devices-late ok");
    return 0;
}
