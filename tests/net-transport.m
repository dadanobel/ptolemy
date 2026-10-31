/* SPDX-License-Identifier: GPL-2.0-or-later */
#import <Foundation/Foundation.h>
#include "net_stream.h"
#include <assert.h>
#include <pthread.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>
#include <errno.h>
/* A same-user mock helper: no vmnet, elevated process or guest is started. */
typedef struct {int listen,mode;char path[104];} Server;
static void exact(int fd,uint8_t *p,unsigned n,int output){while(n){ssize_t k=output?send(fd,p,n,0):recv(fd,p,n,0);assert(k>0);p+=k;n-=(unsigned)k;}}
static void *serve(void *p){Server *s=p;@autoreleasepool{int fd=accept(s->listen,NULL,NULL);assert(fd>=0);int one=1;setsockopt(fd,SOL_SOCKET,SO_NOSIGPIPE,&one,sizeof one);
    struct timeval tv={5,0};setsockopt(fd,SOL_SOCKET,SO_RCVTIMEO,&tv,sizeof tv);setsockopt(fd,SOL_SOCKET,SO_SNDTIMEO,&tv,sizeof tv);
    uint8_t hello[12],reply[16]={'P','T','N','E','T','2',2,1,2,3,4,5};pt_write_le(reply+12,4,1500);exact(fd,hello,12,0);assert(!memcmp(hello,"PTNET2\0\0",8));
    if(s->mode==1)reply[5]='1';for(unsigned k=0;k<16;k++)exact(fd,reply+k,1,1);
    if(s->mode==2){uint8_t bad[]={0xff,0xff};exact(fd,bad,2,1);}else if(s->mode==3){uint8_t bad[]={0,64,0,1,2};exact(fd,bad,sizeof bad,1);}
    else if(!s->mode){usleep(100000);for(unsigned k=0;k<256;k++){uint8_t header[2],frame[PT_NET_FRAME];exact(fd,header,2,0);unsigned n=(unsigned)pt_read_be(header,2);assert(n==1514);exact(fd,frame,n,0);assert(pt_read_le(frame+14,4)==k);exact(fd,header,2,1);if(k<2){for(unsigned j=0;j<n;j++)exact(fd,frame+j,1,1);}else exact(fd,frame,n,1);}
        uint8_t b;assert(recv(fd,&b,1,0)==0);
    }close(fd);return NULL;}}
int main(void){@autoreleasepool{
    char directory[]="/private/tmp/pt-net-test.XXXXXX";assert(mkdtemp(directory));
    for(int mode=0;mode<4;mode++){
        Server s={.mode=mode};snprintf(s.path,sizeof s.path,"%s/control",directory);s.listen=socket(AF_UNIX,SOCK_STREAM,0);assert(s.listen>=0);struct sockaddr_un a={.sun_family=AF_UNIX};strcpy(a.sun_path,s.path);if(bind(s.listen,(struct sockaddr *)&a,sizeof a)){perror("mock helper bind");abort();}assert(!chmod(s.path,0600)&&!listen(s.listen,1));
        pthread_t thread;assert(!pthread_create(&thread,NULL,serve,&s));uint8_t mac[6];int status=0;PtEvent wake;pt_event_init(&wake);_Atomic uint32_t kick=0;
        PtNetHost *h=pt_net_host_socket(s.path,&wake,&kick,mac,&status);
        if(mode==1){assert(!h&&status==EPROTO);}else {assert(h&&!status&&mac[0]==2);PtNetIo io=pt_net_host_io(h);
            if(!mode){unsigned tx=0,rx=0,blocked=0;uint64_t end=clock_gettime_nsec_np(CLOCK_MONOTONIC)+10000000000ull;
                while(rx<256&&clock_gettime_nsec_np(CLOCK_MONOTONIC)<end){uint8_t frame[1514];memset(frame,0x5a,sizeof frame);memcpy(frame,mac,6);pt_write_le(frame+14,4,tx);
                    if(tx<256){int error=io.send(io.opaque,frame,sizeof frame);assert(!error||error==EAGAIN);if(!error)tx++;else blocked++;}
                    int n=io.receive(io.opaque,frame,sizeof frame);assert(n>=0);if(n){assert(n==1514&&pt_read_le(frame+14,4)==rx);rx++;}else usleep(100);
                }assert(tx==256&&rx==256&&blocked&&atomic_load(&kick));
            }else {for(unsigned k=0;k<1000&&io.link(io.opaque);k++)usleep(1000);assert(!io.link(io.opaque));}
            assert(!pt_net_host_close(h));
        }
        assert(!pthread_join(thread,NULL));close(s.listen);unlink(s.path);pt_event_destroy(&wake);
    }
    int f[2];assert(!socketpair(AF_UNIX,SOCK_STREAM,0,f));uint8_t short_header[]={0,13},b[PT_NET_FRAME];assert(write(f[0],short_header,2)==2);PtNetStream stream={0};assert(pt_net_stream_receive(f[1],&stream,b)==-EPROTO);close(f[0]);close(f[1]);
    rmdir(directory);puts("PASS: authenticated socket handshake, partial framing, 256 ordered MTU frames, backpressure, version refusal, oversized/truncated input and close");return 0;
}}
