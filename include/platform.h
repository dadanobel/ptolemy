/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef PT_PLATFORM_H
#define PT_PLATFORM_H
#include <stdint.h>
#include <stddef.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
/* An environment switch is on only when set to exactly "1". */
static inline int pt_env_flag(const char *name){const char *v=getenv(name);return v&&!strcmp(v,"1");}

/* Device methods run on the CPU owner. IRQ producers use pt_pic_line().
 * A line represents an asserted interrupt, not the electrical pin voltage. */
enum { PT_NSRC = 64, PT_MASK = 0x80000000u, PT_ACTIVE = 0x40000000u,
       PT_LEVEL = 0x00400000u, PT_POLARITY = 0x00800000u, PT_DISTRIBUTED = 0x20000000u };
typedef struct {
    pthread_mutex_t lock;
    uint32_t vp[PT_NSRC], dest[PT_NSRC];
    uint64_t wires[PT_NSRC], pending, servicing;
    unsigned ctpr, spurious, gcr;
    uint64_t level_mask,asserted,eligible;
    int line_output;
    void (*notify)(void *);
    void *notify_opaque;
} PtPic;
void pt_pic_init(PtPic *);
void pt_pic_destroy(PtPic *);
/* Install while quiescent. Called AFTER unlocking, on either output transition;
 * it is a wakeup hint, not a sampled level. Receiver re-reads the PIC. */
void pt_pic_set_notify(PtPic *, void (*)(void *), void *);
void pt_pic_line(PtPic *, unsigned src, unsigned producer, int asserted);
uint32_t pt_pic_read(PtPic *, uint32_t offset);
void pt_pic_write(PtPic *, uint32_t offset, uint32_t value);
int pt_pic_output(PtPic *);

/* Pure virtual-time model, owned by the CPU; host ns must be monotonic.
 * DEC uses elapsed ticks, independent of writes to TB. */
typedef struct {
    uint64_t last_ns, ns, tb_offset, dec_at, dec_next;
    uint32_t dec_value;
    int paused, dec_armed, dec_pending;
} PtClock;
void pt_clock_init(PtClock *, uint64_t now_ns);
void pt_clock_update(PtClock *, uint64_t now_ns);
uint64_t pt_clock_tb(const PtClock *);
uint32_t pt_clock_dec(const PtClock *);
void pt_clock_write_tb(PtClock *, int high, uint32_t);
void pt_clock_write_dec(PtClock *, uint32_t);
void pt_clock_pause(PtClock *, uint64_t now_ns, int pause);
/* Relative delay at last update, UINT64_MAX when disarmed/paused. */
uint64_t pt_clock_deadline(const PtClock *);

/* The machine-owned virtual uptime: host uptime (CLOCK_UPTIME_RAW, which already stops while the Mac
 * sleeps) minus manual pauses. Guest-visible time (PtClock, devices, audio, OHCI, script) reads pt_vtime_now; the one
 * conversion back to a host deadline is pt_vtime_host. Paused: virtual time stands still and no host deadline exists. */
typedef struct { _Atomic uint64_t paused, since; } PtVtime;   /* since: host ns the current pause began, 0 = running */
uint64_t pt_vtime_now(PtVtime *, uint64_t host_ns);
void pt_vtime_pause(PtVtime *, uint64_t host_ns);
uint64_t pt_vtime_resume(PtVtime *, uint64_t host_ns);       /* the pause's length in ns, 0 if not paused */
uint64_t pt_vtime_host(PtVtime *, uint64_t virtual_deadline); /* UINT64_MAX while paused or for UINT64_MAX */

/* Generation-based wakeups: capture -> check device state -> wait.
 * No device mutex is held while waiting. */
typedef struct {
    pthread_mutex_t lock;
    pthread_cond_t cond;
    uint64_t generation;
} PtEvent;
void pt_event_init(PtEvent *);
void pt_event_destroy(PtEvent *);
uint64_t pt_event_generation(PtEvent *);
void pt_event_signal(PtEvent *);
int pt_event_wait(PtEvent *, uint64_t observed, uint64_t timeout_ns);
/* timeout_ns == UINT64_MAX means wait until the generation changes. */

typedef struct {
    uint8_t data[2][32][16], len[2][32];
    uint16_t selector;
    uint32_t offset;
    const uint8_t *blob; uint32_t blob_len;   /* PT_FW_BLOB: a 4-byte little-endian length, then the bytes */
} PtFwCfg;
enum { PT_FW_BLOB=0x8010 };
void pt_fw_init(PtFwCfg *, uint64_t ram_bytes);
void pt_fw_select(PtFwCfg *, uint16_t);
uint8_t pt_fw_read(PtFwCfg *);

typedef int (*PtMmioFn)(void *, uint32_t, uint8_t *, unsigned, int);
typedef struct { uint32_t base; uint64_t size; PtMmioFn fn; void *opaque; const char *name; } PtRegion;
typedef struct { PtRegion regions[32]; unsigned count; } PtBus;
int pt_bus_add(PtBus *, PtRegion);
int pt_bus_access(PtBus *, uint32_t, uint8_t *, unsigned, int);
uint64_t pt_read_le(const uint8_t *, unsigned);
void pt_write_le(uint8_t *, unsigned, uint64_t);
uint64_t pt_read_be(const uint8_t *, unsigned);
void pt_write_be(uint8_t *, unsigned, uint64_t);

#endif
