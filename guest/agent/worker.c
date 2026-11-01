/* SPDX-License-Identifier: GPL-2.0-or-later
 * Runs as the logged-in Tiger user. No network, root, shell expansion or daemon.
 * The visible app owns this worker and its log; Stop cancels the child group. */
#include "pt_agent.h"
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/file.h>
#include <sys/time.h>
#include <unistd.h>
#include <signal.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <dirent.h>
#include <sys/stat.h>
#include "log_argv.h"

extern int ptagent_call(uint32_t,uint32_t,uint32_t,uint32_t,uint32_t,uint32_t[3]);
#include "guest/agent_builtin.h"
#include "machine_builtin.h"

static volatile sig_atomic_t stopping;
static void stop(int s){(void)s;stopping=1;}
static uint64_t ms(void){struct timeval t;gettimeofday(&t,NULL);return (uint64_t)t.tv_sec*1000+t.tv_usec/1000;}
static uint32_t u32(const uint8_t *p){return (uint32_t)p[0]<<24|(uint32_t)p[1]<<16|(uint32_t)p[2]<<8|p[3];}
#include "pointer_observer.h"
#include "input_builtin.h"
static void input_tick(uint32_t token,uint64_t now){pt_pointer_guest_tick(token,now);pt_input_guest_tick(token,now);}
static int parse(uint8_t *b,uint32_t n,char *argv[PTA_MAX_ARGS+1],char text[PTA_MAX_REQUEST+PTA_MAX_ARGS]){
    if(n<8)return -1;uint32_t argc=u32(b),at=4,to=0;if(!argc||argc>PTA_MAX_ARGS)return -1;
    for(uint32_t i=0;i<argc;i++){if(n-at<4)return -1;uint32_t k=u32(b+at);at+=4;
        if(k>n-at||memchr(b+at,0,k)||(!i&&(!k||b[at]!='/')))return -1;
        argv[i]=text+to;memcpy(text+to,b+at,k);text[to+k]=0;at+=k;to+=k+1;}
    argv[argc]=NULL;return at==n?0:-1;
}
static unsigned execute(uint32_t token,uint32_t id,uint32_t timeout,char **argv){
    uint32_t r[3]={0};int fd[2];if(pipe(fd)){perror("pipe");return 125;}
    if(stopping||ptagent_call(PTA_CHECK,token,id,0,0,r)||r[0]){close(fd[0]);close(fd[1]);return 125;}
    pid_t child=fork();if(child<0){perror("fork");close(fd[0]);close(fd[1]);return 125;}
    if(!child){setpgid(0,0);signal(SIGINT,SIG_DFL);signal(SIGTERM,SIG_DFL);signal(SIGHUP,SIG_DFL);
        close(fd[0]);int in=open("/dev/null",O_RDONLY);if(in<0||dup2(in,0)<0||dup2(fd[1],1)<0||dup2(fd[1],2)<0)_exit(126);
        if(in>2)close(in);if(fd[1]>2)close(fd[1]);execv(argv[0],argv);perror("execv");_exit(127);}
    setpgid(child,child);close(fd[1]);fcntl(fd[0],F_SETFL,O_NONBLOCK);
    int status=0,exited=0,eof=0;uint32_t logged=0;int log_truncated=0;
    uint64_t deadline=ms()+timeout,killed=0,finished=0;uint8_t b[4096];
    while(!exited||!eof){
        input_tick(token,ms());
        for(unsigned chunks=0;chunks<16;chunks++){ssize_t n=read(fd[0],b,sizeof b);
            if(n>0){uint32_t k=PTA_MAX_OUTPUT-logged;if(k>(uint32_t)n)k=(uint32_t)n;
                if(k){fwrite(b,1,k,stdout);logged+=k;}
                if(k<(uint32_t)n&&!log_truncated){puts("\n[Output log truncated at 1MiB; pipe still drained]");log_truncated=1;}
                (void)ptagent_call(PTA_OUTPUT,token,id,(uint32_t)(uintptr_t)b,(uint32_t)n,r);}
            else {if(!n)eof=1;else if(errno!=EAGAIN&&errno!=EINTR)eof=1;break;}}
        if(!exited){pid_t p=waitpid(child,&status,WNOHANG);if(p==child||(p<0&&errno==ECHILD)){exited=1;finished=ms();}}
        uint32_t cancel=0;int rc=ptagent_call(PTA_CHECK,token,id,0,0,r);if(rc||r[0])cancel=1;
        if(stopping||cancel||ms()>=deadline||(exited&&ms()-finished>1000)){
            if(!killed){kill(-child,SIGTERM);killed=ms();}
            else if(ms()-killed>=2000)kill(-child,SIGKILL);
            if(exited&&ms()-killed>=2200)break;
        }
        if(!exited||!eof)usleep(20000);
    }
    close(fd[0]);/* Descendants must not survive a completed job holding pipes. */
    kill(-child,SIGTERM);if(!exited)while(waitpid(child,&status,0)<0&&errno==EINTR){}
    return WIFEXITED(status)?WEXITSTATUS(status):WIFSIGNALED(status)?128+WTERMSIG(status):125;
}
/* Protocol 2, one host file per transfer. The temp file lives in our incoming directory on the home
 * volume; it is published into ~/Desktop only after COMMIT (size and SHA-256 checked on both sides) by link(), which
 * never replaces an existing file ("name 2.ext" instead). Cancelled or failed transfers leave nothing published. */
static char incoming[2048];
static void clean_incoming(void){DIR *d=opendir(incoming);struct dirent *e;char p[4096];if(!d)return;
    while((e=readdir(d))){size_t n=strlen(e->d_name);if(n>5&&!strcmp(e->d_name+n-5,".part")){snprintf(p,sizeof p,"%s/%s",incoming,e->d_name);unlink(p);}}
    closedir(d);}
static unsigned receive(uint32_t token,uint32_t id,const uint8_t *head,const char *home){
    static uint8_t space[PTA_CHUNK+8192];static char path[PTA_PATH_MAX];
    uint8_t *buf=space+4093,want[32],got[32];   /* deliberately unaligned: every chunk exercises the host's page walk */
    uint64_t size=(uint64_t)u32(head)<<32|u32(head+4),off=0,t0=ms();uint32_t kind=u32(head+40),nn=u32(head+44),r[3];unsigned status=0,i;int rc=0;
    char name[PTA_NAME_MAX+1],tmp[4096];PtSha256 h;memcpy(want,head+8,32);memset(path,0,sizeof path);
    if(kind>PTA_XCLIPBOARD||!nn||nn>PTA_NAME_MAX||memchr(head+48,'/',nn)||memchr(head+48,0,nn)||head[48]=='.'||size>PTA_XLIMIT||(kind&&size>PTA_CLIP_MAX))status=2;
    else {memcpy(name,head+48,nn);name[nn]=0;}
    snprintf(tmp,sizeof tmp,"%s/%u-%d.part",incoming,id,(int)getpid());
    int fd=status?-1:open(tmp,O_CREAT|O_EXCL|O_WRONLY,0600);if(!status&&fd<0)status=errno==ENOSPC?5:6;
    pt_sha256_init(&h);
    for(i=0;i<PTA_CHUNK;i+=4096)buf[i]=0;buf[PTA_CHUNK-1]=0;   /* materialize the zero-fill pages first */
    while(!status&&off<size){
        input_tick(token,ms());
        rc=ptagent_call(PTA_XCHUNK,token,id,(uint32_t)off,(uint32_t)(uintptr_t)buf,r);
        if(rc==PTA_FAULT){for(i=0;i<PTA_CHUNK;i+=4096)buf[i]=0;buf[PTA_CHUNK-1]=0;rc=ptagent_call(PTA_XCHUNK,token,id,(uint32_t)off,(uint32_t)(uintptr_t)buf,r);}
        if(rc){status=rc==PTA_CANCELLED?3:4;break;}
        uint32_t n=r[0],done=0;if(!n||n>PTA_CHUNK||n>size-off){status=4;break;}
        while(done<n){ssize_t k=write(fd,buf+done,n-done);if(k<0&&errno==EINTR)continue;if(k<=0){status=errno==ENOSPC?5:6;break;}done+=(uint32_t)k;}
        pt_sha256_update(&h,buf,n);off+=n;
    }
    if(fd>=0){if(!status&&fsync(fd))status=errno==ENOSPC?5:6;close(fd);}
    pt_sha256_final(&h,got);
    if(!status&&memcmp(got,want,32))status=7;
    if(!status){rc=ptagent_call(PTA_XCOMMIT,token,id,(uint32_t)(uintptr_t)got,0,r);if(rc)status=rc==PTA_CANCELLED?3:7;}
    if(!status&&kind==PTA_XCLIPBOARD){   /* UTF-8 in, through pbcopy in this user's session; no file is published */
        pid_t p=fork();int st=0;
        if(!p){int in=open(tmp,O_RDONLY);char enc[64];snprintf(enc,sizeof enc,"0x%X:0x8000100:0x8000100",(unsigned)getuid());
            if(in<0||dup2(in,0)<0)_exit(126);setenv("LANG","en_US.UTF-8",1);setenv("__CF_USER_TEXT_ENCODING",enc,1);
            execl("/usr/bin/pbcopy","pbcopy",(char *)0);_exit(127);}
        if(p<0||waitpid(p,&st,0)!=p||!WIFEXITED(st)||WEXITSTATUS(st))status=9;else strcpy(path,"clipboard");}
    else if(!status){const char *dot=strrchr(name,'.');size_t stem=dot&&dot!=name?(size_t)(dot-name):strlen(name);
        for(i=1;i<10000;i++){
            if(i==1)snprintf(path,sizeof path,"%s/Desktop/%s",home,name);
            else snprintf(path,sizeof path,"%s/Desktop/%.*s %u%s",home,(int)stem,name,i,name+stem);
            if(!link(tmp,path))break;if(errno!=EEXIST){status=8;break;}}
        if(i>=10000)status=8;if(status)path[0]=0;}
    if(fd>=0)unlink(tmp);
    (void)ptagent_call(PTA_XDONE,token,id,status,(uint32_t)(uintptr_t)path,r);
    double s=(ms()-t0)/1000.0;
    if(status)printf("\n[Transfer %u] not published (status %u)\n",id,status);
    else if(kind)printf("\n[Transfer %u] clipboard text, %llu bytes\n",id,(unsigned long long)size);
    else printf("\n[Transfer %u] %llu bytes in %.1f s (%.2f MB/s) -> %s\n",id,(unsigned long long)size,s,s>0?size/s/1e6:0,path);
    return status;
}
#include "export_builtin.h"
int main(void){
    setvbuf(stdout,NULL,_IONBF,0);const char *home=getenv("HOME");if(!home)return 2;
    char path[4096];if(snprintf(path,sizeof path,"%s/Library/Application Support/Ptolemy/agent.lock",home)>=(int)sizeof path)return 2;
    int lock=open(path,O_CREAT|O_RDWR,0600);if(lock<0||flock(lock,LOCK_EX|LOCK_NB)){perror("agent lock");return 2;}
    fcntl(lock,F_SETFD,FD_CLOEXEC);struct sigaction sa;memset(&sa,0,sizeof sa);sa.sa_handler=stop;sigemptyset(&sa.sa_mask);
    sigaction(SIGINT,&sa,NULL);sigaction(SIGTERM,&sa,NULL);sigaction(SIGHUP,&sa,NULL);
    /* SIGILL intentionally ends this worker, not the Cocoa app, on other CPUs. */
    if(snprintf(incoming,sizeof incoming,"%s/Library/Application Support/Ptolemy/incoming",home)>=(int)sizeof incoming)return 2;
    mkdir(incoming,0700);clean_incoming();   /* temps of an earlier run that was stopped or lost the VM */
    uint32_t r[3]={0},token=0;int rc=ptagent_call(PTA_HELLO,0,PTA_VERSION,PTA_CAP_EXPORT|PTA_CAP_POINTER,0,r);
    if(rc==PTA_INVALID)rc=ptagent_call(PTA_HELLO,0,PTA_TRANSFER_VERSION,0,0,r);
    if(rc){fprintf(stderr,"Agent could not register: %d. Enable --agent-socket on the stand.\n",rc);return 2;}
    token=r[0];uint32_t host=r[1];pt_pointer_guest_begin(r[2]);pt_input_guest_begin(host>=3?r[2]:0);
    printf("Ptolemy development agent ON (protocol %u, host %u). Commands run as this Tiger user. Stop in the app to disable.\n",PTA_VERSION,host);
    while(!stopping){input_tick(token,ms());uint8_t request[PTA_MAX_REQUEST];char text[PTA_MAX_REQUEST+PTA_MAX_ARGS],*argv[PTA_MAX_ARGS+1];
        /* Materialize writable COW/zero-fill stack pages before host copyout.
         * The adapter must not bypass a missing PTE to manufacture a mapping. */
        for(unsigned touch=0;touch<sizeof request;touch+=4096)((volatile uint8_t *)request)[touch]=0;
        ((volatile uint8_t *)request)[sizeof request-1]=0;
        rc=ptagent_call(PTA_POLL,token,(uint32_t)(uintptr_t)request,sizeof request,0,r);
        if(rc==PTA_AGAIN&&host>=2){static uint8_t head[PTA_XHEAD];memset(head,0,sizeof head);
            if(!ptagent_call(PTA_XBEGIN,token,(uint32_t)(uintptr_t)head,0,0,r)){(void)receive(token,r[0],head,home);continue;}}
        if(rc==PTA_AGAIN){usleep(pt_pointer_guest_cap?20000:100000);continue;}if(rc){fprintf(stderr,"Agent poll failed: %d\n",rc);break;}
        uint32_t id=r[0],n=r[1],timeout=r[2];unsigned status=125;
        if(n<=sizeof request&&timeout&&timeout<=600000&&!parse(request,n,argv,text)){
            pt_log_argv(id,argv);
            if(!pt_input_builtin(token,id,argv,&status)&&!pt_export_builtin(token,id,argv,&status)&&!optics_agent_builtin(token,id,argv,&status)&&!pt_machine_builtin(token,id,argv,&status))status=execute(token,id,timeout,argv);
        }else puts("Invalid host command rejected.");
        printf("\n[Job %u exit %u]\n",id,status);(void)ptagent_call(PTA_FINISH,token,id,status,0,r);
    }
    (void)ptagent_call(PTA_BYE,token,0,0,0,r);puts("Ptolemy development agent OFF.");close(lock);return rc&&rc!=PTA_AGAIN?1:0;
}
