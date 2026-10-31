/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "platform.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static void source(PtPic *p,unsigned i,unsigned pr,int level,int mask) {
    pt_pic_write(p,0x10000+i*32,(mask?PT_MASK:0)|(level?PT_LEVEL:0)|(pr<<16)|(0x40+i));
    pt_pic_write(p,0x10010+i*32,1);
}
static void notify(void *opaque) {
    PtPic *p=opaque;
    /* Reentry proves that the callback does not run under the PIC lock. */
    (void)pt_pic_output(p);
}
static void pic_tests(void) {
    PtPic p; pt_pic_init(&p);
    pt_pic_set_notify(&p,notify,&p);
    assert(pt_pic_read(&p,0x200a0)==255);
    pt_pic_write(&p,0x20080,0);
    source(&p,1,5,0,1);
    pt_pic_line(&p,1,0,1); pt_pic_line(&p,1,0,0);
    assert(!pt_pic_output(&p)); source(&p,1,5,0,0);
    assert(pt_pic_output(&p)); assert(pt_pic_read(&p,0x200a0)==0x41);
    assert(!pt_pic_output(&p)); pt_pic_write(&p,0x200b0,0);
    assert(pt_pic_read(&p,0x200a0)==255);
    source(&p,2,6,1,0); pt_pic_line(&p,2,0,1);
    assert(pt_pic_read(&p,0x200a0)==0x42);
    assert(!pt_pic_output(&p)); pt_pic_write(&p,0x200b0,0);
    assert(pt_pic_output(&p));
    /* Shared line remains asserted after either producer deasserts. */
    pt_pic_line(&p,2,1,1); pt_pic_line(&p,2,0,0); assert(pt_pic_output(&p));
    pt_pic_line(&p,2,1,0); assert(!pt_pic_output(&p));
    source(&p,1,5,0,0); pt_pic_line(&p,1,0,1);
    pt_pic_write(&p,0x20080,5); assert(!pt_pic_output(&p));
    pt_pic_write(&p,0x20080,4); assert(pt_pic_read(&p,0x200a0)==0x41);
    source(&p,3,7,0,0); pt_pic_line(&p,3,0,1);
    assert(pt_pic_read(&p,0x200a0)==0x43);
    pt_pic_write(&p,0x200b0,0); assert(p.servicing==(1ull<<1));
    pt_pic_write(&p,0x200b0,0); assert(!p.servicing);
    /* An edge arriving in service survives until EOI. */
    pt_pic_line(&p,1,0,0); pt_pic_line(&p,1,0,1);
    assert(pt_pic_read(&p,0x200a0)==0x41);
    pt_pic_line(&p,1,0,0); pt_pic_line(&p,1,0,1);
    assert(!pt_pic_output(&p)); pt_pic_write(&p,0x200b0,0);
    assert(pt_pic_read(&p,0x200a0)==0x41); pt_pic_write(&p,0x200b0,0);
    pt_pic_destroy(&p);
}
static void clock_tests(void) {
    PtClock c; pt_clock_init(&c,100);
    assert(pt_clock_deadline(&c)==UINT64_MAX);
    pt_clock_write_dec(&c,2); pt_clock_update(&c,180);
    assert(pt_clock_deadline(&c)==40);
    assert(pt_clock_dec(&c)==0 && !c.dec_pending);
    pt_clock_update(&c,220); assert(c.dec_pending && pt_clock_dec(&c)==0xffffffffu);
    c.dec_pending=0;
    pt_clock_write_dec(&c,0xffffffff); assert(!c.dec_pending);
    pt_clock_write_dec(&c,7); pt_clock_write_dec(&c,0xffffffff); assert(c.dec_pending);
    c.dec_pending=0; pt_clock_write_dec(&c,10);
    pt_clock_write_tb(&c,0,0xffffffffu); pt_clock_write_tb(&c,1,7);
    pt_clock_update(&c,260);
    assert(pt_clock_tb(&c)==(8ull<<32)); assert(pt_clock_dec(&c)==9);
    pt_clock_pause(&c,260,1);
    assert(pt_clock_deadline(&c)==UINT64_MAX);
    pt_clock_update(&c,1000000260); assert(pt_clock_dec(&c)==9);
    pt_clock_pause(&c,1000000260,0); pt_clock_update(&c,1000000300);
    assert(pt_clock_dec(&c)==8 && pt_clock_tb(&c)==(8ull<<32)+1);
    assert(pt_clock_deadline(&c)==360);
    pt_clock_update(&c,1); assert(pt_clock_dec(&c)==8);
    /* The virtual uptime across manual pauses (host ns in, as the stand's CPU owner): TB and the DEC
     * remainder continue, the DEC's host deadline moves by the pause, nothing fires while paused, pauses add up. */
    { PtVtime v={0}; PtClock k; pt_clock_init(&k,pt_vtime_now(&v,1000)); pt_clock_write_dec(&k,250);
      pt_clock_update(&k,pt_vtime_now(&v,5000)); uint64_t tb=pt_clock_tb(&k);
      assert(pt_clock_dec(&k)==150 && pt_clock_deadline(&k)==6040 && pt_vtime_host(&v,5000+6040)==11040);
      pt_vtime_pause(&v,5000); pt_vtime_pause(&v,7000);                       /* a second request is the same pause */
      assert(pt_vtime_host(&v,11040)==UINT64_MAX && pt_vtime_now(&v,3000000005000ull)==5000);
      pt_clock_update(&k,pt_vtime_now(&v,3000000005000ull)); assert(pt_clock_tb(&k)==tb && pt_clock_dec(&k)==150 && !k.dec_pending);
      assert(pt_vtime_resume(&v,3000000005000ull)==3000000000000ull && pt_vtime_resume(&v,3000000006000ull)==0);
      assert(pt_vtime_now(&v,3000000005000ull)==5000 && pt_vtime_host(&v,11040)==3000000011040ull);   /* 6040 ns left */
      pt_clock_update(&k,pt_vtime_now(&v,3000000011039ull)); assert(!k.dec_pending);
      pt_clock_update(&k,pt_vtime_now(&v,3000000011040ull)); assert(k.dec_pending && pt_clock_tb(&k)==tb+151);
      pt_vtime_pause(&v,4000000000000ull); assert(pt_vtime_resume(&v,6000000000000ull)==2000000000000ull);
      assert(pt_vtime_now(&v,6000000000000ull)==1000000000000ull && pt_vtime_host(&v,UINT64_MAX)==UINT64_MAX); }
    /* Full wrap: a second 0 -> -1 transition must produce a new event. */
    pt_clock_init(&c,0); pt_clock_write_dec(&c,0); pt_clock_update(&c,40);
    assert(c.dec_pending); c.dec_pending=0;
    pt_clock_update(&c,40*((1ull<<32)+1)); assert(c.dec_pending);
}
static void fw_tests(void) {
    PtFwCfg f; pt_fw_init(&f,1ull<<30);
    const char *sig="PTOL"; for(int i=0;i<4;i++) assert(pt_fw_read(&f)==sig[i]);
    assert(!pt_fw_read(&f)); pt_fw_select(&f,0); assert(pt_fw_read(&f)=='P');
    pt_fw_select(&f,3); uint8_t b[8]; for(int i=0;i<8;i++) b[i]=pt_fw_read(&f);
    assert(pt_read_le(b,8)==1ull<<30);
    pt_fw_select(&f,0x8003); for(int i=0;i<4;i++) b[i]=pt_fw_read(&f);
    assert(pt_read_le(b,4)==25000000);
    pt_fw_select(&f,0x3fff); for(int i=0;i<20;i++) assert(!pt_fw_read(&f));
}
static int access_memory(void *p,uint32_t a,uint8_t *b,unsigned n,int w) {
    if(w) memcpy((uint8_t *)p+a,b,n); else memcpy(b,(uint8_t *)p+a,n); return 0;
}
static void bus_tests(void) {
    PtBus b={0}; uint8_t mem[16]={0}, x[8]={1,2,3,4};
    assert(!pt_bus_add(&b,(PtRegion){0xfffffff0,16,access_memory,mem,"test"}));
    assert(pt_bus_add(&b,(PtRegion){0xfffffff8,16,access_memory,mem,"overflow"})<0);
    assert(pt_bus_add(&b,(PtRegion){0xfffffff0,4,access_memory,mem,"overlap"})<0);
    assert(!pt_bus_access(&b,0xfffffffc,x,4,1));
    memset(x,0,8); assert(!pt_bus_access(&b,0xfffffffc,x,4,0));
    assert(pt_read_be(x,4)==0x01020304); assert(pt_read_le(x,4)==0x04030201);
    assert(pt_bus_access(&b,0xfffffffc,x,8,0)<0);
}
typedef struct { PtEvent event; _Atomic int value; } Publication;
static void *producer(void *v) {
    Publication *p=v; atomic_store_explicit(&p->value,42,memory_order_release);
    pt_event_signal(&p->event); return 0;
}
static void wake_tests(void) {
    Publication p; pt_event_init(&p.event); atomic_init(&p.value,0);
    for(int i=0;i<1000;i++) {
        uint64_t g=pt_event_generation(&p.event);
        pt_event_signal(&p.event);
        assert(pt_event_wait(&p.event,g,1000000)==1);
    }
    uint64_t g=pt_event_generation(&p.event); pthread_t t;
    assert(!pthread_create(&t,0,producer,&p));
    assert(pt_event_wait(&p.event,g,UINT64_MAX)==1);
    assert(atomic_load_explicit(&p.value,memory_order_acquire)==42);
    pthread_join(t,0);
    assert(pt_event_wait(&p.event,pt_event_generation(&p.event),1000000)==0);
    pt_event_destroy(&p.event);
}
int main(void) {
    pic_tests(); clock_tests(); fw_tests(); bus_tests(); wake_tests();
    puts("PASS: PIC, TB/DEC, fw_cfg, MMIO bounds/endian, wakeup/publication");
}
