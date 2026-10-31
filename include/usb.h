/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef PT_USB_H
#define PT_USB_H
#include "platform.h"
enum { PT_USB_SETUP,PT_USB_OUT,PT_USB_IN,PT_USB_STALL=-1,PT_USB_NAK=-2 };
typedef struct {
    void *opaque;
    void (*reset)(void *);
    unsigned (*address)(void *);
    int (*packet)(void *,unsigned ep,unsigned pid,uint8_t *,unsigned n,uint16_t frame);
} PtUsbDevice;
#endif
