/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "timeline.h"
#include <stdlib.h>
#include <string.h>
typedef struct {uint64_t id,first,last;unsigned done,success;} Batch;
typedef struct {uint64_t id,after;unsigned refs,done,success;} Visibility;
typedef struct {uint64_t id,after;unsigned n;uint32_t visibility[16];} Fence;
struct PtR2Timeline {
    uint64_t epoch,next,issued,submitted,gpu_done,failure;
    uint32_t nb,nv,nf,head,count;
    Batch *batch;Visibility *visibility;Fence *fence;
};
PtR2Timeline *pt_r2_timeline_create(uint32_t b,uint32_t v,uint32_t f) {
    if(!b||b>1024||!v||v>65536||!f||f>4096)return NULL;
    PtR2Timeline *t=calloc(1,sizeof *t);if(!t)return NULL;
    t->batch=calloc(b,sizeof(Batch));t->visibility=calloc(v,sizeof(Visibility));t->fence=calloc(f,sizeof(Fence));
    if(!t->batch||!t->visibility||!t->fence){pt_r2_timeline_destroy(t);return NULL;}
    t->nb=b;t->nv=v;t->nf=f;t->epoch=1;return t;
}
void pt_r2_timeline_destroy(PtR2Timeline *t) {if(t){free(t->batch);free(t->visibility);free(t->fence);free(t);}}
int pt_r2_timeline_reset(PtR2Timeline *t) {
    if(!t||t->epoch==UINT64_MAX)return 0;
    t->epoch++;t->issued=t->submitted=t->gpu_done=t->failure=0;t->head=t->count=0;
    memset(t->batch,0,t->nb*sizeof(Batch));memset(t->visibility,0,t->nv*sizeof(Visibility));memset(t->fence,0,t->nf*sizeof(Fence));return 1;
}
uint64_t pt_r2_timeline_issue(PtR2Timeline *t) {return !t||t->failure||t->issued==UINT64_MAX?0:++t->issued;}
static uint64_t id(PtR2Timeline *t) {return t->next==UINT64_MAX?0:++t->next;}
PtR2TimelineResult pt_r2_timeline_submit(PtR2Timeline *t,uint64_t last,PtR2TimelineToken *out) {
    if(!t||!out||last<=t->submitted||last>t->issued)return PT_R2_INVALID;
    if(t->failure)return PT_R2_FAILED;if(t->count==t->nb)return PT_R2_FULL;
    uint64_t key=id(t);if(!key)return PT_R2_FULL;
    t->batch[(t->head+t->count)%t->nb]=(Batch){.id=key,.first=t->submitted+1,.last=last};t->count++;t->submitted=last;
    *out=(PtR2TimelineToken){t->epoch,key};return PT_R2_READY;
}
PtR2TimelineResult pt_r2_timeline_complete(PtR2Timeline *t,PtR2TimelineToken token,int success) {
    if(!t||token.epoch!=t->epoch)return PT_R2_STALE;Batch *b=NULL;
    for(unsigned i=0;i<t->count;i++){Batch *candidate=&t->batch[(t->head+i)%t->nb];if(candidate->id==token.id){b=candidate;break;}}
    if(!b)return PT_R2_STALE;if(b->done)return PT_R2_INVALID;b->done=1;b->success=!!success;
    if(!success&&(!t->failure||b->first<t->failure))t->failure=b->first;
    while(t->count&&t->batch[t->head].done) {
        Batch *first=&t->batch[t->head];if(first->success&&(!t->failure||first->last<t->failure))t->gpu_done=first->last;
        memset(first,0,sizeof *first);t->head=(t->head+1)%t->nb;t->count--;
    }return success?PT_R2_READY:PT_R2_FAILED;
}
static Visibility *visible(PtR2Timeline *t,PtR2TimelineToken token) {
    if(!t||token.epoch!=t->epoch||!token.id)return NULL;
    for(unsigned i=0;i<t->nv;i++)if(t->visibility[i].id==token.id)return &t->visibility[i];return NULL;
}
PtR2TimelineResult pt_r2_visibility_create(PtR2Timeline *t,uint64_t after,PtR2TimelineToken *out) {
    if(!t||!out||!after||after>t->issued)return PT_R2_INVALID;
    for(unsigned i=0;i<t->nv;i++)if(!t->visibility[i].id) {
        uint64_t key=id(t);if(!key)return PT_R2_FULL;
        t->visibility[i]=(Visibility){.id=key,.after=after};*out=(PtR2TimelineToken){t->epoch,key};return PT_R2_READY;
    }
    return PT_R2_FULL;
}
PtR2TimelineResult pt_r2_visibility_finish(PtR2Timeline *t,PtR2TimelineToken token,int success) {
    Visibility *v=visible(t,token);if(!v)return PT_R2_STALE;if(v->done)return PT_R2_INVALID;
    v->done=1;v->success=!!success;return success?PT_R2_READY:PT_R2_FAILED;
}
PtR2TimelineResult pt_r2_visibility_release(PtR2Timeline *t,PtR2TimelineToken token) {
    Visibility *v=visible(t,token);if(!v)return PT_R2_STALE;if(v->refs)return PT_R2_PENDING;
    memset(v,0,sizeof *v);return PT_R2_READY;
}
static Fence *fence(PtR2Timeline *t,PtR2TimelineToken token) {
    if(!t||token.epoch!=t->epoch||!token.id)return NULL;
    for(unsigned i=0;i<t->nf;i++)if(t->fence[i].id==token.id)return &t->fence[i];return NULL;
}
PtR2TimelineResult pt_r2_fence_create(PtR2Timeline *t,uint64_t after,const PtR2TimelineToken *dependencies,size_t n,PtR2TimelineToken *out) {
    if(!t||!out||after>t->issued||n>16||(!dependencies&&n))return PT_R2_INVALID;
    Fence value={.after=after,.n=(unsigned)n};
    for(unsigned i=0;i<n;i++) {
        Visibility *v=visible(t,dependencies[i]);if(!v||v->after>after)return PT_R2_INVALID;
        value.visibility[i]=(uint32_t)(v-t->visibility);
        for(unsigned j=0;j<i;j++)if(value.visibility[i]==value.visibility[j])return PT_R2_INVALID;
    }
    for(unsigned i=0;i<t->nf;i++)if(!t->fence[i].id) {
        value.id=id(t);if(!value.id)return PT_R2_FULL;t->fence[i]=value;
        for(unsigned j=0;j<n;j++)t->visibility[value.visibility[j]].refs++;
        *out=(PtR2TimelineToken){t->epoch,value.id};return PT_R2_READY;
    }
    return PT_R2_FULL;
}
PtR2TimelineResult pt_r2_fence_poll(PtR2Timeline *t,PtR2TimelineToken token) {
    Fence *f=fence(t,token);if(!f)return PT_R2_STALE;
    if(t->failure&&f->after>=t->failure)return PT_R2_FAILED;
    int pending=t->gpu_done<f->after;
    for(unsigned i=0;i<f->n;i++){Visibility *v=&t->visibility[f->visibility[i]];if(v->done&&!v->success)return PT_R2_FAILED;if(!v->done)pending=1;}
    return pending?PT_R2_PENDING:PT_R2_READY;
}
PtR2TimelineResult pt_r2_fence_release(PtR2Timeline *t,PtR2TimelineToken token) {
    Fence *f=fence(t,token);if(!f)return PT_R2_STALE;
    for(unsigned i=0;i<f->n;i++)t->visibility[f->visibility[i]].refs--;memset(f,0,sizeof *f);return PT_R2_READY;
}
uint64_t pt_r2_timeline_gpu_done(const PtR2Timeline *t) {return t?t->gpu_done:0;}
