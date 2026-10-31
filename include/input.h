/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef PT_INPUT_H
#define PT_INPUT_H
#include "platform.h"
enum { PT_INPUT_KEY,PT_INPUT_RELATIVE,PT_INPUT_ABSOLUTE,PT_INPUT_RELEASE };
typedef struct {unsigned type,usage,pressed,buttons;int32_t x,y,wheel;} PtInputEvent;
/* Main-thread producer. send must COPY the event; never retain the pointer. */
typedef struct {void *opaque;void (*send)(void *,const PtInputEvent *);} PtInputSink;
typedef struct {
    _Atomic uint64_t written,read,overflows;_Atomic unsigned release;
    PtInputEvent events[128];PtEvent *wake;_Atomic uint32_t *kick;
} PtInputQueue;
void pt_input_init(PtInputQueue *,PtEvent *,_Atomic uint32_t *);
void pt_input_send(void *,const PtInputEvent *);
int pt_input_take(PtInputQueue *,PtInputEvent *);
#endif
