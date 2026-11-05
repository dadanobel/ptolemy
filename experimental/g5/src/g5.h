/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef PT_G5_H
#define PT_G5_H
#include <pthread.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define PT_G5_CPUS 4
#define PT_G5_PA_BITS 42
#define PT_G5_SLB_COUNT 64
#define PT_G5_RESERVATION 128
#define PT_G5_MSR_SF (UINT64_C(1) << 63)
#define PT_G5_MSR_PR (UINT64_C(1) << 14)
#define PT_G5_MSR_IR (UINT64_C(1) << 5)
#define PT_G5_MSR_DR (UINT64_C(1) << 4)

typedef enum {
    PT_G5_OK,
    PT_G5_UNSUPPORTED,
    PT_G5_BAD_CONFIG,
    PT_G5_BUS,
    PT_G5_ALIGNMENT,
    PT_G5_SEGMENT,
    PT_G5_PAGE,
    PT_G5_PROTECTION,
    PT_G5_RETRY
} PtG5Result;

typedef struct {
    uint64_t base, size;
} PtG5Range;
typedef struct {
    PtG5Range ram[8];
    unsigned count;
    PtG5Range engine;
    unsigned ipa_bits;
} PtG5Layout;
/* Validates a caller-supplied board map; does not invent a firmware memory map. */
PtG5Result pt_g5_layout(PtG5Layout *, const PtG5Range *, unsigned, const PtG5Range *, unsigned, uint64_t, unsigned);
bool pt_g5_dma32(uint64_t, uint64_t, uint32_t *);

typedef struct {
    PtG5Range range;
    uint8_t *host;
    bool readonly;
} PtG5Bank;
typedef struct {
    uint64_t address;
    unsigned bytes;
    bool valid;
} PtG5Reservation;
typedef struct {
    pthread_mutex_t lock;
    PtG5Bank bank[8];
    unsigned count;
    PtG5Reservation reservation[PT_G5_CPUS];
} PtG5Memory;
/* Conservative reference bus: ALL CPU, DMA, debugger and GPU writes use this bus.
 * Direct writable host pointers invalidate its guarantee and are not supported. */
PtG5Result pt_g5_memory_init(PtG5Memory *, const PtG5Bank *, unsigned);
void pt_g5_memory_destroy(PtG5Memory *);
PtG5Result pt_g5_read(PtG5Memory *, uint64_t, void *, size_t);
PtG5Result pt_g5_write(PtG5Memory *, uint64_t, const void *, size_t);
PtG5Result pt_g5_reserve(PtG5Memory *, unsigned, uint64_t, unsigned, uint64_t *);
PtG5Result pt_g5_conditional(PtG5Memory *, unsigned, uint64_t, unsigned, uint64_t, bool *);
void pt_g5_reservation_clear(PtG5Memory *, unsigned);
PtG5Result pt_g5_pte_read(PtG5Memory *, uint64_t, uint64_t[2]);
PtG5Result pt_g5_pte_commit(PtG5Memory *, uint64_t, const uint64_t[2], uint64_t);

typedef struct {
    uint64_t esid, vsid;
    uint16_t flags;
    bool valid;
} PtG5Slb;
enum {
    PT_G5_SLB_KS = 0x800,
    PT_G5_SLB_KP = 0x400,
    PT_G5_SLB_N = 0x200,
    PT_G5_SLB_L = 0x100,
    PT_G5_SLB_C = 0x80
};
typedef struct {
    PtG5Slb slb[PT_G5_SLB_COUNT];
    uint64_t sdr1, generation;
    /* Called outside the memory lock: retirement may publish GPU writes. */
    PtG5Result (*prepare_pte)(void *, uint64_t, unsigned, bool);
    void *prepare_opaque;
} PtG5Mmu;
typedef enum {
    PT_G5_LOAD,
    PT_G5_STORE,
    PT_G5_FETCH
} PtG5Access;
typedef struct {
    uint64_t sdr1;
    PtG5Result (*prepare_pte)(void *, uint64_t, unsigned, bool);
    void *prepare_opaque;
} PtG5Walk;
typedef struct {
    uint64_t pa, pte_address, page_size;
    uint8_t wimg;
    /* Safe native write permission: guest PP/key permit stores AND PTE.C is set.
     * A read miss with C=0 must map read-only so the first store updates C. */
    bool writable;
    uint32_t fault_bits; /* DSISR/SRR1 reason on PAGE/PROTECTION, before the store bit. */
} PtG5Translation;
PtG5Result pt_g5_slbmte(PtG5Mmu *, uint64_t, uint64_t, bool);
PtG5Result pt_g5_mtsr(PtG5Mmu *, unsigned, uint32_t);
PtG5Result pt_g5_mfsr(const PtG5Mmu *, unsigned, uint32_t *);
void pt_g5_slbie(PtG5Mmu *, uint64_t);
void pt_g5_slbia(PtG5Mmu *);
PtG5Result pt_g5_slbmfe(const PtG5Mmu *, unsigned, bool, uint64_t *);
PtG5Result pt_g5_translate(PtG5Mmu *, PtG5Memory *, uint64_t, uint64_t, PtG5Access, PtG5Translation *);
/* The caller has normalized EA and selected exactly one matching valid SLB entry. */
PtG5Result pt_g5_walk_segment(const PtG5Walk *, PtG5Memory *, const PtG5Slb *, uint64_t, bool, PtG5Access,
                              PtG5Translation *);
unsigned pt_g5_dcbz_size(uint64_t hid5, uint32_t instruction);

typedef struct {
    uint64_t gpr[32], pc, lr, ctr, msr;
    uint32_t cr, xer;
} PtG5Integer;
/* Integer extension executor only. Unknown instructions leave all state intact. */
PtG5Result pt_g5_integer(PtG5Integer *, uint32_t);
uint8_t pt_g5_compare(uint64_t, uint64_t, bool, bool, bool);
uint64_t pt_g5_mask(unsigned, unsigned);
#endif
