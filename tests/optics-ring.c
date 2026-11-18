/* SPDX-License-Identifier: GPL-2.0-or-later
 * The CP ring front end (ring.c) at its boundaries: a packet across the ring's end, a packet of N - 1 words in an
 * N-word ring (one slot stays empty) and one of N words, an incomplete packet, BUSY (no side effects; the held bytes
 * are executed later, not re-read), the packet budget, WPTR validation, a header changed under the reader, read and
 * execute errors. CPU only. */
#include "ring.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

enum { N = 16 };
static uint32_t mem[N];
static int busy_left, fail_exec, fail_read, change_header, calls, reads;
static uint32_t got[N + 1], got_words;

static int rd(void *ctx, uint32_t first, uint32_t words, void *le) {
    (void)ctx;
    reads++;
    if (fail_read)
        return -1;
    assert(first < N && words <= N - first);
    memcpy(le, mem + first, 4 * words);
    if (change_header && words == 1) { /* the guest rewrites the header between the two reads */
        mem[first] ^= 1u << 16;
        change_header = 0;
    }
    return 0;
}
static int ex(void *ctx, const PtR2Pm4Packet *p) {
    (void)ctx;
    calls++;
    if (busy_left) {
        busy_left--;
        return 1;
    }
    if (fail_exec)
        return 2;
    got_words = p->words;
    got[0] = p->header;
    memcpy(got + 1, p->payload, 4 * p->words);
    return 0;
}
static uint32_t put(uint32_t at, uint32_t payload, uint32_t tag) { /* type-3 packet: header + payload words */
    mem[at % N] = 3u << 30 | (payload - 1) << 16 | 0x10u << 8;
    for (uint32_t i = 1; i <= payload; i++)
        mem[(at + i) % N] = tag + i;
    return (at + payload + 1) % N;
}
static void expect(uint32_t payload, uint32_t tag) {
    assert(got_words == payload);
    for (uint32_t i = 1; i <= payload; i++)
        assert(got[i] == tag + i);
}
static PtR2Ring ring(uint32_t rptr) {
    PtR2Ring r;
    memset(mem, 0, sizeof mem);
    busy_left = fail_exec = fail_read = change_header = calls = reads = 0;
    got_words = 0;
    assert(pt_r2_ring_init(&r, N, rptr, rd, ex, NULL) == 0);
    return r;
}
int main(void) {
    uint32_t n;
    /* across the end: words 13 14 15 0 1 2 */
    PtR2Ring r = ring(13);
    uint32_t w = put(13, 5, 0x100);
    assert(w == 3 && pt_r2_ring_wptr(&r, w) == 0);
    assert(pt_r2_ring_pump(&r, 8, &n) == PT_R2_RING_EMPTY && n == 1 && calls == 1 && r.rptr == 3);
    expect(5, 0x100);
    pt_r2_ring_destroy(&r);
    /* N - 1 words fill the ring (from rptr 5 to 4); one more word, or a packet of N words, cannot be advertised */
    r = ring(5);
    w = put(5, N - 2, 0x200);
    assert(w == 4 && pt_r2_ring_wptr(&r, 5) == 0); /* WPTR = RPTR: nothing added */
    assert(pt_r2_ring_wptr(&r, w) == 0 && pt_r2_ring_wptr(&r, 5) == -1);
    assert(pt_r2_ring_pump(&r, 8, &n) == PT_R2_RING_EMPTY && n == 1 && r.rptr == 4);
    expect(N - 2, 0x200);
    pt_r2_ring_destroy(&r);
    r = ring(0);
    put(0, N - 1, 0x300); /* claims N words: larger than any advertisable span */
    assert(pt_r2_ring_wptr(&r, N - 1) == 0);
    assert(pt_r2_ring_pump(&r, 8, &n) == PT_R2_RING_BAD && n == 0 && calls == 0 && r.rptr == 0);
    assert(pt_r2_ring_wptr(&r, N) == -1); /* WPTR beyond the ring */
    pt_r2_ring_destroy(&r);
    /* incomplete: header and 2 of 5 payload words, then the rest */
    r = ring(14);
    w = put(14, 5, 0x400);
    assert(pt_r2_ring_wptr(&r, 1) == 0);
    assert(pt_r2_ring_pump(&r, 8, &n) == PT_R2_RING_MORE && n == 0 && calls == 0 && r.rptr == 14);
    assert(pt_r2_ring_wptr(&r, w) == 0);
    assert(pt_r2_ring_pump(&r, 8, &n) == PT_R2_RING_EMPTY && n == 1 && r.rptr == w);
    expect(5, 0x400);
    pt_r2_ring_destroy(&r);
    /* BUSY across the end: RPTR stays; the held bytes run later even though the ring memory changed */
    r = ring(12);
    w = put(12, 6, 0x500);
    assert(pt_r2_ring_wptr(&r, w) == 0);
    busy_left = 2;
    assert(pt_r2_ring_pump(&r, 8, &n) == PT_R2_RING_BUSY && n == 0 && r.rptr == 12);
    int before = reads;
    for (uint32_t i = 0; i < N; i++)
        mem[i] = 0xdeadbeef;
    assert(pt_r2_ring_pump(&r, 8, &n) == PT_R2_RING_BUSY && n == 0 && r.rptr == 12 && reads == before);
    assert(pt_r2_ring_pump(&r, 8, &n) == PT_R2_RING_EMPTY && n == 1 && r.rptr == w && calls == 3);
    expect(6, 0x500);
    pt_r2_ring_destroy(&r);
    /* budget: 3 packets, budget 2, then the third */
    r = ring(0);
    w = put(put(put(0, 1, 0x600), 2, 0x610), 3, 0x620);
    assert(pt_r2_ring_wptr(&r, w) == 0);
    assert(pt_r2_ring_pump(&r, 2, &n) == PT_R2_RING_BUDGET && n == 2 && r.rptr == 5);
    expect(2, 0x610);
    assert(pt_r2_ring_pump(&r, 2, &n) == PT_R2_RING_EMPTY && n == 1 && r.rptr == w);
    expect(3, 0x620);
    pt_r2_ring_destroy(&r);
    /* a header changed between its read and the packet's: invalid input, nothing executed */
    r = ring(3);
    w = put(3, 4, 0x700);
    assert(pt_r2_ring_wptr(&r, w) == 0);
    change_header = 1;
    assert(pt_r2_ring_pump(&r, 8, &n) == PT_R2_RING_BAD && calls == 0 && r.rptr == 3);
    pt_r2_ring_destroy(&r);
    /* read and execute errors: no side effects */
    r = ring(7);
    w = put(7, 2, 0x800);
    assert(pt_r2_ring_wptr(&r, w) == 0);
    fail_read = 1;
    assert(pt_r2_ring_pump(&r, 8, &n) == PT_R2_RING_IO && calls == 0 && r.rptr == 7);
    fail_read = 0;
    fail_exec = 1;
    assert(pt_r2_ring_pump(&r, 8, &n) == PT_R2_RING_EXEC_ERROR && n == 0 && r.rptr == 7);
    fail_exec = 0;
    assert(pt_r2_ring_pump(&r, 8, &n) == PT_R2_RING_EMPTY && n == 1 && r.rptr == w);
    expect(2, 0x800);
    pt_r2_ring_destroy(&r);
    puts("optics-ring: wrap, N-1 / N words, MORE, BUSY hold, budget, WPTR, header race, IO/exec errors PASS");
    return 0;
}
