/* SPDX-License-Identifier: GPL-2.0-or-later
 * Protocol checks on private RAM and a NEW scratch disk, never a VM image. */
#include "devices.h"
#include <assert.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
static uint8_t ram[65536];
static unsigned dma_irqs; static int ata_line;
static void dma_irq(void *p) { (void)p; dma_irqs++; }
static void level(void *p,int v) { *(int *)p=v; }
static void desc(unsigned address,unsigned count,unsigned cmd,unsigned data,unsigned next) {
    memset(ram+address,0,16); pt_write_le(ram+address,2,count); pt_write_le(ram+address+2,2,cmd);
    pt_write_le(ram+address+4,4,data); pt_write_le(ram+address+8,4,next);
}
static void start(PtDma *d,unsigned pointer) {
    pt_dma_write(d,0,((uint32_t)PT_DMA_RUN<<16)); pt_dma_write(d,12,pointer);
    pt_dma_write(d,0,((uint32_t)PT_DMA_RUN<<16)|PT_DMA_RUN);
}
static void dma_test(void) {
    PtDma d; PtDmaSlice s; pt_dma_init(&d,(PtRam){ram,sizeof ram,NULL},NULL,dma_irq,NULL);
    desc(0x100,8,0x1030,0x200,0); desc(0x110,0,0x7000,0,0); start(&d,0x100);
    assert(pt_dma_next(&d,&s)==1 && s.address==0x200 && s.length==8 && !s.input);
    assert(!pt_dma_advance(&d,s.generation,3)); uint64_t old=s.generation;
    pt_dma_write(&d,0,PT_DMA_PAUSE<<16|PT_DMA_PAUSE);
    assert(pt_read_le(ram+0x10c,2)==5 && pt_dma_next(&d,&s)==0);
    assert(pt_dma_advance(&d,old,1)==-1);
    pt_dma_write(&d,0,PT_DMA_PAUSE<<16);
    assert(pt_dma_next(&d,&s)==1 && s.address==0x203 && s.length==5);
    assert(!pt_dma_advance(&d,s.generation,5)); assert(dma_irqs==1);
    assert(pt_read_le(ram+0x10c,2)==0 && (pt_read_le(ram+0x10e,2)&PT_DMA_ACTIVE));
    assert(pt_dma_next(&d,&s)==0 && !(d.regs[1]&PT_DMA_ACTIVE));
    /* WAIT holds completion, never repeats the already transferred bytes. */
    desc(0x100,4,0x1031,0x200,0); pt_dma_write(&d,24,0x10000); start(&d,0x100);
    assert(pt_dma_next(&d,&s)==1); assert(!pt_dma_advance(&d,s.generation,4));
    assert(pt_dma_next(&d,&s)==0 && d.loaded && d.transferred);
    pt_dma_device_status(&d,1); assert(pt_dma_next(&d,&s)==0 && dma_irqs==2);
    /* Branch-to-self is bounded and invalid stream addresses fail closed. */
    desc(0x100,0,0x600c,0,0x100); start(&d,0x100); assert(pt_dma_next(&d,&s)==0);
    desc(0x100,8,0x1000,UINT32_MAX-3,0); start(&d,0x100);
    assert(pt_dma_next(&d,&s)==-1 && d.regs[1]&PT_DMA_DEAD);
}
static void aw(PtAta *a,unsigned off,uint8_t v) { assert(!pt_ata_mmio(a,off,&v,1,1)); }
static unsigned ar(PtAta *a,unsigned off) { uint8_t v; assert(!pt_ata_mmio(a,off,&v,1,0)); return v; }
static void ata_command(PtAta *a,unsigned cmd,unsigned lba) {
    aw(a,0x60,0xe0); aw(a,0x20,1); aw(a,0x30,lba); aw(a,0x40,0); aw(a,0x50,0); aw(a,0x70,cmd);
}
static uint64_t ns(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC,&t); return (uint64_t)t.tv_sec*1000000000+t.tv_nsec; }
static void await_ata(PtAta *a,int drq) {
    uint64_t end=ns()+5000000000ull;
    do {
        pt_ata_service(a);
        if(!(ar(a,0x160)&0x80) && (drq?!!(a->status&8):!a->busy && !a->queued)) return;
        usleep(100); /* Test-only wait, never used in MMIO. */
    } while(ns()<end);
    assert(!"ATA completion timeout");
}
static void ata_test(void) {
    char path[]="/tmp/ptolemy-ata-XXXXXX"; int fd=mkstemp(path); assert(fd>=0);
    assert(!ftruncate(fd,16384));
    uint8_t expected[512],data[512]; for(unsigned i=0;i<512;i++) expected[i]=i*19;
    assert(pwrite(fd,expected,512,512)==512);
    PtEvent e; pt_event_init(&e); PtBlock *b=pt_block_open(path,1,&e); assert(b);
    _Atomic uint32_t kick=0; pt_block_set_kick(b,&kick);
    PtDma d; PtAta a; pt_dma_init(&d,(PtRam){ram,sizeof ram,NULL},NULL,dma_irq,NULL);
    pt_ata_init(&a,b,&d,level,&ata_line);
    aw(&a,0x70,0xec); assert(ata_line && (ar(&a,0x160)&8) && ata_line);
    assert(ar(&a,0x70)&8); assert(!ata_line);
    for(unsigned i=0;i<512;i+=2) assert(!pt_ata_mmio(&a,0,data+i,2,0));
    assert((pt_read_le(data+98,2)&0x300)==0x300 && !(pt_read_le(data+166,2)&(1<<10)));
    ata_command(&a,0x20,1); await_ata(&a,1);
    assert(atomic_load(&kick) && ata_line);
    for(unsigned i=0;i<512;i+=4) assert(!pt_ata_mmio(&a,0,data+i,4,0));
    assert(!memcmp(data,expected,512) && !a.busy);
    ata_command(&a,0x30,2);
    for(unsigned i=0;i<512;i+=2) assert(!pt_ata_mmio(&a,0,expected+i,2,1));
    await_ata(&a,0); assert(!(a.status&1)); assert(pread(fd,data,512,1024)==512 && !memcmp(data,expected,512));
    /* Scatter DMA crosses descriptors, publication before descriptor completion. */
    desc(0x100,137,0x2000,0x2000,0); desc(0x110,375,0x3030,0x3000,0); desc(0x120,0,0x7000,0,0);
    start(&d,0x100); ata_command(&a,0xc8,1); await_ata(&a,0);
    assert(!memcmp(ram+0x2000,expected,137) && !memcmp(ram+0x3000,expected+137,375));
    assert(pt_read_le(ram+0x11c,2)==0 && ata_line);
    pt_ata_dma_interrupt(&a); uint8_t reg[4]; assert(!pt_ata_mmio(&a,0x300,reg,4,0));
    assert(pt_read_le(reg,4)==0xc0000000u); pt_write_le(reg,4,0x80000000);
    assert(!pt_ata_mmio(&a,0x300,reg,4,1) && !a.dma_latch && ata_line);
    /* Queued reset must drain the old request before accepting another one. */
    ata_command(&a,0x20,1); pt_ata_service(&a); assert(a.queued);
    uint64_t generation=a.generation; aw(&a,0x160,4); aw(&a,0x160,0);
    assert(a.generation>generation && a.queued && !ata_line);
    aw(&a,0x70,0xec); assert(a.queued && !(a.status&8)); await_ata(&a,0); assert(!ata_line);
    ata_command(&a,0x20,32); assert(a.status&1 && a.error==0x10);
    pt_block_close(b);
    b=pt_block_open(path,0,&e); assert(b); pt_ata_init(&a,b,&d,level,&ata_line);
    ata_command(&a,0x30,2);
    for(unsigned i=0;i<512;i+=4) assert(!pt_ata_mmio(&a,0,expected+i,4,1));
    await_ata(&a,0); assert(a.status&1);
    pt_block_close(b); pt_event_destroy(&e); close(fd); assert(!unlink(path));
}
static void setup(PtHid *h,unsigned type,unsigned req,unsigned value,unsigned len) {
    uint8_t b[8]={type,req}; pt_write_le(b+2,2,value); pt_write_le(b+6,2,len);
    assert(pt_hid_packet(h,0,PT_USB_SETUP,b,8,0)==8);
}
static void hid_test(void) {
    PtHid h; uint8_t b[64]; pt_hid_init(&h,PT_HID_KEYBOARD);
    setup(&h,0,5,9,0); assert(h.address==0); assert(!pt_hid_packet(&h,0,PT_USB_IN,b,0,0) && h.address==9);
    setup(&h,0,9,1,0); assert(!pt_hid_packet(&h,0,PT_USB_IN,b,0,0));
    pt_hid_key(&h,4,1); pt_hid_key(&h,4,0);
    assert(pt_hid_packet(&h,1,PT_USB_IN,b,8,1)==8 && b[2]==4);
    assert(pt_hid_packet(&h,1,PT_USB_IN,b,8,2)==8 && b[2]==0);
    for(unsigned i=4;i<11;i++) pt_hid_key(&h,i,1);
    for(unsigned i=0;i<7;i++) assert(pt_hid_packet(&h,1,PT_USB_IN,b,8,3)==8);
    for(unsigned i=2;i<8;i++) assert(b[i]==1);
    pt_hid_release(&h); assert(pt_hid_packet(&h,1,PT_USB_IN,b,8,4)==8 && !b[2]);
    pt_hid_destroy(&h); pt_hid_init(&h,PT_HID_MOUSE); h.configuration=1;
    pt_hid_pointer(&h,1,300,-1,0);
    assert(pt_hid_packet(&h,1,PT_USB_IN,b,8,0)==4 && b[1]==127 && b[2]==255);
    assert(pt_hid_packet(&h,1,PT_USB_IN,b,8,1)==4 && b[1]==127 && !b[2]);
    assert(pt_hid_packet(&h,1,PT_USB_IN,b,8,2)==4 && b[1]==46);
    assert(pt_hid_packet(&h,1,PT_USB_IN,b,8,3)==PT_USB_NAK); pt_hid_destroy(&h);
}
static void td(unsigned a,unsigned flags,unsigned cbp,unsigned next,unsigned end) {
    pt_write_le(ram+a,4,flags); pt_write_le(ram+a+4,4,cbp);
    pt_write_le(ram+a+8,4,next); pt_write_le(ram+a+12,4,end);
}
static void ohci_test(void) {
    PtHid h[3]; for(unsigned i=0;i<3;i++) pt_hid_init(&h[i],i);
    PtOhci o; int line=0; pt_ohci_init(&o,(PtRam){ram,sizeof ram,NULL},h,h+1,h+2,level,&line);
    pt_ohci_write(&o,0x54,16); for(unsigned i=0;i<10;i++) pt_ohci_frame(&o);
    assert((pt_ohci_read(&o,0x54)&0x13)==3);
    memset(ram+0x1000,0,256); pt_ohci_write(&o,0x18,0x1000);
    /* SET_ADDRESS on address 0 via real setup and status TDs. */
    uint8_t req[8]={0,5,7,0,0,0,0,0}; memcpy(ram+0x3000,req,8);
    td(0x2100,0xf0000000,0x3000,0x2110,0x3007);
    td(0x2110,0xf0000000|(2<<19)|(3<<24),0,0x2120,0);
    td(0x2000,8<<16,0x2120,0x2100,0);
    pt_ohci_write(&o,0x20,0x2000); pt_ohci_write(&o,0x10,0x80000002);
    pt_ohci_write(&o,4,0x90); pt_ohci_write(&o,8,2); pt_ohci_frame(&o);
    assert(h[0].address==7 && line && (pt_read_le(ram+0x1084,4)&~1u)==0x2110);
    assert((pt_read_le(ram+0x2100,4)>>28)==0 && (pt_read_le(ram+0x2110,4)>>28)==0);
    pt_ohci_write(&o,12,2); assert(!line);
    /* Keyboard report crossing two non-contiguous physical pages. */
    h[0].configuration=1; pt_hid_key(h,4,1);
    td(0x2100,0xf0000000|(2<<19),0x4ffc,0x2120,0x6003);
    td(0x2000,(8<<16)|(1<<7)|7,0x2120,0x2100,0);
    for(unsigned i=0;i<32;i++) pt_write_le(ram+0x1000+4*i,4,0x2000);
    pt_ohci_write(&o,4,0x84); pt_ohci_frame(&o);
    assert(ram[0x4ffe]==4 && ram[0x6000]==0 && line);
    /* PCI bus-master disable prevents any DMA, then invalid ED raises UE. */
    pt_write_le(ram+0x1084,4,0x1234); o.bus_master=0; pt_ohci_frame(&o);
    assert(pt_read_le(ram+0x1084,4)==0x1234);
    o.bus_master=1; for(unsigned i=0;i<32;i++) pt_write_le(ram+0x1000+4*i,4,0xfffffff0);
    pt_ohci_frame(&o); assert(pt_ohci_read(&o,12)&16);
    for(unsigned i=0;i<3;i++) pt_hid_destroy(&h[i]);
}
/* The play-head (PH) contract of include/audio.h, on a fake clock. */
static uint64_t irq_times[64]; static unsigned irq_count; static PtScreamer *irq_sound;
static void ph_irq(void *p) { (void)p; if(irq_count<64) irq_times[irq_count]=irq_sound->now; irq_count++; }
typedef struct { uint32_t counter; unsigned irqs; uint64_t first_irq,last_irq; uint16_t residual; } PhRun;
/* A 4-descriptor ring of 32 frames each (16-bit BE stereo); the last interrupts and branches back. */
static void ph_ring(void) {
    for(unsigned i=0;i<4;i++) desc(0x400+16*i,128,i==3?0x003c:0x0000,0x4000+128*i,0x400);
    for(unsigned i=0;i<512;i++) ram[0x4000+i]=(uint8_t)i;
}
static PhRun ph_run(unsigned latency) {
    PtPcm p; pt_pcm_init(&p,latency); PtDma d; PtScreamer s; float out[1024];
    pt_dma_init(&d,(PtRam){ram,sizeof ram,NULL},NULL,ph_irq,NULL); pt_screamer_init(&s,&d,&p); irq_sound=&s; irq_count=0;
    ph_ring(); uint64_t t0=1000000000ull;
    start(&d,0x400); pt_screamer_service(&s,t0);                       /* PH starts at RUN */
    for(uint64_t t=t0+1000000;t<=t0+100000000;t+=1000000) {
        pt_screamer_service(&s,t);
        if(t==t0+10000000) assert(pt_screamer_read(&s,0x50,t)==441);   /* round(10 ms x 44100) */
    }
    /* 100 ms: 4410 frames = 34 wraps of 128 frames, the first after 128 frames = 2.902 ms */
    assert(pt_screamer_read(&s,0x50,t0+100000000)==4410 && irq_count==34);
    /* frame 128 is at 2.9025 ms; PH places frames within a service step to about one frame (22.7 us) */
    assert(irq_times[0]>=t0+2902494-25000 && irq_times[0]<=t0+2902494+25000);
    for(unsigned i=1;i<34;i++) { uint64_t dt=irq_times[i]-irq_times[i-1]; assert(dt>=2902494-50000 && dt<=2902494+50000); }
    pt_pcm_render(&p,out,512); pt_pcm_render(&p,out,512);              /* the host consumes (or not): no effect */
    PhRun r; memset(&r,0,sizeof r);
    r.counter=pt_screamer_read(&s,0x50,t0+100000000); r.irqs=irq_count; r.first_irq=irq_times[0]; r.last_irq=irq_times[33];
    r.residual=(uint16_t)pt_read_le(ram+0x400+16*3+12,2);
    return r;
}
static void audio_test(void) {
    /* FIFO: priming, underrun primes again, a full FIFO drops and counts. */
    PtPcm p; pt_pcm_init(&p,2); float out[16]; atomic_store(&p.stopped,0);
    assert(pt_pcm_push(&p,0.25f,-0.5f)); pt_pcm_render(&p,out,2);
    assert(!out[0] && pt_pcm_queued(&p)==1 && !atomic_load(&p.underruns));   /* priming: silence, nothing taken */
    assert(pt_pcm_push(&p,0.75f,0.75f)); pt_pcm_render(&p,out,3);
    assert(out[0]==0.25f && out[1]==-0.5f && out[2]==0.75f && !out[4]);
    assert(atomic_load(&p.underruns)==1 && atomic_load(&p.missing_frames)==1 && !atomic_load(&p.primed));
    /* Running dry after the play head stopped is the stream's end, not an underrun. */
    pt_pcm_push(&p,0,0); pt_pcm_push(&p,0,0); pt_pcm_render(&p,out,2); atomic_store(&p.stopped,1); pt_pcm_render(&p,out,2);
    assert(atomic_load(&p.underruns)==1 && atomic_load(&p.tails)==1);
    for(unsigned i=0;i<PT_PCM_CAPACITY;i++) assert(pt_pcm_push(&p,0,0));
    assert(!pt_pcm_push(&p,1,1) && atomic_load(&p.dropped)==1);
    /* PH: counter and wrap interrupts follow time, identically for any host latency. */
    PhRun a=ph_run(256),b=ph_run(1024),c=ph_run(3072);
    assert(!memcmp(&a,&b,sizeof a) && !memcmp(&a,&c,sizeof a));
    /* 0x50: the guest's write sets it; a host underrun does not move it; a stall is bounded and counted. */
    pt_pcm_init(&p,64); PtDma d; PtScreamer s;
    pt_dma_init(&d,(PtRam){ram,sizeof ram,NULL},NULL,ph_irq,NULL); pt_screamer_init(&s,&d,&p); irq_sound=&s;
    ph_ring(); uint64_t t=5000000000ull; start(&d,0x400); pt_screamer_service(&s,t);
    t+=1000000; assert(pt_screamer_read(&s,0x50,t)==44 && !atomic_load(&p.stopped)); pt_screamer_write(&s,0x50,0,t); assert(pt_screamer_read(&s,0x50,t)==0);
    for(unsigned i=0;i<8;i++) pt_pcm_render(&p,out,8);
    assert(pt_screamer_read(&s,0x50,t)==0);
    t+=2000000000ull; pt_screamer_service(&s,t); assert(s.consumed==44+16384 && s.skipped==88200-16384);
    /* Rate 22050: half the guest frames, the same host frames (zero-order hold). */
    pt_pcm_drain(&p); unsigned w=atomic_load(&p.write); uint32_t n0=pt_screamer_read(&s,0x50,t);
    pt_screamer_write(&s,0,2u<<8,t); t+=10000000; pt_screamer_service(&s,t);
    uint32_t got=pt_screamer_read(&s,0x50,t)-n0;
    assert((got==220 || got==221) && atomic_load(&p.write)-w==2*got);
    /* STOP: PH halts. */
    pt_dma_write(&d,0,(uint32_t)PT_DMA_RUN<<16); n0=pt_screamer_read(&s,0x50,t);
    t+=10000000; assert(pt_screamer_read(&s,0x50,t)==n0 && atomic_load(&p.stopped));
    /* A stereo frame split across two descriptors, BE, then the program stops. */
    pt_pcm_init(&p,1); pt_dma_init(&d,(PtRam){ram,sizeof ram,NULL},NULL,dma_irq,NULL); pt_screamer_init(&s,&d,&p);
    memcpy(ram+0x3000,"\x40\x00\xc0\x00",4);
    desc(0x100,1,0,0x3000,0); desc(0x110,3,0x1030,0x3001,0); desc(0x120,0,0x7000,0,0);
    start(&d,0x100); pt_screamer_service(&s,t); pt_screamer_service(&s,t+1000000);
    assert(pt_screamer_read(&s,0x50,t+1000000)==1 && pt_pcm_queued(&p)==1);
    pt_pcm_render(&p,out,1); assert(out[0]==0.5f && out[1]==-0.5f);
    assert(!(d.regs[1]&PT_DMA_ACTIVE));
}
int main(void) {
    dma_test(); ata_test(); hid_test(); ohci_test(); audio_test();
    puts("PASS: DBDMA pause/WAIT/bounds; ATA PIO/scatter DMA/reset/errors; HID reports; OHCI TDs; PCM FIFO/Screamer play head");
}
