/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "devices.h"
#include "atapi.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
/* Board wiring is a physical map, not a second scheduler in each device.
 * CPU ownership covers MMIO, DMA publication, interrupts and guest time. */
static void nd_devices_pulse(PtDevices *d,unsigned source){pt_pic_line(d->pic,source,1,1);pt_pic_line(d->pic,source,1,0);}
static void nd_devices_dma_irq(void *opaque){
    PtDeviceLine *line=opaque;PtDevices *d=line->owner;unsigned source;
    switch(line->index){case 0x10:source=9;if(d->irq_last){uint64_t delta=d->sound.now-d->irq_last;d->irq_n++;d->irq_sum+=delta;if(delta>d->irq_max)d->irq_max=delta;}d->irq_last=d->sound.now;break;
    case 0x12:source=10;break;case 0x16:source=2;pt_ata_dma_interrupt(&d->ata[0]);break;case 0x1a:source=3;pt_ata_dma_interrupt(&d->ata[1]);break;
    default:d->unbound_dma|=1u<<line->index;return;}nd_devices_pulse(d,source);
}
static void nd_devices_ata_irq(void *opaque,int level){PtDeviceLine *line=opaque;pt_pic_line(line->owner->pic,13+line->index,1,level);}
static void nd_devices_usb_irq(void *opaque,int level){PtDevices *d=opaque;pt_pic_line(d->pic,28,1,level);}
static void nd_devices_via_irq(void *opaque,int level){PtDevices *d=opaque;pt_pic_line(d->pic,25,1,level);}
static void nd_devices_gpio_irq(void *opaque,unsigned pin,int level){PtDevices *d=opaque;pt_pic_line(d->pic,pin==1?47:55,1,level);}
int pt_devices_init(PtDevices *d,PtRam ram,PtBus *bus,PtPic *pic,PtEvent *event,_Atomic uint32_t *kick,const char *raw,int audio,uint64_t now,uint32_t seconds){
    memset(d,0,sizeof *d);d->pic=pic;d->origin=now;d->usb_at=now+1000000;d->service_at=UINT64_MAX;d->level_min=UINT32_MAX;
    pt_input_init(&d->input,event,kick);for(unsigned k=0;k<32;k++){d->dma_lines[k]=(PtDeviceLine){d,k};pt_dma_init(&d->dma[k],ram,bus,nd_devices_dma_irq,&d->dma_lines[k]);}
    for(unsigned k=0;k<3;k++)pt_hid_init(&d->hid[k],k);
    pt_gpio_init(&d->gpio,nd_devices_gpio_irq,d);pt_pmu_init(&d->pmu,&d->gpio,now,seconds,nd_devices_via_irq,d);
    pt_ohci_init(&d->usb,ram,&d->hid[0],&d->hid[1],&d->hid[2],nd_devices_usb_irq,d);pt_usb_msc_init(&d->storage);
    unsigned latency=25;const char *text=getenv("PT_PCM_LATENCY_MS");if(text){char *end;unsigned long n=strtoul(text,&end,10);if(*text&&!*end&&n<=1000)latency=(unsigned)n;}
    pt_pcm_init(&d->pcm,latency*PT_PCM_RATE/1000);pt_screamer_init(&d->sound,&d->dma[16],&d->pcm);
    for(unsigned k=0;k<2;k++){d->ata_lines[k]=(PtDeviceLine){d,k};pt_ata_init(&d->ata[k],NULL,&d->dma[k?26:22],nd_devices_ata_irq,&d->ata_lines[k]);}
    pt_scsi_init(&d->optical,1);pt_atapi_bind(&d->ata[1],&d->optical);
    if(raw){PtBlock *block=pt_block_open(raw,0,event);if(!block){int e=errno;pt_devices_close(d);return e;}int e=pt_devices_disk(d,block,kick);if(e){pt_block_close(block);pt_devices_close(d);return e;}}
    if(audio){int32_t status=0;d->audio=pt_audio_open(&d->pcm,&status);if(!d->audio){pt_devices_close(d);return status?EIO:ENODEV;}}
    return 0;
}
int pt_devices_disk(PtDevices *d,PtBlock *block,_Atomic uint32_t *kick){
    if(!block||d->block||block==d->optical.block||block==d->storage.scsi.block)return EINVAL;
    pt_block_set_kick(block,kick);d->block=block;pt_ata_init(&d->ata[0],block,&d->dma[22],nd_devices_ata_irq,&d->ata_lines[0]);return 0;
}
int pt_devices_close(PtDevices *d){
    if(d->audio){if(pt_audio_close(d->audio))return EIO;d->audio=NULL;}
    PtBlock *blocks[]={d->block,d->optical.block,d->storage.scsi.block};int error=0;
    for(unsigned k=0;k<3;k++)if(blocks[k]){int e=pt_block_close(blocks[k]);if(!error)error=e;}
    d->block=d->optical.block=d->storage.scsi.block=NULL;for(unsigned k=0;k<3;k++)pt_hid_destroy(&d->hid[k]);return error;
}
int pt_devices_storage(PtDevices *d,PtBlock *block,int read_only,PtBlock **old){
    if(old)*old=NULL;
    if(block&&block==d->storage.scsi.block)return EALREADY;if(block&&(block==d->block||block==d->optical.block))return EINVAL;
    if(!pt_usb_msc_quiescent(&d->storage))return EBUSY;int e=pt_scsi_media(&d->storage.scsi,block,read_only,old);if(e)return e;
    PtUsbDevice device=pt_usb_msc_device(&d->storage);pt_ohci_storage(&d->usb,block?&device:NULL);d->service_requested=1;return 0;
}
int pt_devices_optical(PtDevices *d,PtBlock *block,PtBlock **old){
    if(old)*old=NULL;
    if(block&&block==d->optical.block)return EALREADY;if(block&&(block==d->block||block==d->storage.scsi.block))return EINVAL;
    /* While Tiger holds the tray locked (PREVENT, a mounted disc) the drive refuses a swap, as a locked tray does: a disc
     * swapped under a mounted volume left Tiger reading the new image through the old file system. */
    if(d->optical.locked&&d->optical.present&&d->optical.block)return EPERM;
    if(d->ata[1].busy)return EBUSY;int e=pt_scsi_media(&d->optical,block,1,old);if(!e)d->service_requested=1;return e;
}
int pt_devices_macio(PtDevices *d,uint32_t at,uint8_t *p,unsigned n,int write,uint64_t now){
    if(!p||!n||n>8)return EINVAL;
    if(at>=0x50&&at<0x7c&&n==1){if(write)pt_gpio_write(&d->gpio,at-0x50,p[0]);else p[0]=pt_gpio_read(&d->gpio,at-0x50);return 0;}
    if(at>=0x16000&&at<0x18000&&n==1){if(write){pt_pmu_write(&d->pmu,at-0x16000,p[0],now);d->service_requested=1;}else p[0]=pt_pmu_read(&d->pmu,at-0x16000,now);return 0;}
    /* The KeyLargo counter, 64 bits at 18.432 MHz from machine start, low word at 0x15038, high word at 0x1503c. Without it
     * AppleKeyLargo's timebase calibration read 0 and the kernel panicked "invalid constant 0 / 1". */
    if((at==0x15038||at==0x1503c)&&n==4&&!write){uint64_t ticks=(uint64_t)((unsigned __int128)(now-d->origin)*18432000u/1000000000u);
        pt_write_le(p,4,at==0x15038?(uint32_t)ticks:(uint32_t)(ticks>>32));return 0;}
    if(at>=0x8000&&at<0x9000&&n==4&&!(at&3)){unsigned index=(at-0x8000)/128,reg=(at-0x8000)%128;if(index==16)pt_screamer_service(&d->sound,now);
        if(write){pt_dma_write(&d->dma[index],reg,(uint32_t)pt_read_le(p,4));if(index==16)pt_screamer_service(&d->sound,now);d->service_requested=1;}
        else pt_write_le(p,4,pt_dma_read(&d->dma[index],reg));return 0;}
    if(at>=0x14000&&at<=0x14050&&n==4&&!(at&15)){if(write){pt_screamer_write(&d->sound,at-0x14000,(uint32_t)pt_read_le(p,4),now);d->service_requested=1;}else pt_write_le(p,4,pt_screamer_read(&d->sound,at-0x14000,now));return 0;}
    if(at>=0x20000&&at<0x22000){int e=pt_ata_mmio(&d->ata[(at-0x20000)/4096],at&4095,p,n,write);d->service_requested=1;return e;}
    return ENOENT;
}
void pt_devices_service(PtDevices *d,uint64_t now){
    PtInputEvent event;for(unsigned budget=0;budget<128&&pt_input_take(&d->input,&event);budget++){
        if(event.type==PT_INPUT_KEY)pt_hid_key(&d->hid[0],event.usage,event.pressed);
        else if(event.type==PT_INPUT_RELATIVE)pt_hid_pointer(&d->hid[1],event.buttons,event.x,event.y,event.wheel);
        else if(event.type==PT_INPUT_ABSOLUTE)pt_hid_pointer(&d->hid[2],event.buttons,event.x,event.y,event.wheel);
        else if(event.type==PT_INPUT_RELEASE)for(unsigned k=0;k<3;k++)pt_hid_release(&d->hid[k]);
    }
    pt_pmu_service(&d->pmu,now);int retry=pt_ata_service(&d->ata[0])|pt_ata_service(&d->ata[1])|pt_usb_msc_service(&d->storage);
    /* Control descriptors (NOP, STORE/LOAD QUAD, STOP, waits) run on every active channel, not only inside an ATA transfer
     * (otherwise "AppleMacIO self test fails"); stream descriptors stay parked until their controller takes them. Audio TX
     * (channel 16) has its own owner. */
    for(unsigned k=0;k<32;k++)if(k!=16&&(d->dma[k].regs[1]&PT_DMA_ACTIVE)){PtDmaSlice slice;int ready=pt_dma_next(&d->dma[k],&slice);
        if((d->dma[k].regs[1]&PT_DMA_ACTIVE)&&ready!=1)retry=1;}
    int clock=pt_ohci_needs_clock(&d->usb);if(!clock||!d->native_usb_clock)d->usb_at=now+1000000;d->native_usb_clock=clock;
    if(clock&&now>=d->usb_at){uint64_t count=(now-d->usb_at)/1000000+1;d->usb_at+=count*1000000;if(count>64){d->skipped_usb_frames+=count-64;count=64;}while(count--)pt_ohci_frame(&d->usb);}
    pt_screamer_service(&d->sound,now);if(!d->audio)pt_pcm_drain(&d->pcm);
    if(d->dma[16].regs[1]&PT_DMA_ACTIVE){if(d->audio_last_ns){uint64_t gap=now-d->audio_last_ns;if(gap>d->audio_max_gap_ns)d->audio_max_gap_ns=gap;if(gap>5000000)d->audio_gaps_5ms++;}d->audio_last_ns=now;
        unsigned level=pt_pcm_queued(&d->pcm);d->level_sum+=level;d->level_n++;if(level<d->level_min)d->level_min=level;if(level>d->level_max)d->level_max=level;}
    else d->audio_last_ns=0;
    d->service_at=retry?now+100000:UINT64_MAX;d->service_requested=0;
}
uint64_t pt_devices_deadline(PtDevices *d){
    uint64_t at=pt_pmu_deadline(&d->pmu);if(d->service_at<at)at=d->service_at;
    if((pt_ohci_needs_clock(&d->usb)||(d->dma[16].regs[1]&PT_DMA_ACTIVE))&&d->usb_at<at)at=d->usb_at;return at;
}
void pt_devices_audio_stats(PtDevices *d){
    fprintf(stderr,"audio: frames=%llu underruns=%llu missing=%llu dropped=%llu skipped=%llu gaps5ms=%llu max_gap_ns=%llu trim_ppm=%d peak=%d/%d mute=%d\n",
        (unsigned long long)atomic_load(&d->pcm.frames),(unsigned long long)atomic_load(&d->pcm.underruns),(unsigned long long)atomic_load(&d->pcm.missing_frames),
        (unsigned long long)atomic_load(&d->pcm.dropped),(unsigned long long)d->sound.skipped,(unsigned long long)d->audio_gaps_5ms,(unsigned long long)d->audio_max_gap_ns,d->sound.trim_ppm,
        d->sound.peak[0],d->sound.peak[1],d->sound.codec[1]>>7&1);
    d->sound.peak[0]=d->sound.peak[1]=0;
}
