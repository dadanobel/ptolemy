/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "dma.h"
#include <string.h>
/* Apple DBDMA: descriptor state is separate from the stream lease. Pausing,
 * reset and descriptor completion revoke old leases before another copy. */
static int nd_dma_condition(PtDma *d,unsigned select,unsigned mode){
    int condition=(d->regs[1]&(d->regs[select]>>16))==(d->regs[select]&0xffff);
    return mode==3||(mode==1&&condition)||(mode==2&&!condition);
}
static void nd_dma_status(PtDma *d){
    /* The load announced this write; a refusal now (the GPU took the page since) fails the channel, never skips the status. */
    if(!d->loaded)return;uint8_t *p=pt_ram_use(d->ram,d->descriptor,16,1,NULL);if(!p){d->regs[1]=(d->regs[1]|PT_DMA_DEAD)&~PT_DMA_ACTIVE;return;}
    pt_write_le(p+12,2,d->count-d->done);pt_write_le(p+14,2,d->regs[1]);
}
void pt_dma_fail(PtDma *d){
    d->regs[1]=(d->regs[1]|PT_DMA_DEAD)&~PT_DMA_ACTIVE;nd_dma_status(d);d->generation++;
}
static int nd_dma_finish(PtDma *d){
    if(!d->loaded||!d->transferred)return 0;
    if(nd_dma_condition(d,6,d->command&3)&&!(d->regs[1]&PT_DMA_WAKE)){nd_dma_status(d);return 0;}
    d->regs[1]&=~PT_DMA_WAKE;
    int branch=nd_dma_condition(d,5,d->command>>2&3);
    if(branch)d->regs[1]|=PT_DMA_BT;else d->regs[1]&=~PT_DMA_BT;
    nd_dma_status(d);d->regs[3]=branch?d->dependency:d->descriptor+16;
    d->loaded=d->transferred=0;d->generation++;
    if(nd_dma_condition(d,4,d->command>>4&3)&&d->interrupt)d->interrupt(d->opaque);
    return 1;
}
void pt_dma_init(PtDma *d,PtRam ram,PtBus *bus,void (*irq)(void *),void *opaque){
    memset(d,0,sizeof *d);d->ram=ram;d->bus=bus;d->interrupt=irq;d->opaque=opaque;d->generation=d->reset_generation=1;
}
uint32_t pt_dma_read(PtDma *d,unsigned offset){return !(offset&3)&&offset<64?d->regs[offset/4]:0;}
void pt_dma_write(PtDma *d,unsigned offset,uint32_t value){
    if((offset&3)||offset>=64)return;
    if(!offset){
        uint32_t mask=value>>16,old=d->regs[1];d->regs[0]=value;
        d->regs[1]=(old&~(mask&~PT_DMA_ACTIVE))|(value&mask&~PT_DMA_ACTIVE);
        if((mask&PT_DMA_RUN)&&!(value&PT_DMA_RUN)){
            /* Stopping publishes the descriptor status, which is the flush, so FLUSH clears here too: Tiger's DBDMA stop clears
             * RUN and sets FLUSH in one write, then waits for both ACTIVE and FLUSH to drop. */
            nd_dma_status(d);d->loaded=d->transferred=0;d->regs[1]&=~(PT_DMA_ACTIVE|PT_DMA_DEAD|PT_DMA_BT|PT_DMA_FLUSH);d->generation++;d->reset_generation++;
        }else{
            if((old^d->regs[1])&PT_DMA_PAUSE){nd_dma_status(d);d->generation++;}
            if(d->regs[1]&PT_DMA_FLUSH){nd_dma_status(d);d->regs[1]&=~PT_DMA_FLUSH;d->generation++;}
            if((d->regs[1]&PT_DMA_RUN)&&!(d->regs[1]&(PT_DMA_PAUSE|PT_DMA_DEAD)))d->regs[1]|=PT_DMA_ACTIVE;else d->regs[1]&=~PT_DMA_ACTIVE;
        }
    }else if(offset!=4){d->regs[offset/4]=value;if(offset==12){d->loaded=d->transferred=0;d->generation++;}}
}
int pt_dma_next(PtDma *d,PtDmaSlice *slice){
    if(!slice)return -1;
    for(unsigned budget=0;budget<128;budget++){
        if(d->regs[1]&PT_DMA_DEAD)return -1;
        if(!(d->regs[1]&PT_DMA_ACTIVE)||d->regs[1]&PT_DMA_PAUSE)return 0;
        if(d->loaded&&d->transferred){if(!nd_dma_finish(d))return 0;continue;}
        if(!d->loaded){
            int busy=0;uint8_t *p=d->regs[2]||d->regs[3]&15?NULL:pt_ram_use(d->ram,d->regs[3],16,1,&busy);   /* its status is written back */
            if(busy)return 0;if(!p){pt_dma_fail(d);return -1;}
            d->descriptor=d->regs[3];d->count=(uint16_t)pt_read_le(p,2);d->command=(uint16_t)pt_read_le(p+2,2);
            d->address=(uint32_t)pt_read_le(p+4,4);d->dependency=(uint32_t)pt_read_le(p+8,4);d->done=0;d->loaded=1;d->transferred=0;d->generation++;
        }
        unsigned op=d->command>>12,key=d->command>>8&7;
        if(op<=3){
            if(key||!pt_ram(d->ram,d->address,d->count)){pt_dma_fail(d);return -1;}
            if(!d->count){d->transferred=1;continue;}
            *slice=(PtDmaSlice){d->address+d->done,d->count-d->done,(int)(op>>1),(int)(op&1),d->generation};return 1;
        }
        if(op==7){d->regs[1]&=~PT_DMA_ACTIVE;nd_dma_status(d);d->loaded=0;d->generation++;return 0;}
        if(op==6){d->transferred=1;continue;}
        if((op==4||op==5)&&d->count>=1&&d->count<=4&&(key==5||key==6||key==7)){
            uint8_t bytes[4]={};int write=op==4;if(write)pt_write_le(bytes,d->count,d->dependency);
            int busy,error=0;uint8_t *p=pt_ram_use(d->ram,d->address,d->count,write,&busy);if(busy)return 0;
            if(p){if(write)memcpy(p,bytes,d->count);else memcpy(bytes,p,d->count);}
            else error=!d->bus||pt_bus_access(d->bus,d->address,bytes,d->count,write);
            if(error){pt_dma_fail(d);return -1;}
            if(!write){d->dependency=(uint32_t)pt_read_le(bytes,d->count);uint8_t *desc=pt_ram_use(d->ram,d->descriptor,16,1,NULL);if(!desc){pt_dma_fail(d);return -1;}pt_write_le(desc+8,4,d->dependency);}
            d->done=d->count;d->transferred=1;continue;
        }
        pt_dma_fail(d);return -1;
    }return 0;
}
int pt_dma_advance(PtDma *d,uint64_t generation,unsigned bytes){
    if(!d->loaded||d->transferred||generation!=d->generation||!(d->regs[1]&PT_DMA_ACTIVE)||d->regs[1]&(PT_DMA_DEAD|PT_DMA_PAUSE)||bytes>d->count-d->done)return -1;
    d->done=(uint16_t)(d->done+bytes);d->generation++;if(d->done==d->count){d->transferred=1;(void)nd_dma_finish(d);}return 0;
}
void pt_dma_device_status(PtDma *d,uint8_t value){d->regs[1]=(d->regs[1]&~255u)|value;}
