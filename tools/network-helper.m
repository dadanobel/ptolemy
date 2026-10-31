/* SPDX-License-Identifier: GPL-2.0-or-later */
#import <Foundation/Foundation.h>
#include "net_stream.h"
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/stat.h>
#include <poll.h>
#include <fcntl.h>
#include <unistd.h>
#include <signal.h>
#include <pwd.h>
#include <errno.h>
/* Development-only helper, explicitly launched by the developer. It owns one
 * vmnet interface and one authenticated stream, never guest memory or disks. */
static volatile sig_atomic_t stopping;
static void stop(int sig){(void)sig;stopping=1;}
static void wake_pipe(void *p){uint8_t b=1;(void)!write(*(int *)p,&b,1);}
static int exact(int fd,void *data,unsigned n,int output){
    uint64_t end=(uint64_t)clock_gettime_nsec_np(CLOCK_MONOTONIC)+5000000000ull;
    while(n&&!stopping){uint64_t now=clock_gettime_nsec_np(CLOCK_MONOTONIC);if(now>=end)return ETIMEDOUT;
        struct pollfd p={fd,output?POLLOUT:POLLIN,0};int k=poll(&p,1,(int)((end-now+999999)/1000000));if(k<0&&errno==EINTR)continue;if(k<=0)return k?errno:ETIMEDOUT;
        ssize_t got=output?send(fd,data,n,MSG_DONTWAIT):recv(fd,data,n,MSG_DONTWAIT);if(got<0&&(errno==EAGAIN||errno==EINTR))continue;if(got<=0)return got?errno:ECONNRESET;data=(uint8_t *)data+got;n-=(unsigned)got;
    }return stopping?ECANCELED:0;
}
static int bridge(int fd,PtNetHost *host,int wakefd){
    PtNetIo io=pt_net_host_io(host);PtNetStream stream={0};uint8_t to_host[PT_NET_FRAME],to_client[PT_NET_FRAME];int incoming=0,outgoing=0;
    while(!stopping&&io.link(io.opaque)){
        int progressed=0;
        for(unsigned budget=0;budget<PT_NET_QUEUE;budget++){
            if(!incoming){incoming=pt_net_stream_receive(fd,&stream,to_host);if(incoming<0)return -incoming;}
            if(!incoming)break;int e=io.send(io.opaque,to_host,(unsigned)incoming);if(e==EAGAIN)break;if(e)return e;incoming=0;progressed=1;
        }
        for(unsigned budget=0;budget<PT_NET_QUEUE;budget++){
            if(!outgoing){outgoing=io.receive(io.opaque,to_client,sizeof to_client);if(outgoing<0)return -outgoing;}
            if(!outgoing)break;int e=pt_net_stream_send(fd,&stream,to_client,(unsigned)outgoing);if(e==EAGAIN||e==EWOULDBLOCK)break;if(e)return e;outgoing=0;progressed=1;
        }
        if(progressed)continue;
        struct pollfd p[2]={{fd,(short)((incoming?0:POLLIN)|(outgoing?POLLOUT:0)),0},{wakefd,POLLIN,0}};
        if(poll(p,2,-1)<0){if(errno==EINTR)continue;return errno;}
        if(p[0].revents&(POLLHUP|POLLERR|POLLNVAL))return ECONNRESET;
        if(p[1].revents&POLLIN){uint8_t b[256];while(read(wakefd,b,sizeof b)>0){}}
    }return stopping?0:ENETDOWN;
}
int main(int argc,const char **argv){@autoreleasepool{
    if(argc!=3||strcmp(argv[1],"--uid")||geteuid()!=0){fprintf(stderr,"Usage (authorized development session only): sudo %s --uid USER_ID\n",argv[0]);return 2;}
    char *end;unsigned long value=strtoul(argv[2],&end,10);uid_t uid=(uid_t)value;
    if(!*argv[2]||*end||!uid||value!=uid||!getpwuid(uid)){fprintf(stderr,"A non-root local user is required\n");return 2;}
    struct sigaction sa={0};sa.sa_handler=stop;sigaction(SIGINT,&sa,NULL);sigaction(SIGTERM,&sa,NULL);sigaction(SIGHUP,&sa,NULL);
    int error=0,listener=-1,client=-1,pipefd[2]={-1,-1};PtNetHost *host=NULL;uint8_t mac[6];
    char directory[]="/private/var/run/ptolemy-net.XXXXXX",path[sizeof(((struct sockaddr_un *)0)->sun_path)]={0};
    if(!mkdtemp(directory)){perror("network helper directory");return 1;}
    if(chmod(directory,0711)||snprintf(path,sizeof path,"%s/control",directory)>=(int)sizeof path){error=errno?errno:ENAMETOOLONG;goto done;}
    if(pipe(pipefd)){error=errno;goto done;}
    for(unsigned k=0;k<2;k++)if(fcntl(pipefd[k],F_SETFL,O_NONBLOCK)||fcntl(pipefd[k],F_SETFD,FD_CLOEXEC)){error=errno;goto done;}
    host=pt_net_host_open(NULL,NULL,mac,&error);if(!host)goto done;pt_net_host_notify(host,wake_pipe,&pipefd[1]);
    listener=socket(AF_UNIX,SOCK_STREAM,0);if(listener<0){error=errno;goto done;}struct sockaddr_un address={.sun_family=AF_UNIX};strcpy(address.sun_path,path);
    if(bind(listener,(struct sockaddr *)&address,sizeof address)||chown(path,uid,(gid_t)-1)||chmod(path,0600)||listen(listener,1)){error=errno;goto done;}
    printf("Network helper ready. Launch Ptolemy as your normal user with:\n  --network-socket %s\n",path);fflush(stdout);
    {struct pollfd p={listener,POLLIN,0};int ready=poll(&p,1,120000);if(ready<=0){error=ready?errno:ETIMEDOUT;goto done;}}
    client=accept(listener,NULL,NULL);if(client<0){error=errno;goto done;}uid_t peer;gid_t group;
    if(getpeereid(client,&peer,&group)||peer!=uid){error=EACCES;goto done;}close(listener);listener=-1;
    {int one=1;if(fcntl(client,F_SETFL,O_NONBLOCK)||fcntl(client,F_SETFD,FD_CLOEXEC)||setsockopt(client,SOL_SOCKET,SO_NOSIGPIPE,&one,sizeof one)){error=errno;goto done;}}
    {uint8_t hello[12],reply[16]={'P','T','N','E','T','2'};
        if((error=exact(client,hello,sizeof hello,0)))goto done;
        if(memcmp(hello,"PTNET2\0\0",8)){error=EPROTO;goto done;}
        memcpy(reply+6,mac,6);pt_write_le(reply+12,4,1500);if((error=exact(client,reply,sizeof reply,1)))goto done;
    }
    error=bridge(client,host,pipefd[0]);if(error==ECONNRESET||error==EPIPE)error=0;
done:
    if(client>=0)close(client);if(listener>=0)close(listener);
    if(host){int e=pt_net_host_close(host);if(!error)error=e;}
    for(unsigned k=0;k<2;k++)if(pipefd[k]>=0)close(pipefd[k]);
    if(path[0])unlink(path);rmdir(directory);if(error)fprintf(stderr,"network helper stopped: status %d\n",error);return error?1:0;
}}
