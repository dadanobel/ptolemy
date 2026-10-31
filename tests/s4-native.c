/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Wire, descriptor and FIFO boundaries; no guest boot or host audio output. */
#include "devices.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static uint64_t ns=1000;
static void via(PtPmu *p,unsigned r,uint8_t v){pt_pmu_write(p,r*512,v,++ns);}
static uint8_t shift(PtPmu *p,uint8_t v,int output,int release){via(p,11,output?0x1c:0x0c);if(output)via(p,10,v);via(p,0,8);uint8_t x=pt_pmu_read(p,5120,++ns);if(release)via(p,0,24);return x;}
static unsigned command(PtPmu *p,uint8_t cmd,const uint8_t *in,unsigned n,uint8_t *out){shift(p,cmd,1,1);if(cmd==0x8f||cmd==0x9a)shift(p,(uint8_t)n,1,1);for(unsigned k=0;k<n;k++)shift(p,in[k],1,1);unsigned len=shift(p,0,0,1);for(unsigned k=0;k<len;k++)out[k]=shift(p,0,0,1);return len;}
static void power(void){
    PtPmu p;PtGpio g;pt_gpio_init(&g,NULL,NULL);pt_pmu_init(&p,&g,ns,100,NULL,NULL);via(&p,2,16);
    shift(&p,0xd0,1,0);assert(!pt_pmu_take_power(&p));via(&p,0,24);assert(pt_pmu_take_power(&p)==PT_POWER_RESET&&!pt_pmu_take_power(&p));
    shift(&p,0x7e,1,1);for(unsigned k=0;k<4;k++)shift(&p,(uint8_t)"MATT"[k],1,1);
    assert(!pt_pmu_take_power(&p));shift(&p,0,0,0);assert(!pt_pmu_take_power(&p));via(&p,0,24);assert(pt_pmu_take_power(&p)==PT_POWER_OFF);
    uint8_t out[128],set[]={1,0x12,0x34},get[]={0},last[]={7},i2c[]={1,0,0,0x70,0,0,1},read[]={0,0,4};
    assert(!command(&p,0x8f,set,3,out));assert(command(&p,0x8f,get,1,out)==2&&pt_read_be(out,2)==0x1234);
    assert(command(&p,0x8f,last,1,out)==1&&!out[0]);assert(command(&p,0x9a,i2c,7,out)==1&&out[0]==0xff&&p.native_i2c_refused==1);
    assert(command(&p,0xe8,read,3,out)==4);for(unsigned k=0;k<4;k++)assert(!out[k]);assert(!p.protocol_errors&&!p.unhandled_commands);
    puts("PASS: PMU reset/off only after final ACK, power-events 7, finite I2C NACK and diagnostic framing");
}
static unsigned irqs;
static void irq(void *p){(void)p;irqs++;}
static void desc(uint8_t *ram,unsigned at,unsigned n,unsigned cmd,unsigned addr,unsigned dep){memset(ram+at,0,16);pt_write_le(ram+at,2,n);pt_write_le(ram+at+2,2,cmd);pt_write_le(ram+at+4,4,addr);pt_write_le(ram+at+8,4,dep);}
static void dma(void){
    uint8_t ram[4096]={};PtDma d;pt_dma_init(&d,(PtRam){ram,sizeof ram,NULL},NULL,irq,NULL);
    desc(ram,0x100,8,0x1030,0x200,0);desc(ram,0x110,0,0x7000,0,0);pt_dma_write(&d,12,0x100);pt_dma_write(&d,0,(uint32_t)PT_DMA_RUN<<16|PT_DMA_RUN);
    PtDmaSlice s;assert(pt_dma_next(&d,&s)==1);uint64_t old=s.generation;assert(!pt_dma_advance(&d,old,1));assert(pt_dma_advance(&d,old,1)==-1);
    assert(pt_dma_next(&d,&s)==1&&s.address==0x201);assert(!pt_dma_advance(&d,s.generation,7)&&irqs==1);assert(!pt_dma_next(&d,&s));
    pt_dma_write(&d,0,(uint32_t)PT_DMA_RUN<<16);desc(ram,0x100,4,0x4600,0x200,0xaabbccdd);desc(ram,0x110,4,0x5600,0x200,0);desc(ram,0x120,0,0x7000,0,0);
    pt_dma_write(&d,12,0x100);pt_dma_write(&d,0,(uint32_t)PT_DMA_RUN<<16|PT_DMA_RUN);assert(!pt_dma_next(&d,&s));assert(pt_read_le(ram+0x200,4)==0xaabbccdd&&pt_read_le(ram+0x118,4)==0xaabbccdd);
    pt_dma_write(&d,0,(uint32_t)PT_DMA_RUN<<16);pt_dma_write(&d,12,0x101);pt_dma_write(&d,0,(uint32_t)PT_DMA_RUN<<16|PT_DMA_RUN);assert(pt_dma_next(&d,&s)==-1&&d.regs[1]&PT_DMA_DEAD);
    puts("PASS: DBDMA leases, word commands, completion order and unaligned descriptor rejection");
}
static unsigned descriptor(PtHid *h,unsigned type,unsigned value,uint8_t *out){uint8_t setup[8]={type,6};pt_write_le(setup+2,2,value);pt_write_le(setup+6,2,255);assert(pt_hid_packet(h,0,PT_USB_SETUP,setup,8,0)==8);unsigned n=0;for(;;){int got=pt_hid_packet(h,0,PT_USB_IN,out+n,8,0);assert(got>=0);n+=(unsigned)got;if(got<8)break;}assert(!pt_hid_packet(h,0,PT_USB_OUT,out,0,0));return n;}
static unsigned report_bytes(const uint8_t *p,unsigned n){unsigned size=0,count=0,bits=0;for(unsigned a=0;a<n;){unsigned tag=p[a++],bytes=tag&3;if(bytes==3)bytes=4;assert(tag!=254&&a+bytes<=n);unsigned value=0;for(unsigned k=0;k<bytes;k++)value|=(unsigned)p[a+k]<<(8*k);a+=bytes;if((tag&252)==0x74)size=value;if((tag&252)==0x94)count=value;if((tag&252)==0x80)bits+=size*count;}assert(!(bits&7));return bits/8;}
static void hid(void){
    PtHid h;uint8_t data[256],cfg[256];for(unsigned kind=0;kind<3;kind++){
        pt_hid_init(&h,kind);assert(descriptor(&h,0x80,0x0100,data)==18);if(!kind)assert(pt_read_le(data+8,2)==0x05ac&&pt_read_le(data+10,2)==0x020e);
        assert(descriptor(&h,0x80,0x0200,cfg)==34&&pt_read_le(cfg+2,2)==34);unsigned n=descriptor(&h,0x81,0x2200,data);assert(n==pt_read_le(cfg+25,2)&&report_bytes(data,n)==(kind==0?8:kind==1?4:6));if(!kind)assert(cfg[22]==33&&cfg[16]==1);
        h.configuration=1;if(kind==PT_HID_TABLET){pt_hid_pointer(&h,3,1234,23456,-3);assert(pt_hid_packet(&h,1,PT_USB_IN,data,8,1)==6&&pt_read_le(data+1,2)==1234&&pt_read_le(data+3,2)==23456&&(int8_t)data[5]==-3);}
        uint8_t invalid[8]={0x80,6,1,1,0,0,18,0};assert(pt_hid_packet(&h,0,PT_USB_SETUP,invalid,8,0)==PT_USB_STALL);pt_hid_reset(&h);assert(!h.address&&!h.configuration);pt_hid_destroy(&h);
    }puts("PASS: native HID identity, descriptor/report sizes, absolute pointer, malformed request and reset");
}
static void audio(void){
    PtPcm p;pt_pcm_init(&p,1);atomic_store(&p.stopped,0);atomic_store(&p.read,UINT32_MAX-3);atomic_store(&p.write,UINT32_MAX-3);float out[16];
    for(unsigned k=0;k<8;k++)assert(pt_pcm_push(&p,k,-(float)k));pt_pcm_render(&p,out,8);for(unsigned k=0;k<8;k++)assert(out[k*2]==k&&out[k*2+1]==-(float)k);assert(!pt_pcm_queued(&p));
    uint8_t ram[4096]={};PtDma d;pt_dma_init(&d,(PtRam){ram,sizeof ram,NULL},NULL,NULL,NULL);PtScreamer s;pt_screamer_init(&s,&d,&p);
    pt_screamer_write(&s,0x10,0x4000|0x155,0);pt_screamer_write(&s,0x10,0x7000|1|(4<<1),0);assert(((pt_screamer_read(&s,0x20,0)>>4)&0xfff)==0x155);
    pt_screamer_write(&s,0x10,0x8000,0);assert(pt_screamer_read(&s,0,0)&(1u<<11));pt_screamer_write(&s,0,1u<<11,0);assert(!(pt_screamer_read(&s,0,0)&(1u<<11)));
    desc(ram,0x100,8,0x1000,0x200,0);desc(ram,0x110,0,0x7000,0,0);pt_dma_write(&d,12,0x100);pt_dma_write(&d,0,(uint32_t)PT_DMA_RUN<<16|PT_DMA_RUN);
    pt_screamer_service(&s,1000);pt_screamer_service(&s,11000);pt_screamer_write(&s,0,1u<<11,11000);pt_screamer_service(&s,24000);assert(pt_screamer_read(&s,0x50,24000)==1);
    puts("PASS: native PCM wraparound, codec readback, error W1C and continuous frame phase");
}
int main(void){power();dma();hid();audio();return 0;}
