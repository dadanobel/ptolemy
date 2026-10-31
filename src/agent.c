/* SPDX-License-Identifier: GPL-2.0-or-later
 * Single-job development bridge. All socket I/O belongs to this thread;
 * vCPU callbacks only copy bounded buffers under the state mutex.
 * Socket op 2 sends one host file to the guest (protocol 2, pt_agent.h); the thread sleeps in poll()
 * until a socket, a deadline or a wake byte from a vCPU callback (job/transfer done, stop) needs it. */
#include "pt_agent.h"
#include "pt_export.h"
#include "optics/diagnostics.h"
#include "pt_sha256.h"
#include <pthread.h>
#include <stdatomic.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/stat.h>
#include <poll.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

typedef struct {   /* one host -> guest file, or clipboard text (mem, op 3) */
    int active,begun,committed,cancel,done,failed,fd,client;
    uint32_t id,timeout_ms,kind; uint64_t size,served,start; uint8_t *mem;
    uint8_t sha[32]; PtSha256 run; char name[PTA_NAME_MAX+1],result[PTA_PATH_MAX+64];
} Transfer;
struct PtAgent {
    pthread_mutex_t lock;pthread_t thread;_Atomic int stop;int listener,client,wake[2];
    char path[sizeof(((struct sockaddr_un *)0)->sun_path)];
    uint32_t token,next,id,request_n,output_n,timeout,status,version,caps;
    int pending,delivered,done,cancel,truncated;
    uint64_t start,heartbeat;
    PtExport *export;PtGuestPointer pointer;
    Transfer x;
    uint8_t optics[OPTICS_DIAG_BYTES];int optics_ready;
    uint8_t request[PTA_MAX_REQUEST],output[PTA_MAX_OUTPUT];
};
static uint64_t ticks(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return (uint64_t)t.tv_sec*1000000000+t.tv_nsec;}
static uint32_t get32(const void *p){const uint8_t *b=p;return (uint32_t)b[0]<<24|(uint32_t)b[1]<<16|(uint32_t)b[2]<<8|b[3];}
static void put32(void *p,uint32_t n){uint8_t *b=p;b[0]=n>>24;b[1]=n>>16;b[2]=n>>8;b[3]=n;}
static void wake(PtAgent *a){char c=1;(void)!write(a->wake[1],&c,1);}   /* pipe full: a wake is pending anyway */
static int transfer(int fd,void *b,size_t n,int sending){
    uint64_t end=ticks()+2000000000ull;
    while(n){uint64_t now=ticks();if(now>=end)return -1;
        struct pollfd p={fd,sending?POLLOUT:POLLIN,0};int ready=poll(&p,1,(int)((end-now+999999)/1000000));
        if(ready<0&&errno==EINTR)continue;if(ready<=0)return -1;
        ssize_t k=sending?send(fd,b,n,MSG_DONTWAIT):recv(fd,b,n,MSG_DONTWAIT);
        if(k<0&&(errno==EINTR||errno==EAGAIN))continue;if(k<=0)return -1;b=(uint8_t *)b+k;n-=k;}return 0;
}
static void reply(int fd,uint32_t kind,uint32_t status,const void *body,uint32_t n){
    uint8_t h[16];memcpy(h,"PTA1",4);put32(h+4,kind);put32(h+8,status);put32(h+12,n);
    if(!transfer(fd,h,sizeof h,1)&&n)(void)transfer(fd,(void *)body,n,1);
}
/* Wire argv: count + (length, bytes)*; no NUL, absolute executable, <=64KiB. */
static int valid_request(const uint8_t *b,uint32_t n){
    if(n<8)return 0;uint32_t count=get32(b),at=4;if(!count||count>PTA_MAX_ARGS)return 0;
    for(uint32_t i=0;i<count;i++){if(n-at<4)return 0;uint32_t k=get32(b+at);at+=4;
        if(k>n-at||memchr(b+at,0,k)||(!i&&(!k||b[at]!='/')))return 0;at+=k;}return at==n;
}
/* Op 2 body: path length, absolute host path, name length, guest file name (no '/', not hidden, <=255 bytes).
 * The file is hashed here (serve thread, no lock held), before the guest sees it; COMMIT also needs the same hash
 * over what was served. */
static const char *prepare_transfer(Transfer *x,const uint8_t *b,uint32_t n,uint32_t seconds,int fd){
    if(n<8)return "invalid transfer request\n";uint32_t pn=get32(b);if(pn>n-8||!pn||pn>=PATH_MAX||b[4]!='/'||memchr(b+4,0,pn))return "invalid transfer request\n";
    uint32_t nn=get32(b+4+pn);if(nn!=n-8-pn||!nn||nn>PTA_NAME_MAX||memchr(b+8+pn,0,nn)||memchr(b+8+pn,'/',nn)||b[8+pn]=='.')return "invalid guest file name\n";
    char path[PATH_MAX];memcpy(path,b+4,pn);path[pn]=0;
    int f=open(path,O_RDONLY|O_CLOEXEC);struct stat st;
    if(f<0)return "cannot open the host file\n";
    if(fstat(f,&st)||!S_ISREG(st.st_mode)||(uint64_t)st.st_size>PTA_XLIMIT){close(f);return "not a regular file under 4 GiB\n";}
    PtSha256 h;pt_sha256_init(&h);static uint8_t chunk[PTA_CHUNK];uint64_t total=0;
    for(ssize_t k;(k=read(f,chunk,sizeof chunk))!=0;){if(k<0){if(errno==EINTR)continue;close(f);return "host read error\n";}pt_sha256_update(&h,chunk,(size_t)k);total+=(uint64_t)k;}
    if(total!=(uint64_t)st.st_size){close(f);return "host file changed while hashing\n";}
    memset(x,0,sizeof *x);pt_sha256_final(&h,x->sha);pt_sha256_init(&x->run);
    memcpy(x->name,b+8+pn,nn);x->name[nn]=0;x->fd=f;x->size=total;x->client=fd;x->timeout_ms=seconds*1000;
    fprintf(stderr,"Ptolemy Agent: transfer: %s -> %s (%llu bytes)\n",path,x->name,(unsigned long long)total);
    return NULL;
}
/* Op 3 body: UTF-8 text (<= 1 MiB) for the Tiger pasteboard, sent as a kind-1 transfer from memory. */
static void prepare_clipboard(Transfer *x,uint8_t *text,uint32_t n,uint32_t seconds,int fd){
    PtSha256 h;memset(x,0,sizeof *x);pt_sha256_init(&h);pt_sha256_update(&h,text,n);pt_sha256_final(&h,x->sha);pt_sha256_init(&x->run);
    strcpy(x->name,"clipboard");x->kind=PTA_XCLIPBOARD;x->mem=text;x->fd=-1;x->size=n;x->client=fd;x->timeout_ms=seconds*1000;
}
static void accept_request(PtAgent *a){
    int fd=accept(a->listener,NULL,NULL);if(fd<0)return;
    fcntl(fd,F_SETFD,FD_CLOEXEC);
    struct timeval timeout={2,0};int yes=1;setsockopt(fd,SOL_SOCKET,SO_RCVTIMEO,&timeout,sizeof timeout);
    setsockopt(fd,SOL_SOCKET,SO_SNDTIMEO,&timeout,sizeof timeout);setsockopt(fd,SOL_SOCKET,SO_NOSIGPIPE,&yes,sizeof yes);
    uint8_t h[16],b[PTA_MAX_REQUEST],*text=NULL;uint32_t op=0,n=0,seconds=0;
    if(transfer(fd,h,sizeof h,0)||memcmp(h,"PTA1",4)){close(fd);return;}
    op=get32(h+4);n=get32(h+8);seconds=get32(h+12);
    if(op==3){if(n>PTA_CLIP_MAX||!(text=malloc(n?n:1))||(n&&transfer(fd,text,n,0))){free(text);close(fd);return;}}
    else if(n>sizeof b||(n&&transfer(fd,b,n,0))){close(fd);return;}
    char message[256];const char *why=NULL;int status_request=op==0&&!n;pthread_mutex_lock(&a->lock);
    uint32_t owner=a->token;int off=!a->token||ticks()-a->heartbeat>5000000000ull,busy=a->pending||a->x.active;
    if(status_request){uint64_t age=a->heartbeat?(ticks()-a->heartbeat)/1000000:0;
        snprintf(message,sizeof message,"agent=%s heartbeat_age_ms=%llu busy=%d job=%u version=%u\n",a->token?"registered":"off",(unsigned long long)age,busy,a->id,a->version);
    }else if((op<1||op>4)||!seconds||seconds>600||(op==1&&!valid_request(b,n)))strcpy(message,"invalid request\n");
    else if(off)strcpy(message,"Tiger agent is off or not polling\n");
    else if(busy)strcpy(message,"another command is active; disconnect its client to cancel\n");
    else if(op>=2&&a->version<PTA_TRANSFER_VERSION)snprintf(message,sizeof message,"the Tiger agent (protocol %u) cannot receive files or text; install the current Guest Tools\n",a->version);
    else if(op==3){Transfer x;prepare_clipboard(&x,text,n,seconds,fd);text=NULL;x.start=ticks();x.id=++a->next;if(!x.id)x.id=++a->next;x.active=1;a->x=x;
        pthread_mutex_unlock(&a->lock);return;}
    else if(op==2){pthread_mutex_unlock(&a->lock);Transfer x;
        if((why=prepare_transfer(&x,b,n,seconds,fd)))snprintf(message,sizeof message,"%s",why);
        pthread_mutex_lock(&a->lock);
        if(!why&&(!a->token||a->version<PTA_TRANSFER_VERSION||a->pending||a->x.active)){close(x.fd);strcpy(message,"the Tiger agent changed state; try again\n");why=message;}
        if(!why){x.start=ticks();x.id=++a->next;if(!x.id)x.id=++a->next;x.active=1;a->x=x;pthread_mutex_unlock(&a->lock);return;}}
    else if(op==4 && !(a->caps&PTA_CAP_EXPORT))strcpy(message,"Current Ptolemy Tools are required for receiving files\n");
    else if(op==4){
        uint32_t dn=n>=8?get32(b):0,gn=n>=8&&dn<=n-8?get32(b+4+dn):0;
        if(!dn||dn>=PATH_MAX||!gn||gn>=PTA_PATH_MAX||dn+gn+8!=n||b[4]!='/'||b[8+dn]!='/'||memchr(b+4,0,dn)||memchr(b+8+dn,0,gn))strcpy(message,"invalid export paths\n");
        else {char directory[PATH_MAX],guest[PTA_PATH_MAX];memcpy(directory,b+4,dn);directory[dn]=0;memcpy(guest,b+8+dn,gn);guest[gn]=0;
            pthread_mutex_unlock(&a->lock);PtExport *export=pt_export_open(directory,strrchr(guest,'/')+1);pthread_mutex_lock(&a->lock);
            if(export&&(a->token!=owner||!(a->caps&PTA_CAP_EXPORT)||a->pending||a->x.active)){pthread_mutex_unlock(&a->lock);pt_export_close(export);pthread_mutex_lock(&a->lock);export=NULL;}
            if(!export)strcpy(message,"cannot create export; choose an owned destination and a new file name\n");
            else {a->export=export;uint8_t *q=a->request;put32(q,2);q+=4;const char *args[]={"/Ptolemy/files",guest};
                for(unsigned i=0;i<2;i++){unsigned k=(unsigned)strlen(args[i]);put32(q,k);q+=4;memcpy(q,args[i],k);q+=k;}
                a->request_n=(uint32_t)(q-a->request);a->output_n=0;a->cancel=a->done=a->delivered=a->truncated=0;
                a->id=++a->next;if(!a->id)a->id=++a->next;a->timeout=seconds*1000;a->start=ticks();a->pending=1;a->client=fd;pthread_mutex_unlock(&a->lock);return;
            }
        }
    }
    else {memcpy(a->request,b,n);a->request_n=n;a->output_n=0;a->cancel=a->done=a->delivered=a->truncated=0;
        a->id=++a->next;if(!a->id)a->id=++a->next;a->timeout=seconds*1000;a->start=ticks();a->pending=1;a->client=fd;
        fprintf(stderr,"Ptolemy Agent: accepted job %u (%u bytes)\n",a->id,n);pthread_mutex_unlock(&a->lock);return;}
    pthread_mutex_unlock(&a->lock);free(text);reply(fd,status_request?1:2,status_request?0:1,message,(uint32_t)strlen(message));close(fd);
}
static int closed(int fd,short revents){(void)fd;return revents&(POLLIN|POLLHUP|POLLERR|POLLNVAL);}   /* the protocol has no further client bytes */
static void *serve(void *opaque){
    PtAgent *a=opaque;
    while(!atomic_load(&a->stop)){
        pt_export_service(a->export);
        pthread_mutex_lock(&a->lock);Transfer *x=&a->x;uint64_t now=ticks(),wait=UINT64_MAX;
        if(a->pending){uint64_t end=a->export&&a->cancel?a->heartbeat+5000000000ull:a->start+(uint64_t)a->timeout*1000000;wait=end>now?end-now:0;}
        if(x->active&&!x->cancel){uint64_t end=x->start+(uint64_t)x->timeout_ms*1000000;if(end<=now)wait=0;else if(end-now<wait)wait=end-now;}
        int xclient=x->active?x->client:-1;pthread_mutex_unlock(&a->lock);
        struct pollfd f[4]={{a->listener,POLLIN,0},{a->client,POLLIN,0},{xclient,POLLIN,0},{a->wake[0],POLLIN,0}};
        int rc=poll(f,4,wait==UINT64_MAX?-1:(int)((wait+999999)/1000000));if(rc<0&&errno==EINTR)continue;
        if(f[3].revents&POLLIN){char drain[64];(void)!read(a->wake[0],drain,sizeof drain);}
        if(f[0].revents&POLLIN)accept_request(a);
        if(a->client>=0&&closed(a->client,f[1].revents)){
            pthread_mutex_lock(&a->lock);a->cancel=1;pt_export_cancel(a->export);pthread_mutex_unlock(&a->lock);close(a->client);a->client=-1;
        }
        pthread_mutex_lock(&a->lock);
        if(xclient>=0&&x->client==xclient&&closed(xclient,f[2].revents)){close(x->client);x->client=-1;x->cancel=1;}
        now=ticks();
        if(a->pending&&now-a->start>(uint64_t)a->timeout*1000000){a->cancel=1;pt_export_cancel(a->export);}
        if(x->active&&!x->cancel&&now-x->start>(uint64_t)x->timeout_ms*1000000){x->cancel=1;snprintf(x->result,sizeof x->result,"timed out; not published\n");}
        if(x->active&&x->cancel&&!x->begun&&!x->done){x->done=x->failed=1;if(!x->result[0])strcpy(x->result,"cancelled before the guest started; not published\n");}
        if(x->active&&!x->done&&now-a->heartbeat>5000000000ull){x->cancel=x->done=x->failed=1;if(!x->result[0])strcpy(x->result,"the Tiger agent stopped responding; not published\n");}
        if(a->export&&a->cancel&&(!a->delivered||now-a->heartbeat>5000000000ull)){a->done=1;a->status=125;}
        int done=a->pending&&a->done;uint32_t status=a->status,n=a->output_n;int truncated=a->truncated;
        int xdone=x->active&&x->done,xfailed=x->failed,xc=x->client;char result[sizeof x->result];memcpy(result,x->result,sizeof result);
        pthread_mutex_unlock(&a->lock);
        if(done){if(a->client>=0){reply(a->client,truncated?3:0,status,a->output,n);close(a->client);a->client=-1;}
            pthread_mutex_lock(&a->lock);PtExport *export=a->export;a->export=NULL;a->pending=a->done=0;pthread_mutex_unlock(&a->lock);pt_export_close(export);}
        if(xdone){if(xc>=0){reply(xc,0,xfailed?1:0,result,(uint32_t)strlen(result));close(xc);}
            pthread_mutex_lock(&a->lock);if(x->fd>=0)close(x->fd);free(x->mem);fprintf(stderr,"Ptolemy Agent: transfer %u: %s",x->id,result);memset(x,0,sizeof *x);pthread_mutex_unlock(&a->lock);}
    }
    if(a->client>=0){static const char m[]="Tiger stopped; command outcome unknown\n";reply(a->client,2,1,m,sizeof m-1);close(a->client);}
    if(a->x.active){static const char m[]="Tiger stopped; transfer not published\n";if(a->x.client>=0){reply(a->x.client,0,1,m,sizeof m-1);close(a->x.client);}if(a->x.fd>=0)close(a->x.fd);free(a->x.mem);}
    pt_export_close(a->export);a->export=NULL;
    return NULL;
}
PtAgent *pt_agent_open(const char *path){
    struct sockaddr_un address={.sun_family=AF_UNIX};struct stat st;
    if(!path||path[0]!='/'||strlen(path)>=sizeof address.sun_path){errno=EINVAL;return NULL;}
    char parent[sizeof address.sun_path];strcpy(parent,path);char *last=strrchr(parent,'/');*last=0;
    if(!*parent||lstat(parent,&st)||!S_ISDIR(st.st_mode)||st.st_uid!=getuid()||(st.st_mode&077)){errno=EACCES;return NULL;}
    PtAgent *a=calloc(1,sizeof *a);if(!a)return NULL;a->client=-1;a->listener=-1;a->wake[0]=a->wake[1]=-1;
    int e=pthread_mutex_init(&a->lock,NULL);if(e){free(a);errno=e;return NULL;}
    if(pipe(a->wake))goto fail;
    for(int i=0;i<2;i++){fcntl(a->wake[i],F_SETFD,FD_CLOEXEC);fcntl(a->wake[i],F_SETFL,O_NONBLOCK);}
    a->listener=socket(AF_UNIX,SOCK_STREAM,0);if(a->listener<0)goto fail;
    fcntl(a->listener,F_SETFD,FD_CLOEXEC);
    strcpy(address.sun_path,path);strcpy(a->path,path);
    if(bind(a->listener,(struct sockaddr *)&address,sizeof address))goto fail;
    if(chmod(path,0600)||listen(a->listener,4)){unlink(path);goto fail;}
    e=pthread_create(&a->thread,NULL,serve,a);if(e){errno=e;unlink(path);goto fail;}
    fprintf(stderr,"Ptolemy Agent development access: %s (enable the visible app in Tiger)\n",path);return a;
fail:{int error=errno;if(a->listener>=0)close(a->listener);for(int i=0;i<2;i++)if(a->wake[i]>=0)close(a->wake[i]);
    pthread_mutex_destroy(&a->lock);free(a);errno=error;return NULL;}
}
void pt_agent_close(PtAgent *a){if(!a)return;atomic_store(&a->stop,1);wake(a);pthread_join(a->thread,NULL);close(a->listener);
    close(a->wake[0]);close(a->wake[1]);unlink(a->path);pthread_mutex_destroy(&a->lock);free(a);}
int pt_agent_register_caps(PtAgent *a,uint32_t version,uint32_t caps,uint32_t *token){if(version>PTA_VERSION){fprintf(stderr,"Ptolemy Agent: unsupported guest protocol %u (host %u)\n",version,PTA_VERSION);return PTA_INVALID;}pthread_mutex_lock(&a->lock);int rc=PTA_BUSY;
    if(!a->token&&!a->pending&&!a->x.active){do a->token=arc4random();while(!a->token);*token=a->token;a->heartbeat=ticks();a->version=version;a->caps=version>=3?caps&(PTA_CAP_EXPORT|PTA_CAP_POINTER):0;memset(&a->pointer,0,sizeof a->pointer);rc=PTA_OK;}
    pthread_mutex_unlock(&a->lock);return rc;
}
static int authorized(PtAgent *a,uint32_t t){if(!t||t!=a->token)return 0;a->heartbeat=ticks();return 1;}
int pt_agent_optics_publish(PtAgent *a,const void *bytes,uint32_t n){
    OpticsStatus value;if(!a||!optics_status_decode(bytes,n,&value))return PTA_INVALID;
    pthread_mutex_lock(&a->lock);memcpy(a->optics,bytes,OPTICS_DIAG_BYTES);a->optics_ready=1;pthread_mutex_unlock(&a->lock);return PTA_OK;
}
int pt_agent_optics_status(PtAgent *a,uint32_t token,void *out){
    if(!a||!out)return PTA_INVALID;pthread_mutex_lock(&a->lock);int rc=PTA_DENIED;
    if(authorized(a,token)){rc=PTA_AGAIN;if(a->optics_ready){memcpy(out,a->optics,OPTICS_DIAG_BYTES);rc=PTA_OK;}}
    pthread_mutex_unlock(&a->lock);return rc;
}

int pt_agent_pull(PtAgent *a,uint32_t t,void *out,uint32_t cap,uint32_t *id,uint32_t *n,uint32_t *ms){
    pthread_mutex_lock(&a->lock);int rc=PTA_DENIED;
    if(authorized(a,t)){rc=PTA_AGAIN;if(a->pending&&!a->delivered){rc=PTA_INVALID;
        if(cap>=a->request_n){memcpy(out,a->request,a->request_n);*id=a->id;*n=a->request_n;*ms=a->timeout;a->delivered=1;rc=PTA_OK;}}}
    pthread_mutex_unlock(&a->lock);return rc;
}
int pt_agent_output(PtAgent *a,uint32_t t,uint32_t id,const void *b,uint32_t n){
    pthread_mutex_lock(&a->lock);int rc=PTA_DENIED;
    if(authorized(a,t)&&a->pending&&!a->done&&a->id==id){uint32_t k=PTA_MAX_OUTPUT-a->output_n;if(k>n)k=n;
        memcpy(a->output+a->output_n,b,k);a->output_n+=k;if(k<n)a->truncated=1;rc=PTA_OK;}
    pthread_mutex_unlock(&a->lock);return rc;
}
int pt_agent_finish(PtAgent *a,uint32_t t,uint32_t id,uint32_t status){
    pthread_mutex_lock(&a->lock);int rc=PTA_DENIED;
    if(authorized(a,t)&&a->pending&&!a->done&&a->id==id){a->status=a->export&&!status&&!pt_export_complete(a->export)?125:status;a->done=1;rc=PTA_OK;wake(a);}
    pthread_mutex_unlock(&a->lock);return rc;
}
int pt_agent_check(PtAgent *a,uint32_t t,uint32_t id,uint32_t *cancel){
    pthread_mutex_lock(&a->lock);int rc=PTA_DENIED;
    if(authorized(a,t)&&a->pending&&a->id==id){*cancel=a->cancel;rc=PTA_OK;}
    pthread_mutex_unlock(&a->lock);return rc;
}
int pt_agent_unregister(PtAgent *a,uint32_t t){pthread_mutex_lock(&a->lock);int rc=PTA_DENIED;
    if(authorized(a,t)){pt_export_cancel(a->export);a->token=0;a->version=0;if(a->pending&&!a->done){a->cancel=a->done=1;a->status=125;}
        if(a->x.active&&!a->x.done){a->x.cancel=a->x.done=a->x.failed=1;strcpy(a->x.result,"the Tiger agent stopped; not published\n");}
        rc=PTA_OK;wake(a);}
    pthread_mutex_unlock(&a->lock);return rc;
}
/* --- transfers (vCPU thread). Errors after BEGIN leave the guest to report DONE; the host never publishes. */
static Transfer *xfer(PtAgent *a,uint32_t t,uint32_t id){Transfer *x=&a->x;return authorized(a,t)&&x->active&&x->begun&&!x->done&&x->id==id?x:NULL;}
int pt_agent_xbegin(PtAgent *a,uint32_t t,uint8_t head[PTA_XHEAD],uint32_t *id){
    pthread_mutex_lock(&a->lock);int rc=PTA_DENIED;Transfer *x=&a->x;
    if(authorized(a,t)){rc=PTA_AGAIN;if(x->active&&!x->begun&&!x->cancel){
        memset(head,0,PTA_XHEAD);put32(head,(uint32_t)(x->size>>32));put32(head+4,(uint32_t)x->size);memcpy(head+8,x->sha,32);
        uint32_t nn=(uint32_t)strlen(x->name);put32(head+40,x->kind);put32(head+44,nn);memcpy(head+48,x->name,nn);x->begun=1;*id=x->id;rc=PTA_OK;}}
    pthread_mutex_unlock(&a->lock);return rc;
}
int pt_agent_xchunk(PtAgent *a,uint32_t t,uint32_t id,uint32_t off,uint8_t out[PTA_CHUNK],uint32_t *n){
    pthread_mutex_lock(&a->lock);int rc=PTA_DENIED;Transfer *x=xfer(a,t,id);
    if(x&&x->cancel)rc=PTA_CANCELLED;
    else if(x){uint64_t left=off<=x->size?x->size-off:0;uint32_t k=left<PTA_CHUNK?(uint32_t)left:PTA_CHUNK;rc=PTA_INVALID;
        int ahead=off==x->served,repeat=(uint64_t)off+k<=x->served&&off<x->served;
        /* pread on the vCPU thread under the lock (64 KiB, usually page cache); prefetch ahead if this ever stalls */
        if(off<=x->size&&(ahead||repeat)){ssize_t r=!k?0:x->mem?(memcpy(out,x->mem+off,k),(ssize_t)k):pread(x->fd,out,k,off);
            if(r==(ssize_t)k){if(ahead){pt_sha256_update(&x->run,out,k);x->served+=k;}*n=k;rc=PTA_OK;}
            else if(!x->failed){x->failed=1;snprintf(x->result,sizeof x->result,"host read error at %u; not published\n",off);}}}
    pthread_mutex_unlock(&a->lock);return rc;
}
int pt_agent_xcommit(PtAgent *a,uint32_t t,uint32_t id,const uint8_t sha[32]){
    pthread_mutex_lock(&a->lock);int rc=PTA_DENIED;Transfer *x=xfer(a,t,id);
    if(x&&x->cancel)rc=PTA_CANCELLED;
    else if(x&&x->committed)rc=PTA_OK;                                    /* a repeated COMMIT */
    else if(x){rc=PTA_INVALID;
        if(x->served==x->size&&!x->failed){uint8_t got[32];PtSha256 h=x->run;pt_sha256_final(&h,got);
            if(memcmp(got,x->sha,32))snprintf(x->result,sizeof x->result,"the host file changed during the transfer; not published\n");
            else if(memcmp(sha,x->sha,32))snprintf(x->result,sizeof x->result,"SHA-256 mismatch in the guest; not published\n");
            else {x->committed=1;rc=PTA_OK;}
            if(rc)x->failed=1;}}
    pthread_mutex_unlock(&a->lock);return rc;
}
int pt_agent_xdone(PtAgent *a,uint32_t t,uint32_t id,uint32_t status,const char *path){
    pthread_mutex_lock(&a->lock);int rc=PTA_DENIED;Transfer *x=xfer(a,t,id);
    if(x){x->done=1;rc=PTA_OK;
        if(!status&&x->committed)snprintf(x->result,sizeof x->result,x->kind==PTA_XCLIPBOARD?"clipboard set\n":"published %s\n",path);
        else {x->failed=1;if(!x->result[0])snprintf(x->result,sizeof x->result,x->cancel?"cancelled; not published\n":"guest error %u; not published\n",status);}
        wake(a);}
    pthread_mutex_unlock(&a->lock);return rc;
}

int pt_agent_input(PtAgent *a,uint32_t token,unsigned state){
    if(!a||state>2)return PTA_INVALID;pthread_mutex_lock(&a->lock);
    int rc=authorized(a,token)?PTA_OK:PTA_DENIED;pthread_mutex_unlock(&a->lock);return rc;
}

int pt_agent_export_begin(PtAgent *a,uint32_t t,uint32_t id,uint64_t size){
    pthread_mutex_lock(&a->lock);int rc=PTA_DENIED;
    if(authorized(a,t)&&a->pending&&a->id==id&&a->export)rc=pt_export_begin(a->export,size);
    pthread_mutex_unlock(&a->lock);wake(a);return rc;
}
int pt_agent_export_chunk(PtAgent *a,uint32_t t,uint32_t id,uint64_t off,const void *p,unsigned n){
    pthread_mutex_lock(&a->lock);int rc=PTA_DENIED;
    if(authorized(a,t)&&a->pending&&a->id==id&&a->export)rc=pt_export_chunk(a->export,off,p,n);
    pthread_mutex_unlock(&a->lock);wake(a);return rc;
}
int pt_agent_export_commit(PtAgent *a,uint32_t t,uint32_t id,const uint8_t sha[32]){
    pthread_mutex_lock(&a->lock);int rc=PTA_DENIED;
    if(authorized(a,t)&&a->pending&&a->id==id&&a->export)rc=pt_export_commit(a->export,sha);
    pthread_mutex_unlock(&a->lock);wake(a);return rc;
}

int pt_agent_register(PtAgent *a,uint32_t version,uint32_t *token){return pt_agent_register_caps(a,version,0,token);}

int pt_agent_pointer(PtAgent *a,uint32_t t,const uint8_t wire[24]){
    if(get32(wire)!=1)return PTA_INVALID;
    PtGuestPointer p={.state=get32(wire+4),.x=(int32_t)get32(wire+8),.y=(int32_t)get32(wire+12),.width=get32(wire+16),.height=get32(wire+20)};
    if(p.state<1||p.state>2||p.width<320||p.width>16384||p.height<200||p.height>16384||p.x<0||p.y<0||(uint32_t)p.x>=p.width||(uint32_t)p.y>=p.height)return PTA_INVALID;
    pthread_mutex_lock(&a->lock);int rc=PTA_DENIED;
    if(authorized(a,t)&&(a->caps&PTA_CAP_POINTER)){p.at=ticks();a->pointer=p;rc=PTA_OK;}pthread_mutex_unlock(&a->lock);return rc;
}
int pt_agent_pointer_read(PtAgent *a,PtGuestPointer *out){
    if(!a||!out)return 0;pthread_mutex_lock(&a->lock);*out=a->pointer;
    uint64_t now=ticks();int valid=a->token&&out->at&&now>=out->at&&now-out->at<=1000000000ull;
    pthread_mutex_unlock(&a->lock);return valid;
}

unsigned pt_agent_capabilities(PtAgent *a,uint32_t *session){if(!a)return 0;pthread_mutex_lock(&a->lock);unsigned caps=a->token&&ticks()-a->heartbeat<2000000000ull?a->caps:0;if(session)*session=caps?a->token:0;pthread_mutex_unlock(&a->lock);return caps;}
