/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef OPTICS_COMMAND_STREAM_H
#define OPTICS_COMMAND_STREAM_H
#include "io.h"
#include "foundation/pm4.h"
typedef struct OpticsCommandStream OpticsCommandStream;
typedef PtR2IoResult (*OpticsCommandRead)(void *,uint64_t address,void *normalized_le,size_t bytes);
/* 0 atomically accepted, 1 BUSY with no side effects, other fatal. No retained
 * packet pointers. Callback may push an IB ONLY when returning accepted. */
typedef int (*OpticsCommandExecute)(void *,const PtR2Pm4Packet *,uint64_t epoch);
typedef enum {OPTICS_STREAM_EMPTY,OPTICS_STREAM_BUDGET,OPTICS_STREAM_BUSY,OPTICS_STREAM_BAD,OPTICS_STREAM_IO,OPTICS_STREAM_EXEC} OpticsStreamResult;
OpticsCommandStream *optics_stream_create(unsigned depth,uint32_t total_words,OpticsCommandRead,OpticsCommandExecute,void *);
void optics_stream_destroy(OpticsCommandStream *);
int optics_stream_reset(OpticsCommandStream *);
/* Explicit hardware dispatch. This module does NOT guess an IB from opcode
 * 0x33 (which is R300 INDX_BUFFER, not an indirect command stream). */
PtR2IoResult optics_stream_push(OpticsCommandStream *,uint64_t address,uint32_t words);
OpticsStreamResult optics_stream_pump(OpticsCommandStream *,unsigned packet_budget,unsigned *accepted);
int optics_stream_pending(const OpticsCommandStream *);   /* nonzero while a pushed IB is unfinished (NULL: 0) */
#endif
