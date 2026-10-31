/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Device access contract (include/dma.h): every device access to guest RAM is announced first. RAM is read-only
 * while devices run; an announced write opens its pages, and a page the "GPU" still owns is unmapped and its
 * announcement answers EAGAIN. A device that writes without announcing, or touches an owned page, dies here
 * (SIGSEGV): a bypassed writer cannot pass. */
#include "devices.h"
#include "net.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
static uint8_t *ram;static size_t page,size;static uint32_t owned_lo,owned_hi;static unsigned writes,refusals;
static int before(void *opaque,uint32_t at,size_t n,int write){
    (void)opaque;if(at<owned_hi&&at+n>owned_lo){refusals++;return EAGAIN;}
    if(write){size_t a=at/page*page,b=(at+n+page-1)/page*page;assert(!mprotect(ram+a,b-a,PROT_READ|PROT_WRITE));writes++;}
    return 0;
}
static const PtRamHooks hooks={before,NULL};
static uint32_t P(unsigned k){return (uint32_t)(k*page);}
static void own(unsigned k){owned_lo=P(k);owned_hi=P(k+1);}
static void release(void){assert(!mprotect(ram+owned_lo,page,PROT_READ));owned_lo=owned_hi=0;}
static void arm(void){assert(!mprotect(ram,size,PROT_READ));if(owned_hi)assert(!mprotect(ram+owned_lo,page,PROT_NONE));}
static void disarm(void){assert(!mprotect(ram,size,PROT_READ|PROT_WRITE));}
static void desc(uint32_t at,unsigned count,unsigned cmd,uint32_t address,uint32_t dep){
    memset(ram+at,0,16);pt_write_le(ram+at,2,count);pt_write_le(ram+at+2,2,cmd);pt_write_le(ram+at+4,4,address);pt_write_le(ram+at+8,4,dep);
}
static void run(PtDma *d,uint32_t at){pt_dma_write(d,0,(uint32_t)PT_DMA_RUN<<16);pt_dma_write(d,12,at);pt_dma_write(d,0,(uint32_t)PT_DMA_RUN<<16|PT_DMA_RUN);}
static void aw(PtAta *a,unsigned off,uint8_t v){assert(!pt_ata_mmio(a,off,&v,1,1));}
static void level(void *p,int v){(void)p;(void)v;}
static void dbdma(void){
    PtDma d;pt_dma_init(&d,(PtRam){ram,size,&hooks},NULL,NULL,NULL);PtDmaSlice s;
    desc(P(0),4,0x4600,P(2)+8,0x11223344);desc(P(0)+16,0,0x7000,0,0);
    arm();run(&d,P(0));assert(pt_dma_next(&d,&s)==0&&pt_read_le(ram+P(2)+8,4)==0x11223344&&pt_read_le(ram+P(0)+14,2));disarm();
    desc(P(0),4,0x4600,P(3),1);desc(P(0)+16,0,0x7000,0,0);own(3);arm();run(&d,P(0));
    for(unsigned k=0;k<4;k++)assert(pt_dma_next(&d,&s)==0&&(d.regs[1]&PT_DMA_ACTIVE));   /* waits, never stores into the owned page */
    release();assert(pt_dma_next(&d,&s)==0&&pt_read_le(ram+P(3),4)==1&&!(d.regs[1]&PT_DMA_ACTIVE));disarm();
    puts("PASS: DBDMA STORE_QUAD and descriptor status announced; an owned target page waits");
}
static void ata(void){
    char path[]="/tmp/ptolemy-s2-XXXXXX";int fd=mkstemp(path);assert(fd>=0&&!ftruncate(fd,16384));uint8_t sector[512];
    for(unsigned i=0;i<512;i++)sector[i]=(uint8_t)(i*7+1);assert(pwrite(fd,sector,512,512)==512);
    PtEvent e;pt_event_init(&e);PtBlock *b=pt_block_open(path,1,&e);assert(b);
    PtDma d;PtAta a;pt_dma_init(&d,(PtRam){ram,size,&hooks},NULL,NULL,NULL);pt_ata_init(&a,b,&d,level,NULL);
    desc(P(0),512,0x3000,P(1),0);desc(P(0)+16,0,0x7000,0,0);own(1);arm();run(&d,P(0));
    aw(&a,0x60,0xe0);aw(&a,0x20,1);aw(&a,0x30,1);aw(&a,0x40,0);aw(&a,0x50,0);aw(&a,0x70,0xc8);
    unsigned before_refusals=refusals;for(unsigned k=0;k<2000&&refusals==before_refusals;k++){pt_ata_service(&a);usleep(100);}
    assert(refusals>before_refusals&&a.busy);for(unsigned k=0;k<20;k++)assert(pt_ata_service(&a)==1&&a.busy);   /* retry, never overwrite */
    release();for(unsigned k=0;k<20000&&a.busy;k++){pt_ata_service(&a);usleep(100);}
    assert(!a.busy&&!(a.status&1)&&!memcmp(ram+P(1),sector,512));disarm();
    pt_block_close(b);pt_event_destroy(&e);close(fd);assert(!unlink(path));
    puts("PASS: ATA READ DMA announced; a GPU-owned buffer page is retried, then filled");
}
static void ohci(void){
    PtHid h[3];for(unsigned i=0;i<3;i++)pt_hid_init(&h[i],i);PtOhci o;pt_ohci_init(&o,(PtRam){ram,size,&hooks},h,h+1,h+2,level,NULL);
    pt_ohci_write(&o,0x54,16);for(unsigned i=0;i<10;i++)pt_ohci_frame(&o);
    uint32_t hcca=P(4),ed=P(4)+0x200,td=P(4)+0x300,tail=P(4)+0x310,data=P(5);memset(ram+hcca,0,0x400);
    for(unsigned i=0;i<32;i++)pt_write_le(ram+hcca+4*i,4,ed);
    pt_write_le(ram+ed,4,(8u<<16)|(1u<<7));pt_write_le(ram+ed+4,4,tail);pt_write_le(ram+ed+8,4,td);pt_write_le(ram+ed+12,4,0);
    pt_write_le(ram+td,4,0xf0000000u|(2u<<19));pt_write_le(ram+td+4,4,data);pt_write_le(ram+td+8,4,tail);pt_write_le(ram+td+12,4,data+7);
    h[0].configuration=1;pt_hid_key(h,4,1);pt_ohci_write(&o,0x18,hcca);pt_ohci_write(&o,4,0x84);
    own(5);arm();for(unsigned k=0;k<4;k++)pt_ohci_frame(&o);assert(pt_read_le(ram+ed+8,4)==td);   /* the TD stays queued */
    release();pt_ohci_frame(&o);assert(ram[data+2]==4&&(pt_read_le(ram+ed+8,4)&~15u)==tail&&!(pt_read_le(ram+td,4)>>28));disarm();
    for(unsigned i=0;i<3;i++)pt_hid_destroy(&h[i]);
    puts("PASS: OHCI HCCA, ED/TD write-back and IN data announced; an owned data page keeps the TD queued");
}
static void screamer(void){
    PtDma d;PtPcm p;PtScreamer s;pt_dma_init(&d,(PtRam){ram,size,&hooks},NULL,NULL,NULL);pt_pcm_init(&p,1);pt_screamer_init(&s,&d,&p);
    desc(P(6),1024,0x100c,P(7),P(6));for(unsigned k=0;k<1024;k+=4)pt_write_be(ram+P(7)+k,2,16384);
    own(7);arm();pt_screamer_write(&s,0,0,0);pt_dma_write(&d,12,P(6));pt_dma_write(&d,0,(uint32_t)PT_DMA_RUN<<16|PT_DMA_RUN);
    pt_screamer_service(&s,0);pt_screamer_service(&s,1000000);assert(!pt_pcm_queued(&p)||s.starved);   /* holds, never reads the owned page */
    release();pt_screamer_service(&s,2000000);assert(pt_pcm_queued(&p));disarm();
    puts("PASS: Screamer reads announced; an owned sample page starves instead of being read");
}
static uint8_t frame[64];static int ready;
static int receive(void *p,uint8_t *b,unsigned n){(void)p;(void)n;if(!ready)return 0;ready=0;memcpy(b,frame,64);return 64;}
static int link_up(void *p){(void)p;return 1;}
static void wr(PtNetRtl *n,unsigned reg,unsigned bytes,uint32_t v){uint8_t b[4];pt_write_le(b,bytes,v);assert(!pt_net_rtl_access(n,reg,b,bytes,1));}
static void network(void){
    uint8_t mac[]={2,1,2,3,4,5};PtNetRtl n;pt_net_rtl_init(&n,(PtRam){ram,size,&hooks},mac,(PtNetIo){0,NULL,receive,link_up},NULL,NULL);
    uint32_t ring=P(8);wr(&n,0x30,4,ring);wr(&n,0x44,4,0x80|8|2);wr(&n,0x3c,2,0x800f);wr(&n,0x37,1,12);n.bus_master=1;
    memset(frame,0x5a,64);memcpy(frame,mac,6);
    own(8);arm();ready=1;pt_net_rtl_service(&n);assert(!ready&&!n.rx&&n.dropped==1);   /* dropped, not written over */
    release();ready=1;pt_net_rtl_service(&n);assert(n.rx==1&&!memcmp(ram+ring+4,frame,64));disarm();
    puts("PASS: RTL8139 receive announced; an owned ring page drops the frame");
}
int main(void){
    page=(size_t)sysconf(_SC_PAGESIZE);size=16*page;
    ram=mmap(NULL,size,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANON,-1,0);assert(ram!=MAP_FAILED);
    dbdma();ata();ohci();screamer();network();
    assert(writes&&refusals);printf("PASS: S2 device writers (%u announced writes, %u refusals)\n",writes,refusals);return 0;
}
