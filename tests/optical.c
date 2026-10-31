/* SPDX-License-Identifier: GPL-2.0-or-later
 * Only newly created fixtures. No installation image is modified. */
#include "block.h"
#include "scsi.h"
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static void read_block(PtBlock *b,uint64_t offset,void *buffer,size_t size) {
    PtBlockRequest r={.offset=offset,.buffer=buffer,.size=size,.operation=PT_BLOCK_READ};
    int e;do{e=pt_block_submit(b,&r);if(e==EAGAIN)sched_yield();}while(e==EAGAIN);
    assert(!e);
    for(unsigned i=0;i<50000;i++) {
        int n=pt_block_take(b,&r);assert(n>=0 || n==-EAGAIN);
        if(n==1){assert(!r.error && r.offset==offset && r.size==size);return;}
        usleep(100);
    }
    assert(!"optical read timed out");
}
static void dvd(PtScsi *s,unsigned format,unsigned limit,uint8_t *out,unsigned expected) {
    uint8_t c[12]={0xad};c[7]=format;pt_write_be(c+8,2,limit);
    assert(!pt_scsi_command(s,c,sizeof c) && !s->status);
    assert(pt_scsi_read(s,out,4096)==(int)expected && s->phase==PT_SCSI_DONE);
}
static void illegal(PtScsi *s,unsigned media,unsigned layer,unsigned format,unsigned key,unsigned asc) {
    uint8_t c[12]={0xad};c[1]=media;c[6]=layer;c[7]=format;c[9]=8;
    assert(!pt_scsi_command(s,c,sizeof c) && s->status==2 && s->key==key && s->asc==asc);
}
static void compare_image(const char *source,const char *reference) {
    PtBlock *b=pt_block_open_optical(source,NULL);assert(b);
    int fd=open(reference,O_RDONLY);assert(fd>=0);struct stat st;assert(!fstat(fd,&st));
    uint64_t bytes=pt_block_size(b);assert(bytes==(uint64_t)st.st_size && bytes>=65536);
    uint8_t got[32768],expected[32768];
    const uint64_t offsets[]={0,32768-2048,32768,(bytes/2)&~2047ull,bytes-4096};
    for(unsigned i=0;i<sizeof offsets/sizeof offsets[0];i++) {
        size_t n=i?4096:sizeof got;read_block(b,offsets[i],got,n);
        assert(pread(fd,expected,n,offsets[i])==(ssize_t)n && !memcmp(got,expected,n));
    }
    pt_block_close(b);assert(!close(fd));
    puts("PASS: virtual optical reads match the separately wrapped reference at APM, boundary, middle and EOF");
}
int main(int argc,char **argv) {
    if(argc!=1 && argc!=3){fprintf(stderr,"Usage: %s [SOURCE_IMAGE WRAPPED_REFERENCE]\n",argv[0]);return 2;}
    char path[]="/tmp/pt-optical-XXXXXX";int fd=mkstemp(path);assert(fd>=0);
    enum { SIZE=65536, PREFIX=32768 };
    uint8_t source[SIZE],got[4096],head[PREFIX];
    for(unsigned i=0;i<SIZE;i++)source[i]=(i*13)^(i/512);
    memcpy(source+1024,"H+",2);assert(write(fd,source,SIZE)==SIZE);
    PtBlock *b=pt_block_open_optical(path,NULL);assert(b && pt_block_size(b)==SIZE+PREFIX);
    read_block(b,0,head,sizeof head);
    assert(!memcmp(head,"ER\x02\0\0\0\0\xc0",8)); /* 512-byte blocks, 192 total. */
    assert(!memcmp(head+512,"PM",2) && pt_read_be(head+516,4)==2);
    assert(pt_read_be(head+520,4)==1 && pt_read_be(head+524,4)==63);
    assert(!strcmp((char *)head+528,"Apple") && !strcmp((char *)head+560,"Apple_partition_map"));
    assert(!memcmp(head+1024,"PM",2) && pt_read_be(head+1032,4)==64 && pt_read_be(head+1036,4)==128);
    assert(!strcmp((char *)head+1040,"Mac_OS_X") && !strcmp((char *)head+1072,"Apple_HFS"));
    assert(pt_read_be(head+1104,4)==0 && pt_read_be(head+1108,4)==128 && pt_read_be(head+1112,4)==0x7f);
    for(unsigned i=1536;i<PREFIX;i++)assert(!head[i]);
    read_block(b,PREFIX-2048,got,4096);assert(!memcmp(got,head+PREFIX-2048,2048) && !memcmp(got+2048,source,2048));
    read_block(b,PREFIX,got,4096);assert(!memcmp(got,source,4096));
    read_block(b,SIZE+PREFIX-4096,got,4096);assert(!memcmp(got,source+SIZE-4096,4096));
    PtBlockRequest r={.buffer=got,.size=2048,.operation=PT_BLOCK_WRITE};assert(pt_block_submit(b,&r)==EROFS);
    r.operation=PT_BLOCK_FLUSH;assert(pt_block_submit(b,&r)==EROFS);
    r.operation=PT_BLOCK_READ;r.offset=SIZE+PREFIX;assert(pt_block_submit(b,&r)==EINVAL);
    r.offset=UINT64_MAX-2047;assert(pt_block_submit(b,&r)==EINVAL);
    PtScsi *s=calloc(1,sizeof *s);assert(s);pt_scsi_init(s,1);PtBlock *old;
    assert(!pt_scsi_media(s,b,1,&old) && !old);s->unit_attention=0;
    uint8_t small[8];dvd(s,1,8,small,8); /* small images are DVD-ROM media too */
    pt_block_close(b);
    uint8_t unchanged[SIZE];assert(pread(fd,unchanged,SIZE,0)==SIZE && !memcmp(unchanged,source,SIZE));
    b=pt_block_open(path,0,NULL);assert(b && pt_block_size(b)==SIZE);pt_block_close(b);
    const char *signatures[]={"HX","BD"};
    for(unsigned i=0;i<2;i++) {
        assert(pwrite(fd,signatures[i],2,1024)==2);
        b=pt_block_open_optical(path,NULL);assert(b && pt_block_size(b)==SIZE+PREFIX);pt_block_close(b);
    }
    assert(pwrite(fd,"ER",2,0)==2);
    b=pt_block_open_optical(path,NULL);assert(b && pt_block_size(b)==SIZE);pt_block_close(b);
    assert(pwrite(fd,source,2048,0)==2048 && pwrite(fd,"\x01" "CD001\x01",7,PREFIX)==7);
    b=pt_block_open_optical(path,NULL);assert(b && pt_block_size(b)==SIZE);
    read_block(b,0,got,2048);assert(!memcmp(got,source,2048));pt_block_close(b);
    assert(pwrite(fd,source+PREFIX,2048,PREFIX)==2048);
    assert(!ftruncate(fd,SIZE+512));assert(!pt_block_open_optical(path,NULL) && errno==EINVAL);
    assert(!ftruncate(fd,1024*1024*1024));
    b=pt_block_open_optical(path,NULL);assert(b && pt_block_size(b)==1024ull*1024*1024+PREFIX);
    pt_scsi_init(s,1);assert(!pt_scsi_media(s,b,1,&old));s->unit_attention=0;
    dvd(s,0,2052,got,2052);
    assert(pt_read_be(got,2)==2050 && got[4]==1 && got[5]==15 && got[6]==1 && !got[7]);
    assert(!pt_read_be(got+8,4) && pt_read_be(got+12,4)==524303 && pt_read_be(got+16,4)==524303);
    for(unsigned i=20;i<2052;i++)assert(!got[i]);
    dvd(s,0,8,got,8);assert(pt_read_be(got,2)==2050);
    dvd(s,0,0,got,0);
    dvd(s,1,8,got,8);assert(pt_read_be(got,2)==6 && !got[4] && !got[5]);
    memset(s->data,0xa5,sizeof s->data);dvd(s,4,2052,got,2052);
    assert(pt_read_be(got,2)==2050);for(unsigned i=2;i<2052;i++)assert(!got[i]);
    illegal(s,0,0,3,5,0x24);illegal(s,0,1,0,5,0x24);illegal(s,1,0,0,5,0x24);
    s->present=0;illegal(s,0,0,0,2,0x3a);
    {uint8_t c[10]={0x46,2};c[8]=64;assert(!pt_scsi_command(s,c,10)&&!s->status&&pt_read_be(s->data+6,2)==0x10);   /* drive properties without a medium */
     /* the profile list of a Combo drive (Disc Recording sees only drives that list a writable profile) */
     assert(pt_read_be(s->data+8,2)==0&&s->data[11]==16&&pt_read_be(s->data+12,2)==0x10&&s->data[14]==1&&pt_read_be(s->data+16,2)==0x0a&&pt_read_be(s->data+20,2)==9&&pt_read_be(s->data+24,2)==8&&!s->data[26]);s->phase=PT_SCSI_DONE;
     uint8_t m[10]={0x5a,8,0x2a};m[8]=64;assert(!pt_scsi_command(s,m,10)&&!s->status&&s->data[8]==0x2a&&s->data[10]==0x3b&&s->data[11]==3);s->phase=PT_SCSI_DONE;}
    s->present=1;s->optical=0;illegal(s,0,0,0,5,0x20);
    pt_block_close(b);free(s);assert(!close(fd) && !unlink(path));
    puts("PASS: virtual APM, split reads, EOF, source preservation, ISO/APM pass-through, HFS signatures, DVD structures/sense");
    if(argc==3)compare_image(argv[1],argv[2]);
}
