/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "diagnostics.h"
#include <stdio.h>
#include <string.h>
static uint32_t get32(const uint8_t *b){return (uint32_t)b[0]<<24|(uint32_t)b[1]<<16|(uint32_t)b[2]<<8|b[3];}
static uint64_t get64(const uint8_t *b){return (uint64_t)get32(b)<<32|get32(b+4);}
static void put32(uint8_t *b,uint32_t n){for(unsigned i=0;i<4;i++)b[i]=(uint8_t)(n>>(24-i*8));}
static void put64(uint8_t *b,uint64_t n){put32(b,(uint32_t)(n>>32));put32(b+4,(uint32_t)n);}
/* The wire magic "PMD1" predates the Optics name; installed guest tools check it. */
int optics_status_encode(const OpticsStatus *s,uint8_t out[OPTICS_DIAG_BYTES]) {
    if(!s||!out||s->state>OPTICS_FAILED||(s->known>>OPTICS_METRICS))return 0;
    memset(out,0,OPTICS_DIAG_BYTES);memcpy(out,"PMD1",4);put32(out+4,OPTICS_DIAG_ABI);put32(out+8,OPTICS_DIAG_BYTES);
    put32(out+12,s->state);put64(out+16,s->features);put64(out+24,s->known);
    for(unsigned i=0;i<OPTICS_METRICS;i++)if(s->known&(UINT64_C(1)<<i))put64(out+32+i*8,s->metric[i]);return 1;
}
int optics_status_decode(const void *data,size_t n,OpticsStatus *out) {
    if(!data||!out||n!=OPTICS_DIAG_BYTES)return 0;const uint8_t *b=data;
    if(memcmp(b,"PMD1",4)||get32(b+4)!=OPTICS_DIAG_ABI||get32(b+8)!=OPTICS_DIAG_BYTES||get32(b+12)>OPTICS_FAILED||(get64(b+24)>>OPTICS_METRICS))return 0;
    OpticsStatus s={.state=get32(b+12),.features=get64(b+16),.known=get64(b+24)};
    for(unsigned i=0;i<OPTICS_METRICS;i++)if(s.known&(UINT64_C(1)<<i))s.metric[i]=get64(b+32+i*8);
    *out=s;return 1;
}
size_t optics_status_json(const OpticsStatus *s,char *out,size_t cap) {
    static const char *const state[]={"disabled","starting","ready","failed"};
    static const char *const names[]={"hostSampleNS","runEpoch","gpuEpoch","draws","passes","batches","compileNS","residentBytes","peakBytes","inflight","unpolled","inputBytes","bufferAllocations","ioReads","ioWrites","invalidations","ioBusy","guardCalls","watchedPages","ioSnapshotBytes","gpuDoneSequence","cpuGuardFaults","cpuGuardWaits","gpuNS","maxSubmitToCallbackNS","unsupported","frameReplaced","frameTaken"};
    if(!s||!out||!cap||s->state>OPTICS_FAILED)return 0;
    int n=snprintf(out,cap,"{\"component\":\"optics\",\"abi\":1,\"state\":\"%s\",\"features\":\"0x%llx\",\"metrics\":{",state[s->state],(unsigned long long)s->features);
    if(n<0||(size_t)n>=cap){out[0]=0;return 0;}size_t used=(size_t)n;
    for(unsigned i=0;i<OPTICS_METRICS;i++) {
        if(s->known&(UINT64_C(1)<<i))n=snprintf(out+used,cap-used,"%s\"%s\":%llu",i?",":"",names[i],(unsigned long long)s->metric[i]);
        else n=snprintf(out+used,cap-used,"%s\"%s\":null",i?",":"",names[i]);
        if(n<0||(size_t)n>=cap-used){out[0]=0;return 0;}used+=(size_t)n;
    }
    if(cap-used<4){out[0]=0;return 0;}memcpy(out+used,"}}\n",4);return used+3;
}
