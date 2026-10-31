/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef PT_USB_MSC_H
#define PT_USB_MSC_H
#include "usb.h"
#include "scsi.h"
typedef struct {
    PtScsi scsi;
    unsigned native_recovery,native_zlp;
    uint8_t address,configuration,halt,setup[8],control[256];
    unsigned control_phase,control_position,control_length,stage,phase_error;
    uint32_t tag,host_length,transferred;int input;
} PtUsbMsc;
void pt_usb_msc_init(PtUsbMsc *);
PtUsbDevice pt_usb_msc_device(PtUsbMsc *);
int pt_usb_msc_quiescent(const PtUsbMsc *);
int pt_usb_msc_service(PtUsbMsc *);
#endif
