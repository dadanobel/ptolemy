/* SPDX-License-Identifier: GPL-2.0-or-later
 * Only touches a newly created 16 KiB fixture. No VM or user image is opened. */
#include "block.h"
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <sched.h>
#include <sys/stat.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void submit(PtBlock *b,PtBlockRequest r) {
    int e;
    /* In the device owner, use a scheduled retry, not this test-only loop. */
    do { e=pt_block_submit(b,&r); if(e==EAGAIN) sched_yield(); } while(e==EAGAIN);
    assert(!e);
}
static PtBlockRequest take(PtBlock *b,PtEvent *event) {
    PtBlockRequest r;
    for(;;) {
        uint64_t g=pt_event_generation(event);
        int n=pt_block_take(b,&r);
        if(n==1) return r;
        if(n==-EAGAIN) { sched_yield(); continue; }
        assert(n==0);
        /* A finite test timeout turns a lost wakeup into an explicit failure. */
        assert(pt_event_wait(event,g,5000000000ull)==1);
    }
}
/* Sparse bundle: a hand-made bundle with 4 KiB bands (128 of them, more than the open-band cache). */
static void put(const char *dir,const char *name,const char *text) {
    char p[512]; snprintf(p,sizeof p,"%s/%s",dir,name);
    int fd=open(p,O_WRONLY|O_CREAT|O_TRUNC,0644); assert(fd>=0);
    assert(write(fd,text,strlen(text))==(ssize_t)strlen(text) && !close(fd));
}
static int band_exists(const char *dir,const char *name) {
    char p[512]; snprintf(p,sizeof p,"%s/bands/%s",dir,name); return !access(p,F_OK);
}
static void plist(const char *dir,unsigned version) {
    char t[1024]; snprintf(t,sizeof t,"<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<plist version=\"1.0\">\n<dict>\n"
        "\t<key>CFBundleInfoDictionaryVersion</key>\n\t<string>6.0</string>\n\t<key>band-size</key>\n\t<integer>4096</integer>\n"
        "\t<key>bundle-backingstore-version</key>\n\t<integer>%u</integer>\n\t<key>diskimage-bundle-type</key>\n"
        "\t<string>com.apple.diskimage.sparsebundle</string>\n\t<key>size</key>\n\t<integer>524288</integer>\n</dict>\n</plist>\n",version);
    put(dir,"Info.plist",t);
}
static void io(PtBlock *b,PtEvent *e,int op,uint64_t off,void *buf,size_t n,int error) {
    submit(b,(PtBlockRequest){.id=off,.offset=off,.buffer=buf,.size=n,.operation=op});
    PtBlockRequest r=take(b,e); assert(r.id==off && r.error==error);
}
static void bundle_tests(PtEvent *e) {
    char dir[]="/tmp/ptolemy-bundle-XXXXXX"; assert(mkdtemp(dir));
    char bands[64]; snprintf(bands,sizeof bands,"%s/bands",dir); assert(!mkdir(bands,0755));
    plist(dir,1); put(dir,"token","");
    uint8_t w[1024],r[1024],z[1024]={0};
    for(unsigned i=0;i<sizeof w;i++) w[i]=(uint8_t)(i*31+7);
    PtBlock *b=pt_block_open(dir,1,e); assert(b && pt_block_size(b)==524288);
    io(b,e,PT_BLOCK_READ,0,r,1024,0); assert(!memcmp(r,z,1024) && !band_exists(dir,"0"));   /* missing band: zeros, no file */
    io(b,e,PT_BLOCK_WRITE,4096-512,w,1024,0);                                                  /* crosses bands 0 and 1 */
    assert(band_exists(dir,"0") && band_exists(dir,"1") && !band_exists(dir,"2"));
    for(unsigned i=0;i<128;i++) io(b,e,PT_BLOCK_WRITE,i*4096ull+2048,w,512,0);                  /* > open-band cache */
    assert(band_exists(dir,"a") && band_exists(dir,"7f"));                                     /* names are lowercase hex */
    submit(b,(PtBlockRequest){.id=9,.operation=PT_BLOCK_FLUSH}); { PtBlockRequest q=take(b,e); assert(q.id==9 && !q.error); }
    assert(!pt_block_close(b));
    b=pt_block_open(dir,0,e); assert(b);
    io(b,e,PT_BLOCK_READ,4096-512,r,1024,0); assert(!memcmp(r,w,1024));
    for(unsigned i=2;i<128;i++) { io(b,e,PT_BLOCK_READ,i*4096ull+2048,r,512,0); assert(!memcmp(r,w,512)); }
    PtBlockRequest ro={.operation=PT_BLOCK_WRITE,.buffer=w,.size=512}; assert(pt_block_submit(b,&ro)==EROFS);
    assert(!pt_block_close(b));
    /* Overlay: a partly written fresh page keeps the bundle's other bytes; the bundle is untouched. */
    b=pt_block_open(dir,PT_BLOCK_OVERLAY,e); assert(b && pt_block_set_save(b,"/nonexistent")==ENOTSUP);
    memset(r,0xa5,512); io(b,e,PT_BLOCK_WRITE,4096,r,512,0);
    uint8_t got[2048]; io(b,e,PT_BLOCK_READ,3072,got,2048,0);
    assert(!memcmp(got,z,512) && !memcmp(got+512,w,512) && !memcmp(got+1024,r,512) && !memcmp(got+1536,z,512));
    io(b,e,PT_BLOCK_READ,6144,got,512,0); assert(!memcmp(got,w,512));                       /* same page, not written */
    assert(!pt_block_close(b));
    b=pt_block_open(dir,0,e); assert(b); io(b,e,PT_BLOCK_READ,4096-512,r,1024,0); assert(!memcmp(r,w,1024)); assert(!pt_block_close(b));
    /* Refused: other backing-store versions and encrypted bundles. */
    plist(dir,2); assert(!pt_block_open(dir,1,e) && errno==EINVAL);
    plist(dir,1); put(dir,"token","x"); assert(!pt_block_open(dir,1,e) && errno==ENOTSUP);
    char cmd[128]; snprintf(cmd,sizeof cmd,"rm -rf %s",dir); assert(!system(cmd));
    puts("bundle: missing bands read as zeros, band-crossing writes, 128 bands through the cache, FLUSH, read-only, overlay, refusals");
}
int main(void) {
    char path[]="/tmp/ptolemy-block-XXXXXX";
    int fd=mkstemp(path); assert(fd>=0);
    assert(!ftruncate(fd,16384)); assert(!close(fd));
    PtEvent event; pt_event_init(&event);
    bundle_tests(&event);
    PtBlock *b=pt_block_open(path,1,&event); assert(b);
    uint8_t written[512],readback[512];
    for(unsigned i=0;i<sizeof written;i++) written[i]=(uint8_t)(i*17);
    submit(b,(PtBlockRequest){.id=1,.generation=7,.offset=512,.buffer=written,.size=512,.operation=PT_BLOCK_WRITE});
    submit(b,(PtBlockRequest){.id=2,.generation=7,.operation=PT_BLOCK_FLUSH});
    submit(b,(PtBlockRequest){.id=3,.generation=8,.offset=512,.buffer=readback,.size=512,.operation=PT_BLOCK_READ});
    for(unsigned i=1;i<=3;i++) {
        PtBlockRequest r=take(b,&event);
        assert(r.id==i && !r.error && r.generation==(i==3?8:7));
    }
    assert(!memcmp(written,readback,512));
    PtBlockRequest bad={.operation=PT_BLOCK_READ,.offset=16384,.buffer=readback,.size=512};
    assert(pt_block_submit(b,&bad)==EINVAL);
    bad.offset=UINT64_MAX-511; assert(pt_block_submit(b,&bad)==EINVAL);
    pt_block_close(b);

    b=pt_block_open(path,0,&event); assert(b);
    bad.offset=0; bad.operation=PT_BLOCK_WRITE; assert(pt_block_submit(b,&bad)==EROFS);
    uint8_t buffers[32][512];
    for(unsigned i=0;i<32;i++)
        submit(b,(PtBlockRequest){.id=i,.buffer=buffers[i],.size=512,.operation=PT_BLOCK_READ});
    bad.operation=PT_BLOCK_READ; assert(pt_block_submit(b,&bad)==EAGAIN);
    for(unsigned i=0;i<32;i++) { PtBlockRequest r=take(b,&event); assert(r.id==i && !r.error); }
    pt_block_close(b);

    /* Overlay: writes stay in memory, the source is untouched, the save is source + written pages. */
    uint8_t source[16384],now[16384],copy[16384],pattern[1024],back[1024];
    fd=open(path,O_RDONLY); assert(fd>=0 && read(fd,source,sizeof source)==(ssize_t)sizeof source && !close(fd));
    char out[sizeof path+16]; snprintf(out,sizeof out,"%s-overlay",path);
    b=pt_block_open(path,PT_BLOCK_OVERLAY,&event); assert(b && !pt_block_set_save(b,out));
    memset(pattern,0x5a,sizeof pattern);
    submit(b,(PtBlockRequest){.id=40,.offset=4096,.buffer=pattern,.size=sizeof pattern,.operation=PT_BLOCK_WRITE});
    submit(b,(PtBlockRequest){.id=41,.operation=PT_BLOCK_FLUSH});
    submit(b,(PtBlockRequest){.id=42,.offset=4096,.buffer=back,.size=sizeof back,.operation=PT_BLOCK_READ});
    for(unsigned i=40;i<=42;i++) { PtBlockRequest r=take(b,&event); assert(r.id==i && !r.error); }
    assert(!memcmp(back,pattern,sizeof back));
    assert(!pt_block_close(b));
    fd=open(path,O_RDONLY); assert(fd>=0 && read(fd,now,sizeof now)==(ssize_t)sizeof now && !close(fd));
    assert(!memcmp(now,source,sizeof now));
    fd=open(out,O_RDONLY); assert(fd>=0 && read(fd,copy,sizeof copy)==(ssize_t)sizeof copy && !close(fd));
    memcpy(source+4096,pattern,sizeof pattern); assert(!memcmp(copy,source,sizeof copy));
    /* Publishing never replaces an existing result. */
    b=pt_block_open(path,PT_BLOCK_OVERLAY,&event); assert(b && !pt_block_set_save(b,out));
    assert(pt_block_close(b)==EEXIST);
    /* Page budget: a refused write fails whole and changes nothing. */
    assert(!setenv("PT_OVERLAY_MAX_MB","0",1));
    b=pt_block_open(path,PT_BLOCK_OVERLAY,&event); assert(b);
    submit(b,(PtBlockRequest){.id=50,.buffer=pattern,.size=512,.operation=PT_BLOCK_WRITE});
    { PtBlockRequest r=take(b,&event); assert(r.id==50 && r.error==EIO); }
    submit(b,(PtBlockRequest){.id=51,.buffer=back,.size=512,.operation=PT_BLOCK_READ});
    { PtBlockRequest r=take(b,&event); assert(r.id==51 && !r.error && !memcmp(back,now,512)); }
    assert(pt_block_close(b)==ENOSPC);
    assert(!unsetenv("PT_OVERLAY_MAX_MB"));

    fd=open(path,O_WRONLY); assert(fd>=0);
    assert(write(fd,"QFI\xfb",4)==4); assert(!close(fd));
    assert(!pt_block_open(path,0,&event) && errno==EINVAL);
    pt_event_destroy(&event);
    /* The refused (EEXIST) save leaves its temporary for inspection by design; remove it with the fixtures. */
    char tmp[sizeof out+16]; snprintf(tmp,sizeof tmp,"%s.%d.tmp",out,(int)getpid());
    assert(!unlink(path) && !unlink(out) && !unlink(tmp));
    puts("PASS: disk FIFO, publication, FLUSH, bounds, capacity, read-only, overlay (untouched source, exclusive save, page budget), qcow2 rejection, sparse bundle");
}
