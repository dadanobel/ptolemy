/* SPDX-License-Identifier: GPL-2.0-or-later
 * Independent CP ring front end. This is not a GPU core or a draw executor. */
#include "ring.h"
#include <stdlib.h>
#include <string.h>

static uint32_t distance(const PtR2Ring *r,uint32_t a,uint32_t b) {return (a-b)&(r->words-1);}
int pt_r2_ring_init(PtR2Ring *r,uint32_t words,uint32_t rptr,PtR2RingRead read,PtR2RingExecute execute,void *context) {
    if(!r||!read||!execute||words<2||words>(1u<<20)||(words&(words-1))||rptr>=words)return -1;
    size_t capacity=(size_t)(words-1)*4;
    if(capacity>65540)capacity=65540; /* largest PM4 packet */
    uint8_t *packet=malloc(capacity);if(!packet)return -1;
    *r=(PtR2Ring){.words=words,.rptr=rptr,.wptr=rptr,.epoch=1,.packet=packet,.capacity=capacity,
                  .read=read,.execute=execute,.context=context};return 0;
}
void pt_r2_ring_destroy(PtR2Ring *r) {if(r){free(r->packet);memset(r,0,sizeof *r);}}
int pt_r2_ring_wptr(PtR2Ring *r,uint32_t wptr) {
    if(!r||!r->packet||wptr>=r->words)return -1;
    uint32_t added=distance(r,wptr,r->wptr),free_words=r->words-1-distance(r,r->wptr,r->rptr);
    if(added>free_words)return -1;
    r->wptr=wptr;return 0;
}
int pt_r2_ring_reset(PtR2Ring *r,uint64_t epoch) {
    if(!r||!r->packet||epoch<=r->epoch)return -1;
    r->rptr=r->wptr=r->pending_words=0;r->epoch=epoch;return 0;
}
static int read_wrapped(PtR2Ring *r,uint32_t count) {
    uint32_t first=r->words-r->rptr;if(first>count)first=count;
    if(r->read(r->context,r->rptr,first,r->packet))return -1;
    return first<count?r->read(r->context,0,count-first,r->packet+4*first):0;
}
PtR2RingResult pt_r2_ring_pump(PtR2Ring *r,uint32_t budget,uint32_t *accepted) {
    if(accepted)*accepted=0;
    if(!r||!r->packet)return PT_R2_RING_BAD;
    uint32_t done=0;
    for(;;) {
        uint32_t available=distance(r,r->wptr,r->rptr);
        if(!available)return PT_R2_RING_EMPTY;
        if(done==budget)return PT_R2_RING_BUDGET;
        PtR2Pm4Packet p;size_t needed;
        if(!r->pending_words) {
            if(read_wrapped(r,1))return PT_R2_RING_IO;
            PtR2Pm4Status status=pt_r2_pm4_decode(r->packet,4,&p,&needed);
            if(status!=PT_R2_PM4_PACKET&&status!=PT_R2_PM4_SHORT)return PT_R2_RING_BAD;
            if(needed>r->capacity)return PT_R2_RING_BAD;
            if(needed/4>available)return PT_R2_RING_MORE;
            uint8_t header[4];memcpy(header,r->packet,4);
            if(read_wrapped(r,(uint32_t)(needed/4)))return PT_R2_RING_IO;
            /* A concurrently modified advertised header is invalid input. */
            if(memcmp(header,r->packet,4)||pt_r2_pm4_decode(r->packet,needed,&p,NULL)!=PT_R2_PM4_PACKET||p.bytes!=needed)return PT_R2_RING_BAD;
            r->pending_words=(uint32_t)(needed/4);
        } else if(pt_r2_pm4_decode(r->packet,4*r->pending_words,&p,NULL)!=PT_R2_PM4_PACKET) return PT_R2_RING_BAD;
        int rc=r->execute(r->context,&p,r->epoch);
        if(rc==1)return PT_R2_RING_BUSY;
        if(rc)return PT_R2_RING_EXEC_ERROR;
        r->rptr=(r->rptr+r->pending_words)&(r->words-1);r->pending_words=0;
        done++;if(accepted)*accepted=done;
    }
}
