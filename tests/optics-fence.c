/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Directed test of the frontend's deferred guest completion (timeline.c): a type-0 packet that writes a register
 * with a side effect (DST_HEIGHT_WIDTH starts a 2D operation) and then SCRATCH_REG0 (with its memory write-back),
 * after a GPU job that has not completed. The packet is accepted once (its other registers run exactly once, RPTR
 * moves on), the scratch value is published only after the job's batch completed successfully, never after a failed
 * batch (the device fails), a full completion queue holds the whole packet unexecuted, and reset clears a failure.
 * That is the deferred mode (OPTICS_EARLY_FENCE=0). Argument "early": the default, the CP publishes the scratch value
 * when it reaches it, with the job still running and without asking the executor. */
#include "frontend.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { RB=0x10000, SCRATCH=0x20000, CP_RB_BASE=0x700, CP_RB_CNTL=0x704, CP_RB_RPTR=0x710, CP_RB_WPTR=0x714,
       SCRATCH_UMSK=0x770, SCRATCH_ADDR=0x774, RBBM_STATUS=0xe40, DST_HEIGHT_WIDTH=0x143c, SCRATCH_REG0=0x15e0 };
/* A fake executor holding submitted timeline tokens until the test completes them (mode). */
static int mode; static unsigned calls,held; static PtR2TimelineToken pend[256];
static void settle(PtRadeon *d) {
    if(mode==OPTICS_FENCE_PENDING) return;
    for(unsigned i=0;i<held;i++) optics_frontend_timeline_complete(d,pend[i],mode==OPTICS_FENCE_DONE); held=0;
}
static int fence(void *o,PtRadeon *d) {
    (void)o; calls++; PtR2TimelineToken k; if(optics_frontend_timeline_submit(d,&k)) pend[held++]=k;
    int any=held!=0; settle(d); return mode==OPTICS_FENCE_FAILED && any?OPTICS_FENCE_FAILED:held?OPTICS_FENCE_PENDING:OPTICS_FENCE_DONE;
}
static int poll_(void *o,PtRadeon *d) { (void)o; settle(d); return 0; }
static unsigned asked; static uint32_t asked_query;
static int count_(void *o,PtRadeon *d,uint32_t q,uint64_t *n) { (void)o; (void)d; asked++; asked_query=q; *n=77; return 0; }
static void mmio(PtRadeon *d,uint32_t a,uint32_t v) { uint8_t b[4]={(uint8_t)v,(uint8_t)(v>>8),(uint8_t)(v>>16),(uint8_t)(v>>24)}; assert(!pt_radeon_mmio(d,a,b,4,1)); }
static uint32_t rd(PtRadeon *d,uint32_t a) { uint8_t b[4]; assert(!pt_radeon_mmio(d,a,b,4,0)); return b[0]|b[1]<<8|b[2]<<16|(uint32_t)b[3]<<24; }
static uint32_t vram32(PtRadeon *d,uint32_t off) { uint8_t b[4]; assert(!pt_radeon_vram(d,off,b,4,0)); return b[0]|b[1]<<8|b[2]<<16|(uint32_t)b[3]<<24; }
static uint32_t wptr;
static void ring(PtRadeon *d,uint32_t w) { uint8_t b[4]={(uint8_t)w,(uint8_t)(w>>8),(uint8_t)(w>>16),(uint8_t)(w>>24)}; assert(!pt_radeon_vram(d,RB+4*wptr++,b,4,1)); }
/* One type-0 packet from DST_HEIGHT_WIDTH through SCRATCH_REG0: zeros, then the scratch value. */
static void packet(PtRadeon *d,uint32_t value) {
    uint32_t n=(SCRATCH_REG0-DST_HEIGHT_WIDTH)/4+1; ring(d,(n-1)<<16|DST_HEIGHT_WIDTH>>2);
    for(uint32_t i=0;i+1<n;i++) ring(d,0); ring(d,value);
}
int main(int argc,char **argv) {
    int early=argc>1 && !strcmp(argv[1],"early"); if(!early) setenv("OPTICS_EARLY_FENCE","0",1);
    static const OpticsExecutor ex={.fence=fence,.poll=poll_}; optics_frontend_set_executor(&ex);
    uint8_t *ram=calloc(1,1<<20); assert(ram);
    PtRadeonConfig c={.ram={ram,1<<20,NULL}}; PtRadeon *d; assert(!pt_radeon_create(&d,&c));
    mmio(d,CP_RB_BASE,RB); mmio(d,CP_RB_CNTL,9); mmio(d,SCRATCH_ADDR,SCRATCH); mmio(d,SCRATCH_UMSK,1);
    OpticsFrontendStats s;
    if(early) {
        mode=OPTICS_FENCE_PENDING; assert(optics_frontend_job(d)); packet(d,0x1234); mmio(d,CP_RB_WPTR,wptr); pt_radeon_service(d,1000000);
        optics_frontend_stats(d,&s);
        assert(vram32(d,SCRATCH)==0x1234 && rd(d,SCRATCH_REG0)==0x1234 && rd(d,CP_RB_RPTR)==wptr && !calls && !s.completions_deferred && !s.failed);
        /* An occlusion query as Tiger's ATI driver ends it (Halo spun on the -1 left in its result words):
         * ZB_ZPASS_DATA starts the count, per pipe SU_REG_DEST = 1 << i and ZB_ZPASS_ADDR; pipe 0 carries the count
         * (here the start value 5, no draws), the other pipes 0, in the byte order the driver's lwz reads. */
        enum { SU_REG_DEST=0x42c8, ZB_ZPASS_DATA=0x4f58, ZB_ZPASS_ADDR=0x4f5c, QUERY=0x30000 };
        uint8_t ff[8]; memset(ff,0xff,8); assert(!pt_radeon_vram(d,QUERY,ff,8,1));
        mmio(d,ZB_ZPASS_DATA,5); mmio(d,SU_REG_DEST,1); mmio(d,ZB_ZPASS_ADDR,QUERY); mmio(d,SU_REG_DEST,2); mmio(d,ZB_ZPASS_ADDR,QUERY+4); mmio(d,SU_REG_DEST,15);
        assert(vram32(d,QUERY)==0x05000000u && vram32(d,QUERY+4)==0);
        /* With an executor that counts (query hook): pipe 0's word waits for the fence over the query's draws, then
         * carries the executor's count for that query; pipe 1's word is 0 at once. */
        static const OpticsExecutor counting={.fence=fence,.poll=poll_,.query=count_}; optics_frontend_set_executor(&counting);
        assert(!pt_radeon_vram(d,QUERY,ff,8,1)); mode=OPTICS_FENCE_PENDING; assert(optics_frontend_job(d));
        mmio(d,ZB_ZPASS_DATA,0); mmio(d,SU_REG_DEST,1); mmio(d,ZB_ZPASS_ADDR,QUERY); mmio(d,SU_REG_DEST,2); mmio(d,ZB_ZPASS_ADDR,QUERY+4); mmio(d,SU_REG_DEST,15);
        pt_radeon_service(d,7000000);
        assert(vram32(d,QUERY)==0xffffffffu && vram32(d,QUERY+4)==0 && !asked);   /* GPU still busy: no word, no question */
        mode=OPTICS_FENCE_DONE; pt_radeon_service(d,8000000);
        assert(asked==1 && asked_query && vram32(d,QUERY)==0x4d000000u);   /* 77, read by lwz */
        /* From the CP, as the driver sends it, with the GPU still busy: every packet is accepted, pipe 1's word is 0 at
         * once, pipe 0's word waits for the GPU, and a SCRATCH stamp after it is deferred behind it although stamps are
         * otherwise published early (the driver recycles the word's page on the stamp; an early stamp hung Halo). */
        assert(!pt_radeon_vram(d,QUERY+8,ff,8,1)); mode=OPTICS_FENCE_PENDING; assert(optics_frontend_job(d));
        const uint32_t reg[7][2]={{ZB_ZPASS_DATA,0},{SU_REG_DEST,1},{ZB_ZPASS_ADDR,QUERY+8},{SU_REG_DEST,2},{ZB_ZPASS_ADDR,QUERY+12},{SU_REG_DEST,15},{SCRATCH_REG0,0x5678}};
        for(int i=0;i<7;i++){ ring(d,reg[i][0]>>2); ring(d,reg[i][1]); }
        mmio(d,CP_RB_WPTR,wptr); pt_radeon_service(d,9000000);
        assert(rd(d,CP_RB_RPTR)==wptr && vram32(d,QUERY+8)==0xffffffffu && vram32(d,QUERY+12)==0 && vram32(d,SCRATCH)==0x1234 && asked==1);
        mode=OPTICS_FENCE_DONE; pt_radeon_service(d,10000000);
        assert(asked==2 && vram32(d,QUERY+8)==0x4d000000u && vram32(d,SCRATCH)==0x5678 && rd(d,SCRATCH_REG0)==0x5678);
        pt_radeon_close(d); free(ram); printf("optics-fence: early completion ok, occlusion query words written, counted query published after its fence, later stamps kept behind it\n"); return 0;
    }
    /* 1. A GPU job still running: the packet is accepted once, its scratch write deferred, the device reports busy. */
    mode=OPTICS_FENCE_PENDING; assert(optics_frontend_job(d)); packet(d,0x1234); mmio(d,CP_RB_WPTR,wptr);
    for(int i=0;i<3;i++) pt_radeon_service(d,1000000ull*(i+1));
    optics_frontend_stats(d,&s);
    assert(vram32(d,SCRATCH)==0 && rd(d,SCRATCH_REG0)==0 && rd(d,CP_RB_RPTR)==wptr && s.blits_refused==1 && s.completions_deferred==1 && !s.failed);
    assert(rd(d,RBBM_STATUS)>>31);
    /* 2. Completion: the scratch value is published, nothing of the packet runs again, the device is idle. */
    mode=OPTICS_FENCE_DONE; pt_radeon_service(d,5000000);
    optics_frontend_stats(d,&s);
    assert(vram32(d,SCRATCH)==0x1234 && rd(d,SCRATCH_REG0)==0x1234 && s.blits_refused==1 && !s.failed);
    assert(!(rd(d,RBBM_STATUS)>>31));
    /* 3. A full completion queue holds the next packet whole (no prefix runs, RPTR stays before it). */
    mode=OPTICS_FENCE_PENDING; assert(optics_frontend_job(d));
    for(int i=0;i<64;i++){ ring(d,SCRATCH_REG0>>2); ring(d,0x100+i); }
    uint32_t before=wptr; packet(d,0x2222); mmio(d,CP_RB_WPTR,wptr); pt_radeon_service(d,5500000);
    optics_frontend_stats(d,&s);
    assert(rd(d,CP_RB_RPTR)==before && s.blits_refused==1 && s.completions_deferred==65 && s.fence_waits==1 && vram32(d,SCRATCH)==0x1234);
    mode=OPTICS_FENCE_DONE; pt_radeon_service(d,5600000); pt_radeon_service(d,5700000);
    optics_frontend_stats(d,&s);   /* in order: the 64 single writes, then the held packet (published at once: nothing pending) */
    assert(rd(d,CP_RB_RPTR)==wptr && s.blits_refused==2 && vram32(d,SCRATCH)==0x2222 && !(rd(d,RBBM_STATUS)>>31));
    /* 4. A failed GPU batch never becomes success: the scratch is not published and the CP stops. */
    mode=OPTICS_FENCE_PENDING; assert(optics_frontend_job(d)); packet(d,0x5678); mmio(d,CP_RB_WPTR,wptr); pt_radeon_service(d,6000000);
    mode=OPTICS_FENCE_FAILED; pt_radeon_service(d,6500000);
    optics_frontend_stats(d,&s);
    assert(vram32(d,SCRATCH)==0x2222 && s.failed);
    mode=OPTICS_FENCE_DONE; pt_radeon_service(d,7000000); assert(vram32(d,SCRATCH)==0x2222);   /* stays stopped */
    /* 5. Reset clears the failure (a new run). */
    assert(!pt_radeon_reset(d)); optics_frontend_stats(d,&s); assert(!s.failed);
    pt_radeon_close(d); free(ram);
    printf("optics-fence: ok (%u fence calls)\n",calls); return 0;
}
