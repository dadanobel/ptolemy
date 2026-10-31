/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "pt_export.h"
#include "pt_sha256.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>
static void hash(const void *p,unsigned n,uint8_t out[32]){PtSha256 s;pt_sha256_init(&s);pt_sha256_update(&s,p,n);pt_sha256_final(&s,out);}
int main(void){
    char directory[]="/private/tmp/pt-export-test.XXXXXX",path[1024];assert(mkdtemp(directory));uint8_t sha[32],data[PTA_CHUNK];for(unsigned i=0;i<sizeof data;i++)data[i]=(uint8_t)(i*7);
    for(unsigned n=0;n<=1;n++){char name[32];snprintf(name,sizeof name,"tiny-%u",n);PtExport *e=pt_export_open(directory,name);assert(e&&!pt_export_begin(e,n));if(n)assert(!pt_export_chunk(e,0,data,n));hash(data,n,sha);assert(pt_export_commit(e,sha)==PTA_AGAIN);pt_export_service(e);assert(!pt_export_commit(e,sha)&&pt_export_complete(e));pt_export_close(e);snprintf(path,sizeof path,"%s/%s",directory,name);unlink(path);}
    PtExport *e=pt_export_open(directory,"bulk");assert(e&&!pt_export_begin(e,5ull*sizeof data));
    for(unsigned k=0;k<4;k++)assert(!pt_export_chunk(e,(uint64_t)k*sizeof data,data,sizeof data));
    assert(!pt_export_chunk(e,3ull*sizeof data,data,sizeof data));assert(pt_export_chunk(e,4ull*sizeof data,data,sizeof data)==PTA_AGAIN);
    assert(pt_export_chunk(e,2ull*sizeof data,data,sizeof data)==PTA_INVALID);pt_export_service(e);assert(!pt_export_chunk(e,4ull*sizeof data,data,sizeof data));
    PtSha256 sum;pt_sha256_init(&sum);for(unsigned i=0;i<5;i++)pt_sha256_update(&sum,data,sizeof data);pt_sha256_final(&sum,sha);
    assert(pt_export_commit(e,sha)==PTA_AGAIN);pt_export_service(e);assert(!pt_export_commit(e,sha));pt_export_close(e);snprintf(path,sizeof path,"%s/bulk",directory);int fd=open(path,O_RDONLY);assert(fd>=0);unsigned total=0;ssize_t got;while((got=read(fd,data,sizeof data))>0)total+=(unsigned)got;close(fd);assert(total==5*sizeof data);
    /* A second complete export cannot replace the existing file. */
    e=pt_export_open(directory,"bulk");assert(e&&!pt_export_begin(e,0));hash(NULL,0,sha);assert(pt_export_commit(e,sha)==PTA_AGAIN);pt_export_service(e);assert(pt_export_commit(e,sha)==PTA_CANCELLED);pt_export_close(e);unlink(path);
    e=pt_export_open(directory,"cancel");assert(e&&!pt_export_begin(e,1));assert(!pt_export_chunk(e,0,data,1));pt_export_cancel(e);pt_export_service(e);pt_export_close(e);snprintf(path,sizeof path,"%s/cancel",directory);assert(access(path,F_OK));
    e=pt_export_open(directory,"wrong-hash");assert(e&&!pt_export_begin(e,1));assert(!pt_export_chunk(e,0,data,1));memset(sha,0,32);pt_export_commit(e,sha);pt_export_service(e);assert(!pt_export_complete(e));pt_export_close(e);
    e=pt_export_open(directory,"large");assert(e&&!pt_export_begin(e,0x100000001ull));assert(pt_export_chunk(e,0x100000000ull,data,1)==PTA_INVALID);pt_export_close(e);
    assert(!pt_export_open(directory,"../escape"));DIR *d=opendir(directory);struct dirent *entry;while((entry=readdir(d)))assert(!strcmp(entry->d_name,".")||!strcmp(entry->d_name,".."));closedir(d);rmdir(directory);
    puts("PASS: reverse bulk 0/1 B, bounded queue, last-chunk retry, order, SHA-256, no-replace publication, cancel cleanup and 64-bit offsets");
}
