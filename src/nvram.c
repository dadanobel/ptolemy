/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "nvram.h"
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>
/* The first 8 KiB is the firmware's interleaved 4 KiB OF view. The second
 * bank is the guest CHRP view; updates to one never reconstruct the other. */
static uint8_t nd_nv_checksum(const uint8_t *h){
    unsigned sum=h[0];for(unsigned k=2;k<16;k++)sum+=h[k];
    while(sum>255)sum=(sum&255)+(sum>>8);return (uint8_t)sum;
}
static void nd_nv_partition(uint8_t *p,unsigned n,unsigned sig,const char *name){
    memset(p,0,n);p[0]=(uint8_t)sig;pt_write_be(p+2,2,n/16);memcpy(p+4,name,strlen(name));p[1]=nd_nv_checksum(p);
}
static void nd_nv_of_defaults(PtNvram *n){
    uint8_t of[4096];nd_nv_partition(of,0xc10,0x70,"common");nd_nv_partition(of+0xc10,sizeof of-0xc10,0x7f,"wwwwwwwwwwww");
    for(unsigned k=0;k<sizeof of;k++)n->data[2*k]=of[k];
}
/* Start of the OF view's common data, 0 when the view is damaged; *end is its limit. */
static unsigned nd_nv_common(const uint8_t *of,unsigned *end){
    for(unsigned at=0;at+16<=4096;){unsigned size=(unsigned)pt_read_be(of+at+2,2)*16;
        if(size<16||size>4096-at||of[at+1]!=nd_nv_checksum(of+at))return 0;
        if(of[at]==0x70&&!memcmp(of+at+4,"common",7)){*end=at+size;return at+16;}at+=size;}
    return 0;
}
void pt_nvram_init(PtNvram *n){
    memset(n,0,sizeof *n);n->generation=1;nd_nv_of_defaults(n);
    uint8_t *bank=n->data+8192;nd_nv_partition(bank,0x800,0x70,"common");nd_nv_partition(bank+0x800,0x100,0xa0,"APL,MacOS75");
    uint8_t *free=bank+0x900;nd_nv_partition(free,8192-0x900,0x7f,"wwwwwwwwwwww");pt_write_be(free+20,4,2);
    unsigned a=0,b=0;for(unsigned k=20;k<8192-0x900;k++){a=(a+free[k])%65521;b=(b+a)%65521;}pt_write_be(free+16,4,(b<<16)|a);
}
int pt_nvram_access(PtNvram *n,unsigned at,uint8_t *bytes,unsigned count,int write){
    if(!bytes||at>sizeof n->data||count>sizeof n->data-at)return EINVAL;
    if(write){if(memcmp(n->data+at,bytes,count)){memcpy(n->data+at,bytes,count);n->generation++;}}
    else memcpy(bytes,n->data+at,count);return 0;
}
int pt_nvram_set_of(PtNvram *n,const char *name,const char *value){
    if(!name||!value||!*name||strchr(name,'=')||strchr(name,'\n'))return EINVAL;
    uint8_t of[4096],out[4096];for(unsigned k=0;k<4096;k++)of[k]=n->data[2*k];memcpy(out,of,sizeof out);
    unsigned at,end=0,base=nd_nv_common(of,&end);if(!base)return EINVAL;memset(out+base,0,end-base);unsigned used=base;size_t key=strlen(name),val=strlen(value);
    for(at=base;at<end&&of[at];){unsigned left=end-at;const uint8_t *zero=memchr(of+at,0,left);if(!zero)return EINVAL;unsigned len=(unsigned)(zero-(of+at));
        if(!(len>key&&of[at+key]=='='&&!memcmp(of+at,name,key))){if(len+1>end-used)return ENOSPC;memcpy(out+used,of+at,len+1);used+=len+1;}at+=len+1;}
    if(key>end-used||val>end-used-key||key+val+3>end-used)return ENOSPC;
    memcpy(out+used,name,key);out[used+key]='=';memcpy(out+used+key+1,value,val);
    int changed=memcmp(of,out,sizeof of)!=0;if(changed){for(unsigned k=0;k<4096;k++)n->data[2*k]=out[k];n->generation++;}return 0;
}
/* Tiger's flash driver can leave 0xff (the flash read-array command) in the first byte of the firmware's OF view, and
 * the firmware then refuses that NVRAM and stops: a damaged OF view gets default partitions again. The engine sets its
 * own OF variables after this. The guest's CHRP bank is left as it is. */
int pt_nvram_repair_of(PtNvram *n){
    uint8_t of[4096];unsigned end;for(unsigned k=0;k<4096;k++)of[k]=n->data[2*k];if(nd_nv_common(of,&end))return 0;
    nd_nv_of_defaults(n);n->generation++;return 1;
}
typedef struct {
    pthread_mutex_t lock;pthread_cond_t wake;pthread_t thread;
    int fd,busy,pending,stop,error;uint64_t saved,generation;
    char *path;uint8_t bytes[PT_NVRAM_SIZE];
} PtNativeNvramStore;
static int nd_nv_commit(PtNativeNvramStore *s){
    size_t length=strlen(s->path);char *tmp=malloc(length+16);if(!tmp)return ENOMEM;snprintf(tmp,length+16,"%s.tmp.XXXXXX",s->path);
    int fd=mkstemp(tmp),error=fd<0?errno:0;size_t at=0;
    while(!error&&at<sizeof s->bytes){ssize_t n=write(fd,s->bytes+at,sizeof s->bytes-at);if(n<0&&errno==EINTR)continue;if(n<=0)error=n<0?errno:EIO;else at+=(size_t)n;}
    if(!error&&fsync(fd))error=errno;if(fd>=0&&close(fd)&&!error)error=errno;
    if(!error&&rename(tmp,s->path))error=errno;
    if(error&&fd>=0)unlink(tmp);
    if(!error){char *dirpath=strdup(s->path);if(!dirpath)error=ENOMEM;
        else {char *slash=strrchr(dirpath,'/');if(slash){if(slash==dirpath)slash[1]=0;else *slash=0;}else strcpy(dirpath,".");
            int dir=open(dirpath,O_RDONLY|O_DIRECTORY);if(dir<0)error=errno;else {if(fsync(dir))error=errno;close(dir);}free(dirpath);}}
    /* A directory-sync failure is a durability error after publication. */
    free(tmp);return error;
}
static void *nd_nv_worker(void *opaque){
    PtNativeNvramStore *s=opaque;pthread_mutex_lock(&s->lock);
    for(;;){while(!s->pending&&!s->stop)pthread_cond_wait(&s->wake,&s->lock);if(!s->pending&&s->stop)break;
        s->pending=0;s->busy=1;pthread_mutex_unlock(&s->lock);int error=nd_nv_commit(s);pthread_mutex_lock(&s->lock);
        s->error=error;if(!error)s->saved=s->generation;s->busy=0;pthread_cond_broadcast(&s->wake);
    }pthread_mutex_unlock(&s->lock);return NULL;
}
int pt_nvram_open(PtNvram *n,const char *path){
    if(!path||!*path||n->store)return EINVAL;
    PtNativeNvramStore *s=calloc(1,sizeof *s);if(!s)return ENOMEM;s->fd=-1;size_t len=strlen(path);char *lock=malloc(len+6);int error=0,exists=0;
    if(!lock){free(s);return ENOMEM;}snprintf(lock,len+6,"%s.lock",path);s->path=strdup(path);if(!s->path)error=ENOMEM;
    if(!error){s->fd=open(lock,O_RDWR|O_CREAT|O_NOFOLLOW,0600);if(s->fd<0)error=errno;else if(flock(s->fd,LOCK_EX|LOCK_NB))error=errno;}
    free(lock);uint8_t loaded[PT_NVRAM_SIZE];if(!error){int fd=open(path,O_RDONLY|O_NOFOLLOW);if(fd<0){if(errno!=ENOENT)error=errno;}
        else {struct stat st;if(fstat(fd,&st))error=errno;else if(!S_ISREG(st.st_mode)||st.st_size!=PT_NVRAM_SIZE)error=EINVAL;
            size_t at=0;while(!error&&at<sizeof loaded){ssize_t got=read(fd,loaded+at,sizeof loaded-at);if(got<0&&errno==EINTR)continue;if(got<=0)error=got<0?errno:EIO;else at+=(size_t)got;}exists=!error;close(fd);}}
    int mutex=0,cond=0;if(!error){error=pthread_mutex_init(&s->lock,NULL);mutex=!error;}if(!error){error=pthread_cond_init(&s->wake,NULL);cond=!error;}
    if(!error){s->saved=exists?n->generation:0;error=pthread_create(&s->thread,NULL,nd_nv_worker,s);}
    if(error){if(cond)pthread_cond_destroy(&s->wake);if(mutex)pthread_mutex_destroy(&s->lock);if(s->fd>=0)close(s->fd);free(s->path);free(s);return error;}
    if(exists)memcpy(n->data,loaded,sizeof loaded);n->store=(PtNvramStore *)s;return 0;
}
int pt_nvram_service(PtNvram *n){
    PtNativeNvramStore *s=(void *)n->store;if(!s)return 0;
    int error=pthread_mutex_trylock(&s->lock);if(error)return error==EBUSY?EAGAIN:error;
    if(s->error)error=s->error;else if(s->busy||s->pending)error=EAGAIN;else if(s->saved!=n->generation){
        memcpy(s->bytes,n->data,sizeof n->data);s->generation=n->generation;s->pending=1;pthread_cond_signal(&s->wake);}
    pthread_mutex_unlock(&s->lock);return error;
}
int pt_nvram_close(PtNvram *n){
    PtNativeNvramStore *s=(void *)n->store;if(!s)return 0;
    pthread_mutex_lock(&s->lock);while(s->busy||s->pending)pthread_cond_wait(&s->wake,&s->lock);
    if(!s->error&&s->saved!=n->generation){memcpy(s->bytes,n->data,sizeof n->data);s->generation=n->generation;s->pending=1;pthread_cond_signal(&s->wake);
        while(s->busy||s->pending)pthread_cond_wait(&s->wake,&s->lock);}
    int error=s->error;s->stop=1;pthread_cond_signal(&s->wake);pthread_mutex_unlock(&s->lock);pthread_join(s->thread,NULL);
    close(s->fd);pthread_cond_destroy(&s->wake);pthread_mutex_destroy(&s->lock);free(s->path);free(s);n->store=NULL;return error;
}
