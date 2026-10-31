/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "input.h"
#include <assert.h>
#include <stdio.h>
#include <sched.h>
static PtInputQueue queue;static _Atomic int done;
static void *producer(void *opaque) {
    (void)opaque;
    for(unsigned i=1;i<=100000;i++) {
        PtInputEvent event={.type=PT_INPUT_KEY,.usage=i,.x=i,.y=(int32_t)~i,.wheel=i^0x5a5a5a5a};pt_input_send(&queue,&event);
    }
    pt_input_send(&queue,&(PtInputEvent){.type=PT_INPUT_RELEASE});atomic_store(&done,1);return NULL;
}
int main(void) {
    pt_input_init(&queue,NULL,NULL);pthread_t thread;assert(!pthread_create(&thread,NULL,producer,NULL));
    unsigned last=0,neutral=0;PtInputEvent event;
    for(;;) {
        int got=pt_input_take(&queue,&event);
        if(!got) {
            if(atomic_load(&done)){if(!pt_input_take(&queue,&event))break;}
            else {sched_yield();continue;}
        }
        if(event.type==PT_INPUT_RELEASE)neutral=1;
        else {assert(event.usage>last && event.x==(int32_t)event.usage && event.y==(int32_t)~event.usage && event.wheel==(int32_t)(event.usage^0x5a5a5a5a));last=event.usage;neutral=0;}
    }
    pthread_join(thread,NULL);assert(neutral);printf("input: SPSC publication/order/overflow neutralization passed; overflows=%llu\n",(unsigned long long)atomic_load(&queue.overflows));
}
