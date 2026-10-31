/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "audio.h"
#include <AudioUnit/AudioUnit.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
struct PtAudioHost { AudioUnit unit; };
static OSStatus render(void *opaque,AudioUnitRenderActionFlags *flags,const AudioTimeStamp *stamp,
                       UInt32 bus,UInt32 n,AudioBufferList *buffers) {
    (void)flags; (void)stamp; (void)bus;
    if(buffers->mNumberBuffers!=1 || buffers->mBuffers[0].mNumberChannels!=2 ||
       !buffers->mBuffers[0].mData || buffers->mBuffers[0].mDataByteSize<(uint64_t)n*8) {
        for(unsigned i=0;i<buffers->mNumberBuffers;i++)
            if(buffers->mBuffers[i].mData) memset(buffers->mBuffers[i].mData,0,buffers->mBuffers[i].mDataByteSize);
        return kAudio_ParamError;
    }
    pt_pcm_render(opaque,buffers->mBuffers[0].mData,n); return noErr;
}
PtAudioHost *pt_audio_open(PtPcm *pcm,int32_t *result) {
    OSStatus status=-1; PtAudioHost *h=calloc(1,sizeof *h);
    if(!h) { if(result) *result=-ENOMEM; return NULL; }
    AudioComponentDescription desc={kAudioUnitType_Output,kAudioUnitSubType_DefaultOutput,kAudioUnitManufacturer_Apple,0,0};
    AudioComponent component=AudioComponentFindNext(NULL,&desc);
    if(!component) goto fail;
    status=AudioComponentInstanceNew(component,&h->unit); if(status) goto fail;
    AudioStreamBasicDescription format={.mSampleRate=PT_PCM_RATE,.mFormatID=kAudioFormatLinearPCM,
        .mFormatFlags=kAudioFormatFlagIsFloat|kAudioFormatFlagIsPacked,
        .mBytesPerPacket=8,.mFramesPerPacket=1,.mBytesPerFrame=8,.mChannelsPerFrame=2,.mBitsPerChannel=32};
    status=AudioUnitSetProperty(h->unit,kAudioUnitProperty_StreamFormat,kAudioUnitScope_Input,0,&format,sizeof format);
    if(status) goto fail;
    AURenderCallbackStruct callback={render,pcm};
    status=AudioUnitSetProperty(h->unit,kAudioUnitProperty_SetRenderCallback,kAudioUnitScope_Input,0,&callback,sizeof callback);
    if(status) goto fail;
    status=AudioUnitInitialize(h->unit); if(status) goto fail;
    status=AudioOutputUnitStart(h->unit); if(status) goto fail;
    if(result) *result=0; return h;
fail:
    if(h->unit) { AudioUnitUninitialize(h->unit); AudioComponentInstanceDispose(h->unit); }
    free(h); if(result) *result=status; return NULL;
}
int32_t pt_audio_close(PtAudioHost *h) {
    if(!h) return 0;
    OSStatus status=AudioOutputUnitStop(h->unit); if(status) return status;
    status=AudioUnitUninitialize(h->unit); if(status) return status;
    status=AudioComponentInstanceDispose(h->unit); if(status) return status;
    free(h); return 0;
}
