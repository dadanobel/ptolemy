/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "command_stream.h"
#include <stdlib.h>
typedef struct {uint64_t address;uint32_t words;size_t offset;uint8_t *data;} Frame;
struct OpticsCommandStream {Frame *frame;unsigned depth,limit;uint32_t resident,budget;uint64_t epoch;int pumping,failed;OpticsCommandRead read;OpticsCommandExecute execute;void *context;};
OpticsCommandStream *optics_stream_create(unsigned depth,uint32_t words,OpticsCommandRead read,OpticsCommandExecute execute,void *context) {
    if(!depth||depth>64||!words||words>(1u<<24)||!read||!execute)return NULL;
    OpticsCommandStream *s=calloc(1,sizeof *s);if(!s)return NULL;s->frame=calloc(depth,sizeof(Frame));if(!s->frame){free(s);return NULL;}
    s->limit=depth;s->budget=words;s->read=read;s->execute=execute;s->context=context;s->epoch=1;return s;
}
static void pop(OpticsCommandStream *s) {Frame *f=&s->frame[--s->depth];s->resident-=f->words;free(f->data);*f=(Frame){0};}
int optics_stream_reset(OpticsCommandStream *s) {if(!s||s->pumping||s->epoch==UINT64_MAX)return 0;while(s->depth)pop(s);s->epoch++;s->failed=0;return 1;}
int optics_stream_pending(const OpticsCommandStream *s) {return s&&s->depth;}
void optics_stream_destroy(OpticsCommandStream *s) {if(s){while(s->depth)pop(s);free(s->frame);free(s);}}
PtR2IoResult optics_stream_push(OpticsCommandStream *s,uint64_t address,uint32_t words) {
    if(!s||!words||(address&3)||(uint64_t)words*4>UINT64_MAX-address)return PT_R2_IO_ARGUMENT;
    if(s->failed)return PT_R2_IO_COHERENCE;
    if(s->depth==s->limit||words>s->budget-s->resident)return PT_R2_IO_CAPACITY;
    for(unsigned i=0;i<s->depth;i++)if(s->frame[i].address==address&&s->frame[i].words==words)return PT_R2_IO_ARGUMENT;
    uint8_t *data=malloc((size_t)words*4);if(!data)return PT_R2_IO_CAPACITY;
    PtR2IoResult r=s->read(s->context,address,data,(size_t)words*4);if(r!=PT_R2_IO_OK){free(data);return r;}
    s->frame[s->depth++]=(Frame){address,words,0,data};s->resident+=words;return PT_R2_IO_OK;
}
OpticsStreamResult optics_stream_pump(OpticsCommandStream *s,unsigned budget,unsigned *accepted) {
    if(accepted)*accepted=0;if(!s||s->pumping||s->failed)return OPTICS_STREAM_EXEC;s->pumping=1;OpticsStreamResult result=OPTICS_STREAM_EMPTY;unsigned done=0;
    while(s->depth) {
        Frame *f=&s->frame[s->depth-1];if(f->offset==(size_t)f->words*4){pop(s);continue;}
        if(done==budget){result=OPTICS_STREAM_BUDGET;break;}
        PtR2Pm4Packet p;if(pt_r2_pm4_decode(f->data+f->offset,(size_t)f->words*4-f->offset,&p,NULL)!=PT_R2_PM4_PACKET){result=OPTICS_STREAM_BAD;break;}
        unsigned before=s->depth;int rc=s->execute(s->context,&p,s->epoch);
        if(rc){result=s->depth==before&&rc==1?OPTICS_STREAM_BUSY:OPTICS_STREAM_EXEC;break;}
        f->offset+=p.bytes;done++; /* f is stable even if callback pushed a child. */
    }
    s->pumping=0;if(result==OPTICS_STREAM_BAD||result==OPTICS_STREAM_IO||result==OPTICS_STREAM_EXEC)s->failed=1;
    if(accepted)*accepted=done;return result;
}
