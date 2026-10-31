/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "audio.h"
#include <assert.h>
#include <math.h>
#include <pthread.h>
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static void stream(PtDma *d,PtPcm *p,PtScreamer *s,uint8_t ram[4096],unsigned rate,int little){
    memset(ram,0,4096);pt_write_le(ram+0x100,2,1024);pt_write_le(ram+0x102,2,0x100c);
    pt_write_le(ram+0x104,4,0x200);pt_write_le(ram+0x108,4,0x100);
    for(unsigned k=0x200;k<0x600;k+=4){if(little){pt_write_le(ram+k,2,16384);pt_write_le(ram+k+2,2,0xc000);}else {pt_write_be(ram+k,2,16384);pt_write_be(ram+k+2,2,0xc000);}}
    pt_dma_init(d,(PtRam){ram,4096,NULL},NULL,NULL,NULL);pt_pcm_init(p,1);pt_screamer_init(s,d,p);
    pt_screamer_write(s,0,rate<<8,0);pt_screamer_write(s,0x40,little,0);
    pt_dma_write(d,12,0x100);pt_dma_write(d,0,(uint32_t)PT_DMA_RUN<<16|PT_DMA_RUN);pt_screamer_service(s,0);
}
static PtPcm concurrent;
static void *produce(void *unused){(void)unused;for(unsigned k=0;k<200000;k++){while(!pt_pcm_push(&concurrent,(float)k,-(float)k))sched_yield();}return NULL;}
int main(void){
    static const unsigned rates[]={44100,29400,22050,17640,14700,11025,8820,7350};
    uint8_t ram[4096];PtDma d;PtPcm p;PtScreamer s;float out[2048];
    for(unsigned r=0;r<8;r++)for(unsigned little=0;little<2;little++){
        stream(&d,&p,&s,ram,r,little);uint64_t rendered=0;
        for(uint64_t t=1000000;t<=1000000000;t+=1000000){pt_screamer_service(&s,t);unsigned n=pt_pcm_queued(&p);assert(n<=1024);pt_pcm_render(&p,out,n);rendered+=n;for(unsigned k=0;k<n;k++)assert(out[k*2]==0.5f&&out[k*2+1]==-0.5f);}
        assert(s.regs[5]==rates[r]&&s.consumed==rates[r]&&rendered==44100&&!s.starved);
    }
    stream(&d,&p,&s,ram,0,0);pt_screamer_write(&s,0x10,0x4000|0x155,0);
    pt_screamer_write(&s,0x10,0x7009,0);assert(((pt_screamer_read(&s,0x20,0)>>4)&0xfff)==0x155);
    assert(pt_screamer_read(&s,0x20,0)&1u<<22);pt_screamer_write(&s,0x10,0x7000,0);
    assert(pt_screamer_read(&s,0x20,0)==((1u<<22)|(3u<<12)|(1u<<8)));
    pt_screamer_write(&s,0x10,0x4000,0);pt_screamer_write(&s,0x10,0x1080,0);pt_screamer_service(&s,1000000);
    unsigned n=pt_pcm_queued(&p);pt_pcm_render(&p,out,n);for(unsigned k=0;k<n*2;k++)assert(out[k]==0);
    pt_screamer_write(&s,0x10,0x1000,1000000);pt_screamer_write(&s,0x10,0x4000|(4<<6)|8,1000000);pt_screamer_service(&s,2000000);
    n=pt_pcm_queued(&p);pt_pcm_render(&p,out,n);assert(n&&fabsf(out[0]-0.5f*powf(10,-0.3f))<1e-6f&&fabsf(out[1]+0.5f*powf(10,-0.6f))<1e-6f);
    /* A synthetic DAC runs 300 ppm faster/slower for two virtual minutes.
     * The control run must expose the drift; correction must not move PH. */
    for(int ppm=-300;ppm<=300;ppm+=600){uint64_t failures[2];
        for(unsigned corrected=0;corrected<2;corrected++){
            setenv("PT_AUDIO_DRIFT",corrected?"1":"0",1);stream(&d,&p,&s,ram,0,0);atomic_store(&p.latency,1024);uint64_t phase=0;
            for(uint64_t t=1000000;t<=120000000000ull;t+=1000000){
                pt_screamer_service(&s,t);phase+=(uint64_t)PT_PCM_RATE*(1000000+ppm);unsigned frames=(unsigned)(phase/1000000000);phase%=1000000000;pt_pcm_render(&p,out,frames);
            }
            assert(s.regs[5]==120*44100&&s.consumed==120*44100&&!s.starved);
            unsigned level=pt_pcm_queued(&p);
            failures[corrected]=atomic_load(&p.underruns)+atomic_load(&p.dropped)+(abs((int)level-1024)>500);
            printf("DAC %+d ppm, correction %u: level=%u trim=%d underruns=%llu dropped=%llu PH=%u\n",ppm,corrected,level,s.trim_ppm,(unsigned long long)atomic_load(&p.underruns),(unsigned long long)atomic_load(&p.dropped),s.regs[5]);
        }assert(failures[0]&& !failures[1]);
    }unsetenv("PT_AUDIO_DRIFT");
    pt_pcm_init(&concurrent,1);pthread_t thread;assert(!pthread_create(&thread,NULL,produce,NULL));
    for(unsigned k=0;k<200000;){unsigned n=pt_pcm_queued(&concurrent);if(n>1024)n=1024;if(!n){sched_yield();continue;}pt_pcm_render(&concurrent,out,n);for(unsigned j=0;j<n;j++,k++)assert(out[j*2]==(float)k&&out[j*2+1]==-(float)k);}
    assert(!pthread_join(thread,NULL)&&!pt_pcm_queued(&concurrent));
    puts("PASS: Screamer readback shift/restore, eight rates, BE/LE stereo, volume/mute, playhead and concurrent PCM publication");
}
