/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "ppc.h"
#include "pt_agent.h"
#include <stdlib.h>
#include "optics/diagnostics.h"
_Static_assert(PTA_OPTICS==OPTICS_AGENT_OPCODE,"Optics agent opcode ABI");
/* Validate ALL pages before consuming a command. RAM only, user translation,
 * write permissions and COW faults respected; no device/VRAM host pointers. */
static int pages(CPU *c,uint32_t ea,uint32_t n,int write,uint32_t physical[17]){
    if(!n||n>PTA_MAX_REQUEST||(uint64_t)ea+n>0x100000000ull||!c->npreg||
       c->preg[0].base||c->preg[0].read_only||!c->preg[0].host)return PTA_FAULT;
    unsigned i=0;while(n){uint32_t k=4096-(ea&4095),ra;int wr;if(k>n)k=n;
        if(pt_xlate(c,ea,write,1,&ra,&wr)||(write&&!wr)||(uint64_t)ra+k>c->preg[0].size)return PTA_FAULT;
        physical[i++]=ra;ea+=k;n-=k;}return PTA_OK;
}
static void copy_pages(CPU *c,uint32_t ea,void *b,uint32_t n,int write,const uint32_t physical[17]){
    if(write)c->reserve=0;   /* Successful host copyout invalidates a guest reservation. */
    unsigned i=0;while(n){uint32_t k=4096-(ea&4095);if(k>n)k=n;
        uint8_t *p=write?pmem_write(c,physical[i++],k):c->preg[0].host+physical[i++];
        if(write)memcpy(p,b,k);else memcpy(b,p,k);b=(uint8_t *)b+k;ea+=k;n-=k;}
}
int pt_agent_hypercall(CPU *c,uint32_t instruction,PtAgent *a){
    if(!a||instruction!=PTA_INSN||c->gpr[0]!=PTA_MAGIC)return 0;
    unsigned need=MSR_PR|MSR_IR|MSR_DR;int rc=PTA_DENIED;
    uint32_t token=c->gpr[4],id=c->gpr[5],op=c->gpr[3];
    if(!c->sys||(c->msr&need)!=need){c->gpr[3]=rc;return 1;}
    uint8_t b[PTA_MAX_REQUEST];uint32_t physical[17],n=0,ms=0;
    if(op==PTA_HELLO){rc=pt_agent_register_caps(a,c->gpr[5],c->gpr[6],&token);if(!rc){c->gpr[4]=token;c->gpr[5]=PTA_VERSION;const char *v=getenv("PT_POINTER_V2");c->gpr[6]=v&&!strcmp(v,"1")?PTA_CAP_INPUT:0;v=getenv("PT_R1_UX");if(v&&!strcmp(v,"1"))c->gpr[6]|=PTA_CAP_POINTER|PTA_CAP_EXPORT;}}
    else if(op==PTA_POLL){uint32_t ea=c->gpr[5],cap=c->gpr[6];
        rc=pages(c,ea,cap,1,physical);if(!rc){rc=pt_agent_pull(a,token,b,cap,&id,&n,&ms);
            if(!rc){copy_pages(c,ea,b,n,1,physical);c->gpr[4]=id;c->gpr[5]=n;c->gpr[6]=ms;}}}
    else if(op==PTA_OUTPUT){n=c->gpr[7];rc=pages(c,c->gpr[6],n,0,physical);
        if(!rc){copy_pages(c,c->gpr[6],b,n,0,physical);rc=pt_agent_output(a,token,id,b,n);}}
    else if(op==PTA_FINISH)rc=pt_agent_finish(a,token,id,c->gpr[6]);
    else if(op==PTA_CHECK){uint32_t cancel=0;rc=pt_agent_check(a,token,id,&cancel);if(!rc)c->gpr[4]=cancel;}
    else if(op==PTA_INPUT)rc=c->gpr[6]==PTA_INPUT_ABI?pt_agent_input(a,token,id):PTA_INVALID;
    else if(op==PTA_POINTER){rc=c->gpr[6]==24?pages(c,c->gpr[5],24,0,physical):PTA_INVALID;
        if(!rc){copy_pages(c,c->gpr[5],b,24,0,physical);rc=pt_agent_pointer(a,token,b);}}
    else if(op==PTA_BYE)rc=pt_agent_unregister(a,token);
    /* Transfers. Every guest buffer is validated in full before the host state moves. */
    else if(op==PTA_XBEGIN){uint32_t ea=c->gpr[5];rc=pages(c,ea,PTA_XHEAD,1,physical);
        if(!rc){rc=pt_agent_xbegin(a,token,b,&id);if(!rc){copy_pages(c,ea,b,PTA_XHEAD,1,physical);c->gpr[4]=id;}}}
    else if(op==PTA_XCHUNK){uint32_t ea=c->gpr[7];rc=pages(c,ea,PTA_CHUNK,1,physical);
        if(!rc){rc=pt_agent_xchunk(a,token,id,c->gpr[6],b,&n);if(!rc){copy_pages(c,ea,b,n,1,physical);c->gpr[4]=n;}}}
    else if(op==PTA_XCOMMIT){rc=pages(c,c->gpr[6],32,0,physical);
        if(!rc){copy_pages(c,c->gpr[6],b,32,0,physical);rc=pt_agent_xcommit(a,token,id,b);}}
    else if(op==PTA_XDONE){rc=pages(c,c->gpr[7],PTA_PATH_MAX,0,physical);
        if(!rc){copy_pages(c,c->gpr[7],b,PTA_PATH_MAX,0,physical);b[PTA_PATH_MAX-1]=0;rc=pt_agent_xdone(a,token,id,c->gpr[6],(char *)b);}}
    else if(op==PTA_EXPORT_BEGIN)rc=pt_agent_export_begin(a,token,id,(uint64_t)c->gpr[6]<<32|c->gpr[7]);
    else if(op==PTA_EXPORT_CHUNK){n=c->gpr[7];
        rc=n>8&&n<=PTA_CHUNK?pages(c,c->gpr[6],n,0,physical):PTA_INVALID;
        if(!rc){copy_pages(c,c->gpr[6],b,n,0,physical);uint64_t off=0;for(unsigned i=0;i<8;i++)off=off<<8|b[i];rc=pt_agent_export_chunk(a,token,id,off,b+8,n-8);}}
    else if(op==PTA_EXPORT_COMMIT){rc=pages(c,c->gpr[6],32,0,physical);
        if(!rc){copy_pages(c,c->gpr[6],b,32,0,physical);rc=pt_agent_export_commit(a,token,id,b);}}
    else if(op==PTA_OPTICS){
        uint32_t ea=c->gpr[5];
        if(c->gpr[6]!=OPTICS_DIAG_BYTES||c->gpr[7]!=OPTICS_DIAG_ABI)rc=PTA_INVALID;
        else {rc=pages(c,ea,OPTICS_DIAG_BYTES,1,physical);if(!rc){rc=pt_agent_optics_status(a,token,b);if(!rc)copy_pages(c,ea,b,OPTICS_DIAG_BYTES,1,physical);}}
    }
    else rc=PTA_INVALID;
    c->gpr[3]=rc;return 1;
}
