/* SPDX-License-Identifier: GPL-2.0-or-later
 * The USB Video Class camera on private RAM: descriptors, the probe and commit negotiation as Mac OS X 10.5 does it
 * (VDCAssistant: probe of 26 bytes, commit of 4), payload transfers, pacing, the stream's opening and closing, and
 * the root hub with and without the fifth port. */
#include "ohci.h"
#include "usb_uvc.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static uint8_t ram[1 << 20], frame[2 + 640 * 480 * 2];
static int opened, closed, asked;
static unsigned width, height;
static void stream(void *o, int on, unsigned w, unsigned h) {
    (void)o;
    on ? opened++ : closed++;
    width = w;
    height = h;
}
static int picture(void *o, unsigned w, unsigned h, uint8_t *yuy2) {
    asked++;
    if (!*(int *)o)
        return 0;
    memset(yuy2, 0x40 + asked, w * h * 2);
    return 1;
}
/* A control transfer: the length of its data, or -1 when a stage stalls. */
static int control(PtUsbDevice *d, unsigned type, unsigned req, unsigned value, unsigned index, uint8_t *data,
                   unsigned length) {
    uint8_t setup[8] = {(uint8_t)type, (uint8_t)req, (uint8_t)value,  (uint8_t)(value >> 8),
                        (uint8_t)index, (uint8_t)(index >> 8), (uint8_t)length, (uint8_t)(length >> 8)},
            packet[64];
    unsigned done = 0;
    if (d->packet(d->opaque, 0, PT_USB_SETUP, setup, 8, 0) != 8)
        return -1;
    if (type & 128) {
        for (int got = 64; got == 64 && done < length; done += (unsigned)got) {
            unsigned n = length - done < 64 ? length - done : 64;
            got = d->packet(d->opaque, 0, PT_USB_IN, packet, n, 0);
            if (got < 0)
                return -1;
            memcpy(data + done, packet, (unsigned)got);
        }
        return d->packet(d->opaque, 0, PT_USB_OUT, packet, 0, 0) ? -1 : (int)done;
    }
    for (; done < length; done += 64) {
        unsigned n = length - done < 64 ? length - done : 64;
        memcpy(packet, data + done, n);
        if (d->packet(d->opaque, 0, PT_USB_OUT, packet, n, 0) != (int)n)
            return -1;
    }
    return d->packet(d->opaque, 0, PT_USB_IN, packet, 0, 0) ? -1 : (int)length;
}
/* One bulk transfer as the host makes it: packets of 64 until a short one or `size` bytes; -1: no data (NAK). */
static int transfer(PtUsbDevice *d, PtUsbUvc *u, uint8_t *to, unsigned size) {
    unsigned done = 0;
    while (done < size) {
        unsigned n = size - done < 64 ? size - done : 64;
        int got = d->packet(d->opaque, 1, PT_USB_IN, to + done, n, 0);
        if (got == PT_USB_NAK) {
            if (!done)
                return -1;
            pt_usb_uvc_tick(u); /* the rest of the payload in the next USB frame */
            continue;
        }
        assert(got >= 0);
        done += (unsigned)got;
        if ((unsigned)got < n)
            break;
    }
    return (int)done;
}
static void descriptors(PtUsbDevice *d) {
    uint8_t b[512];
    assert(control(d, 0x80, 6, 0x100, 0, b, 18) == 18 && b[4] == 0xef && b[5] == 2 && b[6] == 1 && b[7] == 64);
    assert(control(d, 0x80, 6, 0x200, 0, b, 9) == 9);
    unsigned total = b[2] | b[3] << 8, at = 0, vc = 0, vs = 0, frames = 0, in_vs = 0, endpoint = 0, format = 0;
    assert(control(d, 0x80, 6, 0x200, 0, b, sizeof b) == (int)total && b[4] == 2);
    while (at < total) {
        uint8_t *p = b + at;
        assert(p[0] >= 2 && at + p[0] <= total);
        if (p[1] == 11) /* interface association: a video collection of both interfaces */
            assert(p[0] == 8 && p[2] == 0 && p[3] == 2 && p[4] == 14 && p[5] == 3);
        if (p[1] == 4)
            in_vs = p[2] == 1 && p[5] == 14 && p[6] == 2;
        if (p[1] == 0x24 && !in_vs)
            vc += p[0];
        if (p[1] == 0x24 && in_vs)
            vs += p[0];
        if (p[1] == 0x24 && !in_vs && p[2] == 1) /* VideoControl header: UVC 1.0, its length, one streaming interface */
            assert(p[3] == 0 && p[4] == 1 && (p[5] | p[6] << 8) == 50 && p[11] == 1 && p[12] == 1);
        if (p[1] == 0x24 && in_vs && p[2] == 1) /* input header: one format, the endpoint, its length */
            assert(p[3] == 1 && (p[4] | p[5] << 8) == 107 && p[6] == 0x81);
        if (p[1] == 0x24 && in_vs && p[2] == 4) { /* the GUID Mac OS X 10.5 compares in full */
            static const uint8_t yuy2[16] = {'Y', 'U', 'Y', '2', 0, 0, 0x10, 0, 0x80, 0, 0, 0xaa, 0, 0x38, 0x9b, 0x71};
            assert(p[0] == 27 && p[3] == 1 && p[4] == 2 && !memcmp(p + 5, yuy2, 16) && p[21] == 16 && p[22] == 1);
            format = 1;
        }
        if (p[1] == 0x24 && in_vs && p[2] == 5) { /* 640x480 first, then 320x240; 30 frames a second only */
            unsigned w = p[5] | p[6] << 8, h = p[7] | p[8] << 8;
            assert(p[0] == 30 && p[3] == ++frames && w == (frames == 1 ? 640u : 320u) && h == w * 3 / 4);
            assert(pt_read_le(p + 17, 4) == w * h * 2 && pt_read_le(p + 21, 4) == 333333 && p[25] == 1 &&
                   pt_read_le(p + 26, 4) == 333333);
        }
        if (p[1] == 5) { /* bulk IN, 64 bytes */
            assert(in_vs && p[2] == 0x81 && p[3] == 2 && p[4] == 64 && !p[5]);
            endpoint++;
        }
        at += p[0];
    }
    assert(at == total && vc == 50 && vs == 107 && frames == 2 && endpoint == 1 && format);
    assert(control(d, 0x80, 6, 0x600, 0, b, 10) < 0 && control(d, 0x80, 6, 0x302, 0, b, 255) == 30);
}
static void td(unsigned a, unsigned flags, unsigned cbp, unsigned next, unsigned end) {
    pt_write_le(ram + a, 4, flags);
    pt_write_le(ram + a + 4, 4, cbp);
    pt_write_le(ram + a + 8, 4, next);
    pt_write_le(ram + a + 12, 4, end);
}
static void level(void *p, int v) {
    *(int *)p = v;
}
/* The root hub: four ports as before without the camera, a fifth with it; then a keyboard report and a camera read
 * in the same USB frame. */
static void hub(void) {
    PtHid h[3];
    for (unsigned i = 0; i < 3; i++)
        pt_hid_init(&h[i], i);
    PtOhci o;
    int line = 0;
    pt_ohci_init(&o, (PtRam){ram, sizeof ram, NULL}, h, h + 1, h + 2, level, &line);
    static const uint32_t before[] = {0x10, 0, 0, 0x40, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x2edf, 0, 0, 0, 0x628, 0x02000204,
                                      0x000f0000, 0, 0x10101, 0x10101, 0x10101, 0x100, 0, 0};
    for (unsigned r = 0; r < sizeof before / sizeof *before; r++) /* every register as without this device */
        assert(pt_ohci_read(&o, 4 * r) == (r == 5 ? 0 : before[r]));
    PtUsbUvc u;
    pt_usb_uvc_init(&u);
    PtUsbDevice d = pt_usb_uvc_device(&u);
    pt_ohci_camera(&o, &d);
    assert(pt_ohci_read(&o, 0x48) == 0x02000205 && pt_ohci_read(&o, 0x64) == 0x10101 && pt_ohci_read(&o, 0x68) == 0);
    for (unsigned r = 0; r < 25; r++) /* the other registers and the first four ports did not change */
        assert(r == 18 || pt_ohci_read(&o, 4 * r) == (r == 5 ? 0 : before[r]));
    pt_ohci_write(&o, 0x54, 16);
    pt_ohci_write(&o, 0x64, 16);
    for (unsigned i = 0; i < 10; i++)
        pt_ohci_frame(&o);
    assert((pt_ohci_read(&o, 0x54) & 0x13) == 3 && (pt_ohci_read(&o, 0x64) & 0x13) == 3);
    uint8_t probe[26] = {0, 0, 1, 2};
    h[0].address = 7;
    h[0].configuration = 1;
    o.default_port = 4;
    assert(control(&d, 0, 5, 9, 0, NULL, 0) == 0 && control(&d, 0, 9, 1, 0, NULL, 0) == 0 && u.address == 9);
    assert(control(&d, 0x21, 1, 0x200, 1, probe, 4) == 4); /* 320x240: 153602 bytes a payload */
    memset(ram + 0x1000, 0, 256);
    pt_ohci_write(&o, 0x18, 0x1000);
    pt_hid_key(h, 4, 1);
    td(0x2100, 0xf0000000 | (2 << 19), 0x4000, 0x2120, 0x4007);
    td(0x2000, (8 << 16) | (1 << 7) | 7, 0x2120, 0x2100, 0);
    for (unsigned i = 0; i < 32; i++)
        pt_write_le(ram + 0x1000 + 4 * i, 4, 0x2000);
    for (unsigned k = 0; k < 8; k++) /* eight transfer descriptors of two pages each on the bulk list */
        td(0x2300 + 16 * k, 0xf0000000 | (2 << 19), 0x10000 + 0x2000 * k, 0x2310 + 16 * k, 0x11fff + 0x2000 * k);
    td(0x2200, (64 << 16) | (2 << 11) | (1 << 7) | 9, 0x2380, 0x2300, 0);
    pt_ohci_write(&o, 0x28, 0x2200);
    pt_ohci_write(&o, 0x10, 0x80000002);
    pt_ohci_write(&o, 4, 0xb4);
    pt_ohci_frame(&o);
    pt_usb_uvc_tick(&u);
    assert(ram[0x4002] == 4);                                  /* the key, in the frame the camera filled */
    assert(ram[0x10000] == 2 && (ram[0x10001] & 0xfe) == 0x82); /* the payload header */
    assert((pt_read_le(ram + 0x2208, 4) & ~15u) == 0x2340);     /* 512 packets: four descriptors, no more */
    pt_ohci_write(&o, 12, 2);
    pt_ohci_frame(&o);
    assert((pt_read_le(ram + 0x2208, 4) & ~15u) == 0x2380);
    pt_usb_uvc_close(&u);
    for (unsigned i = 0; i < 3; i++)
        pt_hid_destroy(&h[i]);
}
int main(void) {
    PtUsbUvc u;
    int have = 1;
    pt_usb_uvc_init(&u);
    PtUsbDevice d = pt_usb_uvc_device(&u);
    uint8_t b[64], probe[26] = {0}, got[26];
    descriptors(&d);
    assert(control(&d, 0, 5, 5, 0, NULL, 0) == 0 && u.address == 5 && control(&d, 0, 9, 1, 0, NULL, 0) == 0);
    assert(d.packet(d.opaque, 1, PT_USB_IN, b, 64, 0) == PT_USB_NAK); /* nothing committed: no stream */
    /* Probe: 26 bytes out, 26 back with the frame and payload sizes; GET_LEN, GET_INFO and the limits. */
    probe[2] = 1;
    probe[3] = 1;
    pt_write_le(probe + 4, 4, 333333);
    assert(control(&d, 0x21, 1, 0x100, 1, probe, 26) == 26 && control(&d, 0xa1, 0x81, 0x100, 1, got, 26) == 26);
    assert(got[2] == 1 && got[3] == 1 && pt_read_le(got + 4, 4) == 333333 && pt_read_le(got + 18, 4) == 614400 &&
           pt_read_le(got + 22, 4) == 614402);
    assert(control(&d, 0xa1, 0x85, 0x100, 1, b, 2) == 2 && b[0] == 26 && control(&d, 0xa1, 0x86, 0x100, 1, b, 1) == 1);
    assert(control(&d, 0xa1, 0x82, 0x100, 1, b, 26) == 26 && control(&d, 0xa1, 0x83, 0x100, 1, b, 26) == 26 &&
           control(&d, 0xa1, 0x87, 0x100, 1, b, 26) == 26 && pt_read_le(b + 22, 4) == 614402);
    probe[3] = 2; /* the second frame; an interval we do not have comes back as ours; an unknown frame as the first */
    pt_write_le(probe + 4, 4, 666666);
    assert(control(&d, 0x21, 1, 0x100, 1, probe, 26) == 26 && control(&d, 0xa1, 0x81, 0x100, 1, got, 26) == 26);
    assert(got[3] == 2 && pt_read_le(got + 4, 4) == 333333 && pt_read_le(got + 18, 4) == 153600 &&
           pt_read_le(got + 22, 4) == 153602);
    probe[3] = 9;
    assert(control(&d, 0x21, 1, 0x100, 1, probe, 26) == 26 && control(&d, 0xa1, 0x81, 0x100, 1, got, 26) == 26 &&
           got[3] == 1);
    /* A refused SETUP ends the data stage of the request before it: its OUT packet is not taken as data. */
    static uint8_t set[8] = {0x21, 1, 0, 1, 1, 0, 26, 0}, bad[8] = {0x21, 1, 0, 9, 1, 0, 0, 2}, big[512];
    assert(d.packet(d.opaque, 0, PT_USB_SETUP, set, 8, 0) == 8 &&
           d.packet(d.opaque, 0, PT_USB_SETUP, bad, 8, 0) == PT_USB_STALL &&
           d.packet(d.opaque, 0, PT_USB_OUT, big, 512, 0) == PT_USB_STALL);
    /* The VideoControl interface has no controls: a request stalls and the error code says "invalid control". */
    assert(control(&d, 0xa1, 0x81, 0x200, 0x200, b, 2) < 0 && control(&d, 0xa1, 0x81, 0x200, 0, b, 1) == 1 && b[0] == 6);
    /* Commit as VDCAssistant sends it on a bulk stream: four bytes. The camera is still off. */
    u.source = (PtUvcSource){&have, stream, picture};
    probe[3] = 2;
    assert(control(&d, 0x21, 1, 0x200, 1, probe, 4) == 4 && !opened && u.committed);
    assert(control(&d, 0xa1, 0x81, 0x200, 1, got, 26) == 26 && got[3] == 2 && pt_read_le(got + 22, 4) == 153602);
    /* The first read opens the stream; each transfer is one payload: header, then the whole frame. */
    unsigned fid = 2;
    uint64_t first = 0;
    for (unsigned k = 0; k < 62; k++) {
        int n;
        while ((n = transfer(&d, &u, frame, 153602)) < 0) /* no sooner than a thirtieth of a second after the last */
            pt_usb_uvc_tick(&u);
        assert(n == 153602 && opened == 1 && width == 320 && height == 240);
        assert(frame[0] == 2 && (frame[1] & 0xfe) == 0x82 && (frame[1] & 1u) != fid); /* end of frame; the bit alternates */
        assert(frame[2] == (uint8_t)(0x40 + asked) && frame[153601] == frame[2]);
        fid = frame[1] & 1;
        if (!k)
            first = u.ticks;
    }
    /* 62 frames; the 61 intervals after the first one took 61/30 s (a frame of 153602 bytes needs 5 USB frames). */
    assert(u.frames == 62 && u.ticks - first >= 2033 - 34 && u.ticks - first <= 2033 + 6);
    /* No picture from the source: NAK, and the stream stays open while it is read. */
    have = 0;
    for (unsigned k = 0; k < 600; k++) {
        pt_usb_uvc_tick(&u);
        assert(transfer(&d, &u, frame, 153602) < 0);
    }
    assert(opened == 1 && !closed);
    have = 1;
    /* CLEAR_FEATURE(ENDPOINT_HALT) in the middle of a payload: the next transfer starts with a header again. */
    while (d.packet(d.opaque, 1, PT_USB_IN, frame, 64, 0) != 64)
        pt_usb_uvc_tick(&u);
    assert(frame[0] == 2 && d.packet(d.opaque, 1, PT_USB_IN, frame, 64, 0) == 64);
    assert(control(&d, 2, 1, 0, 0x81, NULL, 0) == 0);
    while (transfer(&d, &u, frame, 153602) < 0)
        pt_usb_uvc_tick(&u);
    assert(frame[0] == 2 && (frame[1] & 0xfe) == 0x82);
    /* Nobody reads for half a second: the stream is closed, once; the next read opens it again. */
    for (unsigned k = 0; k < 499; k++)
        pt_usb_uvc_tick(&u);
    assert(!closed);
    pt_usb_uvc_tick(&u);
    pt_usb_uvc_tick(&u);
    assert(closed == 1 && opened == 1);
    while (transfer(&d, &u, frame, 153602) < 0)
        pt_usb_uvc_tick(&u);
    assert(opened == 2 && frame[0] == 2);
    /* A new commit closes the stream and takes the other size; a bus reset closes it too. */
    probe[3] = 1;
    assert(control(&d, 0x21, 1, 0x200, 1, probe, 4) == 4 && closed == 2);
    while (transfer(&d, &u, frame, 614402) < 0)
        pt_usb_uvc_tick(&u);
    assert(opened == 3 && width == 640 && height == 480 && frame[614401] == frame[2]);
    d.reset(d.opaque);
    assert(closed == 3 && !u.committed && !u.address);
    /* Without a source: the test pattern, a different picture every frame. */
    u.source = (PtUvcSource){0};
    assert(control(&d, 0, 9, 1, 0, NULL, 0) == 0 && control(&d, 0x21, 1, 0x200, 1, probe, 4) == 4);
    assert(transfer(&d, &u, frame, 614402) == 614402);
    uint8_t one[64];
    memcpy(one, frame + 2, 64);
    while (transfer(&d, &u, frame, 614402) < 0)
        pt_usb_uvc_tick(&u);
    assert(memcmp(one, frame + 2, 64));
    pt_usb_uvc_close(&u);
    hub();
    puts("usb-uvc: descriptors, probe of 26 and commit of 4 bytes, one frame a payload at 30 a second, 512 packets a "
         "USB frame, stream opened by the first read and closed after 500 ms; root hub of four and of five ports");
    return 0;
}
