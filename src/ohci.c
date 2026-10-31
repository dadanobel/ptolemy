/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "ohci.h"
#include <string.h>
/* OHCI 1.0a general TDs. All descriptor bytes are little endian; physical
 * memory is validated before invoking a device or publishing completion. */
static void nd_ohci_irq(PtOhci *o){if(o->irq)o->irq(o->opaque,!!((o->reg[4]&0x80000000u)&&(o->reg[3]&o->reg[4]&0x7fffffffu)));}
static void nd_ohci_hid_reset(void *p){pt_hid_reset(p);}
static unsigned nd_ohci_hid_address(void *p){return ((PtHid *)p)->address;}
static int nd_ohci_hid_packet(void *p,unsigned ep,unsigned pid,uint8_t *b,unsigned n,uint16_t frame){return pt_hid_packet(p,ep,pid,b,n,frame);}
static PtUsbDevice nd_ohci_hid(PtHid *h){return (PtUsbDevice){h,nd_ohci_hid_reset,nd_ohci_hid_address,nd_ohci_hid_packet};}
static void nd_ohci_reset(PtOhci *o){
    memset(o->reg,0,sizeof o->reg);o->reg[0]=0x10;o->reg[13]=0x2edf;o->reg[17]=0x628;o->reg[18]=0x02000204;o->reg[19]=0x000f0000;
    o->done=0;o->done_delay=7;memset(o->reset_ticks,0,sizeof o->reset_ticks);o->default_port=-1;
    /* A device present at reset is a new connection, so ConnectStatusChange (bit 16) is set and the root hub reports a
     * status change; otherwise Tiger never enumerates the attached HID devices. */
    int connected=0;for(unsigned k=0;k<4;k++){o->ports[k]=o->device[k].packet?0x10101:0x100;connected|=!!o->device[k].packet;}
    if(connected)o->reg[3]|=0x40;nd_ohci_irq(o);
}
void pt_ohci_init(PtOhci *o,PtRam ram,PtHid *keyboard,PtHid *mouse,PtHid *tablet,void (*irq)(void *,int),void *opaque){
    memset(o,0,sizeof *o);o->ram=ram;o->irq=irq;o->opaque=opaque;o->bus_master=1;o->hid[0]=keyboard;o->hid[1]=mouse;o->hid[2]=tablet;
    for(unsigned k=0;k<3;k++)if(o->hid[k])o->device[k]=nd_ohci_hid(o->hid[k]);nd_ohci_reset(o);
}
void pt_ohci_storage(PtOhci *o,const PtUsbDevice *device){
    o->device[3]=device?*device:(PtUsbDevice){0};o->ports[3]=0x10000|0x100|(device?1:0);o->reset_ticks[3]=0;
    if(o->default_port==3)o->default_port=-1;o->reg[3]|=0x40;nd_ohci_irq(o);
}
uint32_t pt_ohci_read(PtOhci *o,unsigned at){
    if(at&3)return 0;
    if(at>=0x54&&at<0x64)return o->ports[(at-0x54)/4];if(at>=0x54)return 0;
    return o->reg[at==0x14?4:at/4];
}
void pt_ohci_write(PtOhci *o,unsigned at,uint32_t v){
    if(at&3)return;
    if(at>=0x54&&at<0x64){unsigned k=(at-0x54)/4;uint32_t *p=&o->ports[k];*p&=~(v&0x1f0000);
        if(v&1)*p&=~2u;if((v&2)&&(*p&1))*p|=2;if(v&4)*p|=4;if(v&8)*p&=~4u;if((v&16)&&(*p&1)){*p|=16;o->reset_ticks[k]=10;}
        if(v&0x100)*p|=0x100;if(v&0x200)*p&=~0x103u;
    }else if(at<0x54){unsigned r=at/4;
        switch(r){case 0:case 14:case 15:case 17:case 18:case 19:break;
        case 1:o->reg[1]=v&0x7ff;break;
        case 2:if(v&1)nd_ohci_reset(o);else o->reg[2]|=v&14;break;
        case 3:o->reg[3]&=~v;break;case 4:o->reg[4]|=v;break;case 5:o->reg[4]&=~v;break;
        case 6:o->reg[r]=v&~255u;break;
        case 7:case 8:case 9:case 10:case 11:o->reg[r]=v&~15u;break;
        case 12:break;case 13:o->reg[13]=v;break;case 16:o->reg[16]=v&0x3fff;break;
        case 20:if(v&1)for(unsigned k=0;k<4;k++)o->ports[k]&=~0x100u;if(v&0x10000)for(unsigned k=0;k<4;k++)o->ports[k]|=0x100;break;
        default:break;}}
    nd_ohci_irq(o);
}
int pt_ohci_needs_clock(const PtOhci *o){
    for(unsigned k=0;k<4;k++)if(o->reset_ticks[k])return 1;
    return (o->reg[1]&0xc0)==0x80;
}
static PtUsbDevice *nd_ohci_device(PtOhci *o,unsigned address){
    if(!address){int k=o->default_port;return k>=0&&(o->ports[k]&3)==3?&o->device[k]:NULL;}
    for(unsigned k=0;k<4;k++)if((o->ports[k]&3)==3&&o->device[k].address&&o->device[k].address(o->device[k].opaque)==address)return &o->device[k];return NULL;
}
static int nd_ohci_fault(PtOhci *o){o->reg[3]|=16;return -1;}
static int nd_ohci_td(PtOhci *o,uint8_t *ed,unsigned *budget){
    uint32_t info=(uint32_t)pt_read_le(ed,4),head=(uint32_t)pt_read_le(ed+8,4),tail=(uint32_t)pt_read_le(ed+4,4)&~15u;
    if((info&0x4000)||(head&1)||(head&~15u)==tail)return 0;
    if(info&0x8000)return nd_ohci_fault(o); /* Isochronous TDs are not part of the attached devices. */
    uint32_t address=head&~15u;int busy=0;uint8_t *td=address?pt_ram_use(o->ram,address,16,1,&busy):NULL;if(busy)return 0;if(!td)return nd_ohci_fault(o);
    uint32_t flags=(uint32_t)pt_read_le(td,4),cbp=(uint32_t)pt_read_le(td+4,4),next=(uint32_t)pt_read_le(td+8,4),end=(uint32_t)pt_read_le(td+12,4);
    if((next&15)||next==address)return nd_ohci_fault(o);
    unsigned left=0;if(cbp){if((cbp&~4095u)==(end&~4095u)){if(end<cbp)return nd_ohci_fault(o);left=end-cbp+1;}
        else left=4096-(cbp&4095)+(end&4095)+1;}else if(end)return nd_ohci_fault(o);
    unsigned max=info>>16&0x7ff;if(!max||max>1023)return nd_ohci_fault(o);unsigned n=left<max?left:max;
    unsigned first=n;if(first>4096-(cbp&4095))first=4096-(cbp&4095);unsigned second=n-first;
    unsigned direction=info>>11&3;if(!direction)direction=flags>>19&3;unsigned pid=direction==0?PT_USB_SETUP:direction==1?PT_USB_OUT:PT_USB_IN;
    if(direction==3)return nd_ohci_fault(o);
    uint8_t *a=n?pt_ram_use(o->ram,cbp,first,pid==PT_USB_IN,&busy):NULL;if(busy)return 0;
    uint8_t *b=second?pt_ram_use(o->ram,end&~4095u,second,pid==PT_USB_IN,&busy):NULL;if(busy)return 0;
    if((n&&!a)||(second&&!b))return nd_ohci_fault(o);PtUsbDevice *device=nd_ohci_device(o,info&127);uint8_t packet[1024];if(pid!=PT_USB_IN&&n){memcpy(packet,a,first);if(second)memcpy(packet+first,b,second);}
    (*budget)--;int got=device&&device->packet?device->packet(device->opaque,info>>7&15,pid,packet,n,(uint16_t)o->reg[15]):PT_USB_STALL;
    if(got==PT_USB_NAK)return 0;unsigned code=got==PT_USB_STALL?4:got<0?5:(unsigned)got>n?8:0;
    unsigned toggle=(flags&(1u<<25))?(flags>>24&1):(head>>1&1);
    int retire=code!=0;
    if(!code){toggle^=1;if(pid==PT_USB_IN&&got){unsigned x=(unsigned)got<first?(unsigned)got:first;memcpy(a,packet,x);if((unsigned)got>x)memcpy(b,packet+x,(unsigned)got-x);}
        unsigned used=(unsigned)got;if(used==left)cbp=0;else if(used<4096-(cbp&4095))cbp+=used;else cbp=(end&~4095u)+(used-(4096-(cbp&4095)));
        if(used<n){code=(flags&(1u<<18))?0:9;retire=1;}if(!cbp)retire=1;
    }
    pt_write_le(td+4,4,cbp);flags=(flags&~(3u<<24))|((2u|toggle)<<24);
    if(retire){unsigned delay=flags>>21&7;if(delay<o->done_delay)o->done_delay=delay;
        pt_write_le(td,4,(flags&0x0fffffffu)|(uint32_t)code<<28);pt_write_le(td+8,4,o->done);o->done=address;o->reg[12]=address;
        pt_write_le(ed+8,4,(next&~15u)|(toggle<<1)|(code?1:0));
    }else {pt_write_le(td,4,flags);pt_write_le(ed+8,4,address|(toggle<<1));}
    return 1;
}
static void nd_ohci_list(PtOhci *o,uint32_t address,unsigned *budget){
    unsigned endpoints=0;while(address&&*budget&&endpoints++<256){int busy=0;uint8_t *ed=pt_ram_use(o->ram,address,16,1,&busy);if(busy)return;if((address&15)||!ed){nd_ohci_fault(o);return;}
        uint32_t next=(uint32_t)pt_read_le(ed+12,4);if(next&15){nd_ohci_fault(o);return;}
        while(*budget){int got=nd_ohci_td(o,ed,budget);if(got<=0)break;}if(next==address){nd_ohci_fault(o);return;}address=next;
    }if(address&&!(*budget))o->budget_exhaustions++;if(endpoints>256)nd_ohci_fault(o);
}
void pt_ohci_frame(PtOhci *o){
    for(unsigned k=0;k<4;k++)if(o->reset_ticks[k]&&!--o->reset_ticks[k]){if(o->device[k].reset)o->device[k].reset(o->device[k].opaque);o->ports[k]=(o->ports[k]&~16u)|2|0x100000;o->default_port=(int)k;o->reg[3]|=0x40;}
    if((o->reg[1]&0xc0)!=0x80||!o->bus_master){nd_ohci_irq(o);return;}
    o->reg[15]=(o->reg[15]+1)&0xffff;if(!(o->reg[15]&0x7fff))o->reg[3]|=0x20;o->reg[3]|=4;
    int busy;uint8_t *hcca=pt_ram_use(o->ram,o->reg[6],256,1,&busy);if(busy){nd_ohci_irq(o);return;}if(!hcca){nd_ohci_fault(o);nd_ohci_irq(o);return;}pt_write_le(hcca+0x80,4,o->reg[15]);
    unsigned budget=2048;if(o->reg[1]&4)nd_ohci_list(o,(uint32_t)pt_read_le(hcca+(o->reg[15]&31)*4,4),&budget);
    if(o->reg[1]&16)nd_ohci_list(o,o->reg[8],&budget);if(o->reg[1]&32)nd_ohci_list(o,o->reg[10],&budget);
    if(o->done&&o->done_delay==0&&!(o->reg[3]&2)){pt_write_le(hcca+0x84,4,o->done|!!(o->reg[3]&o->reg[4]&~2u));o->done=o->reg[12]=0;o->done_delay=7;o->reg[3]|=2;}
    else if(o->done_delay<7&&o->done_delay)o->done_delay--;nd_ohci_irq(o);
}
