/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "audio.h"
#include <stdlib.h>
#include <math.h>
#include <string.h>
#include <stdio.h>
void pt_pcm_init(PtPcm *p,unsigned latency){
    memset(p,0,sizeof *p);atomic_init(&p->read,0);atomic_init(&p->write,0);
    atomic_init(&p->latency,latency>PT_PCM_CAPACITY?PT_PCM_CAPACITY:latency);
    atomic_init(&p->primed,0);atomic_init(&p->stopped,1);atomic_init(&p->frames,0);atomic_init(&p->underruns,0);
    atomic_init(&p->missing_frames,0);atomic_init(&p->callbacks,0);atomic_init(&p->dropped,0);atomic_init(&p->tails,0);atomic_init(&p->max_request,0);
}
unsigned pt_pcm_queued(PtPcm *p){uint32_t tail=atomic_load_explicit(&p->read,memory_order_acquire);return atomic_load_explicit(&p->write,memory_order_acquire)-tail;}
int pt_pcm_push(PtPcm *p,float l,float r){
    uint32_t w=atomic_load_explicit(&p->write,memory_order_relaxed),tail=atomic_load_explicit(&p->read,memory_order_acquire);
    if(w-tail>=PT_PCM_CAPACITY){atomic_fetch_add_explicit(&p->dropped,1,memory_order_relaxed);return 0;}
    p->samples[w%PT_PCM_CAPACITY][0]=l;p->samples[w%PT_PCM_CAPACITY][1]=r;
    atomic_store_explicit(&p->write,w+1,memory_order_release);return 1;
}
void pt_pcm_render(PtPcm *p,float *out,unsigned count){
    if(!out||!count)return;memset(out,0,(size_t)count*2*sizeof(float));
    atomic_fetch_add_explicit(&p->callbacks,1,memory_order_relaxed);
    if(count>atomic_load_explicit(&p->max_request,memory_order_relaxed))atomic_store_explicit(&p->max_request,count,memory_order_relaxed);
    uint32_t tail=atomic_load_explicit(&p->read,memory_order_relaxed),head=atomic_load_explicit(&p->write,memory_order_acquire),available=head-tail;
    int stopped=atomic_load_explicit(&p->stopped,memory_order_acquire);
    if(!atomic_load_explicit(&p->primed,memory_order_relaxed)){
        if(available<atomic_load_explicit(&p->latency,memory_order_relaxed)&&!stopped)return;
        if(!available)return;atomic_store_explicit(&p->primed,1,memory_order_relaxed);
    }
    unsigned got=count<available?count:available;
    for(unsigned k=0;k<got;k++){out[k*2]=p->samples[(tail+k)%PT_PCM_CAPACITY][0];out[k*2+1]=p->samples[(tail+k)%PT_PCM_CAPACITY][1];}
    atomic_store_explicit(&p->read,tail+got,memory_order_release);atomic_fetch_add_explicit(&p->frames,got,memory_order_relaxed);
    if(got<count){atomic_store_explicit(&p->primed,0,memory_order_relaxed);
        if(stopped)atomic_fetch_add_explicit(&p->tails,1,memory_order_relaxed);
        else {atomic_fetch_add_explicit(&p->underruns,1,memory_order_relaxed);atomic_fetch_add_explicit(&p->missing_frames,count-got,memory_order_relaxed);}}
}
void pt_pcm_drain(PtPcm *p){
    atomic_store_explicit(&p->read,atomic_load_explicit(&p->write,memory_order_acquire),memory_order_release);atomic_store(&p->primed,0);
}
static unsigned nd_audio_rate(const PtScreamer *s){static const unsigned rates[]={44100,29400,22050,17640,14700,11025,8820,7350};return rates[s->regs[0]>>8&7];}
void pt_screamer_init(PtScreamer *s,PtDma *d,PtPcm *p){
    memset(s,0,sizeof *s);s->dma=d;s->pcm=p;s->dma_reset=d->reset_generation;s->rate=44100;s->native_gain[0]=s->native_gain[1]=1;s->native_host_drift=pt_env_flag("PT_AUDIO_DRIFT");
    s->regs[2]=(1u<<22)|(3u<<12)|(1u<<8); /* Valid Screamer revision, Crystal manufacturer, no external connector. */
}
/* A frame may cross descriptor boundaries. The caller advances the frame clock
 * before publishing descriptor completion, so IRQ timestamps use that clock. */
static int nd_audio_frame(PtScreamer *s){
    while(s->partial<4){PtDmaSlice slice;int result=pt_dma_next(s->dma,&slice);
        if(result<=0)return result;
        if(slice.input){pt_dma_fail(s->dma);return -1;}
        unsigned n=4-s->partial;if(n>slice.length)n=slice.length;
        int busy;uint8_t *p=pt_ram_use(s->dma->ram,slice.address,n,0,&busy);if(busy)return 0;if(!p||!n){pt_dma_fail(s->dma);return -1;}
        memcpy(s->sample+s->partial,p,n);s->partial+=n;
        if(pt_dma_advance(s->dma,slice.generation,n))return -1;
    }s->partial=0;return 1;
}
static void nd_audio_pcm(PtScreamer *s){
    int little=s->regs[4]&1;int16_t l=(int16_t)(little?pt_read_le(s->sample,2):pt_read_be(s->sample,2));
    int16_t r=(int16_t)(little?pt_read_le(s->sample+2,2):pt_read_be(s->sample+2,2));
    float gain_l=s->native_gain[0],gain_r=s->native_gain[1];
    if(s->codec[1]&0x80)gain_l=gain_r=0;s->held[0]=l*(1.0f/32768)*gain_l;s->held[1]=r*(1.0f/32768)*gain_r;
    {int a=abs(l)>abs(r)?abs(l):abs(r),g=(int)(32768*fmaxf(fabsf(s->held[0]),fabsf(s->held[1])));if(a>s->peak[0])s->peak[0]=a;if(g>s->peak[1])s->peak[1]=g;}
    if(s->native_host_drift){
        uint64_t period=(uint64_t)s->rate*1000000;
        s->native_host_phase+=(uint64_t)PT_PCM_RATE*(1000000+s->trim_ppm);
        while(s->native_host_phase>=period){s->native_host_phase-=period;pt_pcm_push(s->pcm,s->held[0],s->held[1]);}
    }else {s->hold+=PT_PCM_RATE;while(s->hold>=s->rate){s->hold-=s->rate;pt_pcm_push(s->pcm,s->held[0],s->held[1]);}}
    if(s->dump){uint8_t b[4];pt_write_le(b,2,(uint16_t)l);pt_write_le(b+2,2,(uint16_t)r);(void)fwrite(b,1,4,s->dump);}
}
void pt_screamer_service(PtScreamer *s,uint64_t now){
    if(s->native_servicing)return;
    if(s->dma_reset!=s->dma->reset_generation){s->dma_reset=s->dma->reset_generation;s->native_running=0;s->partial=s->hold=0;s->native_host_phase=0;s->trim_ppm=0;s->level_ema=0;}
    if(!(s->dma->regs[1]&PT_DMA_ACTIVE)||s->dma->regs[1]&PT_DMA_PAUSE){s->native_running=0;atomic_store(&s->pcm->stopped,1);return;}
    atomic_store(&s->pcm->stopped,0);
    if(!s->native_running||now<s->ph_ns){s->native_running=1;s->ph_ns=now;s->ph_frac=0;return;}
    s->native_servicing=1;
    /* The DAC and machine clock can drift. Correct only host resampling:
     * guest DMA, frame counters, descriptor completion and IRQ time stay exact. */
    if(s->native_host_drift&&atomic_load_explicit(&s->pcm->frames,memory_order_relaxed)){
        unsigned target=atomic_load_explicit(&s->pcm->latency,memory_order_relaxed);
        if(target<256)target=256;if(target>PT_PCM_CAPACITY/2)target=PT_PCM_CAPACITY/2;
        unsigned level=pt_pcm_queued(s->pcm);if(!s->level_ema)s->level_ema=target<<4;
        s->level_ema+=(int32_t)((int32_t)(level<<4)-(int32_t)s->level_ema)/64;
        int trim=2*((int)target-(int)(s->level_ema>>4));s->trim_ppm=trim>500?500:trim< -500?-500:trim;
    }
    uint64_t start=s->ph_ns,old_fraction=s->ph_frac,elapsed=now-start;
    unsigned __int128 ticks=(unsigned __int128)elapsed*s->rate+old_fraction;
    uint64_t due=(uint64_t)(ticks/1000000000),skip=due>16384?due-16384:0,done=0;
    s->ph_ns=now;s->ph_frac=(uint64_t)(ticks%1000000000);
    while(done<due){
        /* Long host stalls discard host samples, never guest DMA progress. */
        if(done<skip&&!s->partial){PtDmaSlice slice;int k=pt_dma_next(s->dma,&slice);
            if(k<=0){if(!(s->dma->regs[1]&PT_DMA_ACTIVE))break;
                /* WAIT cannot change while this CPU-owned service call runs. */
                uint64_t frames=skip-done;s->regs[5]+=(uint32_t)frames;done+=frames;s->skipped+=frames;s->starved+=frames;continue;}
            else if(!slice.input&&slice.length>=4){uint64_t frames=slice.length/4;if(frames>skip-done)frames=skip-done;
                s->now=start+(uint64_t)(((unsigned __int128)(done+frames)*1000000000-old_fraction+s->rate-1)/s->rate);
                s->regs[5]+=(uint32_t)frames;done+=frames;s->skipped+=frames;
                if(pt_dma_advance(s->dma,slice.generation,(unsigned)frames*4))break;continue;}
        }
        s->now=start+(uint64_t)(((unsigned __int128)(done+1)*1000000000-old_fraction+s->rate-1)/s->rate);
        s->regs[5]++;int result=nd_audio_frame(s);if(result<=0&&!(s->dma->regs[1]&PT_DMA_ACTIVE)){s->regs[5]--;break;}
        done++;
        if(result<=0){s->starved++;memset(s->sample,0,4);}
        if(done<=skip)s->skipped++;else{s->consumed++;nd_audio_pcm(s);}
    }
    if(!(s->dma->regs[1]&PT_DMA_ACTIVE)){s->native_running=0;atomic_store(&s->pcm->stopped,1);}
    s->native_servicing=0;
}
uint32_t pt_screamer_read(PtScreamer *s,unsigned offset,uint64_t now){
    pt_screamer_service(s,now);if(offset>0x50||(offset&15))return 0;
    uint32_t value=s->regs[offset/16];if(offset==0x30)s->regs[3]=0;return value;
}
void pt_screamer_write(PtScreamer *s,unsigned offset,uint32_t value,uint64_t now){
    pt_screamer_service(s,now);if(offset>0x50||(offset&15))return;
    if(offset==0x10){unsigned address=value>>12&0x3ff,data=value&0xfff;s->regs[1]=value&~(1u<<24);
        if(address<8){s->codec[address]=(uint16_t)data;if(address==4){s->native_gain[0]=powf(10.0f,-0.075f*(float)(data>>6&15));s->native_gain[1]=powf(10.0f,-0.075f*(float)(data&15));}if(address==7&&!(data&1))s->regs[2]=(1u<<22)|(3u<<12)|(1u<<8);if(address==7&&(data&1))s->regs[2]=(s->regs[2]&~0xfff0u)|((uint32_t)s->codec[data>>1&7]<<4);}
        else s->regs[0]|=1u<<11;
    }else if(!offset){unsigned old_rate=s->rate;s->regs[0]=(s->regs[0]&((1u<<11)|(1u<<12)))|(value&~((1u<<11)|(1u<<12)));s->regs[0]&=~(value&((1u<<11)|(1u<<12)));s->rate=nd_audio_rate(s);if(s->rate!=old_rate){s->ph_frac=0;s->native_host_phase=0;}}
    else if(offset==0x40)s->regs[4]=value&1;
    else if(offset==0x50)s->regs[5]=value;
}
