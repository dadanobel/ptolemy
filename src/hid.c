/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "hid.h"
#include <stdlib.h>
#include <string.h>
/* USB 2.0 chapter 9 and HID 1.11 boot keyboard/relative mouse/absolute pointer.
 * The keyboard is an Apple Pro Keyboard (ANSI, 05ac:020b): Tiger and Leopard drive it with the generic HID driver and
 * know its layout without asking. As a PowerBook's built-in keyboard (Fountain, 020e) it got Leopard's TopCase driver,
 * which never polled it: no key reached Leopard.
 * The relative pointer is an Apple Mighty Mouse (05ac:0304) with its wheel and AC Pan: Tiger's and Leopard's AppleUSBHIDMouse
 * give it 43 scroll counts per inch and its own acceleration, so the host's fine trackpad deltas scroll smoothly (u237);
 * every scroll goes through it, also while the tablet points. */
static unsigned nd_hid_report(unsigned kind, uint8_t *out) {
    static const uint8_t keys[] = {0x05, 1,    0x09, 6,    0xa1, 1,    0x05, 7,    0x19, 0xe0, 0x29, 0xe7, 0x15,
                                   0,    0x25, 1,    0x75, 1,    0x95, 8,    0x81, 2,    0x75, 8,    0x95, 1,
                                   0x81, 1,    0x05, 8,    0x19, 1,    0x29, 5,    0x75, 1,    0x95, 5,    0x91,
                                   2,    0x75, 3,    0x95, 1,    0x91, 1,    0x05, 7,    0x19, 0,    0x29, 0xe7,
                                   0x15, 0,    0x26, 0xe7, 0,    0x75, 8,    0x95, 6,    0x81, 0,    0xc0};
    static const uint8_t mouse[] = {0x05, 1,    0x09, 2,    0xa1, 1,    0x09, 1,    0xa1, 0,    0x05, 9,    0x19,
                                    1,    0x29, 5,    0x15, 0,    0x25, 1,    0x75, 1,    0x95, 5,    0x81, 2,
                                    0x75, 3,    0x95, 1,    0x81, 1,    0x05, 1,    0x09, 0x30, 0x09, 0x31, 0x09,
                                    0x38, 0x15, 0x81, 0x25, 0x7f, 0x75, 8,    0x95, 3,    0x81, 6,    0x05, 0x0c, 0x0a,
                                    0x38, 0x02, 0x15, 0x81, 0x25, 0x7f, 0x75, 8,    0x95, 1,    0x81, 6,    0xc0, 0xc0};
    static const uint8_t tablet[] = {0x05, 1,    0x09, 2,    0xa1, 1,    0x09, 1,    0xa1, 0,    0x05, 9,    0x19,
                                     1,    0x29, 3,    0x15, 0,    0x25, 1,    0x75, 1,    0x95, 3,    0x81, 2,
                                     0x75, 5,    0x95, 1,    0x81, 1,    0x05, 1,    0x09, 0x30, 0x09, 0x31, 0x15,
                                     0,    0x26, 0xff, 0x7f, 0x75, 16,   0x95, 2,    0x81, 2,    0x09, 0x38, 0x15,
                                     0x81, 0x25, 0x7f, 0x75, 8,    0x95, 1,    0x81, 6,    0xc0, 0xc0};
    const uint8_t *p = kind == PT_HID_KEYBOARD ? keys : kind == PT_HID_MOUSE ? mouse : tablet;
    unsigned n = (unsigned)(kind == PT_HID_KEYBOARD ? sizeof keys
                            : kind == PT_HID_MOUSE  ? sizeof mouse
                                                    : sizeof tablet);
    memcpy(out, p, n);
    return n;
}
/* USB GET_DESCRIPTOR for a HID device: device, configuration, HID report descriptor and the name strings (Apple-
 * compatible keyboard, absolute or relative pointer). */
static int nd_hid_descriptor(PtHid *h, unsigned type, unsigned value, unsigned index, uint8_t *b) {
    unsigned descriptor = value >> 8, number = value & 255;
    uint8_t report_bytes[128];
    unsigned report_size = nd_hid_report(h->kind, report_bytes);
    if (type == 0x80 && descriptor == 1 && !number && !index) {
        uint8_t device[] = {18, 1, 0x10, 1, 0, 0, 0, 8, 0, 0, 0, 0, 0, 1, 1, 2, 0, 1};
        /* The tablet's VID/PID is private-test-only, not globally unique; replace before distribution. */
        unsigned vendor = h->kind == PT_HID_TABLET ? 0x1209 : 0x05ac,
                 product = h->kind == PT_HID_KEYBOARD ? 0x020b : h->kind == PT_HID_MOUSE ? 0x0304 : 0x0001;
        pt_write_le(device + 8, 2, vendor);
        pt_write_le(device + 10, 2, product);
        memcpy(b, device, sizeof device);
        return sizeof device;
    }
    if (type == 0x80 && descriptor == 2 && !number && !index) {
        uint8_t config[] = {9, 2, 34,   0,    1, 1, 0, 0x80, 50, 9, 4, 0, 0,    1, 3, 0, 0,
                            0, 9, 0x21, 0x11, 1, 0, 1, 0x22, 0,  0, 7, 5, 0x81, 3, 8, 0, 10};
        if (pt_env_flag("PT_POINTER_V2") && h->kind != PT_HID_KEYBOARD)
            config[33] = 1;
        config[15] = h->kind == PT_HID_TABLET ? 0 : 1;
        config[16] = h->kind == PT_HID_KEYBOARD ? 1 : h->kind == PT_HID_MOUSE ? 2 : 0;
        config[22] = h->kind == PT_HID_KEYBOARD ? 33 : 0;
        pt_write_le(config + 25, 2, report_size);
        memcpy(b, config, sizeof config);
        return sizeof config;
    }
    if (type == 0x81 && !index && !number) {
        if (descriptor == 0x22) {
            memcpy(b, report_bytes, report_size);
            return (int)report_size;
        }
        if (descriptor == 0x21) {
            uint8_t hid[] = {9, 0x21, 0x11, 1, 0, 1, 0x22, 0, 0};
            hid[4] = h->kind == PT_HID_KEYBOARD ? 33 : 0;
            pt_write_le(hid + 7, 2, report_size);
            memcpy(b, hid, sizeof hid);
            return sizeof hid;
        }
    }
    if (type == 0x80 && descriptor == 3) {
        if (!number && !index) {
            b[0] = 4;
            b[1] = 3;
            b[2] = 9;
            b[3] = 4;
            return 4;
        }
        if ((index == 0 || index == 0x0409) && (number == 1 || number == 2)) {
            const char *text = number == 1                  ? "Ptolemy"
                               : h->kind == PT_HID_KEYBOARD ? "Apple-compatible ANSI Keyboard"
                               : h->kind == PT_HID_TABLET   ? "Absolute Pointer"
                                                            : "Relative Pointer";
            unsigned n = (unsigned)strlen(text);
            b[0] = (uint8_t)(2 + n * 2);
            b[1] = 3;
            for (unsigned k = 0; k < n; k++) {
                b[2 + k * 2] = (uint8_t)text[k];
                b[3 + k * 2] = 0;
            }
            return b[0];
        }
    }
    return -1;
}
/* The boot keyboard report of the keys held now: modifier bits, then up to six keys (more: ErrorRollOver). */
static void keyboard(PtHid *h, uint8_t *b) {
    memset(b, 0, 8);
    for (unsigned i = 0; i < 8; i++)
        b[0] |= !!h->down[224 + i] << i;
    unsigned n = 0;
    for (unsigned i = 4; i < 224; i++)
        if (h->down[i]) {
            if (n < 6)
                b[2 + n] = i;
            n++;
        }
    if (n > 6)
        memset(b + 2, 1, 6); /* USB boot ErrorRollOver */
}
/* Queues one report; a full queue (64) starts over from a neutral report. */
static void push(PtHid *h, PtHidEvent e) {
    if (h->count == 64) {
        /* Neutral first prevents a lost key-up/button-up from sticking.
         * This intentionally loses history; count it rather than hiding it. */
        h->overflows++;
        h->head = 0;
        h->count = 1;
        memset(&h->events[0], 0, sizeof e);
    }
    h->events[(h->head + h->count++) % 64] = e;
}
/* A device of one kind (keyboard, mouse or tablet), unconfigured, in the report protocol (HID protocol 1). */
void pt_hid_init(PtHid *h, unsigned kind) {
    memset(h, 0, sizeof *h);
    if (kind > PT_HID_TABLET || pthread_mutex_init(&h->lock, 0))
        abort();
    h->kind = kind;
    h->protocol = 1;
}
/* Frees what pt_hid_init made. */
void pt_hid_destroy(PtHid *h) {
    pthread_mutex_destroy(&h->lock);
}
/* Every key and button up, the queue replaced by one neutral report (the window lost the focus or the capture). */
void pt_hid_release(PtHid *h) {
    pthread_mutex_lock(&h->lock);
    memset(h->down, 0, sizeof h->down);
    h->buttons = 0;
    h->head = h->count = 0;
    push(h, (PtHidEvent){.x = h->kind == PT_HID_TABLET ? h->x : 0, .y = h->kind == PT_HID_TABLET ? h->y : 0});
    pthread_mutex_unlock(&h->lock);
}
/* A USB bus reset: no address, no configuration, nothing queued or held. */
void pt_hid_reset(PtHid *h) {
    h->address = h->configuration = h->idle = h->halt = 0;
    h->protocol = 1;
    h->phase = h->position = h->length = 0;
    h->last_frame = 0;
    pthread_mutex_lock(&h->lock);
    h->head = h->count = 0;
    memset(h->down, 0, sizeof h->down);
    h->buttons = 0;
    pthread_mutex_unlock(&h->lock);
}
/* A key went down or up: the keyboard's whole state becomes the next report. */
void pt_hid_key(PtHid *h, unsigned usage, int pressed) {
    if (h->kind != PT_HID_KEYBOARD || usage < 4 || usage >= 232)
        return;
    pthread_mutex_lock(&h->lock);
    if (h->down[usage] != !!pressed) {
        h->down[usage] = !!pressed;
        PtHidEvent e = {0};
        keyboard(h, e.keys);
        push(h, e);
    }
    pthread_mutex_unlock(&h->lock);
}
/* a + b, saturated to int32. */
static int32_t sum_delta(int32_t a, int32_t b) {
    int64_t n = (int64_t)a + b;
    return n > INT32_MAX ? INT32_MAX : n < INT32_MIN ? INT32_MIN : n;
}
/* Queues a pointer report from the host; consecutive moves with the same buttons merge, button changes never do. */
void pt_hid_pointer(PtHid *h, uint8_t buttons, int32_t x, int32_t y, int32_t wheel) {
    if (h->kind == PT_HID_KEYBOARD)
        return;
    pthread_mutex_lock(&h->lock);
    h->buttons = buttons & (h->kind == PT_HID_MOUSE ? 31 : 7);
    if (h->kind == PT_HID_TABLET) {
        x = x < 0 ? 0 : x > 32767 ? 32767 : x;
        y = y < 0 ? 0 : y > 32767 ? 32767 : y;
        h->x = x;
        h->y = y;
    }
    /* Merge only the newest same-button packet: never cross a button edge. */
    if (h->count && h->events[(h->head + h->count - 1) % 64].buttons == h->buttons) {
        PtHidEvent *last = &h->events[(h->head + h->count - 1) % 64];
        last->x = h->kind == PT_HID_TABLET ? x : sum_delta(last->x, x);
        last->y = h->kind == PT_HID_TABLET ? y : sum_delta(last->y, y);
        last->wheel = sum_delta(last->wheel, wheel);
    } else
        push(h, (PtHidEvent){.buttons = h->buttons, .x = x, .y = y, .wheel = wheel});
    pthread_mutex_unlock(&h->lock);
}
/* Queues wheel and AC Pan counts for the mouse, merged into the newest report while the buttons stay the same. */
void pt_hid_scroll(PtHid *h, int32_t wheel, int32_t pan) {
    if (h->kind != PT_HID_MOUSE || (!wheel && !pan))
        return;
    pthread_mutex_lock(&h->lock);
    if (h->count && h->events[(h->head + h->count - 1) % 64].buttons == h->buttons) {
        PtHidEvent *last = &h->events[(h->head + h->count - 1) % 64];
        last->wheel = sum_delta(last->wheel, wheel);
        last->pan = sum_delta(last->pan, pan);
    } else
        push(h, (PtHidEvent){.buttons = h->buttons, .wheel = wheel, .pan = pan});
    pthread_mutex_unlock(&h->lock);
}
/* One report's share of a movement: -127..127. */
static int clamp(int32_t x) {
    return x < -127 ? -127 : x > 127 ? 127 : x;
}
/* Builds the next interrupt-IN report (keyboard state or the oldest pointer movement), optionally taking it off the
 * queue. */
static unsigned report(PtHid *h, uint8_t *b, int consume, int force) {
    pthread_mutex_lock(&h->lock);
    if (!h->count && !force) {
        pthread_mutex_unlock(&h->lock);
        return 0;
    }
    PtHidEvent e = {
            .buttons = h->buttons, .x = h->kind == PT_HID_TABLET ? h->x : 0, .y = h->kind == PT_HID_TABLET ? h->y : 0};
    if (h->kind == PT_HID_KEYBOARD)
        keyboard(h, e.keys);
    if (consume && h->count)
        e = h->events[h->head];
    unsigned n;
    if (h->kind == PT_HID_KEYBOARD) {
        memcpy(b, e.keys, 8);
        n = 8;
    } else if (h->kind == PT_HID_MOUSE) {
        b[0] = e.buttons;
        b[1] = clamp(e.x);
        b[2] = clamp(e.y);
        b[3] = clamp(e.wheel);
        b[4] = clamp(e.pan);
        e.x -= clamp(e.x);
        e.y -= clamp(e.y);
        e.wheel -= clamp(e.wheel);
        e.pan -= clamp(e.pan);
        n = h->protocol ? 5 : 3;
    } else {
        b[0] = e.buttons;
        pt_write_le(b + 1, 2, e.x);
        pt_write_le(b + 3, 2, e.y);
        b[5] = clamp(e.wheel);
        e.wheel -= clamp(e.wheel);
        n = 6;
    }
    if (consume && h->count) {
        if (h->kind == PT_HID_KEYBOARD || (h->kind == PT_HID_TABLET ? !e.wheel : !e.x && !e.y && !e.wheel && !e.pan)) {
            h->head = (h->head + 1) % 64;
            h->count--;
        } else
            h->events[h->head] = e;
    }
    pthread_mutex_unlock(&h->lock);
    return n;
}
/* Handles a control SETUP packet: standard requests (address, configuration, descriptors) and the HID class
 * requests (reports, idle, protocol). */
static int setup(PtHid *h, const uint8_t *s) {
    memcpy(h->setup, s, 8);
    h->length = h->position = 0;
    unsigned type = s[0], req = s[1], value = pt_read_le(s + 2, 2), index = pt_read_le(s + 4, 2),
             length = pt_read_le(s + 6, 2);
    uint8_t *b = h->control;
    memset(b, 0, sizeof h->control);
    if (req == 6 && (type == 0x80 || type == 0x81)) {
        int got = nd_hid_descriptor(h, type, value, index, b);
        if (got < 0)
            return -1;
        h->length = (unsigned)got;
    } else if (type == 0 && req == 5 && value < 128 && !index && !length) { /* commit at status */
    } else if (type == 0 && req == 9 && value <= 1 && !index && !length) {
    } else if (type == 0x80 && req == 8 && !value && !index) {
        b[0] = h->configuration;
        h->length = 1;
    } else if ((type == 0x80 || type == 0x81 || type == 0x82) && req == 0 && !value) {
        if (type == 0x82 && index != 0x81 && index != 0)
            return -1;
        b[0] = type == 0x82 && index == 0x81 ? h->halt : 0;
        h->length = 2;
    } else if (type == 2 && (req == 1 || req == 3) && !value && index == 0x81 && !length) {
    } else if (type == 0x81 && req == 10 && !index && !value)
        h->length = 1;
    else if (type == 1 && req == 11 && !index && !value && !length) {
    } else if (type == 0xa1 && req == 1 && index == 0 && value == 0x100)
        h->length = report(h, b, 0, 1);
    else if (type == 0xa1 && req == 2 && !index) {
        b[0] = h->idle;
        h->length = 1;
    } else if (type == 0xa1 && req == 3 && !index) {
        b[0] = h->protocol;
        h->length = 1;
    } else if (type == 0x21 && req == 9 && !index && value == 0x200 && length == 1 && h->kind == PT_HID_KEYBOARD) {
    } else if (type == 0x21 && req == 10 && !index && !(value & 255) && !length) {
    } else if (type == 0x21 && req == 11 && !index && value <= 1 && !length && h->kind != PT_HID_TABLET) {
    } else
        return -1;
    if (h->length > length)
        h->length = length;
    h->phase = type & 128 ? 1 : length ? 2 : 3; /* IN data, OUT data, IN status */
    return 0;
}
/* The status stage of a control transfer: what SET_ADDRESS, SET_CONFIGURATION, the halt feature, SET_IDLE and
 * SET_PROTOCOL asked for takes effect now. */
static void commit(PtHid *h) {
    unsigned req = h->setup[1], value = pt_read_le(h->setup + 2, 2), type = h->setup[0];
    if (type == 0 && req == 5)
        h->address = value;
    if (type == 0 && req == 9) {
        h->configuration = value;
        h->halt = 0;
    }
    if (type == 2 && (req == 1 || req == 3))
        h->halt = req == 3;
    if (type == 0x21 && req == 10)
        h->idle = value >> 8;
    if (type == 0x21 && req == 11)
        h->protocol = value;
    h->phase = 0;
}
/* One USB packet for the device from OHCI: SETUP and control data on endpoint 0, interrupt-IN reports on endpoint
 * 1. */
int pt_hid_packet(PtHid *h, unsigned ep, unsigned pid, uint8_t *b, unsigned n, uint16_t frame) {
    if (ep == 1 && pid == PT_USB_IN && h->configuration) {
        if (h->halt)
            return PT_USB_STALL;
        unsigned required = h->kind == PT_HID_KEYBOARD ? 8 : h->kind == PT_HID_TABLET ? 6 : h->protocol ? 4 : 3;
        if (n < required)
            return PT_USB_STALL; /* do not consume a truncated key-up */
        unsigned got = report(h, b, 1, h->idle && (uint16_t)(frame - h->last_frame) >= h->idle * 4);
        if (!got)
            return PT_USB_NAK;
        h->last_frame = frame;
        return got;
    }
    if (ep)
        return PT_USB_STALL;
    if (pid == PT_USB_SETUP) {
        h->phase = 0;
        return n == 8 && !setup(h, b) ? 8 : PT_USB_STALL;
    }
    if (h->phase == 1 && pid == PT_USB_IN) {
        /* bMaxPacketSize0 is 8: a host asking for more in one packet gets 8, a short packet, as from a real device.
         * Linux's first read at the default address asks for 64 bytes of the device descriptor; a stall there left
         * every Ptolemy keyboard and pointer unenumerated ("device descriptor read/64, error -32"). */
        unsigned left = h->length - h->position, got = left < n ? left : n;
        if (got > 8)
            got = 8;
        memcpy(b, h->control + h->position, got);
        h->position += got;
        /* Host determines end by wLength/short packet; accept its OUT status. */
        return got;
    }
    if (h->phase == 1 && pid == PT_USB_OUT && !n) {
        commit(h);
        return 0;
    }
    if (h->phase == 2 && pid == PT_USB_OUT && n == 1) {
        h->control[0] = b[0];
        h->phase = 3;
        return 1;
    }
    if (h->phase == 3 && pid == PT_USB_IN && !n) {
        commit(h);
        return 0;
    }
    return PT_USB_STALL;
}
