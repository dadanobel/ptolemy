/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef PT_HID_H
#define PT_HID_H
#include "platform.h"
#include "usb.h"
enum { PT_HID_KEYBOARD,PT_HID_MOUSE,PT_HID_TABLET };
typedef struct { uint8_t keys[8],buttons; int32_t x,y,wheel; } PtHidEvent;
typedef struct {
    pthread_mutex_t lock;
    unsigned kind,head,count,overflows;
    uint8_t down[256],buttons; int32_t x,y;
    PtHidEvent events[64];
    /* Below this line: USB/CPU owner only. UI only calls key/pointer/release. */
    uint8_t address,configuration,protocol,idle,leds,halt;
    uint8_t control[256],setup[8];
    unsigned length,position,phase; uint16_t last_frame;
} PtHid;
void pt_hid_init(PtHid *,unsigned kind);
void pt_hid_destroy(PtHid *);
void pt_hid_reset(PtHid *);
/* UI producer APIs accept USB HID usages, NOT macOS virtual key codes. */
void pt_hid_key(PtHid *,unsigned usage,int pressed);
void pt_hid_pointer(PtHid *,uint8_t buttons,int32_t x,int32_t y,int32_t wheel);
void pt_hid_release(PtHid *); /* on focus/capture loss */
/* One USB packet. Returns actual bytes or STALL/NAK. SETUP exactly 8 bytes.
 * SET_ADDRESS takes effect only after the status stage. No AppKit calls. */
int pt_hid_packet(PtHid *,unsigned endpoint,unsigned pid,uint8_t *,unsigned size,uint16_t frame);
#endif
