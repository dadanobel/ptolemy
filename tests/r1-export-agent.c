/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "pt_agent.h"
#include "pt_sha256.h"
#include <assert.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <pthread.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
static char directory[100],path[108];
static uint32_t u32(const uint8_t *p){return (uint32_t)p[0]<<24|(uint32_t)p[1]<<16|(uint32_t)p[2]<<8|p[3];}
static void put(uint8_t *p,uint32_t v){p[0]=v>>24;p[1]=v>>16;p[2]=v>>8;p[3]=v;}
typedef struct {int status,hangup;} Client;
static void *client(void *v){Client *c=v;int fd=socket(AF_UNIX,SOCK_STREAM,0);struct sockaddr_un sa={.sun_family=AF_UNIX};strcpy(sa.sun_path,path);assert(fd>=0&&!connect(fd,(struct sockaddr *)&sa,sizeof sa));
    uint8_t b[512];memcpy(b,"PTA1",4);put(b+4,4);unsigned n=(unsigned)strlen(directory),k=8+n+15;put(b+8,k);put(b+12,30);put(b+16,n);memcpy(b+20,directory,n);put(b+20+n,15);memcpy(b+24+n,"/Users/u/export",15);assert(send(fd,b,k+16,0)==k+16);
    if(c->hangup){usleep(200000);close(fd);return NULL;}
    assert(recv(fd,b,16,MSG_WAITALL)==16);unsigned len=u32(b+12);assert(len<sizeof b);c->status=u32(b+8);assert(recv(fd,b,len,MSG_WAITALL)==len);close(fd);return NULL;
}
static unsigned pull(PtAgent *a,uint32_t t){uint8_t b[1024];uint32_t id,n,ms;for(unsigned i=0;i<2000;i++){int rc=pt_agent_pull(a,t,b,sizeof b,&id,&n,&ms);if(!rc){assert(n>=8&&memmem(b,n,"/Ptolemy/files",14));return id;}assert(rc==PTA_AGAIN);usleep(1000);}abort();}
int main(void){strcpy(directory,"/private/tmp/pt-r1-agent.XXXXXX");assert(mkdtemp(directory));snprintf(path,sizeof path,"%s/control",directory);PtAgent *a=pt_agent_open(path);assert(a);uint32_t token;assert(!pt_agent_register_caps(a,2,PTA_CAP_EXPORT|PTA_CAP_POINTER,&token));assert(!pt_agent_capabilities(a,NULL));Client c={0};pthread_t thread;pthread_create(&thread,NULL,client,&c);pthread_join(thread,NULL);assert(c.status);assert(!pt_agent_unregister(a,token));assert(!pt_agent_register_caps(a,PTA_VERSION,PTA_CAP_EXPORT|PTA_CAP_POINTER,&token));
    uint32_t session=0;assert(pt_agent_capabilities(a,&session)==(PTA_CAP_EXPORT|PTA_CAP_POINTER)&&session==token);
    for(unsigned cancel=0;cancel<2;cancel++){
        c=(Client){0,(int)cancel};pthread_create(&thread,NULL,client,&c);unsigned id=pull(a,token);assert(pt_agent_export_begin(a,token,id+1,5)==PTA_DENIED);assert(!pt_agent_export_begin(a,token,id,5));assert(!pt_agent_export_chunk(a,token,id,0,"hello",5));
        if(cancel){pthread_join(thread,NULL);usleep(100000);assert(pt_agent_export_chunk(a,token,id,0,"hello",5)==PTA_CANCELLED);assert(!pt_agent_finish(a,token,id,125));continue;}
        PtSha256 h;uint8_t sha[32];pt_sha256_init(&h);pt_sha256_update(&h,"hello",5);pt_sha256_final(&h,sha);int rc=PTA_AGAIN;for(unsigned i=0;i<2000&&rc==PTA_AGAIN;i++){rc=pt_agent_export_commit(a,token,id,sha);usleep(1000);}assert(!rc);assert(!pt_agent_finish(a,token,id,0));pthread_join(thread,NULL);assert(!c.status);char f[128];snprintf(f,sizeof f,"%s/export",directory);FILE *in=fopen(f,"rb");assert(in&&fread(sha,1,5,in)==5&&!memcmp(sha,"hello",5));fclose(in);unlink(f);
    }
    usleep(50000);pt_agent_close(a);DIR *d=opendir(directory);struct dirent *e;while((e=readdir(d)))assert(!strcmp(e->d_name,".")||!strcmp(e->d_name,".."));closedir(d);assert(!rmdir(directory));
    puts("PASS: real export socket -> bounded CPU API -> worker disk/hash/publication; capability refusal, wrong job and disconnect cleanup");
}
