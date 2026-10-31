/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "guard.h"
#include <stdlib.h>
#include <string.h>
typedef struct {uint64_t base;uint8_t child[4],ram,used;} Group;
struct PtR2GuardMap {Group *table;uint32_t slots,count;size_t granule;PtR2GuardApply apply;void *context;};
static uint32_t home(PtR2GuardMap *g,int ram,uint64_t base) {
    uint64_t x=(base>>12)^(ram?UINT64_C(0x9e3779b97f4a7c15):0);x^=x>>33;x*=UINT64_C(0xff51afd7ed558ccd);x^=x>>33;
    return (uint32_t)x&(g->slots-1);
}
static uint32_t find(PtR2GuardMap *g,int ram,uint64_t base) {
    uint32_t i=home(g,ram,base);while(g->table[i].used&&(g->table[i].base!=base||g->table[i].ram!=!!ram))i=(i+1)&(g->slots-1);return i;
}
static PtR2IoGuard effective(Group p) {unsigned mode=0;for(unsigned i=0;i<4;i++)if(p.child[i]>mode)mode=p.child[i];return (PtR2IoGuard)mode;}
static void erase(PtR2GuardMap *g,uint32_t hole) {
    uint32_t mask=g->slots-1;
    for(uint32_t next=(hole+1)&mask;g->table[next].used;next=(next+1)&mask) {
        Group other=g->table[next];uint32_t h=home(g,other.ram,other.base);
        if(((next-h)&mask)>=((next-hole)&mask)){g->table[hole]=other;hole=next;}
    }memset(&g->table[hole],0,sizeof(Group));g->count--;
}
PtR2GuardMap *pt_r2_guard_create(size_t granule,uint32_t slots,PtR2GuardApply apply,void *context) {
    if((granule!=4096&&granule!=16384)||slots<16||slots>(1u<<20)||(slots&(slots-1))||!apply)return NULL;
    PtR2GuardMap *g=calloc(1,sizeof *g);if(!g)return NULL;g->table=calloc(slots,sizeof(Group));if(!g->table){free(g);return NULL;}
    g->granule=granule;g->slots=slots;g->apply=apply;g->context=context;return g;
}
int pt_r2_guard_set(void *context,int ram,uint64_t page,PtR2IoGuard mode) {
    PtR2GuardMap *g=context;if(!g||(page&4095)||(unsigned)mode>PT_R2_IO_GPU_OWNED)return 0;
    uint64_t base=page&~(uint64_t)(g->granule-1);uint32_t at=find(g,ram,base);Group old=g->table[at];
    if(!old.used&&mode==PT_R2_IO_OPEN)return 1;
    if(!old.used&&g->count>=g->slots/2)return 0;
    Group next=old;next.used=1;next.ram=!!ram;next.base=base;next.child[(page-base)/4096]=(uint8_t)mode;
    PtR2IoGuard before=effective(old),after=effective(next);
    if(before!=after&&!g->apply(g->context,!!ram,base,g->granule,after))return 0;
    if(after==PT_R2_IO_OPEN){if(old.used)erase(g,at);return 1;}
    g->table[at]=next;if(!old.used)g->count++;return 1;
}
int pt_r2_guard_get(PtR2GuardMap *g,int ram,uint64_t physical,PtR2IoGuard *child,PtR2IoGuard *group) {
    if(!g||!child||!group)return 0;uint64_t base=physical&~(uint64_t)(g->granule-1);Group p=g->table[find(g,ram,base)];
    *child=(PtR2IoGuard)p.child[(physical-base)/4096];*group=effective(p);return 1;
}
/* Apply every non-open group again (a new mapping of the same storage starts unprotected). */
int pt_r2_guard_reapply(PtR2GuardMap *g) {
    if(!g)return 0;int ok=1;
    for(uint32_t i=0;i<g->slots;i++)if(g->table[i].used&&effective(g->table[i])!=PT_R2_IO_OPEN)ok&=!!g->apply(g->context,g->table[i].ram,g->table[i].base,g->granule,effective(g->table[i]));
    return ok;
}
int pt_r2_guard_destroy(PtR2GuardMap *g) {if(!g)return 1;if(g->count)return 0;free(g->table);free(g);return 1;}
