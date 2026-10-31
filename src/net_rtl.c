/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "net.h"
#include <errno.h>
#include <string.h>
/* RTL8139 register/receive-ring contract, classic four-buffer transmit mode.
 * No C+ offloads or guest driver installation is required. */
static uint32_t nr_get(PtNetRtl *n,unsigned at,unsigned size){return (uint32_t)pt_read_le(n->reg+at,size);}
static void nr_set(PtNetRtl *n,unsigned at,unsigned size,uint32_t v){pt_write_le(n->reg+at,size,v);}
static void nr_irq(PtNetRtl *n){if(n->irq)n->irq(n->opaque,!!(nr_get(n,0x3c,2)&nr_get(n,0x3e,2)));}
static void nr_event(PtNetRtl *n,unsigned bits){nr_set(n,0x3e,2,nr_get(n,0x3e,2)|bits);nr_irq(n);}
static void nr_reset(PtNetRtl *n){memset(n->reg,0,256);memcpy(n->reg,n->mac,6);memset(n->pending,0,4);n->producer=0;n->link_up=-1;
    nr_set(n,0x38,2,0xfff0);nr_set(n,0x40,4,0x60000000);n->reg[0x5e]=0x10;nr_set(n,0x62,2,0x3100);
    for(unsigned k=0;k<4;k++)nr_set(n,0x10+k*4,4,1u<<13);nr_irq(n);}
void pt_net_rtl_pci(uint8_t c[256],uint8_t mask[256]){memset(c,0,256);memset(mask,0,256);pt_write_le(c,4,0x813910ec);c[8]=0x10;c[11]=2;c[0x3c]=30;c[0x3d]=1;mask[4]=7;}
void pt_net_rtl_init(PtNetRtl *n,PtRam ram,const uint8_t mac[6],PtNetIo io,void (*irq)(void *,int),void *opaque){
    memset(n,0,sizeof *n);n->ram=ram;n->io=io;n->irq=irq;n->opaque=opaque;memcpy(n->mac,mac,6);nr_reset(n);
}
static uint32_t nr_crc(const uint8_t *p,unsigned size){uint32_t c=~0u;for(unsigned k=0;k<size;k++){c^=p[k];for(unsigned b=0;b<8;b++)c=c>>1^((0u-(c&1))&0xedb88320u);}return ~c;}
static int nr_accept(PtNetRtl *n,const uint8_t *p){
    unsigned r=nr_get(n,0x44,4);if(r&1)return 1;int broadcast=1;for(unsigned k=0;k<6;k++)if(p[k]!=255)broadcast=0;
    if(broadcast)return !!(r&8);if(!(p[0]&1))return (r&2)&&!memcmp(p,n->reg,6);if(!(r&4))return 0;
    uint32_t c=~nr_crc(p,6),rev=0;for(unsigned k=0;k<32;k++){rev=rev<<1|(c&1);c>>=1;}unsigned hash=rev>>26;return !!(n->reg[8+hash/8]&(1u<<(hash&7)));
}
static int nr_receive(PtNetRtl *n,const uint8_t *p,unsigned size){
    if(size<14||size>1518||!nr_accept(n,p))return 0;
    uint32_t rcr=nr_get(n,0x44,4),capacity=8192u<<((rcr>>11)&3),read=(nr_get(n,0x38,2)+16)&(capacity-1);
    unsigned wire=size<60?60:size,total=(4+wire+4+3)&~3u,free=(read+capacity-n->producer-1)&(capacity-1);
    if(total>free){n->dropped++;nr_set(n,0x4c,4,nr_get(n,0x4c,4)+1);nr_event(n,16);return 0;}
    uint8_t packet[PT_NET_FRAME]={0};unsigned status=1;int broadcast=1;for(unsigned k=0;k<6;k++)if(p[k]!=255)broadcast=0;if(broadcast)status|=0x2000;else if(p[0]&1)status|=0x8000;else if(!memcmp(p,n->reg,6))status|=0x4000;
    pt_write_le(packet,2,status);pt_write_le(packet+2,2,wire+4);memcpy(packet+4,p,size);pt_write_le(packet+4+wire,4,nr_crc(packet+4,wire));
    unsigned first=total,second=0;int linear=(rcr&128)&&capacity!=65536;
    if(!linear&&first>capacity-n->producer){first=capacity-n->producer;second=total-first;}
    uint64_t base=nr_get(n,0x30,4),address=base+n->producer;int busy=0;uint8_t *a=address<=UINT32_MAX?pt_ram_use(n->ram,(uint32_t)address,first,1,&busy):NULL;
    uint8_t *b=second&&!busy?pt_ram_use(n->ram,(uint32_t)base,second,1,&busy):NULL;if(busy){n->dropped++;return 0;}
    if(!a||(second&&!b)){n->faults++;nr_event(n,0x8002);return -1;}
    memcpy(a,packet,first);if(second)memcpy(b,packet+first,second);
    n->producer=(n->producer+total)&(capacity-1);n->rx++;nr_set(n,0x3a,2,n->producer);nr_event(n,1);return 1;
}
void pt_net_rtl_service(PtNetRtl *n){
    int up=n->io.link?n->io.link(n->io.opaque):0;if(up!=n->link_up){n->link_up=up;nr_event(n,0x20);}
    n->reg[0x58]=(uint8_t)(up?0:4);nr_set(n,0x64,2,up?0x782d:0x7809);nr_set(n,0x66,2,0x1e1);nr_set(n,0x68,2,up?0x45e1:0);
    unsigned capacity=8192u<<((nr_get(n,0x44,4)>>11)&3);if(((nr_get(n,0x38,2)+16)&(capacity-1))==n->producer)n->reg[0x37]|=1;else n->reg[0x37]&=~1u;
    if(!n->bus_master)return;
    for(unsigned k=0;k<4;k++)if(n->pending[k]&&(n->reg[0x37]&4)&&up){unsigned count=nr_get(n,0x10+k*4,4)&8191;int busy=0;uint8_t *p=count&&count<=1518?pt_ram_use(n->ram,nr_get(n,0x20+k*4,4),count,0,&busy):NULL;if(busy)continue;
        if(!p){n->pending[k]=0;n->faults++;nr_set(n,0x10+k*4,4,(1u<<30)|(1u<<13));nr_event(n,8);continue;}
        uint8_t frame[PT_NET_FRAME]={0};memcpy(frame,p,count);unsigned size=count<60?60:count;int error=n->io.send?n->io.send(n->io.opaque,frame,size):ENETDOWN;
        if(error==EAGAIN)continue;n->pending[k]=0;nr_set(n,0x10+k*4,4,count|(1u<<13)|(error?1u<<30:1u<<15));if(!error)n->tx++;nr_event(n,error?8:4);
    }
    if((n->reg[0x37]&8)&&up&&n->io.receive){uint8_t frame[PT_NET_FRAME];for(unsigned budget=0;budget<PT_NET_QUEUE;budget++){int got=n->io.receive(n->io.opaque,frame,sizeof frame);if(got<=0)break;if(got>(int)sizeof frame){n->faults++;break;}nr_receive(n,frame,(unsigned)got);}}
    capacity=8192u<<((nr_get(n,0x44,4)>>11)&3);if(((nr_get(n,0x38,2)+16)&(capacity-1))==n->producer)n->reg[0x37]|=1;else n->reg[0x37]&=~1u;
}
int pt_net_rtl_access(PtNetRtl *n,unsigned at,uint8_t *p,unsigned size,int write){
    if(!p||!(size==1||size==2||size==4)||at>256||size>256-at)return EINVAL;
    pt_net_rtl_service(n);if(!write){memcpy(p,n->reg+at,size);return 0;}
    uint32_t v=(uint32_t)pt_read_le(p,size);
    if(at==0x37&&size==1){if(v&16)nr_reset(n);else n->reg[0x37]=(uint8_t)((v&12)|(n->reg[0x37]&1));}
    else if(at==0x3e&&size==2){nr_set(n,at,2,nr_get(n,at,2)&~v);nr_irq(n);}
    else if(at>=0x10&&at<=0x1c&&!(at&3)&&size==4){unsigned k=(at-0x10)/4;nr_set(n,at,4,v&~((1u<<13)|(1u<<15)|(1u<<30)));n->pending[k]=1;}
    else if(at==0x62&&size==2){nr_set(n,at,2,(v&~0x8200u)|0x100);n->link_up=-1;}
    else if(at==0x40&&size==4)nr_set(n,at,4,(v&0x007fffff)|0x60000000);
    else if(at<0x10||(at>=0x20&&at<0x34)||at==0x38||at==0x3c||at==0x44||at==0x4c||at==0x50||at==0x52||at==0x66)memcpy(n->reg+at,p,size);
    pt_net_rtl_service(n);return 0;
}
