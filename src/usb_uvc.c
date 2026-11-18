/* SPDX-License-Identifier: GPL-2.0-or-later */
/* The USB camera (UVC 1.0) on the root hub's fifth port: its descriptors, the probe/commit handshake and the bulk
 * video stream. Pictures come from a PtUvcSource (the Mac's camera, camera_macos.m) or, without one, a test pattern. */
#include "usb_uvc.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
enum {
    ND_UVC_CONTROL = 0, /* interface numbers */
    ND_UVC_STREAM = 1,
    ND_UVC_IN = 0x81,        /* the stream's bulk endpoint */
    ND_UVC_INTERVAL = 333333, /* 100 ns units: 30 frames a second, the one rate */
    ND_UVC_IDLE_TICKS = 500,  /* USB frames without a read of the stream: the guest closed it */
    /* Packets of the stream in one USB frame. The bus model has no 12 Mbit/s limit; this leaves three quarters of a
     * frame's packets to the other bulk devices (640x480 at 30 frames a second needs 288 a frame on average). */
    ND_UVC_FRAME_PACKETS = 512
};
static const uint16_t nd_uvc_size[2][2] = {{640, 480}, {320, 240}}; /* bFrameIndex 1 and 2 */
/* 1209:0002: pid.codes' test ID, kept for this virtual device during the betas. The device class says "interface
 * association" (0xEF/2/1): Mac OS X 10.5 looks for cameras among such devices. */
static const uint8_t nd_uvc_device[] = {18, 1, 0, 2, 0xef, 2, 1, 64, 9, 0x12, 2, 0, 0, 1, 1, 2, 0, 1};
#define ND_LE16(v) (uint8_t)((v) & 255), (uint8_t)((v) >> 8)
#define ND_LE32(v) (uint8_t)((v) & 255), (uint8_t)((v) >> 8 & 255), (uint8_t)((v) >> 16 & 255), (uint8_t)((v) >> 24)
#define ND_FRAME(index, w, h)                                                                                          \
    30, 0x24, 5, index, 0, ND_LE16(w), ND_LE16(h), ND_LE32((w) * (h) * 16 * 30), ND_LE32((w) * (h) * 16 * 30),         \
            ND_LE32((w) * (h) * 2), ND_LE32(ND_UVC_INTERVAL), 1, ND_LE32(ND_UVC_INTERVAL)
static const uint8_t nd_uvc_config[] = {
        9, 2, ND_LE16(199), 2, 1, 0, 0x80, 50,
        8, 11, 0, 2, 14, 3, 0, 2,                    /* interface association: video collection, interfaces 0 and 1 */
        9, 4, ND_UVC_CONTROL, 0, 0, 14, 1, 0, 2,     /* VideoControl, no endpoint of its own */
        13, 0x24, 1, ND_LE16(0x100), ND_LE16(50), ND_LE32(48000000), 1, ND_UVC_STREAM,
        17, 0x24, 2, 1, ND_LE16(0x201), 0, 0, 0, 0, 0, 0, 0, 0, 2, 0, 0, /* camera terminal 1, no controls */
        11, 0x24, 5, 2, 1, 0, 0, 2, 0, 0, 0,                             /* processing unit 2, no controls */
        9, 0x24, 3, 3, ND_LE16(0x101), 0, 2, 0,                          /* output terminal 3: the stream */
        9, 4, ND_UVC_STREAM, 0, 1, 14, 2, 0, 0,                          /* VideoStreaming, the bulk endpoint */
        14, 0x24, 1, 1, ND_LE16(107), ND_UVC_IN, 0, 3, 0, 0, 0, 1, 0,
        27, 0x24, 4, 1, 2, 'Y', 'U', 'Y', '2', 0, 0, 0x10, 0, 0x80, 0, 0, 0xaa, 0, 0x38, 0x9b, 0x71, 16, 1, 0, 0, 0, 0,
        ND_FRAME(1, 640, 480),
        ND_FRAME(2, 320, 240),
        6, 0x24, 13, 1, 1, 4,
        7, 5, ND_UVC_IN, 2, ND_LE16(64), 0};
_Static_assert(sizeof nd_uvc_config == 199, "wTotalLength");
static void nd_uvc_stream(PtUsbUvc *u, int on) {
    unsigned w = nd_uvc_size[u->commit[3] - 1][0], h = nd_uvc_size[u->commit[3] - 1][1];
    if (u->open == on)
        return;
    u->open = on;
    if (on) {
        u->opened_at = u->ticks;
        u->frames = 0;
        fprintf(stderr, "ptolemy: camera: stream opened, %ux%u\n", w, h);
    } else
        fprintf(stderr, "ptolemy: camera: stream closed, %llu frames of %ux%u in %.1f s\n",
                (unsigned long long)u->frames, w, h, (double)(u->ticks - u->opened_at) / 1000);
    if (u->source.stream)
        u->source.stream(u->source.opaque, on, w, h);
}
/* The negotiated stream parameters (the 26-byte structure of UVC 1.0, 4.3.1.1) for a requested frame. */
static void nd_uvc_negotiate(uint8_t *p, unsigned frame) {
    if (frame != 2)
        frame = 1;
    uint32_t bytes = (uint32_t)nd_uvc_size[frame - 1][0] * nd_uvc_size[frame - 1][1] * 2;
    memset(p, 0, 26);
    p[2] = 1;
    p[3] = (uint8_t)frame;
    pt_write_le(p + 4, 4, ND_UVC_INTERVAL);
    pt_write_le(p + 18, 4, bytes);
    pt_write_le(p + 22, 4, bytes + 2); /* a payload: the two-byte header and the whole frame */
}
static void nd_uvc_bus_reset(void *opaque) {
    PtUsbUvc *u = opaque;
    if (u->committed)
        nd_uvc_stream(u, 0);
    u->address = u->configuration = u->halt = u->error = 0;
    u->control_phase = 0;
    u->committed = 0;
    u->length = u->position = 0;
    nd_uvc_negotiate(u->probe, 1);
    memcpy(u->commit, u->probe, 26);
}
void pt_usb_uvc_init(PtUsbUvc *u) {
    memset(u, 0, sizeof *u);
    nd_uvc_bus_reset(u);
}
void pt_usb_uvc_close(PtUsbUvc *u) {
    if (u->committed)
        nd_uvc_stream(u, 0);
    free(u->payload);
    u->payload = NULL;
    u->committed = 0;
}
static unsigned nd_uvc_address(void *opaque) {
    return ((PtUsbUvc *)opaque)->address;
}
/* SETUP requests: the standard ones, and the class requests of the VideoStreaming interface (probe and commit). The
 * VideoControl interface has no controls: its requests stall, and its error code control says why. */
static int nd_uvc_setup(PtUsbUvc *u, const uint8_t *p, unsigned n) {
    if (n != 8)
        return PT_USB_STALL;
    memcpy(u->setup, p, 8);
    u->control_position = u->control_length = 0;
    u->control_phase = 0; /* a refused request leaves no stage of the one before it */
    memset(u->control, 0, sizeof u->control);
    unsigned type = p[0], req = p[1], value = (unsigned)pt_read_le(p + 2, 2), index = (unsigned)pt_read_le(p + 4, 2),
             limit = (unsigned)pt_read_le(p + 6, 2), selector = value >> 8;
    if (type == 0x80 && req == 6) {
        if (value == 0x100) {
            memcpy(u->control, nd_uvc_device, sizeof nd_uvc_device);
            u->control_length = sizeof nd_uvc_device;
        } else if (value == 0x200) {
            memcpy(u->control, nd_uvc_config, sizeof nd_uvc_config);
            u->control_length = sizeof nd_uvc_config;
        } else if (value == 0x300) {
            static const uint8_t languages[] = {4, 3, 9, 4};
            memcpy(u->control, languages, 4);
            u->control_length = 4;
        } else if (value == 0x301 || value == 0x302) {
            const char *text = value == 0x301 ? "Ptolemy" : "Ptolemy Camera";
            unsigned len = (unsigned)strlen(text);
            u->control[0] = (uint8_t)(2 + len * 2);
            u->control[1] = 3;
            for (unsigned k = 0; k < len; k++)
                u->control[2 + 2 * k] = (uint8_t)text[k];
            u->control_length = 2 + len * 2;
        } else
            return PT_USB_STALL; /* also the device qualifier: a full-speed-only device */
    } else if ((type & 0x7f) == 0x21 && (index & 255) == ND_UVC_STREAM && !(index >> 8) && !(value & 255) &&
               (selector == 1 || selector == 2)) {
        uint8_t *held = selector == 1 ? u->probe : u->commit;
        if (type == 0x21 && req == 1 && limit && limit <= 26) { /* SET_CUR: the data stage follows */
        } else if (type == 0xa1 && req >= 0x81 && req <= 0x87 && req != 0x84 && req != 0x85 && req != 0x86) {
            memcpy(u->control, held, 26); /* GET_CUR; the limits and the default are the same: one rate */
            u->control_length = 26;
        } else if (type == 0xa1 && req == 0x85) { /* GET_LEN */
            u->control[0] = 26;
            u->control_length = 2;
        } else if (type == 0xa1 && req == 0x86) { /* GET_INFO: GET and SET */
            u->control[0] = 3;
            u->control_length = 1;
        } else
            return PT_USB_STALL;
    } else if (type == 0xa1 && req == 0x81 && index == ND_UVC_CONTROL && value == 0x200) {
        u->control[0] = u->error; /* VC_REQUEST_ERROR_CODE_CONTROL */
        u->control_length = 1;
    } else if ((type & 0x7f) == 0x21) {
        u->error = 6; /* invalid control: no unit or terminal here has any */
        return PT_USB_STALL;
    } else if (type == 0 && req == 5 && value < 128 && !index && !limit) {
    } else if (type == 0 && req == 9 && value <= 1 && !index && !limit) {
    } else if (type == 0x80 && req == 8 && !value && !index && limit == 1) {
        u->control[0] = u->configuration;
        u->control_length = 1;
    } else if (type == 0x81 && req == 10 && !value && index <= ND_UVC_STREAM && limit == 1) {
        u->control_length = 1; /* alternate setting 0, the only one */
    } else if (type == 1 && req == 11 && !value && index <= ND_UVC_STREAM && !limit) {
    } else if ((type == 0x80 || type == 0x81 || type == 0x82) && req == 0 && !value && limit == 2) {
        if (type == 0x82) {
            if (index != ND_UVC_IN && index)
                return PT_USB_STALL;
            u->control[0] = index == ND_UVC_IN && u->halt;
        } else if (type == 0x80 ? index : index > ND_UVC_STREAM)
            return PT_USB_STALL;
        u->control_length = 2;
    } else if (type == 2 && (req == 1 || req == 3) && !value && !limit && index == ND_UVC_IN) {
        u->halt = req == 3;
        u->length = u->position = 0; /* the next transfer starts a payload */
    } else
        return PT_USB_STALL;
    if (u->control_length > limit)
        u->control_length = limit;
    u->control_phase = type & 128 ? 1 : limit ? 4 : 3;
    return 8;
}
/* The data of SET_CUR on the probe or commit control: any length up to the structure's (Mac OS X 10.5 commits a bulk
 * stream with four bytes: hint, format and frame). A commit fixes the stream's frame size. */
static void nd_uvc_set(PtUsbUvc *u) {
    int commit = u->setup[3] == 2;
    uint8_t *held = commit ? u->commit : u->probe;
    if (commit && u->committed)
        nd_uvc_stream(u, 0);
    nd_uvc_negotiate(held, u->control_length > 3 ? u->control[3] : u->probe[3]);
    if (!commit)
        return;
    uint8_t *payload = realloc(u->payload, pt_read_le(held + 22, 4));
    if (!payload)
        return; /* no memory: the stream stays uncommitted and its endpoint answers NAK */
    u->payload = payload;
    u->committed = 1;
    u->length = u->position = 0;
    u->next_at = 0;
}
/* Control endpoint packets: SETUP, then its data and status stages. */
static int nd_uvc_control(PtUsbUvc *u, unsigned pid, uint8_t *p, unsigned n) {
    if (pid == PT_USB_SETUP)
        return nd_uvc_setup(u, p, n);
    if (u->control_phase == 1 && pid == PT_USB_IN) {
        unsigned take = u->control_length - u->control_position;
        if (take > n)
            take = n;
        if (take)
            memcpy(p, u->control + u->control_position, take);
        u->control_position += take;
        if (take < n || u->control_position == u->control_length)
            u->control_phase = 2;
        return (int)take;
    }
    if (u->control_phase == 2 && pid == PT_USB_OUT && !n) {
        u->control_phase = 0;
        return 0;
    }
    if (u->control_phase == 4 && pid == PT_USB_OUT) {
        unsigned limit = (unsigned)pt_read_le(u->setup + 6, 2);
        if (n > limit - u->control_length)
            return PT_USB_STALL;
        memcpy(u->control + u->control_length, p, n);
        u->control_length += n;
        if (u->control_length == limit || n < 64)
            u->control_phase = 3;
        return (int)n;
    }
    if (u->control_phase == 3 && pid == PT_USB_IN && !n) {
        if (u->setup[0] == 0 && u->setup[1] == 5)
            u->address = u->setup[2];
        if (u->setup[0] == 0 && u->setup[1] == 9)
            u->configuration = u->setup[2];
        if (u->setup[0] == 0x21)
            nd_uvc_set(u);
        u->control_phase = 0;
        return 0;
    }
    return PT_USB_STALL;
}
/* The picture without a source: diagonal bands of brightness and colour that move by a step a frame. */
static void nd_uvc_pattern(PtUsbUvc *u, unsigned w, unsigned h, uint8_t *yuy2) {
    unsigned t = u->pattern++ * 4;
    for (unsigned y = 0; y < h; y++)
        for (unsigned x = 0; x < w; x += 2, yuy2 += 4) {
            yuy2[0] = (uint8_t)(16 + (x + y + t) % 220);
            yuy2[1] = (uint8_t)(64 + (x * 128 / w + t) % 128);
            yuy2[2] = (uint8_t)(16 + (x + 1 + y + t) % 220);
            yuy2[3] = (uint8_t)(64 + (y * 128 / h + t) % 128);
        }
}
/* One USB packet. The stream endpoint sends payloads of one frame each: the header (its length, then end of header,
 * end of frame and the frame's alternating identifier; UVC 1.0, 2.4.3.3), then width * height * 2 bytes of YUY2. A new
 * frame starts no sooner than a thirtieth of a second after the last one (on average exactly: the remainder of a USB
 * frame is carried), and only when the source has one; a USB frame carries at most ND_UVC_FRAME_PACKETS packets. */
static int nd_uvc_packet(void *opaque, unsigned ep, unsigned pid, uint8_t *p, unsigned n, uint16_t frame) {
    (void)frame;
    PtUsbUvc *u = opaque;
    if ((!p && n) || n > 65536)
        return PT_USB_STALL;
    if (!ep)
        return nd_uvc_control(u, pid, p, n);
    if (ep != 1 || pid != PT_USB_IN || u->halt)
        return PT_USB_STALL;
    if (!u->configuration || !u->committed)
        return PT_USB_NAK;
    u->last_in = u->ticks;
    nd_uvc_stream(u, 1);
    if (u->packets_at != u->ticks) {
        u->packets_at = u->ticks;
        u->packets = 0;
    }
    if (u->packets == ND_UVC_FRAME_PACKETS)
        return PT_USB_NAK;
    if (u->position == u->length) {
        unsigned w = nd_uvc_size[u->commit[3] - 1][0], h = nd_uvc_size[u->commit[3] - 1][1];
        uint64_t now = u->ticks * 10000; /* 100 ns units */
        if (now < u->next_at)
            return PT_USB_NAK;
        if (!u->source.frame)
            nd_uvc_pattern(u, w, h, u->payload + 2);
        else if (!u->source.frame(u->source.opaque, w, h, u->payload + 2))
            return PT_USB_NAK;
        u->payload[0] = 2;
        u->payload[1] = (uint8_t)(0x82 | u->fid);
        u->fid ^= 1;
        u->length = 2 + w * h * 2;
        u->position = 0;
        u->next_at = (now - u->next_at > ND_UVC_INTERVAL ? now : u->next_at) + ND_UVC_INTERVAL;
        u->frames++;
    }
    u->packets++;
    unsigned take = u->length - u->position;
    if (take > n)
        take = n;
    memcpy(p, u->payload + u->position, take);
    u->position += take;
    return (int)take;
}
PtUsbDevice pt_usb_uvc_device(PtUsbUvc *u) {
    return (PtUsbDevice){u, nd_uvc_bus_reset, nd_uvc_address, nd_uvc_packet};
}
void pt_usb_uvc_tick(PtUsbUvc *u) {
    u->ticks++;
    if (u->open && u->ticks - u->last_in >= ND_UVC_IDLE_TICKS) {
        nd_uvc_stream(u, 0);
        u->length = u->position = 0;
    }
}
