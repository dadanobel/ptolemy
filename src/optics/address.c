/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "address.h"
#include <string.h>
static int inside(uint64_t p,uint64_t base,uint64_t length) {return p>=base&&p-base<length;}
int pt_r2_address_resolve(const PtR2AddressSpace *s,uint64_t p,int write,PtR2Span *out) {
    if(!s||!out||!s->generation)return 0;
    if(s->fb_length>s->vram_bytes||(s->fb_length&&!s->vram)||s->fb_base>UINT64_MAX-s->fb_length||s->gart_base>UINT64_MAX-s->gart_length)return 0;
    if(s->fb_length&&s->gart_length&&s->fb_base<s->gart_base+s->gart_length&&s->gart_base<s->fb_base+s->fb_length)return 0;
    if(inside(p,s->fb_base,s->fb_length)) {
        uint64_t off=p-s->fb_base;
        if(!s->vram||off>=s->vram_bytes)return 0;
        uint64_t count=s->fb_length-off;
        if(count>s->vram_bytes-off)count=s->vram_bytes-off;
        *out=(PtR2Span){s->vram+(size_t)off,(size_t)count,s->generation,0,off};return 1;
    }
    if(inside(p,s->gart_base,s->gart_length)&&s->map_page) {
        PtR2Span page={0};uint64_t page_start=p&~UINT64_C(4095);size_t off=(size_t)(p-page_start);
        if(!s->map_page(s->context,page_start,write,&page)||!page.bytes||page.length!=4096||!page.mapping_generation||(page.physical&4095)||page.physical>UINT64_MAX-4096)return 0;
        size_t count=4096-off;uint64_t left=s->gart_length-(p-s->gart_base);
        if(count>left)count=(size_t)left;
        *out=(PtR2Span){page.bytes+off,count,page.mapping_generation,1,page.physical+off};return 1;
    }
    return 0;
}
static int transfer(const PtR2AddressSpace *s,uint64_t p,void *data,size_t bytes,int write) {
    if(!s||(!data&&bytes)||bytes>UINT64_MAX-p)return 0;
    /* Validate first: page faults cannot leave half a guest write behind. */
    for(size_t n=0;n<bytes;) {
        PtR2Span span;if(!pt_r2_address_resolve(s,p+n,write,&span)||!span.length)return 0;
        size_t count=span.length;if(count>bytes-n)count=bytes-n;n+=count;
    }
    for(size_t n=0;n<bytes;) {
        PtR2Span span;if(!pt_r2_address_resolve(s,p+n,write,&span))return 0;
        size_t count=span.length;if(count>bytes-n)count=bytes-n;
        if(write){memmove(span.bytes,(const uint8_t *)data+n,count);if(s->dirty)s->dirty(s->context,p+n,count,span.gart);}
        else memmove((uint8_t *)data+n,span.bytes,count);
        n+=count;
    }
    return 1;
}
int pt_r2_address_read(const PtR2AddressSpace *s,uint64_t p,void *out,size_t n) {return transfer(s,p,out,n,0);}
int pt_r2_address_write(const PtR2AddressSpace *s,uint64_t p,const void *in,size_t n) {return transfer(s,p,(void *)in,n,1);}
