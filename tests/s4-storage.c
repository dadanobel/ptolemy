/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "usb_msc.h"
#include "board.h"
#include "ata.h"
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
static void drain(PtScsi *s){for(unsigned k=0;k<50000;k++){pt_scsi_service(s);if(!s->pending&&!s->retry)return;usleep(100);}assert(!"worker completion timeout");}
static void ctrl(PtUsbDevice *d,unsigned type,unsigned req,unsigned value,unsigned index){uint8_t p[8]={type,req,value,value>>8,index,index>>8};assert(d->packet(d->opaque,0,PT_USB_SETUP,p,8,0)==8);assert(!d->packet(d->opaque,0,PT_USB_IN,p,0,0));}
static void bot(PtBlock *block,unsigned host,int input,unsigned direction,unsigned status,unsigned residue){
    PtUsbMsc *u=calloc(1,sizeof *u);assert(u);pt_usb_msc_init(u);PtBlock *old;assert(!pt_scsi_media(&u->scsi,block,0,&old));u->scsi.unit_attention=0;
    PtUsbDevice d=pt_usb_msc_device(u);ctrl(&d,0,9,1,0);uint8_t p[64]={0};pt_write_le(p,4,0x43425355);pt_write_le(p+4,4,0x10203040);pt_write_le(p+8,4,host);p[12]=input?128:0;p[14]=direction?10:6;p[15]=direction==1?0x28:direction==2?0x2a:0;if(direction)p[23]=1;
    assert(d.packet(d.opaque,2,PT_USB_OUT,p,31,0)==31);unsigned transferred=0;
    for(unsigned k=0;k<40&&transferred<host;k++){drain(&u->scsi);unsigned n=host-transferred;if(n>64)n=64;memset(p,0x5a,n);
        int got=d.packet(d.opaque,input?1:2,input?PT_USB_IN:PT_USB_OUT,p,n,0);if(got==PT_USB_STALL){ctrl(&d,2,1,0,input?0x81:2);break;}assert(got>=0);transferred+=(unsigned)got;if((unsigned)got<n)break;}
    drain(&u->scsi);int got=d.packet(d.opaque,1,PT_USB_IN,p,64,0);if(got==PT_USB_STALL){ctrl(&d,2,1,0,0x81);got=d.packet(d.opaque,1,PT_USB_IN,p,64,0);}assert(got==13);
    assert(pt_read_le(p,4)==0x53425355&&pt_read_le(p+4,4)==0x10203040&&p[12]==status&&pt_read_le(p+8,4)==residue);
    d.reset(d.opaque);drain(&u->scsi);free(u);
}
static void aw(PtAta *a,unsigned reg,uint8_t value){assert(!pt_ata_mmio(a,reg*16,&value,1,1));}
static void ata_cache(PtBlock *block){
    PtAta *a=calloc(1,sizeof *a);assert(a);pt_ata_init(a,block,NULL,NULL,NULL);
    aw(a,1,0x82);aw(a,7,0xef);for(unsigned k=0;k<50000&&a->busy;k++){pt_ata_service(a);usleep(100);}assert(!a->busy&&!a->native_cache&&!(a->status&1));
    aw(a,7,0xec);uint8_t identify[512];for(unsigned k=0;k<512;k+=2)assert(!pt_ata_mmio(a,0,identify+k,2,0));
    assert((pt_read_le(identify+164,2)&32)&&!(pt_read_le(identify+170,2)&32));assert(!(pt_read_le(identify+164,2)&(1u<<12)));
    aw(a,6,0xe0);aw(a,2,1);aw(a,3,1);aw(a,4,0);aw(a,5,0);aw(a,7,0x30);
    uint8_t word[2]={0x5a,0xa5};for(unsigned k=0;k<512;k+=2)assert(!pt_ata_mmio(a,0,word,2,1));
    int saw_flush=0;for(unsigned k=0;k<50000&&a->busy;k++){pt_ata_service(a);if(a->native_flush)saw_flush=1;usleep(100);}assert(saw_flush&&!a->busy&&!(a->status&1));free(a);
}
int main(void){
    char path[]="/tmp/pt-s4-storage-XXXXXX";int fd=mkstemp(path);assert(fd>=0&&!ftruncate(fd,4096));PtBlock *block=pt_block_open(path,1,NULL);assert(block);
    /* BOT 1.0 section 6.7: all thirteen host/device direction/length cases. */
    bot(block,0,0,0,0,0);bot(block,0,1,1,2,0);bot(block,0,0,2,2,0);
    bot(block,512,1,0,0,512);bot(block,1024,1,1,0,512);bot(block,512,1,1,0,0);bot(block,256,1,1,2,0);bot(block,512,1,2,2,512);
    bot(block,512,0,0,0,512);bot(block,512,0,1,2,512);bot(block,1024,0,2,0,512);bot(block,512,0,2,0,0);bot(block,256,0,2,2,0);
    ata_cache(block);
    PtScsi *s=calloc(1,sizeof *s);assert(s);pt_scsi_init(s,0);PtBlock *old;assert(!pt_scsi_media(s,block,1,&old));s->unit_attention=0;uint8_t cdb[16]={0x2a};cdb[8]=1;
    assert(!pt_scsi_command(s,cdb,10)&&s->status==2&&s->key==7&&s->asc==0x27);assert(pt_scsi_command(s,cdb,1)==EINVAL);
    memset(cdb,0,16);cdb[0]=0x88;pt_write_be(cdb+2,8,UINT64_MAX);cdb[13]=1;assert(!pt_scsi_command(s,cdb,16)&&s->status==2&&s->asc==0x21);
    s->read_only=0;memset(cdb,0,sizeof cdb);cdb[0]=0x2a;cdb[1]=8;cdb[8]=1;
    assert(!pt_scsi_command(s,cdb,10)&&s->native_fua);uint8_t data[512]={0x5a};assert(pt_scsi_write(s,data,sizeof data)==512);
    int saw_flush=0;for(unsigned k=0;k<50000;k++){pt_scsi_service(s);if(s->phase==PT_SCSI_FLUSH_WAIT)saw_flush=1;if(s->phase==PT_SCSI_DONE)break;usleep(100);}
    assert(saw_flush&&s->phase==PT_SCSI_DONE&&!s->status);
    PtNvram nv;pt_nvram_init(&nv);uint8_t bank[8192];memcpy(bank,nv.data+8192,sizeof bank);
    char value[4000];memset(value,'x',sizeof value-1);value[sizeof value-1]=0;uint8_t before[sizeof nv.data];memcpy(before,nv.data,sizeof before);assert(pt_nvram_set_of(&nv,"too-large",value)==ENOSPC&&!memcmp(before,nv.data,sizeof before));
    assert(!pt_nvram_set_of(&nv,"boot-device","hd:2")&&!pt_nvram_set_of(&nv,"boot-device","cd:")&&!memcmp(bank,nv.data+8192,sizeof bank));
    uint8_t byte=0;assert(pt_nvram_access(&nv,PT_NVRAM_SIZE,&byte,1,1)==EINVAL);nv.data[2]^=1;memcpy(before,nv.data,sizeof before);assert(pt_nvram_set_of(&nv,"x","y")==EINVAL&&!memcmp(before,nv.data,sizeof before));
    assert(pt_nvram_repair_of(&nv)==1&&!pt_nvram_repair_of(&nv)&&!pt_nvram_set_of(&nv,"x","y")&&!memcmp(bank,nv.data+8192,sizeof bank));
    free(s);assert(!pt_block_close(block)&&!close(fd)&&!unlink(path));puts("PASS: thirteen BOT cases, readonly/truncated/out-of-range SCSI and transactional NVRAM boundaries");
}
