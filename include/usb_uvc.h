/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef PT_USB_UVC_H
#define PT_USB_UVC_H
#include "usb.h"
/* Where the camera's pictures come from. Both calls are made by the CPU owner (the USB frame). Without a source the
 * device draws a moving test pattern. */
typedef struct {
    void *opaque;
    /* The guest opened (on) or closed the video stream: frames of width x height are wanted from now, or none. */
    void (*stream)(void *, int on, unsigned width, unsigned height);
    /* A picture newer than the last one taken, as width * height * 2 bytes of YUY2: 1, or 0 when there is none yet. */
    int (*frame)(void *, unsigned width, unsigned height, uint8_t *yuy2);
} PtUvcSource;
/* A USB Video Class 1.0 camera (USB-IF "USB Device Class Definition for Video Devices" 1.0, with the Uncompressed
 * Payload specification): full speed, one bulk IN endpoint for the stream, one uncompressed YUY2 format, 640x480 and
 * 320x240 at 30 frames a second. One payload transfer carries one frame. */
typedef struct {
    PtUvcSource source;
    uint8_t address, configuration, halt, error, setup[8], control[256];
    unsigned control_phase, control_position, control_length;
    uint8_t probe[26], commit[26];
    int committed, open;
    uint8_t *payload; /* header and frame of the committed size, allocated at the commit */
    unsigned length, position, fid, pattern, packets;
    uint64_t ticks, next_at, last_in, opened_at, frames, packets_at;
} PtUsbUvc;
void pt_usb_uvc_init(PtUsbUvc *);
void pt_usb_uvc_close(PtUsbUvc *);
PtUsbDevice pt_usb_uvc_device(PtUsbUvc *);
/* Once per USB frame (1 ms): the stream's clock. A stream nobody read for half a second is closed. */
void pt_usb_uvc_tick(PtUsbUvc *);
#endif
