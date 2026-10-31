/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "input.h"
#include <string.h>
void pt_input_init(PtInputQueue *q,PtEvent *wake,_Atomic uint32_t *kick) {
    memset(q,0,sizeof *q);atomic_init(&q->written,0);atomic_init(&q->read,0);atomic_init(&q->overflows,0);atomic_init(&q->release,0);q->wake=wake;q->kick=kick;
}
void pt_input_send(void *opaque,const PtInputEvent *e) {
    PtInputQueue *q=opaque;uint64_t w=atomic_load_explicit(&q->written,memory_order_relaxed);
    if(w-atomic_load_explicit(&q->read,memory_order_acquire)>=128) {
        atomic_fetch_add_explicit(&q->overflows,1,memory_order_relaxed);
        atomic_store_explicit(&q->release,1,memory_order_release);
    } else {q->events[w%128]=*e;atomic_store_explicit(&q->written,w+1,memory_order_release);}
    if(q->kick)atomic_store_explicit(q->kick,1,memory_order_release);
    if(q->wake)pt_event_signal(q->wake);
}
int pt_input_take(PtInputQueue *q,PtInputEvent *e) {
    if(atomic_exchange_explicit(&q->release,0,memory_order_acq_rel)) {
        uint64_t w=atomic_load_explicit(&q->written,memory_order_acquire);
        atomic_store_explicit(&q->read,w,memory_order_release);*e=(PtInputEvent){.type=PT_INPUT_RELEASE};return 1;
    }
    uint64_t r=atomic_load_explicit(&q->read,memory_order_relaxed);
    if(r==atomic_load_explicit(&q->written,memory_order_acquire))return 0;
    *e=q->events[r%128];atomic_store_explicit(&q->read,r+1,memory_order_release);return 1;
}
