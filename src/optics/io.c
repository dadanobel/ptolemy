/* SPDX-License-Identifier: GPL-2.0-or-later
 * Single-owner guest-memory transactions. No Metal, host waits or global state. */
#include "io.h"
#include <stdlib.h>
#include <string.h>
typedef struct {
    uint64_t physical,version,writer;
    uint32_t refs;
    uint8_t ram,guard,used;
} Page;
typedef struct {
    uint64_t gpu,physical,generation,version;
    uint8_t *pointer;
    size_t bytes;
    uint32_t page;
} Ref;
typedef struct {
    uint64_t id,address;
    size_t bytes,count;
    Ref *refs;
    uint8_t *data;
    unsigned kind,discarded; /* 1 snapshot, 2 GPU destination, 3 write watch without data */
} Transfer;
struct PtR2Io {
    PtR2IoConfig config;
    PtR2IoStats stats;
    Page *pages;
    uint32_t *hash,*free_pages,free_count;
    Transfer *transfers;
    uint64_t next_id,version;
};
static uint32_t hash_page(const PtR2Io *io,int ram,uint64_t physical) {
    uint64_t x=(physical>>12)^(ram?UINT64_C(0xbaddecaf12345678):0);
    x^=x>>30;x*=UINT64_C(0xbf58476d1ce4e5b9);x^=x>>27;x*=UINT64_C(0x94d049bb133111eb);x^=x>>31;
    return (uint32_t)x&(io->config.pages*2-1);
}
static Page *find_page(PtR2Io *io,int ram,uint64_t physical,int create) {
    physical&=~UINT64_C(4095);uint32_t at=hash_page(io,ram,physical),mask=io->config.pages*2-1;
    while(io->hash[at]) {
        Page *p=&io->pages[io->hash[at]-1];if(p->ram==!!ram&&p->physical==physical)return p;
        at=(at+1)&mask;
    }
    if(!create||!io->free_count||io->version==UINT64_MAX)return NULL;
    uint32_t index=io->free_pages[--io->free_count];Page *p=&io->pages[index];
    *p=(Page){.physical=physical,.version=++io->version,.ram=!!ram,.used=1};io->hash[at]=index+1;io->stats.pages++;
    return p;
}
static PtR2IoResult protect(PtR2Io *io,Page *p,PtR2IoGuard mode) {
    if(p->guard==(unsigned)mode)return PT_R2_IO_OK;
    io->stats.guard_calls++;
    void *context=io->config.protect_context?io->config.protect_context:io->config.context;
    if(!io->config.protect(context,p->ram,p->physical,mode)){io->stats.fatal=1;return PT_R2_IO_COHERENCE;}
    p->guard=(uint8_t)mode;return PT_R2_IO_OK;
}
static void erase_page(PtR2Io *io,Page *p) {
    uint32_t index=(uint32_t)(p-io->pages),hole=hash_page(io,p->ram,p->physical),mask=io->config.pages*2-1;
    while(io->hash[hole]!=index+1)hole=(hole+1)&mask;
    /* Backshift HASH indices; Ref.page points to a stable entry pool. No
     * tombstones or moving live Page entries under outstanding snapshots. */
    for(uint32_t next=(hole+1)&mask;io->hash[next];next=(next+1)&mask) {
        Page *other=&io->pages[io->hash[next]-1];uint32_t home=hash_page(io,other->ram,other->physical);
        if(((next-home)&mask)>=((next-hole)&mask)){io->hash[hole]=io->hash[next];hole=next;}
    }
    io->hash[hole]=0;memset(p,0,sizeof *p);io->free_pages[io->free_count++]=index;io->stats.pages--;
}
static PtR2IoResult drop(PtR2Io *io,Transfer *t) {
    PtR2IoResult result=PT_R2_IO_OK;
    for(size_t i=0;i<t->count;i++) {
        Page *p=&io->pages[t->refs[i].page];
        if(p->writer==t->id) {p->writer=0;if(protect(io,p,PT_R2_IO_OPEN)!=PT_R2_IO_OK)result=PT_R2_IO_COHERENCE;}
        p->refs--;
        if(!p->refs) {
            if(protect(io,p,PT_R2_IO_OPEN)==PT_R2_IO_OK)erase_page(io,p);
            else result=PT_R2_IO_COHERENCE;
        }
    }
    if(t->kind==1&&!t->discarded)io->stats.snapshot_bytes-=t->bytes;
    free(t->refs);free(t->data);memset(t,0,sizeof *t);io->stats.transfers--;return result;
}
static Transfer *lookup(PtR2Io *io,PtR2IoToken token,unsigned kind) {
    if(!io||token.epoch!=io->stats.epoch||!token.id)return NULL;
    for(uint32_t i=0;i<io->config.transfers;i++)if(io->transfers[i].kind==kind&&io->transfers[i].id==token.id)return &io->transfers[i];
    return NULL;
}
static PtR2IoResult gather(PtR2Io *io,uint64_t at,size_t n,unsigned kind,Transfer **out) {
    if(!io||io->stats.fatal)return PT_R2_IO_COHERENCE;
    if(n>UINT64_MAX-at||n>SIZE_MAX-8192||(!n&&kind==2))return PT_R2_IO_ARGUMENT;
    if(kind==1&&(n>io->config.snapshot_bytes||n>io->config.snapshot_bytes-io->stats.snapshot_bytes))return PT_R2_IO_CAPACITY;
    size_t capacity=n/4096+2;if(capacity>io->config.pages)return PT_R2_IO_CAPACITY;
    Transfer *t=NULL;for(uint32_t i=0;i<io->config.transfers;i++)if(!io->transfers[i].kind){t=&io->transfers[i];break;}
    if(!t||io->next_id==UINT64_MAX)return PT_R2_IO_CAPACITY;
    Ref *refs=calloc(capacity,sizeof *refs);if(!refs)return PT_R2_IO_CAPACITY;
    *t=(Transfer){.id=++io->next_id,.address=at,.bytes=n,.refs=refs,.kind=kind};io->stats.transfers++;
    if(kind==1) {t->data=n?malloc(n):NULL;io->stats.snapshot_bytes+=n;if(n&&!t->data){drop(io,t);return PT_R2_IO_CAPACITY;}}
    for(size_t offset=0;offset<n;) {
        PtR2Span s;
        if(!pt_r2_address_resolve(&io->config.address,at+offset,kind==2,&s)){drop(io,t);return PT_R2_IO_MAPPING;}
        size_t bytes=4096-(size_t)(s.physical&4095);if(bytes>s.length)bytes=s.length;if(bytes>n-offset)bytes=n-offset;
        Page *p=find_page(io,s.gart,s.physical,1);if(!p){drop(io,t);return PT_R2_IO_CAPACITY;}
        p->refs++;t->refs[t->count++]=(Ref){at+offset,s.physical,s.mapping_generation,p->version,s.bytes,bytes,(uint32_t)(p-io->pages)};
        offset+=bytes;
    }
    for(size_t i=0;i<t->count;i++)if(io->pages[t->refs[i].page].writer){io->stats.busy++;drop(io,t);return PT_R2_IO_BUSY;}
    for(size_t i=0;i<t->count;i++) {
        Page *p=&io->pages[t->refs[i].page];
        if(kind==2&&p->writer==t->id)continue;
        if(protect(io,p,kind==2?PT_R2_IO_GPU_OWNED:PT_R2_IO_WATCH_WRITES)!=PT_R2_IO_OK){drop(io,t);return PT_R2_IO_COHERENCE;}
        if(kind==2) {
            if(io->version==UINT64_MAX){io->stats.fatal=1;drop(io,t);return PT_R2_IO_COHERENCE;}
            p->writer=t->id;p->version=++io->version;io->stats.invalidations++;
        }
    }
    *out=t;return PT_R2_IO_OK;
}
PtR2Io *pt_r2_io_create(const PtR2IoConfig *c) {
    if(!c||!c->protect||!c->written||!c->address.generation||c->pages<16||c->pages>(1u<<20)||(c->pages&(c->pages-1))||!c->transfers||c->transfers>65536||!c->snapshot_bytes||
       ((c->address.fb_base|c->address.fb_length|c->address.gart_base|c->address.gart_length)&4095))return NULL;
    PtR2Io *io=calloc(1,sizeof *io);if(!io)return NULL;io->config=*c;io->stats.epoch=1;
    io->pages=calloc(c->pages,sizeof(Page));io->hash=calloc(c->pages*2,sizeof(uint32_t));io->free_pages=malloc(c->pages*sizeof(uint32_t));io->transfers=calloc(c->transfers,sizeof(Transfer));
    if(!io->pages||!io->hash||!io->free_pages||!io->transfers){free(io->pages);free(io->hash);free(io->free_pages);free(io->transfers);free(io);return NULL;}
    io->free_count=c->pages;for(uint32_t i=0;i<c->pages;i++)io->free_pages[i]=i;return io;
}
PtR2IoResult pt_r2_io_reset(PtR2Io *io) {
    if(!io||io->stats.epoch==UINT64_MAX)return PT_R2_IO_ARGUMENT;
    io->stats.epoch++; /* Late completions are stale BEFORE any protection changes. */
    for(uint32_t i=0;i<io->config.transfers;i++){free(io->transfers[i].refs);free(io->transfers[i].data);memset(&io->transfers[i],0,sizeof(Transfer));}
    io->stats.snapshot_bytes=io->stats.transfers=0;int failed=0;
    for(uint32_t i=0;i<io->config.pages;i++)if(io->pages[i].used) {
        io->pages[i].writer=0;io->pages[i].refs=0;if(protect(io,&io->pages[i],PT_R2_IO_OPEN)!=PT_R2_IO_OK)failed=1;
    }
    io->stats.fatal=failed;if(failed)return PT_R2_IO_COHERENCE;
    memset(io->pages,0,io->config.pages*sizeof(Page));memset(io->hash,0,io->config.pages*2*sizeof(uint32_t));
    io->stats.pages=0;io->free_count=io->config.pages;for(uint32_t i=0;i<io->config.pages;i++)io->free_pages[i]=i;
    return PT_R2_IO_OK;
}
PtR2IoResult pt_r2_io_destroy(PtR2Io *io) {
    if(!io)return PT_R2_IO_OK;PtR2IoResult result=pt_r2_io_reset(io);if(result!=PT_R2_IO_OK)return result;
    free(io->pages);free(io->hash);free(io->free_pages);free(io->transfers);free(io);return PT_R2_IO_OK;
}
PtR2IoResult pt_r2_io_before_physical(PtR2Io *io,int ram,uint64_t at,size_t n,int write) {
    if(!io||io->stats.fatal)return PT_R2_IO_COHERENCE;
    if(n>UINT64_MAX-at)return PT_R2_IO_ARGUMENT;
    for(size_t offset=0;offset<n;) {
        Page *p=find_page(io,ram,at+offset,0);if(p&&p->writer){io->stats.busy++;return PT_R2_IO_BUSY;}
        size_t bytes=4096-((size_t)(at+offset)&4095);if(bytes>n-offset)bytes=n-offset;offset+=bytes;
    }
    if(write)for(size_t offset=0;offset<n;) {
        Page *p=find_page(io,ram,at+offset,0);
        if(p) {
            if(io->version==UINT64_MAX){io->stats.fatal=1;return PT_R2_IO_COHERENCE;}
            p->version=++io->version;io->stats.invalidations++;
            if(protect(io,p,PT_R2_IO_OPEN)!=PT_R2_IO_OK)return PT_R2_IO_COHERENCE;
        }
        size_t bytes=4096-((size_t)(at+offset)&4095);if(bytes>n-offset)bytes=n-offset;offset+=bytes;
    }
    return PT_R2_IO_OK;
}
static PtR2IoResult cpu(PtR2Io *io,uint64_t at,void *data,size_t n,unsigned lane,int write) {
    if(!io||io->stats.fatal)return PT_R2_IO_COHERENCE;
    if((!data&&n)||lane>3||n>UINT64_MAX-at)return PT_R2_IO_ARGUMENT;
    if(!n)return PT_R2_IO_OK;
    unsigned align=lane==2?3:lane;uint64_t start=at&~(uint64_t)align,end=at+n;
    if(end>UINT64_MAX-align)return PT_R2_IO_ARGUMENT;end=(end+align)&~(uint64_t)align;
    for(uint64_t p=start;p<end;) {
        PtR2Span s;if(!pt_r2_address_resolve(&io->config.address,p,write,&s))return PT_R2_IO_MAPPING;
        size_t bytes=4096-(size_t)(s.physical&4095);if(bytes>s.length)bytes=s.length;if(bytes>end-p)bytes=(size_t)(end-p);
        PtR2IoResult r=pt_r2_io_before_physical(io,s.gart,s.physical,bytes,0);if(r!=PT_R2_IO_OK)return r;p+=bytes;
    }
    if(write)for(uint64_t p=start;p<end;) {
        PtR2Span s;if(!pt_r2_address_resolve(&io->config.address,p,1,&s))return PT_R2_IO_MAPPING;
        size_t bytes=4096-(size_t)(s.physical&4095);if(bytes>s.length)bytes=s.length;if(bytes>end-p)bytes=(size_t)(end-p);
        PtR2IoResult r=pt_r2_io_before_physical(io,s.gart,s.physical,bytes,1);if(r!=PT_R2_IO_OK)return r;p+=bytes;
    }
    for(uint64_t p=start;p<end;) {
        PtR2Span s;if(!pt_r2_address_resolve(&io->config.address,p,write,&s))return PT_R2_IO_MAPPING;
        size_t count=4096-(size_t)(s.physical&4095);if(count>s.length)count=s.length;if(count>end-p)count=(size_t)(end-p);
        uint64_t first=p>at?p:at,last=p+count<at+n?p+count:at+n;
        if(!lane) {
            if(write)memcpy(s.bytes,(const uint8_t *)data+(p-at),count);else memcpy((uint8_t *)data+(p-at),s.bytes,count);
        } else for(uint64_t a=first;a<last;a++) {
            size_t index=(size_t)((a^(uint64_t)lane)-p);
            if(write)s.bytes[index]=((const uint8_t *)data)[a-at];else ((uint8_t *)data)[a-at]=s.bytes[index];
        }
        if(write) {
            io->config.written(io->config.context,s.gart,s.physical,count);
            if(io->config.address.dirty)io->config.address.dirty(io->config.address.context,p,count,s.gart);
        }
        p+=count;
    }
    if(write)io->stats.cpu_writes++;else io->stats.cpu_reads++;return PT_R2_IO_OK;
}
PtR2IoResult pt_r2_io_cpu_read(PtR2Io *io,uint64_t at,void *data,size_t n,unsigned lane) {return cpu(io,at,data,n,lane,0);}
PtR2IoResult pt_r2_io_cpu_write(PtR2Io *io,uint64_t at,const void *data,size_t n,unsigned lane) {return cpu(io,at,(void *)data,n,lane,1);}
PtR2IoResult pt_r2_io_snapshot(PtR2Io *io,uint64_t at,size_t n,PtR2IoToken *out) {
    if(!out)return PT_R2_IO_ARGUMENT;Transfer *t;PtR2IoResult r=gather(io,at,n,1,&t);if(r!=PT_R2_IO_OK)return r;
    size_t offset=0;for(size_t i=0;i<t->count;i++){Ref *ref=&t->refs[i];ref->version=io->pages[ref->page].version;memcpy(t->data+offset,ref->pointer,ref->bytes);offset+=ref->bytes;}
    *out=(PtR2IoToken){io->stats.epoch,t->id};io->stats.snapshots++;return PT_R2_IO_OK;
}
PtR2IoResult pt_r2_io_snapshot_data(PtR2Io *io,PtR2IoToken token,const uint8_t **data,size_t *n) {
    Transfer *t=lookup(io,token,1);if(!t||t->discarded)return PT_R2_IO_STALE;
    if(!data||!n)return PT_R2_IO_ARGUMENT;*data=t->data;*n=t->bytes;return PT_R2_IO_OK;
}
PtR2IoResult pt_r2_io_snapshot_forget_data(PtR2Io *io,PtR2IoToken token) {
    Transfer *t=lookup(io,token,1);if(!t)return PT_R2_IO_STALE;
    if(!t->discarded){free(t->data);t->data=NULL;t->discarded=1;io->stats.snapshot_bytes-=t->bytes;}return PT_R2_IO_OK;
}
PtR2IoResult pt_r2_io_snapshot_take_data(PtR2Io *io,PtR2IoToken token,uint8_t **out,size_t *n) {
    Transfer *t=lookup(io,token,1);if(!t||t->discarded)return PT_R2_IO_STALE;
    if(!out||!n)return PT_R2_IO_ARGUMENT;*out=t->data;*n=t->bytes;t->data=NULL;t->discarded=1;io->stats.snapshot_bytes-=t->bytes;return PT_R2_IO_OK;
}
int pt_r2_io_snapshot_valid(PtR2Io *io,PtR2IoToken token) {
    Transfer *t=lookup(io,token,1);if(!t||io->stats.fatal)return 0;
    for(size_t i=0;i<t->count;i++) {
        Ref *r=&t->refs[i];Page *p=&io->pages[r->page];PtR2Span s;
        if(p->writer||p->version!=r->version||!pt_r2_address_resolve(&io->config.address,r->gpu,0,&s)||s.physical!=r->physical||s.mapping_generation!=r->generation||s.bytes!=r->pointer||s.length<r->bytes)return 0;
    }return 1;
}
PtR2IoResult pt_r2_io_release(PtR2Io *io,PtR2IoToken token) {Transfer *t=lookup(io,token,1);if(!t)t=lookup(io,token,3);return t?drop(io,t):PT_R2_IO_STALE;}
/* The guard/version effect of a snapshot without its byte copy, for GPU copies whose bytes the owner
 * already read itself (a clean GPU-owned surface, a decoded texture). Released with pt_r2_io_release. */
PtR2IoResult pt_r2_io_watch(PtR2Io *io,uint64_t at,size_t n,PtR2IoToken *out) {
    if(!out)return PT_R2_IO_ARGUMENT;Transfer *t;PtR2IoResult r=gather(io,at,n,3,&t);if(r!=PT_R2_IO_OK)return r;
    for(size_t i=0;i<t->count;i++)t->refs[i].version=io->pages[t->refs[i].page].version;
    *out=(PtR2IoToken){io->stats.epoch,t->id};return PT_R2_IO_OK;
}
PtR2IoResult pt_r2_io_write_begin(PtR2Io *io,uint64_t at,size_t n,PtR2IoToken *out) {
    if(!out)return PT_R2_IO_ARGUMENT;Transfer *t;PtR2IoResult r=gather(io,at,n,2,&t);if(r!=PT_R2_IO_OK)return r;
    *out=(PtR2IoToken){io->stats.epoch,t->id};io->stats.gpu_writes++;return PT_R2_IO_OK;
}
PtR2IoResult pt_r2_io_write_abort(PtR2Io *io,PtR2IoToken token) {Transfer *t=lookup(io,token,2);return t?drop(io,t):PT_R2_IO_STALE;}
int pt_r2_io_write_pending(PtR2Io *io,PtR2IoToken token) {return lookup(io,token,2)!=NULL;}
static void write_segment(PtR2Io *io,Transfer *t,size_t offset,const uint8_t *data,size_t n,const uint8_t *lanes) {
    size_t first=0,consumed=0;
    for(size_t i=0;i<t->count&&n;i++) {
        Ref *r=&t->refs[i];if(offset>=first+r->bytes){first+=r->bytes;continue;}
        size_t within=offset-first,count=r->bytes-within;if(count>n)count=n;
        if(lanes)for(size_t k=0;k<count;k++){size_t j=consumed+k;r->pointer[within+k]=data[(j&~(size_t)3)+lanes[j&3]];}
        else memcpy(r->pointer+within,data+consumed,count);
        io->config.written(io->config.context,io->pages[r->page].ram,r->physical+within,count);
        if(io->config.address.dirty)io->config.address.dirty(io->config.address.context,r->gpu+within,count,io->pages[r->page].ram);
        consumed+=count;offset+=count;n-=count;first+=r->bytes;
    }
}
PtR2IoResult pt_r2_io_write_complete(PtR2Io *io,PtR2IoToken token,const void *data,size_t n) {
    Transfer *t=lookup(io,token,2);if(!t)return PT_R2_IO_STALE;if(io->stats.fatal)return PT_R2_IO_COHERENCE;
    if(!data||n!=t->bytes)return PT_R2_IO_ARGUMENT;write_segment(io,t,0,data,n,NULL);return drop(io,t);
}
PtR2IoResult pt_r2_io_write_rgba_rows(PtR2Io *io,PtR2IoToken token,const uint8_t *data,size_t n,uint32_t width,uint32_t height,size_t pitch,const uint8_t lanes[4]) {
    Transfer *t=lookup(io,token,2);if(!t)return PT_R2_IO_STALE;if(io->stats.fatal)return PT_R2_IO_COHERENCE;
    uint64_t row=(uint64_t)width*4;
    if(!height||row>UINT64_MAX/height||(height>1&&pitch>(UINT64_MAX-row)/(height-1)))return PT_R2_IO_ARGUMENT;
    uint64_t total=(uint64_t)height*row,span=(uint64_t)(height-1)*pitch+row;
    unsigned mask=0;if(lanes)for(unsigned j=0;j<4;j++)if(lanes[j]<4)mask|=1u<<lanes[j];
    if(!data||!width||!height||row>pitch||total>n||span!=t->bytes||mask!=15)return PT_R2_IO_ARGUMENT;
    for(unsigned y=0;y<height;y++)write_segment(io,t,(size_t)y*pitch,data+(size_t)y*(size_t)row,(size_t)row,lanes);
    return drop(io,t);
}
void pt_r2_io_stats(const PtR2Io *io,PtR2IoStats *out) {if(io&&out)*out=io->stats;}
const char *pt_r2_io_result(PtR2IoResult r) {
    static const char *const names[]={"ok","busy: materialize/yield","invalid argument","unmapped GPU address","capacity/backpressure","coherence protection failed","stale token"};
    return (unsigned)r<sizeof names/sizeof *names?names[r]:"unknown I/O result";
}
