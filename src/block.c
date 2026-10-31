/* SPDX-License-Identifier: GPL-2.0-or-later
 * One FIFO worker; no disk I/O while holding the queue mutex. */
#include "block.h"
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#ifdef __APPLE__
#include <sys/clonefile.h>
#include <sys/disk.h>
#include <sys/ioctl.h>
#endif
enum { CAPACITY=32, APM_BYTES=32768, APM_BLOCKS=64, BUNDLE_FDS=64 };
struct PtBlock {
    int fd, writable, closing;
    unsigned alignment;
    uint64_t bytes, submitted, started, completed, consumed;
    uint8_t *prefix;
    PtBlockRequest requests[CAPACITY];
    pthread_t worker;
    pthread_mutex_t mutex;
    pthread_cond_t work;
    PtEvent *completion;
    _Atomic uint32_t *cpu_kick;
    /* Overlay: MAP_PRIVATE view and written-page bitmap, owned by the worker until close. */
    uint8_t *map;
    uint64_t *dirty, page, pages, budget;
    int overflow;
    char *source, *save;
    /* Sparse bundle (macOS UDSB): files bands/<hex index> of `band` bytes each. Only written bands
     * exist, so the image occupies what the guest wrote; `hdiutil compact` returns what it freed. */
    uint64_t band, tick;
    int bands, created;
    int8_t *missing;                                  /* band known to have no file yet */
    struct { uint64_t i, used; int fd, dirty; } open[BUNDLE_FDS];
};
/* An open band from a small LRU (the process has few descriptors); evicting a written band syncs it
 * first, so FLUSH still covers it. *fd = -1: the band has no file, its bytes are zeros. */
static int band_fd(PtBlock *b,uint64_t i,int create,int *fd) {
    unsigned slot=0;
    for(unsigned k=0;k<BUNDLE_FDS;k++) {
        if(b->open[k].fd>0 && b->open[k].i==i) { b->open[k].used=++b->tick; *fd=b->open[k].fd; return 0; }
        if(b->open[k].used<b->open[slot].used) slot=k;
    }
    *fd=-1;
    if(b->missing[i] && !create) return 0;
    char name[24]; snprintf(name,sizeof name,"%llx",(unsigned long long)i);
    int f=openat(b->bands,name,(create?O_RDWR|O_CREAT:b->writable && !b->map?O_RDWR:O_RDONLY)|O_CLOEXEC,0644);
    if(f<0) { if(errno!=ENOENT) return errno; b->missing[i]=1; return 0; }
    if(create) b->created=1;                          /* FLUSH also syncs the bands directory */
    b->missing[i]=0;
    if(b->open[slot].fd>0) {
        if(b->open[slot].dirty && fsync(b->open[slot].fd)) { int e=errno; close(f); return e; }
        close(b->open[slot].fd);
    }
    b->open[slot].i=i; b->open[slot].fd=*fd=f; b->open[slot].dirty=0; b->open[slot].used=++b->tick;
    return 0;
}
static int bundle_io(PtBlock *b,int write,uint64_t off,uint8_t *p,size_t n) {
    while(n) {
        uint64_t i=off/b->band, in=off%b->band;
        size_t k=b->band-in<n?(size_t)(b->band-in):n, done=0;
        int fd,e=band_fd(b,i,write,&fd);
        if(e) return e;
        if(fd<0) memset(p,0,k);
        while(fd>=0 && done<k) {
            ssize_t r=write?pwrite(fd,p+done,k-done,(off_t)(in+done)):pread(fd,p+done,k-done,(off_t)(in+done));
            if(r<0 && errno==EINTR) continue;
            if(r<0) return errno;
            if(!r) { if(write) return EIO; memset(p+done,0,k-done); break; }   /* past the band file's end */
            done+=(size_t)r;
        }
        if(write) for(unsigned s=0;s<BUNDLE_FDS;s++) if(b->open[s].fd==fd) b->open[s].dirty=1;
        off+=k; p+=k; n-=k;
    }
    return 0;
}
static int bundle_flush(PtBlock *b) {
    int last=-1;
    for(unsigned s=0;s<BUNDLE_FDS;s++) if(b->open[s].fd>0 && b->open[s].dirty) {
        if(fsync(b->open[s].fd)) return errno;
        b->open[s].dirty=0; last=b->open[s].fd;
    }
    if(b->created) { if(fsync(b->bands)) return errno; b->created=0; last=b->bands; }
#ifdef __APPLE__
    if(last>=0 && fcntl(last,F_FULLFSYNC)) return errno;    /* one drive-cache flush covers every band */
#else
    (void)last;
#endif
    return 0;
}
/* Distinct written pages of all overlays in this process (PT_OVERLAY_MAX_MB). */
static _Atomic uint64_t overlay_pages;
static int overlay_reserve(uint64_t n, uint64_t budget) {
    uint64_t used=atomic_load(&overlay_pages);
    do { if(used>budget || n>budget-used) return 0; }
    while(!atomic_compare_exchange_weak(&overlay_pages,&used,used+n));
    return 1;
}
static int overlay_transfer(PtBlock *b, PtBlockRequest *r) {
    if(r->operation==PT_BLOCK_FLUSH) return 0;       /* nothing in an overlay is durable */
    uint64_t first=r->offset/b->page, last=(r->offset+r->size-1)/b->page, fresh=0;
    if(r->operation==PT_BLOCK_READ) {
        if(!b->band) { memcpy(r->buffer,b->map+r->offset,r->size); return 0; }
        /* Bundle: written pages from memory, runs of unwritten pages straight from the bands
         * (never through the anonymous map, which would make them resident). */
        for(uint64_t from=r->offset,end=r->offset+r->size;from<end;) {
            int written=b->dirty[from/b->page/64]>>(from/b->page%64)&1;
            uint64_t to=from;
            while(to<end && (b->dirty[to/b->page/64]>>(to/b->page%64)&1)==(unsigned)written) to=(to/b->page+1)*b->page;
            if(to>end) to=end;
            uint8_t *dst=(uint8_t *)r->buffer+(from-r->offset);
            if(written) memcpy(dst,b->map+from,to-from);
            else { int e=bundle_io(b,0,from,dst,(size_t)(to-from)); if(e) return e; }
            from=to;
        }
        return 0;
    }
    for(uint64_t p=first;p<=last;p++) fresh+=!(b->dirty[p/64]>>(p%64)&1);
    /* Reserve the whole growth first: a refused request changes no data. */
    if(fresh && !overlay_reserve(fresh,b->budget)) { b->overflow=1; return EIO; }
    uint64_t edge[2]={first,last};                       /* bundle: a partly written fresh page keeps its other bytes */
    for(unsigned k=0;b->band && k<2-(first==last);k++) {
        uint64_t p=edge[k], off=p*b->page, n=b->bytes-off<b->page?b->bytes-off:b->page;
        if(b->dirty[p/64]>>(p%64)&1 || (r->offset<=off && r->offset+r->size>=off+n)) continue;
        int e=bundle_io(b,0,off,b->map+off,(size_t)n);
        if(e) { atomic_fetch_sub(&overlay_pages,fresh); return e; }
    }
    for(uint64_t p=first;p<=last;p++) b->dirty[p/64]|=1ull<<(p%64);
    b->pages+=fresh;
    memcpy(b->map+r->offset,r->buffer,r->size);
    return 0;
}
static int transfer(PtBlock *b, PtBlockRequest *r) {
    if(b->map) return overlay_transfer(b,r);
    if(b->band) return r->operation==PT_BLOCK_FLUSH ? bundle_flush(b) : bundle_io(b,r->operation==PT_BLOCK_WRITE,r->offset,r->buffer,r->size);
    if(r->operation==PT_BLOCK_FLUSH) {
#ifdef __APPLE__
        if(fcntl(b->fd,F_FULLFSYNC)==0) return 0;
        return errno; /* Never acknowledge a failed durability operation. */
#else
        return fsync(b->fd)==0 ? 0 : errno;
#endif
    }
    size_t done=0;
    uint64_t prefix=b->prefix?APM_BYTES:0;
    if(r->offset<prefix) {
        done=prefix-r->offset;
        if(done>r->size)done=r->size;
        memcpy(r->buffer,b->prefix+r->offset,done);
    }
    while(done<r->size) {
        ssize_t n=r->operation==PT_BLOCK_READ
            ? pread(b->fd,(uint8_t *)r->buffer+done,r->size-done,(off_t)(r->offset+done-prefix))
            : pwrite(b->fd,(uint8_t *)r->buffer+done,r->size-done,(off_t)(r->offset+done-prefix));
        if(n<0 && errno==EINTR) continue;
        if(n<0) return errno;
        if(!n) return EIO;
        done+=(size_t)n;
    }
    return 0;
}
static void *worker(void *opaque) {
    PtBlock *b=opaque;
    pthread_mutex_lock(&b->mutex);
    for(;;) {
        while(b->started==b->submitted && !b->closing)
            pthread_cond_wait(&b->work,&b->mutex);
        if(b->started==b->submitted && b->closing) break;
        unsigned slot=b->started++%CAPACITY;
        PtBlockRequest *r=&b->requests[slot];
        pthread_mutex_unlock(&b->mutex);
        int error=transfer(b,r);
        pthread_mutex_lock(&b->mutex);
        r->error=error; b->completed++;
        pthread_mutex_unlock(&b->mutex);
        /* Queue lock publishes buffer writes. Wakeups are hints, not IRQs. */
        if(b->cpu_kick) atomic_store_explicit(b->cpu_kick,1,memory_order_release);
        if(b->completion) pt_event_signal(b->completion);
        pthread_mutex_lock(&b->mutex);
    }
    pthread_mutex_unlock(&b->mutex); return 0;
}
static void apm_entry(uint8_t *p,uint32_t start,uint32_t count,const char *name,const char *type,unsigned status) {
    memcpy(p,"PM",2);pt_write_be(p+4,4,2);pt_write_be(p+8,4,start);pt_write_be(p+12,4,count);
    memcpy(p+16,name,strlen(name));memcpy(p+48,type,strlen(type));
    pt_write_be(p+84,4,count);pt_write_be(p+88,4,status);
}
static int optical_prefix(PtBlock *b) {
    if(b->bytes%2048)return EINVAL;
    /* Optical commands address 2048-byte sectors. Do not accept a raw device
     * whose alignment would make valid single-sector commands fail later. */
    if(b->alignment>2048)return ENOTSUP;
    uint8_t head[2048];
    ssize_t n=pread(b->fd,head,sizeof head,0);
    if(n!=sizeof head)return n<0?errno:EIO;
    if(!memcmp(head,"ER",2) || (memcmp(head+1024,"H+",2) && memcmp(head+1024,"HX",2) && memcmp(head+1024,"BD",2)))return 0;
    /* A hybrid ISO uses absolute ISO sector addresses; never shift it. */
    if(b->bytes>=APM_BYTES+sizeof head) {
        n=pread(b->fd,head,sizeof head,APM_BYTES);
        if(n!=sizeof head)return n<0?errno:EIO;
        if(!memcmp(head+1,"CD001",5))return 0;
    }
    uint64_t blocks=b->bytes/512;
    if(blocks>UINT32_MAX-APM_BLOCKS)return EFBIG;
    b->prefix=calloc(1,APM_BYTES);if(!b->prefix)return ENOMEM;
    memcpy(b->prefix,"ER",2);pt_write_be(b->prefix+2,2,512);pt_write_be(b->prefix+4,4,blocks+APM_BLOCKS);
    apm_entry(b->prefix+512,1,APM_BLOCKS-1,"Apple","Apple_partition_map",3);
    apm_entry(b->prefix+1024,APM_BLOCKS,blocks,"Mac_OS_X","Apple_HFS",0x7f);
    b->bytes+=APM_BYTES;return 0;
}
/* Unencrypted sparse bundle, backing-store version 1, as `hdiutil create -type SPARSEBUNDLE` writes it. */
static int plist_integer(const char *text,const char *key,uint64_t *value) {
    const char *k=strstr(text,key);
    if(!k) return 0;
    k+=strlen(key); k+=strspn(k," \t\r\n");
    if(strncmp(k,"<integer>",9)) return 0;
    char *end; *value=strtoull(k+9,&end,10);
    return end>k+9 && !strncmp(end,"</integer>",10);
}
static int open_bundle(PtBlock *b,int dir) {
    char text[4096]; ssize_t n=-1; struct stat st;
    int f=openat(dir,"Info.plist",O_RDONLY|O_CLOEXEC);
    if(f>=0) { n=read(f,text,sizeof text-1); close(f); }
    if(n<=0) return EINVAL;
    text[n]=0; uint64_t version=0;
    if(!strstr(text,"<string>com.apple.diskimage.sparsebundle</string>") ||
       !plist_integer(text,"<key>bundle-backingstore-version</key>",&version) || version!=1 ||
       !plist_integer(text,"<key>band-size</key>",&b->band) || !plist_integer(text,"<key>size</key>",&b->bytes)) return EINVAL;
    if(!fstatat(dir,"token",&st,0) && st.st_size) return ENOTSUP;               /* encrypted */
    if(b->band<4096 || b->band>(1u<<30) || b->band%4096 || !b->bytes || b->bytes%512 || b->bytes/b->band>=(1u<<26)) return EINVAL;
    if((b->bands=openat(dir,"bands",O_RDONLY|O_DIRECTORY|O_CLOEXEC))<0) return errno;
    b->missing=calloc((size_t)((b->bytes+b->band-1)/b->band),1);
    return b->missing?0:ENOMEM;
}
static int map_overlay(PtBlock *b,const char *path) {
    const char *limit=getenv("PT_OVERLAY_MAX_MB");
    b->page=(uint64_t)getpagesize();
    b->budget=(limit?strtoull(limit,NULL,10):1024)*1048576/b->page;
    uint64_t pages=(b->bytes+b->page-1)/b->page;
    if(!(b->source=strdup(path)) || !(b->dirty=calloc((pages+63)/64,sizeof *b->dirty))) return ENOMEM;
    void *m=b->band ? mmap(NULL,b->bytes,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANON|MAP_NORESERVE,-1,0)  /* written pages only */
                    : mmap(NULL,b->bytes,PROT_READ|PROT_WRITE,MAP_PRIVATE,b->fd,0);
    if(m==MAP_FAILED) return errno;
    b->map=m; return 0;
}
static PtBlock *open_block(const char *path,int writable,int optical,PtEvent *completion) {
    int overlay=writable==PT_BLOCK_OVERLAY;
    struct stat st; uint8_t magic[4096];uint64_t bytes=0;unsigned alignment=512;
    int error=0, bundle=!stat(path,&st) && S_ISDIR(st.st_mode) && !optical;
    int fd=open(path,(bundle?O_DIRECTORY:writable && !overlay?O_RDWR:0)|O_RDONLY|O_CLOEXEC);
    if(fd<0) return NULL;
    if(fstat(fd,&st))error=errno;
    else if(bundle) bytes=1;                                    /* sized by open_bundle */
    else if(S_ISREG(st.st_mode)) {
        if(st.st_size<512 || (st.st_size&511))error=EINVAL;
        else {
            bytes=st.st_size;
            if(pread(fd,magic,512,0)!=512)error=EIO;
            else if(!memcmp(magic,"QFI\xfb",4))error=EINVAL;
            else if(pread(fd,magic,512,st.st_size-512)!=512)error=EIO;
            else if(!memcmp(magic,"koly",4))error=EINVAL; /* attach UDIF first */
        }
    }
#ifdef __APPLE__
    else if(S_ISCHR(st.st_mode) && !writable) {
        uint32_t block_size;uint64_t count;
        if(ioctl(fd,DKIOCGETBLOCKSIZE,&block_size) || ioctl(fd,DKIOCGETBLOCKCOUNT,&count))error=errno;
        else if(block_size<512 || block_size>4096 || (block_size&(block_size-1)) || !count || count>INT64_MAX/block_size)error=EINVAL;
        else {alignment=block_size;bytes=count*block_size;}
    }
#endif
    else error=EINVAL;
    if(error) { close(fd); errno=error; return NULL; }
    PtBlock *b=calloc(1,sizeof *b);
    if(!b) { int saved=errno; close(fd); errno=saved; return NULL; }
    b->fd=fd; b->writable=!!writable; b->bytes=bytes;b->alignment=alignment; b->completion=completion;
    for(unsigned k=0;k<BUNDLE_FDS;k++) b->open[k].fd=-1;
    if(bundle && (error=open_bundle(b,fd)))goto fail;
    if(optical && (error=optical_prefix(b)))goto fail;
    if(overlay && (error=map_overlay(b,path)))goto fail;
    if((error=pthread_mutex_init(&b->mutex,NULL))) goto fail;
    if((error=pthread_cond_init(&b->work,NULL))) { pthread_mutex_destroy(&b->mutex); goto fail; }
    if((error=pthread_create(&b->worker,NULL,worker,b))) {
        pthread_cond_destroy(&b->work); pthread_mutex_destroy(&b->mutex); goto fail;
    }
    return b;
fail:
    if(b->map) munmap(b->map,b->bytes);
    if(b->band && b->bands>0) close(b->bands);
    close(fd); free(b->prefix); free(b->source); free(b->dirty); free(b->missing); free(b); errno=error; return NULL;
}
PtBlock *pt_block_open(const char *path,int writable,PtEvent *completion) {return open_block(path,writable,0,completion);}
PtBlock *pt_block_open_optical(const char *path,PtEvent *completion) {return open_block(path,0,1,completion);}
uint64_t pt_block_size(const PtBlock *b) { return b?b->bytes:0; }
void pt_block_set_kick(PtBlock *b,_Atomic uint32_t *cell) { b->cpu_kick=cell; }
int pt_block_set_save(PtBlock *b,const char *path) {
    if(!b || !b->map || b->save || !path) return EINVAL;
    if(b->band) return ENOTSUP;                                 /* saving is for raw overlays (results volumes) */
    return (b->save=strdup(path)) ? 0 : ENOMEM;
}
/* Unwritten blocks stay shared with the source. On failure nothing is published;
 * the temporary file is left in place for inspection. */
static int save_overlay(PtBlock *b,uint64_t *saved) {
#ifdef __APPLE__
    char tmp[PATH_MAX];
    if(snprintf(tmp,sizeof tmp,"%s.%d.tmp",b->save,(int)getpid())>=(int)sizeof tmp) return ENAMETOOLONG;
    if(clonefile(b->source,tmp,0)) return errno;
    int e=0,fd=chmod(tmp,0600)?-1:open(tmp,O_WRONLY|O_CLOEXEC);
    if(fd<0) e=errno;
    for(uint64_t p=0;!e && p*b->page<b->bytes;p++) {
        if(!(b->dirty[p/64]>>(p%64)&1)) continue;
        uint64_t off=p*b->page, n=b->bytes-off<b->page?b->bytes-off:b->page;
        ssize_t w=pwrite(fd,b->map+off,n,(off_t)off);
        if(w!=(ssize_t)n) e=w<0?errno:EIO; else *saved+=n;
    }
    if(fd>=0 && close(fd) && !e) e=errno;
    if(!e && renamex_np(tmp,b->save,RENAME_EXCL)) e=errno;
    return e;
#else
    (void)b;(void)saved; return ENOTSUP;
#endif
}
int pt_block_submit(PtBlock *b,const PtBlockRequest *r) {
    if(!b || !r || r->operation<PT_BLOCK_READ || r->operation>PT_BLOCK_FLUSH) return EINVAL;
    if(r->operation!=PT_BLOCK_READ && !b->writable) return EROFS;
    if(r->operation!=PT_BLOCK_FLUSH &&
       (!r->buffer || !r->size || r->size>1024*1024 || (r->offset&(b->alignment-1)) || (r->size&(b->alignment-1)) ||
        r->offset>b->bytes || r->size>b->bytes-r->offset)) return EINVAL;
    int e=pthread_mutex_trylock(&b->mutex);
    if(e) return e==EBUSY?EAGAIN:e;
    if(b->closing) e=ESHUTDOWN;
    else if(b->submitted-b->consumed==CAPACITY) e=EAGAIN;
    else {
        b->requests[b->submitted++%CAPACITY]=*r;
        pthread_cond_signal(&b->work);
    }
    pthread_mutex_unlock(&b->mutex); return e;
}
int pt_block_take(PtBlock *b,PtBlockRequest *r) {
    if(!b || !r) return -EINVAL;
    int error=pthread_mutex_trylock(&b->mutex);
    if(error) return error==EBUSY?-EAGAIN:-error;
    int available=b->consumed<b->completed;
    if(available) *r=b->requests[b->consumed++%CAPACITY];
    pthread_mutex_unlock(&b->mutex); return available;
}
int pt_block_close(PtBlock *b) {
    if(!b) return 0;
    pthread_mutex_lock(&b->mutex); b->closing=1;
    pthread_cond_signal(&b->work); pthread_mutex_unlock(&b->mutex);
    pthread_join(b->worker,NULL);
    int e=0;
    if(b->map) {
        uint64_t saved=0; const char *name=strrchr(b->source,'/'); name=name?name+1:b->source;
        if(b->save) e=save_overlay(b,&saved);
        fprintf(stderr,"PT_DISK %s written_mb=%.1f overflow=%d",name,(double)(b->pages*b->page)/1048576,b->overflow);
        if(!b->save) fprintf(stderr,"\n");
        else if(e) fprintf(stderr," save failed: %s\n",strerror(e));
        else fprintf(stderr," saved_mb=%.1f\n",(double)saved/1048576);
        if(!e && b->overflow) e=ENOSPC;
        atomic_fetch_sub(&overlay_pages,b->pages);
        munmap(b->map,b->bytes);
    }
    for(unsigned k=0;k<BUNDLE_FDS;k++) if(b->open[k].fd>0) close(b->open[k].fd);   /* bands were synced by FLUSH */
    if(b->band) close(b->bands);
    close(b->fd); pthread_cond_destroy(&b->work); pthread_mutex_destroy(&b->mutex);
    free(b->prefix); free(b->source); free(b->save); free(b->dirty); free(b->missing); free(b);
    return e;
}
