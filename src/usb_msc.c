/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "usb_msc.h"
#include <errno.h>
#include <string.h>
enum { ND_BOT_CBW,ND_BOT_IN,ND_BOT_OUT,ND_BOT_CSW,ND_BOT_INVALID };
static void nd_msc_bus_reset(void *opaque){
    PtUsbMsc *u=opaque;pt_scsi_reset(&u->scsi);u->address=u->configuration=u->halt=0;u->control_phase=0;
    u->stage=ND_BOT_CBW;u->native_recovery=u->native_zlp=u->phase_error=0;u->host_length=u->transferred=0;
}
void pt_usb_msc_init(PtUsbMsc *u){
    memset(u,0,sizeof *u);pt_scsi_init(&u->scsi,0);nd_msc_bus_reset(u);
}
static unsigned nd_msc_address(void *opaque){return ((PtUsbMsc *)opaque)->address;}
static int nd_msc_setup(PtUsbMsc *u,const uint8_t *p,unsigned n){
    if(n!=8)return PT_USB_STALL;memcpy(u->setup,p,8);u->control_position=u->control_length=0;memset(u->control,0,sizeof u->control);
    unsigned type=p[0],req=p[1],value=(unsigned)pt_read_le(p+2,2),index=(unsigned)pt_read_le(p+4,2),limit=(unsigned)pt_read_le(p+6,2);
    if(type==0x80&&req==6){
        /* 1209:0001 is private-test-only, not globally unique; replace before distribution. */
        static const uint8_t device[]={18,1,0,2,0,0,0,64,9,0x12,1,0,0,1,1,2,3,1};
        static const uint8_t config[]={9,2,32,0,1,1,0,0x80,50,9,4,0,0,2,8,6,0x50,0,7,5,0x81,2,64,0,0,7,5,2,2,64,0,0};
        if(value==0x100){memcpy(u->control,device,sizeof device);u->control_length=sizeof device;}
        else if(value==0x200){memcpy(u->control,config,sizeof config);u->control_length=sizeof config;}
        else if((value>>8)==3){unsigned id=value&255;if(!id){u->control[0]=4;u->control[1]=3;u->control[2]=9;u->control[3]=4;u->control_length=4;}
            else {const char *text=id==1?"Ptolemy":id==2?"Transfer Disk":id==3?"PT000001":NULL;if(!text)return PT_USB_STALL;unsigned len=(unsigned)strlen(text);u->control[0]=(uint8_t)(2+len*2);u->control[1]=3;for(unsigned k=0;k<len;k++)u->control[2+2*k]=(uint8_t)text[k];u->control_length=2+len*2;}}
        else return PT_USB_STALL;
    }else if(type==0xa1&&req==0xfe&&!value&&!index&&limit==1){u->control_length=1;}
    else if(type==0x21&&req==0xff&&!value&&!index&&!limit){pt_scsi_reset(&u->scsi);u->stage=ND_BOT_CBW;u->native_recovery=3;u->phase_error=u->native_zlp=0;}
    else if(type==0&&req==5&&value<128&&!index&&!limit){}
    else if(type==0&&req==9&&value<=1&&!index&&!limit){}
    else if(type==0x80&&req==8&&!value&&!index&&limit==1){u->control[0]=u->configuration;u->control_length=1;}
    else if(type==0x81&&req==10&&!value&&!index&&limit==1){u->control_length=1;}
    else if(type==1&&req==11&&!value&&!index&&!limit){}
    else if((type==0x80||type==0x81||type==0x82)&&req==0&&!value&&limit==2){
        if(type==0x82){unsigned bit=index==0x81?1:index==2?2:0;if(!bit&&index)return PT_USB_STALL;u->control[0]=(u->halt&bit)?1:0;}else if(index)return PT_USB_STALL;u->control_length=2;
    }else if(type==2&&(req==1||req==3)&&!value&&!limit&&(index==0x81||index==2)){
        unsigned bit=index==0x81?1:2;if(req==1){u->halt&=(uint8_t)~bit;u->native_recovery&=~bit;}else u->halt|=(uint8_t)bit;
    }else return PT_USB_STALL;
    if(u->control_length>limit)u->control_length=limit;u->control_phase=type&128?1:3;return 8;
}
static int nd_msc_control(PtUsbMsc *u,unsigned pid,uint8_t *p,unsigned n){
    if(pid==PT_USB_SETUP)return nd_msc_setup(u,p,n);
    if(u->control_phase==1&&pid==PT_USB_IN){unsigned take=u->control_length-u->control_position;if(take>n)take=n;
        if(take)memcpy(p,u->control+u->control_position,take);u->control_position+=take;
        if(take<n||u->control_position==u->control_length)u->control_phase=2;return (int)take;}
    if(u->control_phase==2&&pid==PT_USB_OUT&&!n){u->control_phase=0;return 0;}
    if(u->control_phase==3&&pid==PT_USB_IN&&!n){if(u->setup[1]==5)u->address=u->setup[2];if(u->setup[1]==9)u->configuration=u->setup[2];u->control_phase=0;return 0;}
    return PT_USB_STALL;
}
static int nd_msc_cbw(PtUsbMsc *u,const uint8_t *p,unsigned n){
    if(n!=31||pt_read_le(p,4)!=0x43425355||(p[12]&127)||p[13]||!p[14]||p[14]>16){u->stage=ND_BOT_INVALID;u->halt=3;return PT_USB_STALL;}
    if(u->scsi.pending||u->scsi.retry)return PT_USB_NAK;
    u->tag=(uint32_t)pt_read_le(p+4,4);u->host_length=(uint32_t)pt_read_le(p+8,4);u->input=!!(p[12]&128);u->transferred=u->phase_error=u->native_zlp=0;
    int error=pt_scsi_command(&u->scsi,p+15,p[14]);if(error){u->scsi.status=2;u->scsi.key=5;u->scsi.asc=0x24;u->scsi.phase=PT_SCSI_DONE;u->scsi.expected=0;}
    uint64_t expected=u->scsi.expected;int input=u->scsi.phase==PT_SCSI_IN||u->scsi.phase==PT_SCSI_READ_WAIT;
    u->stage=u->host_length?(u->input?ND_BOT_IN:ND_BOT_OUT):ND_BOT_CSW;
    if((!u->host_length&&expected)||(u->host_length&&expected&&u->input!=input)){u->phase_error=1;pt_scsi_reset(&u->scsi);u->stage=ND_BOT_CSW;if(u->host_length)u->halt|=u->input?1:2;}
    else if(u->host_length<expected)u->phase_error=1;
    if(!expected&&!u->input&&u->host_length){u->halt|=2;u->stage=ND_BOT_CSW;}
    return 31;
}
static int nd_msc_packet(void *opaque,unsigned ep,unsigned pid,uint8_t *p,unsigned n,uint16_t frame){
    (void)frame;PtUsbMsc *u=opaque;if((!p&&n)||n>65536)return PT_USB_STALL;if(!ep)return nd_msc_control(u,pid,p,n);
    if(!u->configuration)return PT_USB_NAK;if((ep!=1||pid!=PT_USB_IN)&&(ep!=2||pid!=PT_USB_OUT))return PT_USB_STALL;
    unsigned bit=ep==1?1:2;if(u->halt&bit||u->stage==ND_BOT_INVALID||u->native_recovery)return PT_USB_STALL;
    pt_scsi_service(&u->scsi);
    if(u->stage==ND_BOT_CBW)return ep==2?nd_msc_cbw(u,p,n):PT_USB_NAK;
    if(u->stage==ND_BOT_IN&&ep==1){
        if(u->scsi.pending||u->scsi.retry)return PT_USB_NAK;unsigned left=u->host_length-u->transferred,limit=n<left?n:left;
        int take=pt_scsi_read(&u->scsi,p,limit);if(take==-EAGAIN)return PT_USB_NAK;if(take<0){u->halt|=1;return PT_USB_STALL;}u->transferred+=(unsigned)take;
        int ended=u->scsi.phase==PT_SCSI_DONE||u->transferred==u->host_length;
        if(ended){if(u->transferred==u->host_length){u->stage=ND_BOT_CSW;if(u->phase_error)pt_scsi_reset(&u->scsi);}
            else if((unsigned)take<n){u->stage=ND_BOT_CSW;u->halt|=1;}else u->native_zlp=1;}
        return take;
    }
    if(u->stage==ND_BOT_OUT&&ep==2){
        if(u->scsi.pending||u->scsi.retry)return PT_USB_NAK;if(u->scsi.phase!=PT_SCSI_OUT){u->stage=ND_BOT_CSW;u->halt|=2;return PT_USB_STALL;}
        unsigned left=u->host_length-u->transferred;if(n>left){u->phase_error=1;u->halt|=2;u->stage=ND_BOT_CSW;pt_scsi_reset(&u->scsi);return PT_USB_STALL;}
        int take=pt_scsi_write(&u->scsi,p,n);if(take==-EAGAIN)return PT_USB_NAK;if(take<0)return PT_USB_STALL;u->transferred+=(unsigned)take;
        if((unsigned)take!=n){u->halt|=2;u->stage=ND_BOT_CSW;}else if(u->transferred==u->host_length){u->stage=ND_BOT_CSW;if(u->phase_error)pt_scsi_reset(&u->scsi);}
        return take;
    }
    if(u->stage==ND_BOT_CSW&&ep==1){if(u->scsi.pending||u->scsi.retry)return PT_USB_NAK;if(n<13)return PT_USB_STALL;
        pt_write_le(p,4,0x53425355);pt_write_le(p+4,4,u->tag);pt_write_le(p+8,4,u->host_length-u->transferred);p[12]=u->phase_error?2:u->scsi.status?1:0;
        u->stage=u->phase_error?ND_BOT_INVALID:ND_BOT_CBW;return 13;}
    return PT_USB_NAK;
}
PtUsbDevice pt_usb_msc_device(PtUsbMsc *u){return (PtUsbDevice){u,nd_msc_bus_reset,nd_msc_address,nd_msc_packet};}
int pt_usb_msc_quiescent(const PtUsbMsc *u){return u->stage==ND_BOT_CBW&&!u->scsi.pending&&!u->scsi.retry;}
int pt_usb_msc_service(PtUsbMsc *u){return pt_scsi_service(&u->scsi);}
