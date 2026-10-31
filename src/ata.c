/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "ata.h"
#include <string.h>
#include "atapi.h"
#include "ata_internal.h"
#include <errno.h>
#include <stdio.h>
static void nd_ata_identify(PtAta *a){
    memset(a->buffer,0,512);uint8_t *p=a->buffer;uint64_t sectors=a->block?pt_block_size(a->block)/512:0;
    pt_write_le(p,2,a->optical?0x8580:0x0040);pt_write_le(p+2,2,16383);pt_write_le(p+6,2,16);pt_write_le(p+12,2,63);
    pt_write_le(p+94,2,0x8010);pt_write_le(p+98,2,0xb00);pt_write_le(p+106,2,6);pt_write_le(p+120,4,sectors>0x10000000?0x10000000:sectors);
    pt_write_le(p+126,2,7);pt_write_le(p+128,2,3);pt_write_le(p+118,2,0x100|a->multiple);
    for(unsigned word=65;word<=68;word++)pt_write_le(p+word*2,2,word==67?240:120);pt_write_le(p+160,2,0x7e);pt_write_le(p+164,2,(1u<<5)|(1u<<3));pt_write_le(p+170,2,(a->native_cache?1u<<5:0)|(1u<<3));pt_write_le(p+174,2,0x4000);pt_write_le(p+166,2,0x4000|1u<<12);pt_write_le(p+172,2,0x4000|1u<<12);
    nd_ata_string(p+20,20,"PT0000000001");nd_ata_string(p+54,40,a->optical?"Ptolemy DVD-ROM":"Ptolemy ATA Disk");
    a->buffer_size=512;a->position=0;a->phase=ND_ATA_IDENTIFY;a->busy=1;a->status=0x58;nd_ata_irq(a,1);
}
void pt_ata_init(PtAta *a,PtBlock *block,PtDma *dma,void (*irq)(void *,int),void *opaque){
    memset(a,0,sizeof *a);a->block=block;a->dma=dma;a->irq=irq;a->opaque=opaque;a->generation=1;a->native_cache=1;a->multiple=1;a->power=0xff;nd_ata_signature(a);
}
static void nd_ata_progress(PtAta *a){
    a->lba_next+=a->chunk_sectors;a->sectors-=a->chunk_sectors;a->count=(uint8_t)a->sectors;
    a->lba[0]=(uint8_t)a->lba_next;a->lba[1]=(uint8_t)(a->lba_next>>8);a->lba[2]=(uint8_t)(a->lba_next>>16);a->select=(a->select&0xf0)|(a->lba_next>>24&15);
}
static void nd_ata_chunk(PtAta *a){
    unsigned limit=a->native_dma?256:(a->command==0xc4||a->command==0xc5?a->multiple:1);
    if(!limit)limit=1;a->chunk_sectors=a->sectors<limit?a->sectors:limit;a->buffer_size=a->chunk_sectors*512;a->position=0;
    if(a->native_write){a->phase=a->native_dma?ND_ATA_DMA_WRITE:ND_ATA_WRITE;a->status=0x58;}
    else {a->phase=a->native_dma?ND_ATA_DMA_READ:ND_ATA_READ;a->status=0x80;a->retry=1;}
}
static void nd_ata_command(PtAta *a,uint8_t cmd){
    if(a->queued||a->busy||a->native_reset)return;nd_ata_irq(a,0);a->command=cmd;a->error=0;a->native_dma=a->native_write=a->native_flush=0;
    if(a->optical){pt_atapi_command(a,cmd);return;}
    if(!a->block){a->status=0;return;}
    switch(cmd){
    case 0xec:nd_ata_identify(a);return;
    case 0xc6:if(a->count>16||(a->count&&(a->count&(a->count-1)))){nd_ata_error(a,4);return;}a->multiple=a->count;break;
    case 0xef:
        if(a->feature==2)a->native_cache=1;
        if(a->feature==0x82){a->native_cache=0;a->native_flush=1;a->retry=a->busy=1;a->status=0x80;return;}
        if(a->feature!=3&&a->feature!=2&&a->feature!=0x82&&a->feature!=0xaa&&a->feature!=0x55){nd_ata_error(a,4);return;}break;
    case 0xe7:case 0xea:a->native_flush=1;a->retry=a->busy=1;a->status=0x80;return;
    case 0xe5:case 0x98:a->count=a->power;break;
    case 0xe0:case 0xe2:case 0xe6:case 0x94:case 0x96:case 0x99:a->power=0;break;
    case 0xe1:case 0xe3:case 0x95:case 0x97:a->power=0xff;break;
    case 0x10:case 0x90:case 0x91:break;
    case 0x20:case 0x21:case 0x30:case 0x31:case 0xc4:case 0xc5:case 0xc8:case 0xc9:case 0xca:case 0xcb:case 0x40:case 0x41:{
        uint64_t at;if(a->select&0x40)at=(uint64_t)(a->select&15)<<24|(uint32_t)pt_read_le(a->lba,3);
        else {unsigned sector=a->lba[0];if(!sector||sector>63){nd_ata_error(a,0x10);return;}at=((uint64_t)pt_read_le(a->lba+1,2)*16+(a->select&15))*63+sector-1;}
        unsigned count=a->count?a->count:256;uint64_t cap=pt_block_size(a->block)/512;
        if(at>cap||count>cap-at){nd_ata_error(a,0x10);return;}a->lba_next=at;a->sectors=count;a->native_flush=0;a->native_write=cmd==0x30||cmd==0x31||cmd==0xc5||cmd==0xca||cmd==0xcb;
        a->native_dma=cmd>=0xc8&&cmd<=0xcb;if(cmd==0x40||cmd==0x41){a->chunk_sectors=count;nd_ata_progress(a);break;}
        a->busy=1;nd_ata_chunk(a);return;}
    default:nd_ata_error(a,4);return;
    }nd_ata_done(a);
}
static int nd_ata_dma(PtAta *a){
    if(!a->dma){nd_ata_error(a,4);return -1;}
    for(unsigned budget=0;a->position<a->buffer_size&&budget<256;budget++){
        PtDmaSlice slice;int got=pt_dma_next(a->dma,&slice);if(got<0){nd_ata_error(a,4);return -1;}if(!got)return 0;
        if(slice.input==a->native_write){pt_dma_fail(a->dma);nd_ata_error(a,4);return -1;}
        unsigned take=a->buffer_size-a->position;if(take>slice.length)take=slice.length;int busy;uint8_t *ram=pt_ram_use(a->dma->ram,slice.address,take,!a->native_write,&busy);if(busy)return 2;
        if(!ram||!take){pt_dma_fail(a->dma);nd_ata_error(a,4);return -1;}
        if(a->native_write)memcpy(a->buffer+a->position,ram,take);else memcpy(ram,a->buffer+a->position,take);
        if(pt_dma_advance(a->dma,slice.generation,take)){nd_ata_error(a,4);return -1;}a->position+=take;
    }return a->position==a->buffer_size;
}
int pt_ata_service(PtAta *a){
    if(a->optical)return pt_atapi_service(a);
    if(a->queued){PtBlockRequest done;int got=pt_block_take(a->block,&done);if(got<=0)return got<0?1:0;a->queued=0;
        if(a->native_reset||done.generation!=a->generation){if(!(a->control&4))nd_ata_signature(a);return 0;}
        if(done.error){nd_ata_error(a,4);return 0;}
        if(a->native_flush){a->native_flush=0;nd_ata_done(a);return 0;}
        if(a->native_write){nd_ata_progress(a);if(!a->sectors){if(!a->native_cache){a->native_flush=1;a->retry=1;a->status=0x80;}else nd_ata_done(a);}else {nd_ata_chunk(a);nd_ata_irq(a,1);}}
        else {a->status=0x58;if(!a->native_dma)nd_ata_irq(a,1);}
    }
    if(a->native_reset)return 0;
    if(a->busy&&a->native_dma&&!a->retry&&(a->status&8)){
        int got=nd_ata_dma(a);if(got==2)return 1;if(got<=0)return 0;
        if(a->native_write){a->retry=1;a->status=0x80;}
        else {nd_ata_progress(a);if(!a->sectors){nd_ata_done(a);return 0;}nd_ata_chunk(a);}
    }
    if(a->retry){int op=a->native_flush?PT_BLOCK_FLUSH:a->native_write?PT_BLOCK_WRITE:PT_BLOCK_READ;
        PtBlockRequest request={.generation=a->generation,.operation=op,.offset=a->lba_next*512,.buffer=a->buffer,.size=op==PT_BLOCK_FLUSH?0:a->buffer_size};
        int error=pt_block_submit(a->block,&request);if(error==EAGAIN)return 1;a->retry=0;if(error)nd_ata_error(a,4);else a->queued=1;
    }return 0;
}
void pt_ata_dma_interrupt(PtAta *a){a->dma_latch=1;}
int pt_ata_mmio(PtAta *a,uint32_t at,uint8_t *p,unsigned n,int write){
    if(!p||!(n==1||n==2||n==4))return EINVAL;pt_ata_service(a);
    if(at==0x200){if(write)a->timing=(uint32_t)pt_read_le(p,n);else pt_write_le(p,n,a->timing);return 0;}
    /* The KeyLargo ATA interrupt register reports the drive's INTRQ in bit 30 (unmasked by nIEN) beside the DBDMA completion
     * in bit 31. Tiger's handler ignores interrupts that show neither; without bit 30 it never read IDENTIFY data. */
    if(at==0x300){if(write){if(pt_read_le(p,n)&0x80000000u)a->dma_latch=0;}else pt_write_le(p,n,(a->dma_latch?0x80000000u:0)|(a->pending&&!(a->control&2)?0x40000000u:0));return 0;}
    if(at%16)return EINVAL;unsigned reg=at/16;if(reg>8&&reg!=22){if(!write)memset(p,0,n);return 0;}
    if(reg==8||reg==22){if(!write){pt_write_le(p,n,a->select&16?0:a->status);return 0;}   /* absent device 1 reads 00h (ATA/ATAPI-6 9.16); otherwise Tiger probes a phantom slave */
        uint8_t old=a->control;a->control=p[0];
        if(!(old&4)&&(a->control&4)){a->generation++;a->native_reset=1;a->status=0x80;a->busy=1;a->retry=0;nd_ata_irq(a,0);if(a->optical)pt_atapi_reset(a);}
        if((old&4)&&!(a->control&4)&&!a->queued&&(!a->optical||!a->optical->pending))nd_ata_signature(a);nd_ata_irq(a,a->pending);return 0;}
    if(a->select&16){if(write&&reg==6)a->select=p[0];else if(!write)memset(p,0,n);return 0;}
    if(!reg){
        if(a->optical)return pt_atapi_data(a,p,n,write);
        if(!(a->status&8)||a->native_dma||a->position+n>a->buffer_size){if(!write)memset(p,0xff,n);return 0;}
        if(write){if(a->phase!=ND_ATA_WRITE)return 0;memcpy(a->buffer+a->position,p,n);}else {if(a->phase!=ND_ATA_READ&&a->phase!=ND_ATA_IDENTIFY)return 0;memcpy(p,a->buffer+a->position,n);}a->position+=n;
        if(a->position==a->buffer_size){if(a->phase==ND_ATA_IDENTIFY)nd_ata_done(a);else if(write){a->retry=1;a->status=0x80;}else {nd_ata_progress(a);if(a->sectors)nd_ata_chunk(a);else nd_ata_done(a);}}return 0;
    }
    if(write){switch(reg){case 1:a->feature=p[0];break;case 2:a->count=p[0];break;case 3:case 4:case 5:a->lba[reg-3]=p[0];break;case 6:a->select=p[0];break;case 7:nd_ata_command(a,p[0]);break;}}
    else {uint8_t value=reg==1?a->error:reg==2?a->count:reg>=3&&reg<=5?a->lba[reg-3]:reg==6?a->select:a->status;pt_write_le(p,n,value);if(reg==7)nd_ata_irq(a,0);}return 0;
}
