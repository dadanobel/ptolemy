/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef PT_POINTER_MAILBOX_H
#define PT_POINTER_MAILBOX_H
#include <stdatomic.h>
#include <stdint.h>
#include <time.h>
enum {PT_CURSOR_UNKNOWN,PT_CURSOR_VISIBLE,PT_CURSOR_HIDDEN};
typedef struct {_Atomic uint64_t sample;} PtCursorMailbox;
static inline uint64_t pt_cursor_now(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return (uint64_t)t.tv_sec*1000000000+t.tv_nsec;}
/* One atomic publication keeps state and freshness in the same observation. */
static inline void pt_cursor_publish(PtCursorMailbox *p,unsigned state,uint64_t now){atomic_store_explicit(&p->sample,((now/1000)<<2)|(state<=2?state:0),memory_order_release);}
static inline unsigned pt_cursor_read(PtCursorMailbox *p,uint64_t now){
    uint64_t v=atomic_load_explicit(&p->sample,memory_order_acquire),at=v>>2,t=now/1000;
    return t<at||t-at>1000000?PT_CURSOR_UNKNOWN:v&3;
}
#endif
