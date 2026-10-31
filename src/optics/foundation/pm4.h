/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef PT_R2_PM4_H
#define PT_R2_PM4_H
#include <stddef.h>
#include <stdint.h>

/* Input is an immutable, contiguous LE dword stream AFTER CP endian handling.
 * This decoder neither reads guest memory nor executes register/IB side effects.
 * A caller must reassemble ring wrap and retain the bytes while using a packet. */
typedef struct {
    const uint8_t *payload;
    uint32_t header, bytes, words;
    uint16_t reg[2];
    uint8_t type, opcode, one_reg;
} PtR2Pm4Packet;

typedef enum {
    PT_R2_PM4_PACKET,
    PT_R2_PM4_SHORT,
    PT_R2_PM4_BAD_HEADER,
    PT_R2_PM4_BAD_ARGUMENT
} PtR2Pm4Status;

/* On failure, packet is untouched. needed receives the complete packet size,
 * or 4 for an incomplete header. Reserved R300 address bits and register-range
 * overflow are rejected before any writes can be enumerated. */
PtR2Pm4Status pt_r2_pm4_decode(const void *data, size_t bytes,
                             PtR2Pm4Packet *packet, size_t *needed);
/* Accessors accept packets returned successfully by the decoder. */
int pt_r2_pm4_word(const PtR2Pm4Packet *packet, uint32_t index, uint32_t *value);
int pt_r2_pm4_write(const PtR2Pm4Packet *packet, uint32_t index,
                    uint32_t *reg, uint32_t *value);
/* A name identifies a documented opcode, not an implemented command. */
const char *pt_r2_pm4_opcode_name(uint8_t opcode);
#endif
