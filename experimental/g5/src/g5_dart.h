/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef PT_G5_DART_H
#define PT_G5_DART_H
#include "g5.h"
/* CPC945 table-walk semantics, UM A15-6010-02a sections 3.4 and 12.9.19-22.
 * No guest register bank, TLB/debug array, IRQ or asynchronous invalidation yet.
 * Caller snapshots configuration under the board lock. Memory banks stay immutable.
 * Every DMA must split at returned length, then use the shared writer protocol. */
typedef struct {
    uint32_t base_page, pages;
    bool enabled;
} PtG5DartConfig;
typedef struct {
    uint64_t pa;
    size_t length;
} PtG5DmaMapping;
typedef struct {
    bool valid;
    uint32_t exception;
} PtG5DartFault;
PtG5Result pt_g5_dart_translate(const PtG5DartConfig *, PtG5Memory *, uint64_t, bool, bool, PtG5DmaMapping *,
                                PtG5DartFault *);
#endif
