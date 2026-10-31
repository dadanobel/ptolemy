/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "pt_export.h"
#include "pt_sha256.h"
#include <pthread.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <limits.h>
struct PtExport {
    pthread_mutex_t lock;int dir,fd,started,cancel,publishing,done,error,commit;
    uint64_t size,accepted,written,last_offset;unsigned r,w,last_n;
    struct {unsigned n;uint8_t bytes[PTA_CHUNK];} queue[4];
    uint8_t last[PTA_CHUNK],expected[32];PtSha256 sha;
    char temporary[80],name[256],path[PATH_MAX];
};
PtExport *pt_export_open(const char *directory,const char *name){
    if(!directory||!name||!*name||strlen(name)>255||strchr(name,'/')||!strcmp(name,".")||!strcmp(name,"..")){errno=EINVAL;return NULL;}
    PtExport *e=calloc(1,sizeof *e);if(!e)return NULL;e->dir=e->fd=-1;pthread_mutex_init(&e->lock,NULL);
    char resolved[PATH_MAX];struct stat st;
    if(!realpath(directory,resolved)||(e->dir=open(resolved,O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC))<0||fstat(e->dir,&st)||st.st_uid!=getuid()){errno=EACCES;goto fail;}
    if(snprintf(e->path,sizeof e->path,"%s/%s",resolved,name)>=(int)sizeof e->path){errno=ENAMETOOLONG;goto fail;}
    strcpy(e->name,name);snprintf(e->temporary,sizeof e->temporary,".ptolemy-%u-%08x.part",(unsigned)getpid(),arc4random());
    e->fd=openat(e->dir,e->temporary,O_WRONLY|O_CREAT|O_EXCL|O_CLOEXEC|O_NOFOLLOW,0600);if(e->fd<0)goto fail;pt_sha256_init(&e->sha);return e;
fail:{int error=errno;pt_export_close(e);errno=error;return NULL;}
}
void pt_export_close(PtExport *e){if(!e)return;if(e->fd>=0)close(e->fd);if(e->dir>=0){if(e->fd>=0&&e->temporary[0])unlinkat(e->dir,e->temporary,0);close(e->dir);}pthread_mutex_destroy(&e->lock);free(e);}
int pt_export_begin(PtExport *e,uint64_t size){if(!e||size>INT64_MAX)return PTA_INVALID;pthread_mutex_lock(&e->lock);int rc=PTA_OK;
    if(e->cancel)rc=PTA_CANCELLED;else if(e->started&&e->size!=size)rc=PTA_INVALID;else{e->started=1;e->size=size;}
    pthread_mutex_unlock(&e->lock);return rc;
}
int pt_export_chunk(PtExport *e,uint64_t off,const void *bytes,unsigned n){
    if(!e||!bytes||!n||n>PTA_CHUNK)return PTA_INVALID;pthread_mutex_lock(&e->lock);int rc=PTA_INVALID;
    if(e->cancel||e->error)rc=PTA_CANCELLED;
    else if(e->last_n==n&&off==e->last_offset&&!memcmp(e->last,bytes,n))rc=PTA_OK;
    else if(e->started&&!e->commit&&off==e->accepted&&off<=e->size&&n<=e->size-off){
        if(e->w-e->r==4)rc=PTA_AGAIN;else{unsigned k=e->w++%4;memcpy(e->queue[k].bytes,bytes,n);e->queue[k].n=n;
            memcpy(e->last,bytes,n);e->last_offset=off;e->last_n=n;e->accepted+=n;rc=PTA_OK;}}
    pthread_mutex_unlock(&e->lock);return rc;
}
int pt_export_commit(PtExport *e,const uint8_t sha[32]){if(!e||!sha)return PTA_INVALID;pthread_mutex_lock(&e->lock);int rc=PTA_INVALID;
    if(e->cancel||e->error)rc=PTA_CANCELLED;
    else if(e->started&&e->accepted==e->size){
        if(!e->commit){memcpy(e->expected,sha,32);e->commit=1;}
        if(!memcmp(sha,e->expected,32))rc=e->done?PTA_OK:PTA_AGAIN;
    }pthread_mutex_unlock(&e->lock);return rc;
}
void pt_export_cancel(PtExport *e){if(!e)return;pthread_mutex_lock(&e->lock);if(!e->publishing&&!e->done)e->cancel=1;pthread_mutex_unlock(&e->lock);}
void pt_export_service(PtExport *e){if(!e)return;uint8_t bytes[PTA_CHUNK];
    for(unsigned budget=0;budget<4;budget++){
        pthread_mutex_lock(&e->lock);int stop=e->cancel||e->error||e->done;unsigned n=0;
        if(!stop&&e->r!=e->w){unsigned k=e->r++%4;n=e->queue[k].n;memcpy(bytes,e->queue[k].bytes,n);}pthread_mutex_unlock(&e->lock);
        if(stop||!n)break;
        unsigned at=0;while(at<n){ssize_t k=write(e->fd,bytes+at,n-at);if(k<0&&errno==EINTR)continue;if(k<=0){pthread_mutex_lock(&e->lock);e->error=errno?errno:EIO;pthread_mutex_unlock(&e->lock);break;}at+=(unsigned)k;}
        if(at==n){pt_sha256_update(&e->sha,bytes,n);e->written+=n;}
    }
    pthread_mutex_lock(&e->lock);int complete=e->commit&&!e->cancel&&!e->error&&!e->done&&e->r==e->w&&e->written==e->size;pthread_mutex_unlock(&e->lock);
    if(!complete)return;
    uint8_t sha[32];pt_sha256_final(&e->sha,sha);int error=memcmp(sha,e->expected,32)?EBADMSG:0;
    if(!error&&fsync(e->fd))error=errno;
    pthread_mutex_lock(&e->lock);if(e->cancel){pthread_mutex_unlock(&e->lock);return;}e->publishing=1;pthread_mutex_unlock(&e->lock);
    /* Commit linearizes here: cancellation may no longer revoke a complete,
     * hash-checked file. linkat never replaces a user file. */
    if(!error&&linkat(e->dir,e->temporary,e->dir,e->name,0))error=errno;
    pthread_mutex_lock(&e->lock);e->error=error;e->done=!error;pthread_mutex_unlock(&e->lock);
}
const char *pt_export_path(PtExport *e){return e?e->path:"";}

int pt_export_complete(PtExport *e){if(!e)return 0;pthread_mutex_lock(&e->lock);int done=e->done&&!e->error;pthread_mutex_unlock(&e->lock);return done;}
