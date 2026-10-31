/* SPDX-License-Identifier: GPL-2.0-or-later
 * OpenPIC 1.2 Register Interface Specification (AMD 19725, 1995), sections 4-5.
 * Firmware-port values follow the maintained OpenBIOS fork ABI.
 * This implementation does not implement MPIC timers or IPIs. */
#include "platform.h"
#include "pt_boot_mode.h"
#include <string.h>
#include <time.h>
#include <errno.h>
#include <assert.h>
#include <stdlib.h>

uint64_t pt_read_le(const uint8_t *p, unsigned n) {
    uint64_t v = 0; for (unsigned i = 0; i < n; i++) v |= (uint64_t)p[i] << (8*i); return v;
}
uint64_t pt_read_be(const uint8_t *p, unsigned n) {
    uint64_t v = 0; for (unsigned i = 0; i < n; i++) v = (v << 8) | p[i]; return v;
}
void pt_write_le(uint8_t *p, unsigned n, uint64_t v) {
    for (unsigned i = 0; i < n; i++) { p[i] = v; v >>= 8; }
}
void pt_write_be(uint8_t *p, unsigned n, uint64_t v) {
    for (unsigned i = n; i; ) { p[--i] = v; v >>= 8; }
}
int pt_bus_add(PtBus *b, PtRegion r) {
    if (!r.size || !r.fn || r.size > (1ull<<32) - r.base || b->count == 32) return -1;
    for (unsigned i=0; i<b->count; i++)
        if ((uint64_t)r.base < b->regions[i].base+b->regions[i].size &&
            (uint64_t)b->regions[i].base < r.base+r.size) return -1;
    b->regions[b->count++] = r; return 0;
}
int pt_bus_access(PtBus *b, uint32_t addr, uint8_t *buf, unsigned n, int write) {
    if (!buf || !n || n>8 || (uint64_t)addr+n > (1ull<<32)) return -1;
    for (unsigned i=0; i<b->count; i++) {
        PtRegion *r = b->regions+i;
        if (addr>=r->base && (uint64_t)addr-r->base+n<=r->size)
            return r->fn(r->opaque,addr-r->base,buf,n,write);
    }
    if (!write) memset(buf,0xff,n);
    return -1;
}

/* OpenPIC 1.2, sections 4.1/4.2/5.2: single destination CPU, external sources.
 * Pending events and eligible routing are bitsets; output reads do no arbitration.
 * Timer/IPI register banks remain outside this board's exposed contract. */
static uint64_t pic_events(const PtPic *p) {return (p->pending&~p->level_mask)|(p->asserted&p->level_mask);}
static int pic_winner(const PtPic *p,uint64_t candidates,unsigned threshold) {
    int winner=-1;
    while(candidates) {
        unsigned i=(unsigned)__builtin_ctzll(candidates);candidates&=candidates-1;
        unsigned rank=p->vp[i]>>16&15;
        if(rank>threshold){threshold=rank;winner=(int)i;}
    }
    return winner;
}
static void pic_reset(PtPic *p) {
    p->ctpr=15;p->spurious=255;p->gcr=0;p->pending=p->servicing=0;
    p->level_mask=p->asserted=p->eligible=0;p->line_output=0;
    memset(p->wires,0,sizeof p->wires);
    for(unsigned i=0;i<PT_NSRC;i++){p->vp[i]=PT_MASK|PT_DISTRIBUTED;p->dest[i]=0;}
}
static int pic_next(const PtPic *p) {
    unsigned threshold=p->ctpr;
    int serving=pic_winner(p,p->servicing,0);
    if(serving>=0){unsigned rank=p->vp[serving]>>16&15;if(rank>threshold)threshold=rank;}
    return pic_winner(p,pic_events(p)&p->eligible&~p->servicing,threshold);
}
static void pic_unlock(PtPic *p,int old) {
    p->line_output=pic_next(p)>=0;
    void (*wake)(void *)=old!=p->line_output?p->notify:NULL;void *context=p->notify_opaque;
    pthread_mutex_unlock(&p->lock);if(wake)wake(context);
}
void pt_pic_init(PtPic *p) {memset(p,0,sizeof *p);if(pthread_mutex_init(&p->lock,NULL))abort();pic_reset(p);}
void pt_pic_destroy(PtPic *p) {pthread_mutex_destroy(&p->lock);}
void pt_pic_set_notify(PtPic *p,void (*fn)(void *),void *context) {p->notify=fn;p->notify_opaque=context;}
void pt_pic_line(PtPic *p,unsigned source,unsigned producer,int high) {
    if(source>=PT_NSRC||producer>=64)return;
    pthread_mutex_lock(&p->lock);int old=p->line_output;uint64_t previous=p->wires[source],bit=UINT64_C(1)<<source;
    uint64_t wire=UINT64_C(1)<<producer;
    if(high)p->wires[source]|=wire;else p->wires[source]&=~wire;
    if(p->wires[source])p->asserted|=bit;else p->asserted&=~bit;
    if(!previous&&p->wires[source]&&!(p->level_mask&bit))p->pending|=bit;
    pic_unlock(p,old);
}
int pt_pic_output(PtPic *p) {pthread_mutex_lock(&p->lock);int high=p->line_output;pthread_mutex_unlock(&p->lock);return high;}
uint32_t pt_pic_read(PtPic *p,uint32_t address) {
    pthread_mutex_lock(&p->lock);int old=p->line_output;uint32_t result=0;
    switch(address) {
    case 0x1000:result=(PT_NSRC-1u)<<16|2;break;
    case 0x1020:result=p->gcr;break;
    case 0x10e0:result=p->spurious;break;
    case 0x20080:result=p->ctpr;break;
    case 0x200a0:{int i=pic_next(p);result=p->spurious;
        if(i>=0){uint64_t bit=UINT64_C(1)<<i;result=p->vp[i]&255;p->servicing|=bit;if(!(p->level_mask&bit))p->pending&=~bit;}break;}
    default:
        if(address>=0x10000&&address<0x10000+PT_NSRC*32u){
            unsigned i=(address-0x10000)/32;uint64_t bit=UINT64_C(1)<<i;
            if(!(address&31)){
                static int exact=-1;if(exact<0){const char *v=getenv("PT_PIC_ACTIVITY");exact=v&&atoi(v)!=0;}
                uint64_t active=(pic_events(p)&bit&~(p->vp[i]&PT_MASK?bit:0))|(exact?p->servicing&bit:0);
                result=p->vp[i]|(active?PT_ACTIVE:0);
            }else if((address&31)==16)result=p->dest[i];
        }
    }
    pic_unlock(p,old);return result;
}
void pt_pic_write(PtPic *p,uint32_t address,uint32_t value) {
    pthread_mutex_lock(&p->lock);int old=p->line_output;
    switch(address) {
    case 0x1020:if(value&0x80000000u)pic_reset(p);else p->gcr=value&0x20000000u;break;
    case 0x10e0:p->spurious=value&255;break;
    case 0x20080:p->ctpr=value&15;break;
    case 0x200b0:{int i=pic_winner(p,p->servicing,0);if(i>=0)p->servicing&=~(UINT64_C(1)<<i);break;}
    default:
        if(address>=0x10000&&address<0x10000+PT_NSRC*32u){
            unsigned i=(address-0x10000)/32;uint64_t bit=UINT64_C(1)<<i;
            if(!(address&31))p->vp[i]=value&(PT_MASK|PT_DISTRIBUTED|PT_POLARITY|PT_LEVEL|0x000f00ffu);
            else if((address&31)==16)p->dest[i]=value&1;
            if(p->vp[i]&PT_LEVEL)p->level_mask|=bit;else p->level_mask&=~bit;
            if(!(p->vp[i]&PT_MASK)&&(p->dest[i]&1))p->eligible|=bit;else p->eligible&=~bit;
        }
    }
    pic_unlock(p,old);
}

static uint64_t ticks(const PtClock *c) { return c->ns/40; } /* exactly 25 MHz */
void pt_clock_init(PtClock *c,uint64_t now) { memset(c,0,sizeof *c); c->last_ns=now; }
void pt_clock_update(PtClock *c,uint64_t now) {
    if (now<c->last_ns) return;
    if (!c->paused) c->ns+=now-c->last_ns;
    c->last_ns=now;
    uint64_t t=ticks(c);
    if(c->dec_armed && t>=c->dec_next) {
        c->dec_pending=1;
        c->dec_next+=(((t-c->dec_next)>>32)+1)*(1ull<<32);
    }
}
uint64_t pt_clock_tb(const PtClock *c) { return ticks(c)+c->tb_offset; }
uint32_t pt_clock_dec(const PtClock *c) { return c->dec_value-(uint32_t)(ticks(c)-c->dec_at); }
void pt_clock_write_tb(PtClock *c,int high,uint32_t v) {
    uint64_t tb=pt_clock_tb(c);
    tb=high ? ((uint64_t)v<<32)|(uint32_t)tb : (tb&0xffffffff00000000ull)|v;
    c->tb_offset=tb-ticks(c);
}
void pt_clock_write_dec(PtClock *c,uint32_t v) {
    uint32_t old=pt_clock_dec(c);
    if (!(old>>31) && (v>>31)) c->dec_pending=1;
    c->dec_value=v; c->dec_at=ticks(c); c->dec_armed=1;
    c->dec_next=c->dec_at+(uint64_t)v+1;
}
void pt_clock_pause(PtClock *c,uint64_t now,int pause) {
    pt_clock_update(c,now); c->paused=!!pause;
}
uint64_t pt_vtime_now(PtVtime *v,uint64_t host) {
    uint64_t since=atomic_load(&v->since); return (since?since:host)-atomic_load(&v->paused);
}
void pt_vtime_pause(PtVtime *v,uint64_t host) { uint64_t zero=0; atomic_compare_exchange_strong(&v->since,&zero,host?host:1); }
uint64_t pt_vtime_resume(PtVtime *v,uint64_t host) {
    uint64_t since=atomic_load(&v->since); if(!since) return 0;
    uint64_t d=host>since?host-since:0;
    /* Running first: a concurrent reader may see time run ahead by the pause for an instant, never go back. */
    atomic_store(&v->since,0); atomic_fetch_add(&v->paused,d); return d;
}
uint64_t pt_vtime_host(PtVtime *v,uint64_t t) {
    if(t==UINT64_MAX || atomic_load(&v->since)) return UINT64_MAX;
    uint64_t p=atomic_load(&v->paused); return t>UINT64_MAX-p?UINT64_MAX:t+p;
}
uint64_t pt_clock_deadline(const PtClock *c) {
    if(c->paused || !c->dec_armed) return UINT64_MAX;
    uint64_t t=ticks(c);
    if(c->dec_next<=t) return 0;
    return (c->dec_next-t)*40-c->ns%40;
}
void pt_event_init(PtEvent *e) {
    memset(e,0,sizeof *e);
    if(pthread_mutex_init(&e->lock,0)) abort();
    pthread_condattr_t attr;
    if(pthread_condattr_init(&attr)) abort();
#ifndef __APPLE__
    if(pthread_condattr_setclock(&attr,CLOCK_MONOTONIC)) abort();
#endif
    if(pthread_cond_init(&e->cond,&attr)) abort();
    pthread_condattr_destroy(&attr);
}
void pt_event_destroy(PtEvent *e) { pthread_cond_destroy(&e->cond); pthread_mutex_destroy(&e->lock); }
uint64_t pt_event_generation(PtEvent *e) {
    pthread_mutex_lock(&e->lock); uint64_t g=e->generation; pthread_mutex_unlock(&e->lock); return g;
}
void pt_event_signal(PtEvent *e) {
    pthread_mutex_lock(&e->lock); e->generation++; pthread_cond_broadcast(&e->cond); pthread_mutex_unlock(&e->lock);
}
int pt_event_wait(PtEvent *e,uint64_t observed,uint64_t ns) {
    struct timespec ts; clock_gettime(CLOCK_MONOTONIC,&ts);
    if(ns!=UINT64_MAX) { ts.tv_sec+=ns/1000000000; ts.tv_nsec+=ns%1000000000; }
    if(ts.tv_nsec>=1000000000) { ts.tv_sec++; ts.tv_nsec-=1000000000; }
    pthread_mutex_lock(&e->lock); int rc=0;
    while(e->generation==observed && !rc) {
        if(ns==UINT64_MAX) {
            rc=pthread_cond_wait(&e->cond,&e->lock);
            continue;
        }
#ifdef __APPLE__
        struct timespec now,remain; clock_gettime(CLOCK_MONOTONIC,&now);
        int64_t left=(int64_t)(ts.tv_sec-now.tv_sec)*1000000000+ts.tv_nsec-now.tv_nsec;
        if(left<=0) { rc=ETIMEDOUT; break; }
        remain.tv_sec=left/1000000000; remain.tv_nsec=left%1000000000;
        rc=pthread_cond_timedwait_relative_np(&e->cond,&e->lock,&remain);
#else
        rc=pthread_cond_timedwait(&e->cond,&e->lock,&ts);
#endif
    }
    int changed=e->generation!=observed; pthread_mutex_unlock(&e->lock);
    return changed ? 1 : rc==ETIMEDOUT ? 0 : -1;
}

/* Ptolemy firmware-port ABI: selector bit 15 is the board namespace; a
 * selection rewinds its byte stream. Multi-byte fields are little-endian.
 * Keys and bootstrap values are consumed by the maintained OpenBIOS fork. */
void pt_fw_init(PtFwCfg *f,uint64_t ram_bytes) {
    struct Value {uint16_t key;uint8_t bytes;uint64_t value;};
    const PtBootMode *display=pt_boot_mode(getenv("PT_EDID_MODE"));
    const struct Value values[]={
        {1,4,1},{3,8,ram_bytes},{4,2,1},{5,2,1},{6,2,1},{12,2,'c'},{15,2,1},
        {0x8000,2,display?display->width:1024},{0x8001,2,display?display->height:768},{0x8002,2,32},
        {0x8003,4,25000000},{0x8004,4,1666666666},{0x8008,4,0xfff04000},{0x8009,4,166666666},{0x800b,4,1}
    };
    memset(f,0,sizeof *f);memcpy(f->data[0][0],"PTOL",4);f->len[0][0]=4;
    /* Private board feature stream, paired with the maintained firmware fork. */
    f->data[0][31][0]=1;f->len[0][31]=4;
    for(size_t i=0;i<sizeof values/sizeof *values;i++) {
        unsigned space=values[i].key>>15,key=values[i].key&31;
        f->len[space][key]=values[i].bytes;
        for(unsigned byte=0;byte<values[i].bytes;byte++)f->data[space][key][byte]=(uint8_t)(values[i].value>>(byte*8));
    }
}
void pt_fw_select(PtFwCfg *f,uint16_t selector) {f->offset=0;f->selector=selector;}
uint8_t pt_fw_read(PtFwCfg *f) {
    if(f->selector==PT_FW_BLOB){uint32_t at=f->offset++;return at<4?(uint8_t)(f->blob_len>>(8*at)):at-4<f->blob_len?f->blob[at-4]:0;}
    unsigned space=f->selector>>15,key=f->selector&0x3fff;
    if(key>=32)return 0;
    unsigned count=f->len[space][key];
    if(f->offset>=count)return 0;
    return f->data[space][key][f->offset++];
}
