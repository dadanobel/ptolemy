/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "board.h"
#include "atapi.h"
#include "usb_msc.h"
#include "ohci.h"
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
static void await(PtScsi *s) {
    for(unsigned i=0;i<50000;i++) {
        pt_scsi_service(s);
        if(!s->pending && !s->retry)return;
        usleep(100);
    }assert(!"SCSI timeout");
}
static void control(PtUsbDevice *d,unsigned type,unsigned req,unsigned value,unsigned index) {
    uint8_t p[8]={type,req,value,value>>8,index,index>>8,0,0};
    assert(d->packet(d->opaque,0,PT_USB_SETUP,p,8,0)==8);
    assert(d->packet(d->opaque,0,PT_USB_IN,p,0,0)==0);
}
static void cbw(PtUsbDevice *d,const uint8_t *cdb,unsigned len,unsigned bytes,int input) {
    uint8_t p[31]={0};pt_write_le(p,4,0x43425355);pt_write_le(p+4,4,0x12345678);
    pt_write_le(p+8,4,bytes);p[12]=input?128:0;p[14]=len;memcpy(p+15,cdb,len);
    assert(d->packet(d->opaque,2,PT_USB_OUT,p,31,0)==31);
}
static void csw(PtUsbDevice *d,unsigned residue,unsigned status) {
    uint8_t p[64];int got=d->packet(d->opaque,1,PT_USB_IN,p,64,0);
    /* BOT 6.7.2 permits a short packet and requires clearing the IN halt. */
    if(got==PT_USB_STALL){control(d,2,1,0,0x81);got=d->packet(d->opaque,1,PT_USB_IN,p,64,0);}assert(got==13);
    assert(pt_read_le(p,4)==0x53425355 && pt_read_le(p+4,4)==0x12345678);
    assert(pt_read_le(p+8,4)==residue && p[12]==status);
}
static void aw(PtAta *a,unsigned r,unsigned value){uint8_t b=value;assert(!pt_ata_mmio(a,r*16,&b,1,1));}
int main(void) {
    uint64_t bytes;assert(!pt_board_ram(2048,&bytes) && bytes==0x80000000ull);assert(pt_board_ram(2049,&bytes)==EINVAL);
    uint8_t cfg[256]={0},mask[256]={0};PtAgp agp;pt_agp_init(cfg,mask,&agp);
    assert(cfg[0x34]==0x80 && cfg[0x80]==2 && pt_read_le(cfg+0x98,4)==1 && !mask[0x98]);
    pt_write_le(cfg+0x8c,4,0x12345001);pt_agp_update(&agp,cfg,0x8e,1);assert(agp.gart==0x12345001 && agp.generation==1);
    PtNvram nv;pt_nvram_init(&nv);uint8_t osx[8192];memcpy(osx,nv.data+8192,8192);
    assert(!pt_board_boot(&nv,1,"cd:,\\\\:tbxi") && !memcmp(osx,nv.data+8192,8192));
    uint8_t of[4096];for(unsigned i=0;i<4096;i++)of[i]=nv.data[2*i];
    assert(memmem(of,sizeof of,"PowerBook5,8",12) && memmem(of,sizeof of,"auto-boot?=true",15));
    assert(pt_nvram_set_of(&nv,"bad=name","x")==EINVAL);
    char path[]="/tmp/pt-product-XXXXXX";int fd=mkstemp(path);assert(fd>=0);
    enum { SIZE=512*1024 };uint8_t *expected=malloc(SIZE),*got=malloc(SIZE);assert(expected && got);
    for(unsigned i=0;i<SIZE;i++)expected[i]=(i*7)^(i/512);
    assert(write(fd,expected,SIZE)==SIZE);
    PtBlock *b=pt_block_open(path,1,NULL);assert(b);
    PtScsi *s=calloc(1,sizeof *s);pt_scsi_init(s,1);PtBlock *old;
    assert(!pt_scsi_media(s,b,1,&old) && !old);
    uint8_t cdb[16]={0};assert(!pt_scsi_command(s,cdb,6) && s->status==2 && s->key==6);
    cdb[0]=3;cdb[4]=18;assert(!pt_scsi_command(s,cdb,6));assert(pt_scsi_read(s,got,18)==18 && got[2]==6 && got[12]==0x28);
    memset(cdb,0,16);cdb[0]=0xa8;pt_write_be(cdb+6,4,SIZE/2048);assert(!pt_scsi_command(s,cdb,12));
    unsigned pos=0;
    while(s->phase!=PT_SCSI_DONE){await(s);int n=pt_scsi_read(s,got+pos,1033);assert(n>=0);pos+=n;}
    assert(pos==SIZE && !memcmp(got,expected,SIZE) && !s->status);
    assert(!pt_scsi_command(s,cdb,12));pt_scsi_service(s);assert(s->pending);
    pt_scsi_reset(s);assert(pt_scsi_command(s,cdb,12)==EBUSY && pt_scsi_media(s,NULL,1,&old)==EBUSY);await(s);
    assert(s->phase==PT_SCSI_IDLE);
    PtAta *a=calloc(1,sizeof *a);pt_ata_init(a,NULL,NULL,NULL,NULL);pt_atapi_bind(a,s);
    assert(a->lba[1]==0x14 && a->lba[2]==0xeb);
    aw(a,6,0xb0);uint8_t absent=255;assert(!pt_ata_mmio(a,0x40,&absent,1,0) && !absent);aw(a,6,0xa0);
    aw(a,1,0);aw(a,4,8);aw(a,5,0);aw(a,7,0xa0);
    memset(cdb,0,12);cdb[0]=0x12;cdb[4]=36;
    for(unsigned i=0;i<12;i+=2)assert(!pt_ata_mmio(a,0,cdb+i,2,1));
    pos=0;while(a->busy) {
        assert((a->status&8) && a->count==2);
        unsigned size=((unsigned)a->lba[2]<<8)|a->lba[1];assert(size<=8);
        for(unsigned i=0;i<size;i+=2)assert(!pt_ata_mmio(a,0,got+pos+i,2,0));pos+=size;
    }
    assert(pos==36 && got[0]==5 && a->count==3 && a->pending);
    uint8_t dma_ram[65536]={0};PtDma dma;pt_dma_init(&dma,(PtRam){dma_ram,sizeof dma_ram,NULL},NULL,NULL,NULL);a->dma=&dma;
    for(unsigned i=0;i<2;i++){uint8_t *d=dma_ram+0x100+i*16;pt_write_le(d,2,8192);pt_write_le(d+2,2,i?0x3000:0x2000);pt_write_le(d+4,4,0x2000+i*8192);}
    pt_write_le(dma_ram+0x122,2,0x7000);pt_dma_write(&dma,12,0x100);pt_dma_write(&dma,0,((uint32_t)PT_DMA_RUN<<16)|PT_DMA_RUN);
    aw(a,1,1);aw(a,4,0xff);aw(a,5,0xff);aw(a,7,0xa0);memset(cdb,0,12);cdb[0]=0x28;cdb[8]=8;
    for(unsigned i=0;i<12;i+=2)assert(!pt_ata_mmio(a,0,cdb+i,2,1));
    for(unsigned i=0;a->busy && i<50000;i++){pt_ata_service(a);usleep(100);}
    assert(!a->busy && !(a->status&1) && !memcmp(dma_ram+0x2000,expected,16384));
    aw(a,1,0);aw(a,7,0xa0);for(unsigned i=0;i<12;i+=2)assert(!pt_ata_mmio(a,0,cdb+i,2,1));
    pt_ata_service(a);assert(s->pending);aw(a,8,4);assert(a->status&0x80);await(s);pt_ata_service(a);assert(a->status&0x80);
    aw(a,8,0);assert(!(a->status&0x80) && a->lba[1]==0x14 && a->lba[2]==0xeb);
    PtUsbMsc *u=calloc(1,sizeof *u);pt_usb_msc_init(u);assert(!pt_scsi_media(&u->scsi,b,0,&old));u->scsi.unit_attention=0;
    PtUsbDevice dev=pt_usb_msc_device(u);control(&dev,0,5,7,0);assert(dev.address(dev.opaque)==7);control(&dev,0,9,1,0);
    memset(cdb,0,16);cdb[0]=0x28;pt_write_be(cdb+7,2,1);cbw(&dev,cdb,10,1024,1);
    for(unsigned i=0;i<8;i++){await(&u->scsi);assert(dev.packet(dev.opaque,1,PT_USB_IN,got+i*64,64,0)==64);}
    assert(!memcmp(got,expected,512));assert(dev.packet(dev.opaque,1,PT_USB_IN,got,64,0)==0);csw(&dev,512,0);
    memset(cdb,0,16);cdb[0]=0x2a;pt_write_be(cdb+2,4,2);pt_write_be(cdb+7,2,1);cbw(&dev,cdb,10,512,0);
    for(unsigned i=0;i<8;i++)assert(dev.packet(dev.opaque,2,PT_USB_OUT,expected+i*64,64,0)==64);
    await(&u->scsi);csw(&dev,0,0);assert(pread(fd,got,512,1024)==512 && !memcmp(got,expected,512));
    u->scsi.read_only=1;cbw(&dev,cdb,10,512,0);
    assert(dev.packet(dev.opaque,2,PT_USB_OUT,got,64,0)==PT_USB_STALL);csw(&dev,512,1);
    control(&dev,2,1,0,2);memset(cdb,0,6);cdb[0]=3;cdb[4]=18;cbw(&dev,cdb,6,18,1);
    assert(dev.packet(dev.opaque,1,PT_USB_IN,got,18,0)==18 && got[2]==7 && got[12]==0x27);csw(&dev,0,0);
    uint8_t invalid[31]={0};assert(dev.packet(dev.opaque,2,PT_USB_OUT,invalid,31,0)==PT_USB_STALL);
    control(&dev,0x21,0xff,0,0);control(&dev,2,1,0,0x81);control(&dev,2,1,0,2);
    memset(cdb,0,6);cbw(&dev,cdb,6,0,0);csw(&dev,0,0);
    PtHid hid[3];for(unsigned i=0;i<3;i++)pt_hid_init(&hid[i],i);
    uint8_t ram[4096]={0};PtOhci ohci;pt_ohci_init(&ohci,(PtRam){ram,sizeof ram,NULL},&hid[0],&hid[1],&hid[2],NULL,NULL);
    assert((pt_ohci_read(&ohci,0x48)&255)==4 && !(pt_ohci_read(&ohci,0x60)&1));
    pt_ohci_storage(&ohci,&dev);assert(pt_ohci_read(&ohci,0x60)&1);pt_ohci_write(&ohci,0x60,16);
    for(unsigned i=0;i<10;i++)pt_ohci_frame(&ohci);assert(ohci.default_port==3 && (ohci.ports[3]&3)==3);
    pt_ohci_storage(&ohci,NULL);assert(!(pt_ohci_read(&ohci,0x60)&1));
    for(unsigned i=0;i<3;i++)pt_hid_destroy(&hid[i]);
    pt_block_close(b);close(fd);assert(!unlink(path));free(expected);free(got);free(s);free(a);free(u);
    puts("product: board, optical streaming/reset, ATAPI PIO/DMA/SRST, USB BOT read/write/ZLP/recovery, hotplug passed");
}
