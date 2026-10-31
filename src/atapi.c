/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "atapi.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
/* A packet device signals DRQ as 48h; bit 4 would be SERV. */
#include "ata_internal.h"
#include <errno.h>
void pt_atapi_bind(PtAta *a,PtScsi *s){
    a->optical=s;nd_ata_signature(a);
}
void pt_atapi_reset(PtAta *a){
    if(a->optical)pt_scsi_reset(a->optical);
    a->generation++;a->native_reset=1;a->buffer_size=a->position=0;a->busy=1;a->status=0x80;nd_ata_irq(a,0);
}
static void nd_atapi_done(PtAta *a){
    a->count=3;a->lba[1]=a->lba[2]=0;nd_ata_done(a);if(a->optical->status){a->status|=1;a->error=(uint8_t)(a->optical->key<<4|4);}
}
void pt_atapi_command(PtAta *a,uint8_t command){
    a->command=command;a->position=a->buffer_size=0;
    switch(command){
    case 0xa1:
        memset(a->buffer,0,512);pt_write_le(a->buffer,2,0x8580);pt_write_le(a->buffer+98,2,0x300);pt_write_le(a->buffer+126,2,7);pt_write_le(a->buffer+160,2,0x7e);
        /* Word 53 bit 1 validates word 64 (PIO modes 3-4); without it Tiger left the drive in PIO mode 0 without DMA. */
        pt_write_le(a->buffer+106,2,2);pt_write_le(a->buffer+128,2,3);
        nd_ata_string(a->buffer+20,20,"PT0000000002");nd_ata_string(a->buffer+46,8,"1.0");nd_ata_string(a->buffer+54,40,"Ptolemy DVD-ROM");   /* serial number, firmware revision, model; without them Tiger read empty strings */
        a->buffer_size=512;a->phase=ND_ATA_IDENTIFY;a->busy=1;a->status=0x48;nd_ata_irq(a,1);break;
    case 0xa0:
        a->packet_limit=((unsigned)a->lba[2]<<8|a->lba[1])&~1u;if(!a->packet_limit)a->packet_limit=65536;
        a->native_dma=a->feature&1;a->phase=ND_ATAPI_PACKET;a->buffer_size=12;a->busy=1;a->status=0x48;a->count=1;break;
    case 8:pt_atapi_reset(a);break;
    case 0xef:if(a->feature==3)nd_ata_done(a);else nd_ata_error(a,4);break;
    case 0x90:nd_ata_signature(a);nd_ata_irq(a,1);break;
    default:nd_ata_error(a,4);if(command==0xec){a->lba[1]=0x14;a->lba[2]=0xeb;}break;
    }
}
int pt_atapi_service(PtAta *a){
    PtScsi *s=a->optical;if(!s)return 0;int retry=pt_scsi_service(s);
    if(a->native_reset){if(!s->pending&&!s->retry&&!(a->control&4))nd_ata_signature(a);return retry;}
    if(!a->busy||a->phase==ND_ATAPI_PACKET||a->phase==ND_ATA_IDENTIFY)return retry;
    for(unsigned budget=0;budget<256;budget++){
        if(a->position==a->buffer_size){
            if(s->pending||s->retry)return retry||s->retry;
            if(s->phase==PT_SCSI_DONE||s->phase==PT_SCSI_IDLE){nd_atapi_done(a);return 0;}
            a->position=a->buffer_size=0;
            if(s->phase==PT_SCSI_IN){unsigned size=s->length-s->position;if(size>a->packet_limit&&!a->native_dma)size=a->packet_limit;if(size>sizeof a->buffer)size=sizeof a->buffer;
                int got=pt_scsi_read(s,a->buffer,size);if(got<0)return 1;a->buffer_size=(unsigned)got;a->phase=ND_ATAPI_IN;}
            else if(s->phase==PT_SCSI_OUT){unsigned size=s->length-s->position;if(size>a->packet_limit&&!a->native_dma)size=a->packet_limit;a->buffer_size=size;a->phase=ND_ATAPI_OUT;}
            else return 1;
            a->status=0x48;a->count=a->phase==ND_ATAPI_IN?2:0;a->lba[1]=(uint8_t)a->buffer_size;a->lba[2]=(uint8_t)(a->buffer_size>>8);
            if(!a->native_dma){nd_ata_irq(a,1);return 0;}
        }
        if(!a->native_dma)return 0;if(!a->dma){nd_ata_error(a,4);return 0;}
        PtDmaSlice slice;int got=pt_dma_next(a->dma,&slice);if(got<0){nd_ata_error(a,4);return 0;}if(!got)return 0;
        int input=a->phase==ND_ATAPI_IN;if(slice.input!=input){pt_dma_fail(a->dma);nd_ata_error(a,4);return 0;}
        unsigned count=a->buffer_size-a->position;if(count>slice.length)count=slice.length;int busy;uint8_t *ram=pt_ram_use(a->dma->ram,slice.address,count,input,&busy);if(busy)return 1;
        if(!ram||!count){pt_dma_fail(a->dma);nd_ata_error(a,4);return 0;}
        if(input)memcpy(ram,a->buffer+a->position,count);else memcpy(a->buffer+a->position,ram,count);
        if(pt_dma_advance(a->dma,slice.generation,count)){nd_ata_error(a,4);return 0;}a->position+=count;
        if(a->position==a->buffer_size&&!input){int wrote=pt_scsi_write(s,a->buffer,a->buffer_size);if(wrote!=(int)a->buffer_size){nd_ata_error(a,4);return 0;}}
    }return 1;
}
int pt_atapi_data(PtAta *a,uint8_t *p,unsigned n,int write){
    if(!(a->status&8)||a->position+n>a->buffer_size){if(!write)memset(p,0xff,n);return 0;}
    if(write){if(a->phase!=ND_ATAPI_PACKET&&a->phase!=ND_ATAPI_OUT)return 0;memcpy(a->buffer+a->position,p,n);}
    else {if(a->phase!=ND_ATA_IDENTIFY&&a->phase!=ND_ATAPI_IN)return 0;memcpy(p,a->buffer+a->position,n);}a->position+=n;
    if(a->position==a->buffer_size){
        if(a->phase==ND_ATAPI_PACKET){int error=pt_scsi_command(a->optical,a->buffer,12);a->position=a->buffer_size=0;a->phase=ND_ATAPI_WAIT;a->status=0x80;if(error){nd_ata_error(a,4);return 0;}}
        else if(a->phase==ND_ATA_IDENTIFY){nd_ata_done(a);return 0;}
        else if(a->phase==ND_ATAPI_OUT&&pt_scsi_write(a->optical,a->buffer,a->buffer_size)!=(int)a->buffer_size){nd_ata_error(a,4);return 0;}
        pt_atapi_service(a);
    }return 0;
}
