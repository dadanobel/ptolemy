/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Directed test of the CP thread (PtRadeonConfig.cp_thread): the command processor runs beside the caller and every
 * entry point takes the device lock. Each round submits, through CPU VRAM writes and WPTR like a driver, 2D fills
 * (type-0 packets), an indirect buffer with one more fill, and a scratch write with its write-back. While the CP thread
 * works, this thread keeps writing and reading its own VRAM bytes and polling RPTR/RBBM_STATUS/scratch, and a second
 * thread calls the status and DMA-coherence entry points. Every round then checks its fills, the CPU's bytes, RPTR and
 * idle. One reset lands with work in flight; close joins the thread. Run under ThreadSanitizer and AddressSanitizer. */
#include "frontend.h"
#include <assert.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

enum { RB=0x10000, SCRATCH=0x20000, IB=0x30000, FILL=0x100000, MINE=0x200000, PITCH=256, ROUNDS=400,
       CP_RB_BASE=0x700, CP_RB_CNTL=0x704, CP_RB_RPTR=0x710, CP_RB_WPTR=0x714, CP_IB_BASE=0x738, CP_IB_BUFSZ=0x73c,
       SCRATCH_UMSK=0x770, SCRATCH_ADDR=0x774, RBBM_STATUS=0xe40, DST_PITCH_OFFSET=0x142c, DST_Y_X=0x1438,
       DST_HEIGHT_WIDTH=0x143c, DP_GUI_MASTER_CNTL=0x146c, DP_BRUSH_FRGD_CLR=0x147c, SCRATCH_REG0=0x15e0 };
static PtRadeon *d; static uint32_t wptr; static atomic_int done;
static void mmio(uint32_t a,uint32_t v) { uint8_t b[4]={(uint8_t)v,(uint8_t)(v>>8),(uint8_t)(v>>16),(uint8_t)(v>>24)}; assert(!pt_radeon_mmio(d,a,b,4,1)); }
static uint32_t rd(uint32_t a) { uint8_t b[4]; assert(!pt_radeon_mmio(d,a,b,4,0)); return b[0]|b[1]<<8|b[2]<<16|(uint32_t)b[3]<<24; }
static void vw(uint32_t off,uint32_t v) { uint8_t b[4]={(uint8_t)v,(uint8_t)(v>>8),(uint8_t)(v>>16),(uint8_t)(v>>24)}; assert(!pt_radeon_vram(d,off,b,4,1)); }
static uint32_t vr(uint32_t off) { uint8_t b[4]; assert(!pt_radeon_vram(d,off,b,4,0)); return b[0]|b[1]<<8|b[2]<<16|(uint32_t)b[3]<<24; }
static void ring(uint32_t w) { vw(RB+4*(wptr&1023),w); wptr=(wptr+1)&1023; }
static void reg(uint32_t a,uint32_t v) { ring(a>>2); ring(v); }   /* type-0, one register */
static uint32_t color(unsigned r) { return 0x01000193u*r^0x5a5a5a5au; }
static void setup(void) {
    mmio(CP_RB_BASE,RB); mmio(CP_RB_CNTL,9); mmio(SCRATCH_ADDR,SCRATCH); mmio(SCRATCH_UMSK,1); wptr=0;
}
static void submit(unsigned r) {
    uint32_t ib_at=IB+64*(r%8);   /* up to 8 rounds in flight, each with its own IB */
    reg(DP_GUI_MASTER_CNTL,0x00f006d2u);              /* solid fill (ROP 0xF0, brush 13), 32 bpp, DST_PITCH_OFFSET */
    reg(DST_PITCH_OFFSET,(PITCH/64)<<22|FILL>>10); reg(DP_BRUSH_FRGD_CLR,color(r));
    reg(DST_Y_X,(r%64)<<16); reg(DST_HEIGHT_WIDTH,1u<<16|64);
    uint32_t ib[4]={DST_Y_X>>2,(64+r%64)<<16,DST_HEIGHT_WIDTH>>2,1u<<16|64};   /* the IB: one more row */
    for(unsigned i=0;i<4;i++) vw(ib_at+4*i,ib[i]);
    reg(CP_IB_BASE,ib_at); reg(CP_IB_BUFSZ,4);
    reg(SCRATCH_REG0,r);
    mmio(CP_RB_WPTR,wptr);
}
static uint64_t now_ms(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC,&t); return (uint64_t)t.tv_sec*1000+(uint64_t)t.tv_nsec/1000000; }
static unsigned mine; static uint32_t mine_v[4096];   /* the CPU's own VRAM bytes, checked while the CP runs */
static void cpu_work(void) {
    uint32_t off=MINE+4*(mine%4096),v=mine*2654435761u|1; vw(off,v); assert(vr(off)==v); mine_v[mine%4096]=v; mine++;
    (void)rd(CP_RB_RPTR); (void)rd(RBBM_STATUS); (void)vr(SCRATCH);
}
static void check_fills(unsigned r) {
    uint32_t c=color(r),be=c>>24|(c>>8&0xff00)|(c<<8&0xff0000)|c<<24;   /* fills store the brush big-endian */
    for(unsigned y=r%64;y<128;y+=64) for(unsigned x=0;x<64;x++) assert(vr(FILL+y*PITCH+4*x)==be);
}
static void wait_round(unsigned r) {
    uint64_t limit=now_ms()+10000;
    while(vr(SCRATCH)!=r) { cpu_work(); assert(now_ms()<limit); }
    while(!optics_frontend_idle(d)) { cpu_work(); assert(now_ms()<limit); }
    assert(rd(CP_RB_RPTR)==wptr && !(rd(RBBM_STATUS)>>31) && rd(SCRATCH_REG0)==r);
    check_fills(r);
}
static void *other(void *o) {   /* agent status and another device's DMA coherence, from a third thread */
    (void)o; unsigned n=0;
    while(!atomic_load(&done)) { OpticsStatus s; optics_frontend_status(d,&s); n++; assert(!optics_frontend_ram_before(d,4096*(n%16),64,n&1)); }
    return NULL;
}
int main(void) {
    optics_frontend_set_executor(NULL);
    uint8_t *ram=calloc(1,1<<20); assert(ram);
    PtRadeonConfig c={.ram={ram,1<<20,NULL},.cp_thread=1}; assert(!pt_radeon_create(&d,&c));
    pthread_t t; assert(!pthread_create(&t,NULL,other,NULL));
    setup();
    for(unsigned r=1;r<=ROUNDS;r++) {
        if(r==ROUNDS/2) {   /* reset with a long round in flight: the CP thread stops between two packets, a new run follows */
            uint32_t start=wptr; OpticsFrontendStats a,b; optics_frontend_stats(d,&a);
            for(unsigned i=0;i<400;i++) reg(DP_BRUSH_FRGD_CLR,i);
            submit(r); while(rd(CP_RB_RPTR)==start) {}   /* the CP thread is in the middle of it */
            assert(!pt_radeon_reset(d)); optics_frontend_stats(d,&b); setup();
            fprintf(stderr,"reset after %llu of 410 packets\n",(unsigned long long)(b.packets-a.packets));
        }
        if(r%50==0 && r+8<=ROUNDS) {   /* 8 rounds in the ring at once */
            for(unsigned k=0;k<8;k++) submit(r+k);
            wait_round(r+7); for(unsigned k=0;k<7;k++) if((r+k)%64!=(r+7)%64) check_fills(r+k);
            r+=7; continue;
        }
        submit(r); wait_round(r);
    }
    for(unsigned i=0;i<4096;i++) assert(vr(MINE+4*i)==mine_v[i]);
    atomic_store(&done,1); pthread_join(t,NULL);
    OpticsFrontendStats s; optics_frontend_stats(d,&s);
    assert(!s.failed && s.blits>=2*ROUNDS && !s.blits_refused && !s.packets_refused);
    pt_radeon_close(d); free(ram);
    printf("optics-cp-thread: %u rounds, %u CPU accesses beside the CP, reset in flight, close ok\n",ROUNDS,mine); return 0;
}
