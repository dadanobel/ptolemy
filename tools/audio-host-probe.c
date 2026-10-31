/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "audio.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <math.h>
/* Explicit host-only output test. --silent exercises two clients without a
 * sound; --tone is an opt-in quiet 997 Hz signal. No microphone is opened. */
int main(int argc,char **argv){
    if(argc!=2||(strcmp(argv[1],"--silent")&&strcmp(argv[1],"--tone"))){fprintf(stderr,"Usage: audio-host-probe --silent | --tone\n");return 2;}
    PtPcm pcm[2];PtAudioHost *host[2]={0};uint64_t count=0;int error=0,tone=!strcmp(argv[1],"--tone");
    for(unsigned k=0;k<2;k++){pt_pcm_init(&pcm[k],1024);atomic_store(&pcm[k].stopped,0);for(unsigned j=0;j<1024;j++)pt_pcm_push(&pcm[k],0,0);int32_t status;host[k]=pt_audio_open(&pcm[k],&status);if(!host[k]){fprintf(stderr,"audio: client %u failed, status %d\n",k,status);error=1;goto done;}}
    {uint64_t start=clock_gettime_nsec_np(CLOCK_MONOTONIC),end=start+1500000000ull;
    while(clock_gettime_nsec_np(CLOCK_MONOTONIC)<end){uint64_t due=(clock_gettime_nsec_np(CLOCK_MONOTONIC)-start)*PT_PCM_RATE/1000000000;
        while(count<due){float sample=tone?0.01f*sinf((float)(2*3.141592653589793*997*(count%PT_PCM_RATE)/PT_PCM_RATE)):0;for(unsigned k=0;k<2;k++)pt_pcm_push(&pcm[k],sample,sample);count++;}usleep(1000);}}
done:
    for(unsigned k=0;k<2;k++)if(host[k]){atomic_store(&pcm[k].stopped,1);int status=pt_audio_close(host[k]);if(status){fprintf(stderr,"audio close: %d\n",status);error=1;}
        uint64_t callbacks=atomic_load(&pcm[k].callbacks),frames=atomic_load(&pcm[k].frames);printf("client %u: callbacks=%llu frames=%llu underruns=%llu dropped=%llu\n",k,(unsigned long long)callbacks,(unsigned long long)frames,(unsigned long long)atomic_load(&pcm[k].underruns),(unsigned long long)atomic_load(&pcm[k].dropped));if(callbacks<2||!frames)error=1;
    }
    if(!error)puts("PASS: two host CoreAudio output clients render and close; this does not establish Tiger driver attachment or audible correctness");return error;
}
