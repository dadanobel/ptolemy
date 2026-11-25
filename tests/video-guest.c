/* SPDX-License-Identifier: GPL-2.0-or-later */
/* The video calls of the agent's hypercall (src/agent_guest.c, src/video_guest.c) on a made-up guest: a CPU with
 * user-mode translation over 32 MiB of RAM whose pages the test unmaps or write-protects, and a decoder the test
 * completes by hand. Checked: no agent is needed; every page is validated before a byte moves; the limits; AGAIN and
 * FAULT with nothing lost; sessions and their idle time; the queue; frames dropped while decoding; every picture
 * released; the stub decoder. */
#include "ppc.h"
#include "pt_agent.h"
#include "pt_video.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { RAM = 32 << 20, BASE = 0x20000000, PAGES = RAM / 4096 };
static uint8_t *ram, access_[PAGES]; /* 0 writable, 1 read-only, 2 unmapped */
static CPU *c;
uint32_t pt_xlate(CPU *cpu, uint32_t ea, int acc, int pr, uint32_t *ra, int *wr) {
    (void)cpu;
    (void)acc;
    assert(pr == 1);
    if (ea < BASE || ea - BASE >= RAM || access_[(ea - BASE) >> 12] == 2)
        return 1;
    *ra = ea - BASE;
    *wr = !access_[(ea - BASE) >> 12];
    return 0;
}
/* the agent's own calls are not this test's */
int pt_agent_register_caps(PtAgent *a, uint32_t v, uint32_t caps, uint32_t *t) { (void)a; (void)v; (void)caps; (void)t; return PTA_DENIED; }
int pt_agent_pull(PtAgent *a, uint32_t t, void *b, uint32_t cap, uint32_t *id, uint32_t *n, uint32_t *ms) { (void)a; (void)t; (void)b; (void)cap; (void)id; (void)n; (void)ms; return PTA_DENIED; }
int pt_agent_output(PtAgent *a, uint32_t t, uint32_t id, const void *b, uint32_t n) { (void)a; (void)t; (void)id; (void)b; (void)n; return PTA_DENIED; }
int pt_agent_finish(PtAgent *a, uint32_t t, uint32_t id, uint32_t s) { (void)a; (void)t; (void)id; (void)s; return PTA_DENIED; }
int pt_agent_check(PtAgent *a, uint32_t t, uint32_t id, uint32_t *x) { (void)a; (void)t; (void)id; (void)x; return PTA_DENIED; }
int pt_agent_input(PtAgent *a, uint32_t t, unsigned s) { (void)a; (void)t; (void)s; return PTA_DENIED; }
int pt_agent_pointer(PtAgent *a, uint32_t t, const uint8_t w[24]) { (void)a; (void)t; (void)w; return PTA_DENIED; }
int pt_agent_unregister(PtAgent *a, uint32_t t) { (void)a; (void)t; return PTA_DENIED; }
int pt_agent_xbegin(PtAgent *a, uint32_t t, uint8_t h[PTA_XHEAD], uint32_t *id) { (void)a; (void)t; (void)h; (void)id; return PTA_DENIED; }
int pt_agent_xchunk(PtAgent *a, uint32_t t, uint32_t id, uint32_t o, uint8_t out[PTA_CHUNK], uint32_t *n) { (void)a; (void)t; (void)id; (void)o; (void)out; (void)n; return PTA_DENIED; }
int pt_agent_xcommit(PtAgent *a, uint32_t t, uint32_t id, const uint8_t s[32]) { (void)a; (void)t; (void)id; (void)s; return PTA_DENIED; }
int pt_agent_xdone(PtAgent *a, uint32_t t, uint32_t id, uint32_t s, const char *p) { (void)a; (void)t; (void)id; (void)s; (void)p; return PTA_DENIED; }
int pt_agent_export_begin(PtAgent *a, uint32_t t, uint32_t j, uint64_t s) { (void)a; (void)t; (void)j; (void)s; return PTA_DENIED; }
int pt_agent_export_chunk(PtAgent *a, uint32_t t, uint32_t j, uint64_t o, const void *b, unsigned n) { (void)a; (void)t; (void)j; (void)o; (void)b; (void)n; return PTA_DENIED; }
int pt_agent_export_commit(PtAgent *a, uint32_t t, uint32_t j, const uint8_t s[32]) { (void)a; (void)t; (void)j; (void)s; return PTA_DENIED; }
int pt_agent_optics_status(PtAgent *a, uint32_t t, void *b) { (void)a; (void)t; (void)b; return PTA_DENIED; }
#ifdef __APPLE__
const PtVideoBackend *pt_video_macos(void) { return NULL; } /* the Mac's decoder has its own test (video-vt.m) */
#endif

/* The decoder: remembers what it was given; the test completes frames with done(). A picture is its frame's number
 * in every byte pair, with a stride wider than the row. */
enum { W = 64, H = 6, STRIDE = 2 * W + 10 };
typedef struct {
    void *owner;
    uint32_t pending[64], n, bytes[64], sum[64];
} Fake;
typedef struct {
    uint8_t rows[H * STRIDE];
} Picture;
static unsigned opened, closed, made, released, refuse_submit;
static Fake *fake[8];
static void *f_open(const uint8_t *avcc, uint32_t n, uint32_t width, uint32_t height, void *owner) {
    if (n < 2 || avcc[0] != 1 || width != W || height != H)
        return NULL;
    Fake *f = calloc(1, sizeof *f);
    f->owner = owner;
    fake[opened++ % 8] = f;
    return f;
}
static int f_submit(void *o, uint32_t frame, const uint8_t *data, uint32_t n) {
    Fake *f = o;
    if (refuse_submit)
        return 1;
    assert(f->n < 64);
    f->bytes[f->n] = n;
    f->sum[f->n] = 0;
    for (uint32_t i = 0; i < n; i++)
        f->sum[f->n] += data[i];
    f->pending[f->n++] = frame;
    return 0;
}
static void done(Fake *f, uint32_t frame, int ok) {
    Picture *p = NULL;
    if (ok) {
        p = malloc(sizeof *p);
        made++;
        memset(p->rows, 0xee, sizeof p->rows);
        for (unsigned y = 0; y < H; y++)
            for (unsigned x = 0; x < 2 * W; x++)
                p->rows[y * STRIDE + x] = (uint8_t)(frame + y + (x & 1));
    }
    pt_video_done(f->owner, frame, p);
}
static const uint8_t *f_pixels(void *picture, uint32_t *stride) {
    *stride = STRIDE;
    return ((Picture *)picture)->rows;
}
static void f_release(void *picture) {
    released++;
    free(picture);
}
static void f_close(void *decoder) {
    closed++;
    free(decoder);
}
static const PtVideoBackend fake_backend = {f_open, f_submit, f_pixels, f_release, f_close};
static uint64_t clock_ms = 1000;
static uint64_t test_clock(void) {
    return clock_ms;
}

static uint32_t r4, r5;
static int call(uint32_t op, uint32_t a4, uint32_t a5, uint32_t a6, uint32_t a7) {
    c->gpr[0] = PTA_MAGIC;
    c->gpr[3] = op;
    c->gpr[4] = a4;
    c->gpr[5] = a5;
    c->gpr[6] = a6;
    c->gpr[7] = a7;
    assert(pt_agent_hypercall(c, PTA_INSN, NULL) == 1);
    r4 = c->gpr[4];
    r5 = c->gpr[5];
    return (int)c->gpr[3];
}
static void protect(uint32_t ea, uint32_t n, uint8_t how) {
    for (uint32_t p = (ea - BASE) >> 12; p <= (ea - BASE + n - 1) >> 12; p++)
        access_[p] = how;
}
static uint32_t open_session(void) {
    assert(call(PTA_VIDEO_OPEN, 0, W << 16 | H, BASE + 0x1000, 8) == PTA_OK && r4);
    return r4;
}
int main(void) {
    ram = calloc(1, RAM);
    c = calloc(1, sizeof *c);
    assert(ram && c);
    c->npreg = 1;
    c->preg[0] = (PRegion){0, RAM, ram, 0};
    c->sys = 2;
    c->msr = MSR_PR | MSR_IR | MSR_DR;
    pt_video_clock(test_clock);
    pt_video_backend(&fake_backend);
    const uint32_t avcc = BASE + 0x1000, data = BASE + 0x100000, out = BASE + 0x1000000;
    ram[0x1000] = 1;

    /* no agent: its own calls are not answered at all, the video calls are; supervisor code is denied */
    c->gpr[0] = PTA_MAGIC;
    c->gpr[3] = PTA_HELLO;
    assert(pt_agent_hypercall(c, PTA_INSN, NULL) == 0);
    assert(call(PTA_VIDEO_PROBE, 0, 0, 0, 0) == PTA_OK && r4 == PTV_VERSION && r5 == 1);
    c->msr &= ~(uint32_t)MSR_PR;
    assert(call(PTA_VIDEO_PROBE, 0, 0, 0, 0) == PTA_DENIED);
    c->msr |= MSR_PR;
    c->gpr[3] = PTA_VIDEO_CLOSE + 1; /* past the video calls: the agent's, unanswered here */
    assert(pt_agent_hypercall(c, PTA_INSN, NULL) == 0);

    /* OPEN: lengths, sizes, pages, a stream the decoder refuses, the table */
    assert(call(PTA_VIDEO_OPEN, 0, W << 16 | H, avcc, 0) == PTA_INVALID);
    assert(call(PTA_VIDEO_OPEN, 0, W << 16 | H, avcc, PTA_MAX_REQUEST + 1) == PTA_INVALID);
    assert(call(PTA_VIDEO_OPEN, 0, 0 << 16 | H, avcc, 8) == PTA_INVALID);
    assert(call(PTA_VIDEO_OPEN, 0, (PTV_MAX_WIDTH + 2) << 16 | H, avcc, 8) == PTA_INVALID);
    assert(call(PTA_VIDEO_OPEN, 0, W << 16 | (PTV_MAX_HEIGHT + 1), avcc, 8) == PTA_INVALID);
    assert(call(PTA_VIDEO_OPEN, 0, (W + 1) << 16 | H, avcc, 8) == PTA_INVALID);
    assert(call(PTA_VIDEO_OPEN, 0, W << 16 | H, BASE + RAM - 4, 8) == PTA_FAULT);
    protect(avcc + 4096, 1, 2);
    assert(call(PTA_VIDEO_OPEN, 0, W << 16 | H, avcc + 4090, 8) == PTA_FAULT && !opened);
    protect(avcc + 4096, 1, 0);
    ram[0x1000] = 0;
    assert(call(PTA_VIDEO_OPEN, 0, W << 16 | H, avcc, 8) == PTA_DENIED);
    ram[0x1000] = 1;
    uint32_t s[PTV_SESSIONS];
    for (unsigned i = 0; i < PTV_SESSIONS; i++) {
        s[i] = open_session();
        for (unsigned k = 0; k < i; k++)
            assert(s[i] != s[k]);
    }
    assert(call(PTA_VIDEO_OPEN, 0, W << 16 | H, avcc, 8) == PTA_BUSY && opened == PTV_SESSIONS);
    Fake *f = fake[0];

    /* DECODE: the session, the length, every page before the decoder sees a byte, a repeated number */
    for (uint32_t i = 0; i < 3 * 4096; i++)
        ram[0x100000 + i] = (uint8_t)(i * 7);
    assert(call(PTA_VIDEO_DECODE, s[0] + 1000, 1, data, 100) == PTA_CANCELLED);
    assert(call(PTA_VIDEO_DECODE, s[0], 1, data, 0) == PTA_INVALID);
    assert(call(PTA_VIDEO_DECODE, s[0], 1, data, PTV_MAX_FRAME + 1) == PTA_INVALID);
    assert(call(PTA_VIDEO_DECODE, s[0], PTV_ALL, data, 100) == PTA_INVALID);
    protect(data + 2 * 4096, 1, 2);
    assert(call(PTA_VIDEO_DECODE, s[0], 1, data + 100, 2 * 4096) == PTA_FAULT && !f->n);
    protect(data + 2 * 4096, 1, 1); /* read-only is enough to read */
    assert(call(PTA_VIDEO_DECODE, s[0], 1, data + 100, 2 * 4096) == PTA_OK && f->n == 1 && f->bytes[0] == 2 * 4096);
    uint32_t sum = 0;
    for (uint32_t i = 0; i < 2 * 4096; i++)
        sum += ram[0x100000 + 100 + i];
    assert(f->sum[0] == sum && f->pending[0] == 1);
    protect(data + 2 * 4096, 1, 0);
    assert(call(PTA_VIDEO_DECODE, s[0], 1, data, 100) == PTA_INVALID && f->n == 1);
    /* a frame of the largest size crosses 2048 pages */
    assert(call(PTA_VIDEO_DECODE, s[0], 2, data, PTV_MAX_FRAME) == PTA_OK && f->bytes[1] == PTV_MAX_FRAME);

    /* FETCH: not ready; the row length; a picture with padding in the guest's rows; a read-only page loses nothing */
    enum { ROW = 2 * W + 24 };
    memset(ram + 0x1000000, 0x55, 2 * 4096);
    uint32_t at = out + 4096 - 3 * ROW - 5; /* rows 0-2 in one page, the rest in the next */
    assert(call(PTA_VIDEO_FETCH, s[0], 1, at, ROW) == PTA_AGAIN);
    assert(call(PTA_VIDEO_FETCH, s[0], 77, at, ROW) == PTA_INVALID);
    done(f, 1, 1);
    assert(call(PTA_VIDEO_FETCH, s[0], 1, at, 2 * W - 2) == PTA_INVALID);
    assert(call(PTA_VIDEO_FETCH, s[0], 1, at, 2 * W + 4096 + 2) == PTA_INVALID);
    protect(out + 4096, 1, 1);
    assert(call(PTA_VIDEO_FETCH, s[0], 1, at, ROW) == PTA_FAULT && !released);
    for (uint32_t i = 0; i < 2 * 4096; i++)
        assert(ram[0x1000000 + i] == 0x55); /* nothing was written before the fault */
    protect(out + 4096, 1, 0);
    c->reserve = 1;
    assert(call(PTA_VIDEO_FETCH, s[0], 1, at, ROW) == PTA_OK && released == 1 && !c->reserve);
    for (unsigned y = 0; y < H; y++)
        for (unsigned x = 0; x < ROW; x++) {
            uint8_t v = ram[at - BASE + y * ROW + x];
            if (y == H - 1 && x >= 2 * W)
                break; /* past the last row's pixels: not the picture's */
            assert(v == (x < 2 * W ? (uint8_t)(1 + y + (x & 1)) : 0x55));
        }
    assert(ram[at - BASE - 1] == 0x55 && ram[at - BASE + (H - 1) * ROW + 2 * W] == 0x55);
    assert(call(PTA_VIDEO_FETCH, s[0], 1, at, ROW) == PTA_INVALID); /* fetched: gone */
    /* a frame the decoder failed: DENIED once, then its number is free again */
    done(f, 2, 0);
    assert(call(PTA_VIDEO_FETCH, s[0], 2, at, ROW) == PTA_DENIED);
    assert(call(PTA_VIDEO_FETCH, s[0], 2, at, ROW) == PTA_INVALID);
    /* a frame the decoder refuses at once */
    refuse_submit = 1;
    assert(call(PTA_VIDEO_DECODE, s[0], 3, data, 100) == PTA_DENIED);
    refuse_submit = 0;
    assert(call(PTA_VIDEO_FETCH, s[0], 3, at, ROW) == PTA_INVALID);

    /* the queue: PTV_QUEUE frames, then AGAIN until one is fetched or dropped; pictures fetched out of order */
    f->n = 0;
    for (uint32_t i = 0; i < PTV_QUEUE; i++)
        assert(call(PTA_VIDEO_DECODE, s[0], 100 + i, data, 64) == PTA_OK);
    assert(call(PTA_VIDEO_DECODE, s[0], 200, data, 64) == PTA_AGAIN && f->n == PTV_QUEUE);
    done(f, 105, 1);
    done(f, 101, 1);
    assert(call(PTA_VIDEO_FETCH, s[0], 100, at, ROW) == PTA_AGAIN);
    assert(call(PTA_VIDEO_FETCH, s[0], 105, at, ROW) == PTA_OK && ram[at - BASE] == 105);
    assert(call(PTA_VIDEO_DECODE, s[0], 200, data, 64) == PTA_OK);
    assert(call(PTA_VIDEO_DECODE, s[0], 201, data, 64) == PTA_AGAIN);
    /* DROP: a ready picture is released at once; a frame still decoding keeps its place until the decoder is done,
     * then its picture is released unseen; another session's frames are not touched */
    unsigned before = released;
    assert(call(PTA_VIDEO_DROP, s[0], 101, 0, 0) == PTA_OK && released == before + 1);
    assert(call(PTA_VIDEO_DECODE, s[0], 201, data, 64) == PTA_OK);
    assert(call(PTA_VIDEO_DECODE, s[1], 100, data, 64) == PTA_OK);
    assert(call(PTA_VIDEO_DROP, s[0], PTV_ALL, 0, 0) == PTA_OK);
    assert(call(PTA_VIDEO_DECODE, s[0], 300, data, 64) == PTA_AGAIN); /* all still with the decoder */
    assert(call(PTA_VIDEO_FETCH, s[0], 100, at, ROW) == PTA_INVALID);
    before = released;
    done(f, 100, 1);
    done(f, 102, 0);
    assert(released == before + 1);
    assert(call(PTA_VIDEO_DECODE, s[0], 300, data, 64) == PTA_OK);
    assert(call(PTA_VIDEO_DECODE, s[0], 301, data, 64) == PTA_OK);
    assert(call(PTA_VIDEO_DECODE, s[0], 302, data, 64) == PTA_AGAIN);
    assert(call(PTA_VIDEO_FETCH, s[1], 100, at, ROW) == PTA_AGAIN);
    assert(call(PTA_VIDEO_DROP, s[0] + 1000, PTV_ALL, 0, 0) == PTA_CANCELLED);

    /* CLOSE: pictures nobody fetched are released, the decoder is closed, the id is unknown from then on */
    done(f, 300, 1);
    before = released;
    assert(call(PTA_VIDEO_CLOSE, s[0], 0, 0, 0) == PTA_OK && closed == 1 && released == before + 1);
    assert(call(PTA_VIDEO_CLOSE, s[0], 0, 0, 0) == PTA_CANCELLED);
    assert(call(PTA_VIDEO_FETCH, s[0], 301, at, ROW) == PTA_CANCELLED);
    /* idle: a session nobody called for PTV_IDLE_MS is closed by the next call of anyone */
    clock_ms += PTV_IDLE_MS - 1;
    assert(call(PTA_VIDEO_FETCH, s[1], 100, at, ROW) == PTA_AGAIN && closed == 1);
    clock_ms += 1; /* s[2] and s[3] are idle for the whole time now, s[1] for a millisecond */
    assert(call(PTA_VIDEO_FETCH, s[1], 100, at, ROW) == PTA_AGAIN && closed == 3);
    assert(call(PTA_VIDEO_DROP, s[2], PTV_ALL, 0, 0) == PTA_CANCELLED);
    done(fake[1], 100, 1);
    assert(call(PTA_VIDEO_CLOSE, s[1], 0, 0, 0) == PTA_OK && closed == 4 && made == released);

    /* the stub: no decoder at all, the frame's number as 32 bars; its pictures come from inside submit */
    pt_video_backend(&pt_video_stub);
    ram[0x1000] = 0;
    assert(call(PTA_VIDEO_OPEN, 0, 1920u << 16 | 1080, avcc, 8) == PTA_DENIED);
    ram[0x1000] = 1;
    assert(call(PTA_VIDEO_OPEN, 0, 1920u << 16 | 1080, avcc, 8) == PTA_OK);
    uint32_t t = r4, number = 0xa5c30f01, rowbytes = 3840 + 64;
    assert(call(PTA_VIDEO_DECODE, t, number, data, 64) == PTA_OK);
    memset(ram + 0x1000000, 0x55, 1080 * rowbytes);
    assert(call(PTA_VIDEO_FETCH, t, number, out + 7, rowbytes) == PTA_OK); /* 1030 pages */
    for (unsigned y = 0; y < 1080; y += 539)
        for (unsigned bar = 0; bar < 32; bar++) {
            const uint8_t *p = ram + 0x1000000 + 7 + y * rowbytes + 2 * (bar * 60 + 30);
            assert(p[0] == 128 && p[1] == (number >> (31 - bar) & 1 ? 235 : 16));
        }
    assert(ram[0x1000000 + 7 + 3840] == 0x55 && ram[0x1000000 + 6] == 0x55);
    assert(call(PTA_VIDEO_CLOSE, t, 0, 0, 0) == PTA_OK);
    /* no decoder: PROBE says so, OPEN is denied */
    pt_video_backend(NULL);
    assert(call(PTA_VIDEO_PROBE, 0, 0, 0, 0) == PTA_OK && r5 == 0);
    assert(call(PTA_VIDEO_OPEN, 0, W << 16 | H, avcc, 8) == PTA_DENIED);
    free(c);
    free(ram);
    puts("video-guest: ok");
    return 0;
}
