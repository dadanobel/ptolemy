/* SPDX-License-Identifier: GPL-2.0-or-later */
#import "diagnostics_metal.h"
#include <cmath>
void optics_status_renderer(OpticsStatus *s,PtR2Renderer *r) {
    if(!s||!r)return;NSDictionary *d=[r diagnostics];if(d[@"error"])return;
    s->features|=OPTICS_ENGINE|OPTICS_DIRECT_SHADER;if([d[@"gpuFailed"] boolValue])s->state=OPTICS_FAILED;else if(s->state!=OPTICS_FAILED)s->state=OPTICS_READY;
    NSArray *keys=@[@"epoch",@"draws",@"renderPasses",@"commits",@"compileNS",@"residentBytes",@"peakBytes",@"inflightBatches",@"completedUnpolled",@"snapshotBytes",@"bufferAllocations",@"epochMaxSubmitToCallbackNS"];
    const unsigned index[]={OPTICS_GPU_EPOCH,OPTICS_DRAWS,OPTICS_PASSES,OPTICS_BATCHES,OPTICS_COMPILE_NS,OPTICS_RESIDENT_BYTES,OPTICS_PEAK_BYTES,OPTICS_INFLIGHT,OPTICS_UNPOLLED,OPTICS_INPUT_BYTES,OPTICS_BUFFER_ALLOCS,OPTICS_CALLBACK_MAX_NS};
    for(unsigned i=0;i<keys.count;i++)if([d[keys[i]] isKindOfClass:NSNumber.class]){s->metric[index[i]]=[d[keys[i]] unsignedLongLongValue];s->known|=UINT64_C(1)<<index[i];}
    double seconds=[d[@"epochGPUSeconds"] doubleValue];
    if(std::isfinite(seconds)&&seconds>=0&&seconds<1e10){s->metric[OPTICS_GPU_NS]=(uint64_t)(seconds*1e9);s->known|=UINT64_C(1)<<OPTICS_GPU_NS;}
}
