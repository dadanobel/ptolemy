/* SPDX-License-Identifier: GPL-2.0-or-later
 * Ptolemy's display driver for Tiger, a PEF "NDRV" that the engine places in the boot display's
 * Open Firmware node ("driver,AAPL,MacOS,PowerPC"; mkpef.py builds the container). It answers like the boot driver
 * Tiger uses without one (one mode with the firmware's geometry) and adds a 64x64 ARGB hardware cursor that it hands
 * to the host by hypercall, so WindowServer no longer draws the pointer into VRAM (each draw read the scanout back).
 * Several modes and depths. Mode 100 is the firmware's geometry; 101-103 are 640x480, 800x600 and
 * 1024x768 when smaller than it; each in 8 bits indexed (depth mode 128), 16 bits 1555 (129) and 32 bits (130). A switch and
 * the colour table go to the engine by hypercall (ops 3 and 4), which programs the CRTC, the aperture swap and the DAC; the
 * switch returns the framebuffer's address (8 and 16 bits lie in aperture 1: Tiger's driver swaps aperture 0 for 32 bits).
 * Kernel code: clang, powerpc SVR4, no globals or literals. r2 (the PEF TOC) is the data section, given to C as d. */
typedef unsigned int u32;
typedef unsigned short u16;
typedef unsigned char u8;

enum { UNSUPPORTED = (int)0xe00002c7, BAD_ARGUMENT = (int)0xe00002c2, BOOT_MODE = 100, LAST_MODE = 103, DEPTH8 = 128,
       DEPTH32 = 130 };

/* The data section. mkpef.py writes the initialized part (up to entry) and the loader relocations: tv gets the code
 * and data addresses, prepare and get the imports. The rest is zero filled. */
struct Data {
    u32 tv[2];          /*   0 DoDriverIO TVector (export) */
    u8 desc[128];       /*   8 TheDriverDescription (export) */
    u32 prepare, get;   /* 136 imports: VSLPrepareCursorForHardwareCursor, RegistryPropertyGet */
    char name[5][12];   /* 144 the display node's "address", "linebytes", "width", "height", "depth" */
    u32 hw[25];         /* 204 HardwareCursorDescriptor: 64x64, 32 bits, inverting pixel 0x00ffffff */
    u32 entry[4];       /* 304 RegEntryID of the display node */
    u32 geo[5];         /* 320 the five properties */
    u32 info[11];       /* 340 HardwareCursorInfo */
    u32 image[64 * 64]; /* 384 cursor pixels, filled by IOFramebuffer */
    u32 mode, depth;    /* 16768 current mode and depth mode; 0 until the first switch: BOOT_MODE, DEPTH32 */
    u32 pal[256];       /* 16776 colour table, 0x00RRGGBB */
    u32 base;           /* 17800 the framebuffer's address from the engine; 0 until the first switch: geo[0] */
    u32 gamma[256];     /* 17804 output per input level, 0x00RRGGBB (all zero until the first cscSetGamma: linear) */
    u8 gtab[12 + 768];  /* 18828 the same as a GammaTbl (3 channels, 256 entries, 8 bits) for cscGetGamma */
};
_Static_assert(__builtin_offsetof(struct Data, prepare) == 136 && __builtin_offsetof(struct Data, name) == 144 &&
               __builtin_offsetof(struct Data, hw) == 204 && __builtin_offsetof(struct Data, entry) == 304 &&
               __builtin_offsetof(struct Data, image) == 384 && __builtin_offsetof(struct Data, gamma) == 17804 &&
               sizeof(struct Data) == 17804 + 1024 + 780, "mkpef.py layout");

/* Entry (code offset 0): DoDriverIO(space, id, contents, code, kind) with the data section as sixth argument.
 * call_tv calls a Mac OS TVector: 64-byte frame (24 linkage + 32 parameter area), r2 saved above both. */
u32 call_tv(u32 tv, u32 a, u32 b, u32 c, u32 e);
__asm__(".section .text.entry,\"ax\",@progbits\n"
        "  mr 8, 2\n  b ndrv_io\n"
        ".text\n"
        "call_tv:\n"
        "  stwu 1, -64(1)\n  mflr 0\n  stw 0, 68(1)\n  stw 2, 56(1)\n"
        "  lwz 0, 0(3)\n  lwz 2, 4(3)\n  mtctr 0\n"
        "  mr 3, 4\n  mr 4, 5\n  mr 5, 6\n  mr 6, 7\n  bctrl\n"
        "  lwz 2, 56(1)\n  lwz 0, 68(1)\n  mtlr 0\n  addi 1, 1, 64\n  blr\n");

/* Engine hypercall (firmware.c cursor_call): r0 'PTCU', r3 op, r4-r6 arguments. */
static u32 host(u32 op, u32 a, u32 b, u32 c)
{
    register u32 r0 __asm__("r0") = 0x50544355, r3 __asm__("r3") = op, r4 __asm__("r4") = a, r5 __asm__("r5") = b,
                 r6 __asm__("r6") = c;
    __asm__ volatile(".long 0x00005043" : "+r"(r3) : "r"(r0), "r"(r4), "r"(r5), "r"(r6) : "memory");
    return r3;
}

/* Mac OS records are 2-byte packed (mac68k): byte access. */
static u32 get(const u8 *p) { return (u32)p[0] << 24 | (u32)p[1] << 16 | (u32)p[2] << 8 | p[3]; }
static void put(u8 *p, u32 v) { p[0] = v >> 24; p[1] = v >> 16; p[2] = v >> 8; p[3] = v; }
static void put16(u8 *p, u32 v) { p[0] = v >> 8; p[1] = v; }
static u32 get16(const u8 *p) { return (u32)p[0] << 8 | p[1]; }

static int init(struct Data *d, const u8 *info)
{
    for (int i = 0; i < 4; i++) d->entry[i] = get(info + 2 + 4 * i);   /* DriverInitInfo: refNum, RegEntryID */
    for (int i = 0; i < 5; i++) {
        u32 size = 4;
        int err = call_tv(d->get, (u32)d->entry, (u32)d->name[i], (u32)&d->geo[i], (u32)&size);
        if (err) return err;
    }
    return 0;
}

/* Mode geometry; 0 when the mode does not exist. */
static int mode_size(const struct Data *d, u32 mode, u32 *w, u32 *h)
{
    switch (mode) {
    case BOOT_MODE: *w = d->geo[2]; *h = d->geo[3]; return 1;
    case 101: *w = 640; *h = 480; break;
    case 102: *w = 800; *h = 600; break;
    case 103: *w = 1024; *h = 768; break;
    default: return 0;
    }
    return *w <= d->geo[2] && *h <= d->geo[3] && (*w < d->geo[2] || *h < d->geo[3]);
}
static u32 bits(u32 depth_mode) { return depth_mode == DEPTH8 ? 8 : depth_mode == DEPTH8 + 1 ? 16 : depth_mode == DEPTH32 ? 32 : 0; }
/* The firmware's pitch for its own mode, else rows of whole 64-pixel units (the engine programs the same). */
static u32 row_bytes(const struct Data *d, u32 mode, u32 w, u32 b)
{
    return mode == BOOT_MODE && b == 32 ? d->geo[1] : (w + 63) / 64 * 64 * (b / 8);
}
static u32 cur_mode(const struct Data *d) { return d->mode ? d->mode : BOOT_MODE; }
static u32 cur_depth(const struct Data *d) { return d->depth ? d->depth : DEPTH32; }
static u32 cur_base(const struct Data *d) { return d->base ? d->base : d->geo[0]; }

/* cscSetGamma: a GammaTbl (version, type, formula size, channels 1 or 3, entries, bits per entry, then the formula and
 * channel after channel of entries of whole bytes; nil = linear) becomes 256 output levels per channel for the engine,
 * which applies them to the displayed image (the hardware's output gamma ramp). */
static int set_gamma(struct Data *d, const u8 *g)
{
    u32 ch = 3, cnt = 256, width = 8, bytes = 1, i, c, k, v, out;
    const u8 *t = 0;
    if (g) {
        ch = get16(g + 6); cnt = get16(g + 8); width = get16(g + 10); bytes = (width + 7) / 8;
        if ((ch != 1 && ch != 3) || !cnt || width < 8 || bytes > 2) return BAD_ARGUMENT;
        t = g + 12 + get16(g + 4);
    }
    for (i = 0; i < 256; i++) {
        out = 0;
        for (c = 0; c < 3; c++) {
            k = i * cnt / 256;
            if (!t) v = i;
            else {
                const u8 *e = t + ((ch == 3 ? c : 0) * cnt + k) * bytes;
                v = bytes == 1 ? e[0] : ((u32)e[0] << 8 | e[1]) >> (width - 8);
            }
            out = out << 8 | (v & 255);
            d->gtab[12 + c * 256 + i] = v;
        }
        d->gamma[i] = out;
    }
    put16(d->gtab, 0); put16(d->gtab + 2, 0); put16(d->gtab + 4, 0); put16(d->gtab + 6, 3); put16(d->gtab + 8, 256);
    put16(d->gtab + 10, 8);
    return host(5, (u32)d->gamma, 0, 256) ? BAD_ARGUMENT : 0;
}

static int set_mode(struct Data *d, u32 mode, u32 depth_mode)
{
    u32 w, h, b = bits(depth_mode);
    if (!b || !mode_size(d, mode, &w, &h)) return BAD_ARGUMENT;
    u32 base = host(3, w, h, b);   /* the engine picks the aperture: 8 and 16 bits are not in the 32-bit swap one */
    if (!base) return BAD_ARGUMENT;
    d->mode = mode; d->depth = depth_mode; d->base = base;
    return 0;
}

static int control(struct Data *d, u32 code, u8 *p)
{
    u32 n, start, i, k, e;
    const u8 *t;
    switch (code) {
    case 2:                                         /* cscSetMode: depth for the current mode */
        e = set_mode(d, cur_mode(d), get16(p));
        if (!e) put(p + 8, cur_base(d));
        return e;
    case 10:                                        /* cscSwitchMode: VDSwitchInfoRec */
        e = set_mode(d, get(p + 2), get16(p));
        if (!e) put(p + 8, cur_base(d));
        return e;
    case 3:                                         /* cscSetEntries: ColorSpecs, start -1 = by their value */
        t = (const u8 *)get(p); start = get16(p + 4); n = get16(p + 6) + 1;
        for (i = 0; i < n && i < 256; i++, t += 8) {
            k = start == 0xffff ? t[1] : start + i;
            if (k < 256) d->pal[k] = (u32)t[2] << 16 | (u32)t[4] << 8 | t[6];
        }
        return host(4, (u32)d->pal, 0, 256) ? BAD_ARGUMENT : 0;
    case 4: return set_gamma(d, (const u8 *)get(p)); /* cscSetGamma: VDGammaRecord */
    case 22:                                        /* cscSetHardwareCursor: IOFramebuffer converts the image */
        d->info[0] = 0x00010000; d->info[3] = 0; d->info[4] = (u32)d->image;
        if (!(u8)call_tv(d->prepare, get(p), (u32)d->hw, (u32)d->info, 0)) { host(1, 0, 0, 0); return UNSUPPORTED; }
        host(1, (u32)d->image, d->info[2], d->info[1]);
        return 0;
    case 23:                                        /* cscDrawHardwareCursor: top-left x, y, visible */
        host(2, get(p), get(p + 4), get(p + 8));
        return 0;
    }
    return UNSUPPORTED;
}

static int status(struct Data *d, u32 code, u8 *p)
{
    u32 prev, mode, w, h, b;
    u8 *vp;
    switch (code) {
    case 10:                                        /* cscGetCurMode */
        put16(p, cur_depth(d)); put(p + 2, cur_mode(d)); put16(p + 6, 1); put(p + 8, cur_base(d));
        return 0;
    case 13:                                        /* cscGetModeTiming: valid, safe; the firmware's mode is the default */
        mode = get(p);
        if (!mode_size(d, mode, &w, &h)) return BAD_ARGUMENT;
        put(p + 8, 0x6465636c); put(p + 12, 0); put(p + 16, mode == BOOT_MODE ? 7 : 3);
        return 0;
    case 17:                                        /* cscGetNextResolution */
        prev = get(p);
        if (prev == 0xfffffffe || prev == 0) mode = BOOT_MODE;
        else if (!mode_size(d, prev, &w, &h)) return BAD_ARGUMENT;
        else mode = prev + 1;
        while (mode <= LAST_MODE && !mode_size(d, mode, &w, &h)) mode++;
        if (mode > LAST_MODE) { put(p + 4, 0xfffffffd); return 0; }
        put(p + 4, mode); put(p + 8, w); put(p + 12, h); put(p + 16, 0); put16(p + 20, DEPTH32);
        return 0;
    case 18:                                        /* cscGetVideoParameters: VPBlock */
        mode = get(p); b = bits(p[4] << 8 | p[5]);
        if (!b || !mode_size(d, mode, &w, &h)) return BAD_ARGUMENT;
        vp = (u8 *)get(p + 6);
        put(vp, 0); put16(vp + 4, row_bytes(d, mode, w, b)); put16(vp + 6, 0); put16(vp + 8, 0); put16(vp + 10, h);
        put16(vp + 12, w); put16(vp + 14, 0); put16(vp + 16, 0); put(vp + 18, 0); put(vp + 22, 72 << 16);
        put(vp + 26, 72 << 16); put16(vp + 30, b == 8 ? 0 : 16); put16(vp + 32, b); put16(vp + 34, b == 8 ? 1 : 3);
        put16(vp + 36, b == 16 ? 5 : 8); put(vp + 38, 0);
        return 0;
    case 8:                                         /* cscGetGamma: VDGammaRecord */
        if (!d->gtab[7]) set_gamma(d, 0);
        put(p, (u32)d->gtab);
        return 0;
    case 22:                                        /* cscSupportsHardwareCursor */
        put(p, 1);
        return 0;
    }
    return UNSUPPORTED;
}

int ndrv_io(u32 space, u32 id, u8 *contents, u32 code, u32 kind, struct Data *d)
{
    (void)space; (void)id; (void)kind;
    switch (code) {
    case 0: case 1: case 8: case 10: return 0;      /* open, close, finalize, superseded */
    case 7: case 9: return init(d, contents);       /* initialize, replace */
    case 4: return control(d, contents[0x1a] << 8 | contents[0x1b], (u8 *)get(contents + 0x1c));
    case 5: return status(d, contents[0x1a] << 8 | contents[0x1b], (u8 *)get(contents + 0x1c));
    }
    return UNSUPPORTED;
}
