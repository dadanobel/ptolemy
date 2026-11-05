/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef OPTICS_R300_STATE_H
#define OPTICS_R300_STATE_H
#include "foundation/pm4.h"
#include "foundation/shader_ir.h"
typedef struct OpticsR300State OpticsR300State;
typedef enum {
    OPTICS_STATE_OK,
    OPTICS_STATE_UNKNOWN,
    OPTICS_STATE_RANGE,
    OPTICS_STATE_EXTERNAL,
    OPTICS_STATE_UNSUPPORTED
} OpticsStateResult;
typedef enum {
    OPTICS_EFFECT_NONE,
    OPTICS_EFFECT_CONTROL,
    OPTICS_EFFECT_BARRIER,
    OPTICS_EFFECT_DRAW,
    OPTICS_EFFECT_VERTEX_ARRAYS,
    OPTICS_EFFECT_INDEX_BUFFER,
    OPTICS_EFFECT_2D
} OpticsStateEffect;
typedef struct {
    uint32_t words[PT_R2_SHADER_WORDS], flow;
    size_t count;
} OpticsR300Program;
/* 3D state and SRAM start zeroed, a virtual reset, not a physical Radeon reset claim. The argument is unused. */
OpticsR300State *optics_r300_state_create(int virtual_zero_reset);
void optics_r300_state_destroy(OpticsR300State *);
void optics_r300_state_reset(OpticsR300State *);
OpticsStateResult optics_r300_write(OpticsR300State *, uint32_t address, uint32_t);
/* Pure register packets are validated in FULL before state changes. External
 * effects return without mutating the packet's prefix. Caller owns its atomic
 * admission/ordering. Unknown packet3 is never silently ignored. */
OpticsStateResult optics_r300_packet(OpticsR300State *, const PtR2Pm4Packet *, OpticsStateEffect *);
OpticsStateResult optics_r300_program(const OpticsR300State *, PtR2ShaderStage, OpticsR300Program *);
/* Changes whenever the stage's program registers/SRAM may have changed (cheap reuse test); no value repeats, in any state. */
uint64_t optics_r300_generation(const OpticsR300State *, PtR2ShaderStage);
/* MC field units are 64 KiB, top inclusive. Separate window from storage size. */
OpticsStateResult optics_r300_pvs_vector(const OpticsR300State *, unsigned vector, uint32_t out[4]);
int optics_r300_memory_window(uint32_t location, uint64_t *base, uint64_t *length);
#endif
