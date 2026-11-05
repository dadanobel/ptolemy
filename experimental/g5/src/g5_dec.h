/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef PT_G5_DEC_H
#define PT_G5_DEC_H
#include "g5.h"
/* CPU-owner state. ticks is the monotonic virtual oscillator, NOT the writable TB.
 * Reset explicitly writes a value; no undocumented hardware reset value is assumed.
 * IBM 970MP UM 2.3, section 4.5.11: DEC is level-sensitive while negative. */
typedef struct {
    uint64_t at;
    uint32_t value;
    bool initialized;
} PtG5Decrementer;
void pt_g5_dec_write(PtG5Decrementer *, uint64_t, uint32_t);
PtG5Result pt_g5_dec_read(const PtG5Decrementer *, uint64_t, uint32_t *);
PtG5Result pt_g5_dec_pending(const PtG5Decrementer *, uint64_t, bool, bool *);
PtG5Result pt_g5_dec_next(const PtG5Decrementer *, uint64_t, uint64_t *);
#endif
