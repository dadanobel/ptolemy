/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "net.h"
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
static unsigned publications;
static int before(void *p,uint32_t at,size_t n,int write){(void)p;if(write){assert(n&&at>=0x1000);publications++;}return 0;}
static const PtRamHooks hooks={before,NULL};
static uint8_t frame[128],sent[PT_NET_FRAME];static int ready,blocked,line;static unsigned sent_size;
static int send_packet(void *p,const uint8_t *b,unsigned n){(void)p;if(blocked)return EAGAIN;memcpy(sent,b,n);sent_size=n;return 0;}
static int receive(void *p,uint8_t *b,unsigned n){(void)p;assert(n>=64);if(!ready)return 0;ready=0;memcpy(b,frame,64);return 64;}
static int link(void *p){(void)p;return 1;}
static void irq(void *p,int active){(void)p;line=active;}
static void write_reg(PtNetRtl *n,unsigned reg,unsigned size,uint32_t value){uint8_t b[4];pt_write_le(b,size,value);assert(!pt_net_rtl_access(n,reg,b,size,1));}
int main(void){
    static uint8_t ram[65536];uint8_t mac[]={2,1,2,3,4,5},config[256],mask[256];pt_net_rtl_pci(config,mask);assert(pt_read_le(config,4)==0x813910ec&&config[11]==2&&mask[4]==7);
    PtNetRtl n;pt_net_rtl_init(&n,(PtRam){ram,sizeof ram,&hooks},mac,(PtNetIo){0,send_packet,receive,link},irq,NULL);
    write_reg(&n,0x30,4,0x1000);write_reg(&n,0x44,4,0x80|8|2);write_reg(&n,0x3c,2,0x800f);write_reg(&n,0x37,1,12);
    memset(frame,0x5a,64);memcpy(frame,mac,6);ready=1;pt_net_rtl_service(&n);assert(ready&&!n.rx);n.bus_master=1;pt_net_rtl_service(&n);assert(!ready&&n.rx==1&&publications==1&&line&&pt_read_le(ram+0x1002,2)==68&&!memcmp(ram+0x1004,frame,64));
    assert(pt_read_le(ram+0x1044,4)==0x7c6f85d2u);write_reg(&n,0x3e,2,0xffff);assert(!line);write_reg(&n,0x62,2,0x1200);assert(pt_read_le(n.reg+0x3e,2)&0x20);write_reg(&n,0x3e,2,0xffff);memcpy(ram+0x8000,frame,64);write_reg(&n,0x20,4,0x8000);blocked=1;write_reg(&n,0x10,4,64);assert(n.pending[0]&&!n.tx);blocked=0;pt_net_rtl_service(&n);assert(n.tx==1&&!n.pending[0]&&sent_size==64&&!memcmp(sent,frame,64));
    write_reg(&n,0x24,4,0xfffffff0);write_reg(&n,0x14,4,64);assert(n.faults==1&&!n.pending[1]);
    /* Linear tail and wrapped-ring writes are validated before publication. */
    write_reg(&n,0x37,1,16);n.bus_master=1;write_reg(&n,0x30,4,0x1000);write_reg(&n,0x44,4,2);write_reg(&n,0x37,1,8);n.producer=8192-32;write_reg(&n,0x38,2,4000);ready=1;pt_net_rtl_service(&n);assert(n.rx==2&&n.producer==40&&ram[0x1000]==frame[28]);
    write_reg(&n,0x30,4,0xfffffff0);ready=1;pt_net_rtl_service(&n);assert(n.faults==2);assert(pt_net_rtl_access(&n,255,frame,4,0)==EINVAL);
    puts("PASS: RTL PCI identity, DMA gating, RX/CRC layout, IRQ ack, TX backpressure, invalid DMA and RX ring wrap");
}
