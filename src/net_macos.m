/* SPDX-License-Identifier: GPL-2.0-or-later */
#import <Foundation/Foundation.h>
#include "net.h"
#include "net_stream.h"
#include <vmnet/vmnet.h>
#include <xpc/xpc.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <poll.h>
#include <unistd.h>
#include <errno.h>
#include <stdatomic.h>
typedef struct {unsigned n;uint8_t data[PT_NET_FRAME];} NetFrame;
@interface PTNetBackend:NSObject {
@public
    dispatch_queue_t queue;dispatch_source_t pump,reader;interface_ref iface;
    PtEvent *wake;_Atomic uint32_t *kick;_Atomic uint32_t txw,txr,rxw,rxr;_Atomic int up;
    NetFrame tx[PT_NET_QUEUE],rx[PT_NET_QUEUE];uint8_t mac[6];int status,fd;BOOL closing,retryScheduled,started;
    PtNetStream stream;void (*notify)(void *);void *notifyOpaque;
}
- (void)drain;
- (void)arm;
@end
static void nh_wake(PTNetBackend *h){if(h->closing)return;if(h->notify)h->notify(h->notifyOpaque);if(h->kick)atomic_store_explicit(h->kick,1,memory_order_release);if(h->wake)pt_event_signal(h->wake);}
static void nh_retry(PTNetBackend *h){if(h->closing||h->retryScheduled)return;h->retryScheduled=YES;dispatch_after(dispatch_time(DISPATCH_TIME_NOW,1000000),h->queue,^{h->retryScheduled=NO;if(!h->closing)[h drain];});}
@implementation PTNetBackend
- (instancetype)init{if((self=[super init])){fd=-1;queue=dispatch_queue_create("org.ptolemy.network",DISPATCH_QUEUE_SERIAL);atomic_init(&txw,0);atomic_init(&txr,0);atomic_init(&rxw,0);atomic_init(&rxr,0);atomic_init(&up,0);}return self;}
- (void)arm{
    pump=dispatch_source_create(DISPATCH_SOURCE_TYPE_DATA_ADD,0,0,queue);__weak PTNetBackend *weak=self;
    dispatch_source_set_event_handler(pump,^{PTNetBackend *h=weak;if(h&&!h->closing)[h drain];});dispatch_resume(pump);
    if(fd>=0){reader=dispatch_source_create(DISPATCH_SOURCE_TYPE_READ,(uintptr_t)fd,0,queue);dispatch_source_set_event_handler(reader,^{PTNetBackend *h=weak;if(h&&!h->closing)[h drain];});dispatch_resume(reader);}
    else vmnet_interface_set_event_callback(iface,VMNET_INTERFACE_PACKETS_AVAILABLE,queue,^(interface_event_t event,xpc_object_t values){(void)event;(void)values;PTNetBackend *h=weak;if(h&&!h->closing)[h drain];});
}
- (void)drain{
    if(closing||!atomic_load_explicit(&up,memory_order_acquire))return;
    uint32_t r=atomic_load_explicit(&txr,memory_order_relaxed),w=atomic_load_explicit(&txw,memory_order_acquire);
    for(unsigned budget=0;r!=w&&budget<PT_NET_QUEUE;budget++){
        NetFrame *f=&tx[r%PT_NET_QUEUE];int error=0,count=1;
        if(fd>=0)error=pt_net_stream_send(fd,&stream,f->data,f->n);
        else {struct iovec v={f->data,f->n};struct vmpktdesc packet={.vm_pkt_size=f->n,.vm_pkt_iov=&v,.vm_pkt_iovcnt=1};vmnet_return_t ret=vmnet_write(iface,&packet,&count);if(ret!=VMNET_SUCCESS)error=EIO;else if(!count)error=EAGAIN;}
        if(error==EAGAIN||error==EWOULDBLOCK){nh_retry(self);break;}
        if(error){status=error;atomic_store_explicit(&up,0,memory_order_release);if(reader){dispatch_source_cancel(reader);reader=nil;}nh_wake(self);return;}
        r++;atomic_store_explicit(&txr,r,memory_order_release);nh_wake(self);
    }
    w=atomic_load_explicit(&rxw,memory_order_relaxed);r=atomic_load_explicit(&rxr,memory_order_acquire);BOOL received=NO;
    for(unsigned budget=0;budget<PT_NET_QUEUE;budget++){
        if(w-r>=PT_NET_QUEUE)break;NetFrame *f=&rx[w%PT_NET_QUEUE];int count=1;unsigned size=0;
        if(fd>=0){int n=pt_net_stream_receive(fd,&stream,f->data);
            if(!n)break;
            if(n<0){status=-n;atomic_store(&up,0);if(reader){dispatch_source_cancel(reader);reader=nil;}nh_wake(self);break;}
            size=(unsigned)n;
        }else {struct iovec v={f->data,sizeof f->data};struct vmpktdesc packet={.vm_pkt_size=sizeof f->data,.vm_pkt_iov=&v,.vm_pkt_iovcnt=1};vmnet_return_t ret=vmnet_read(iface,&packet,&count);
            if(ret!=VMNET_SUCCESS){status=ret;atomic_store(&up,0);nh_wake(self);break;}if(!count)break;if(packet.vm_pkt_size>sizeof f->data||packet.vm_pkt_size<14)continue;size=(unsigned)packet.vm_pkt_size;}
        f->n=size;w++;atomic_store_explicit(&rxw,w,memory_order_release);received=YES;
    }if(received)nh_wake(self);
}
- (void)dealloc{if(fd>=0)close(fd);}
@end
static int nh_send(void *opaque,const uint8_t *p,unsigned n){PTNetBackend *h=(__bridge PTNetBackend *)opaque;if(!atomic_load_explicit(&h->up,memory_order_acquire))return ENETDOWN;if(!p||n<14||n>PT_NET_FRAME)return EMSGSIZE;
    uint32_t w=atomic_load_explicit(&h->txw,memory_order_relaxed),r=atomic_load_explicit(&h->txr,memory_order_acquire);if(w-r>=PT_NET_QUEUE)return EAGAIN;
    NetFrame *f=&h->tx[w%PT_NET_QUEUE];memcpy(f->data,p,n);f->n=n;atomic_store_explicit(&h->txw,w+1,memory_order_release);dispatch_source_merge_data(h->pump,1);return 0;}
static int nh_receive(void *opaque,uint8_t *p,unsigned size){PTNetBackend *h=(__bridge PTNetBackend *)opaque;uint32_t r=atomic_load_explicit(&h->rxr,memory_order_relaxed),w=atomic_load_explicit(&h->rxw,memory_order_acquire);if(r==w)return 0;
    NetFrame *f=&h->rx[r%PT_NET_QUEUE];if(f->n>size)return -EMSGSIZE;memcpy(p,f->data,f->n);unsigned n=f->n;atomic_store_explicit(&h->rxr,r+1,memory_order_release);dispatch_source_merge_data(h->pump,1);return (int)n;}
static int nh_link(void *opaque){PTNetBackend *h=(__bridge PTNetBackend *)opaque;return atomic_load_explicit(&h->up,memory_order_acquire);}
PtNetIo pt_net_host_io(PtNetHost *h){return (PtNetIo){h,nh_send,nh_receive,nh_link};}
PtNetHost *pt_net_host_open(PtEvent *wake,_Atomic uint32_t *kick,uint8_t mac[6],int *status){
    PTNetBackend *h=[PTNetBackend new];h->wake=wake;h->kick=kick;dispatch_semaphore_t done=dispatch_semaphore_create(0);xpc_object_t desc=xpc_dictionary_create(NULL,NULL,0);
    xpc_dictionary_set_uint64(desc,vmnet_operation_mode_key,VMNET_SHARED_MODE);
    dispatch_sync(h->queue,^{h->iface=vmnet_start_interface(desc,h->queue,^(vmnet_return_t result,xpc_object_t info){
        h->started=YES;h->status=result;if(result==VMNET_SUCCESS){const char *s=xpc_dictionary_get_string(info,vmnet_mac_address_key);unsigned b[6];
            if(!s||sscanf(s,"%x:%x:%x:%x:%x:%x",b,b+1,b+2,b+3,b+4,b+5)!=6||b[0]>255||b[1]>255||b[2]>255||b[3]>255||b[4]>255||b[5]>255||(b[0]&1)||xpc_dictionary_get_uint64(info,vmnet_max_packet_size_key)>PT_NET_FRAME)h->status=EMSGSIZE;
            else {for(unsigned k=0;k<6;k++)h->mac[k]=(uint8_t)b[k];if(!h->closing){atomic_store(&h->up,1);[h arm];}}}
        if(h->closing&&h->iface)vmnet_stop_interface(h->iface,h->queue,^(vmnet_return_t r){(void)r;h->iface=NULL;});dispatch_semaphore_signal(done);
    });});
    if(!h->iface){if(status)*status=EACCES;return NULL;}
    if(dispatch_semaphore_wait(done,dispatch_time(DISPATCH_TIME_NOW,10*NSEC_PER_SEC))){dispatch_sync(h->queue,^{h->closing=YES;h->wake=NULL;h->kick=NULL;h->notify=NULL;h->notifyOpaque=NULL;atomic_store(&h->up,0);if(h->started&&h->iface)vmnet_stop_interface(h->iface,h->queue,^(vmnet_return_t result){(void)result;h->iface=NULL;});});if(status)*status=ETIMEDOUT;return NULL;}
    if(!atomic_load(&h->up)){int error=h->status;PtNetHost *ref=(__bridge_retained PtNetHost *)h;pt_net_host_close(ref);if(status)*status=error;return NULL;}
    memcpy(mac,h->mac,6);if(status)*status=0;return (__bridge_retained PtNetHost *)h;
}
/* Blocking handshake has one five-second deadline, including connect. All later
 * packet I/O is nonblocking on the backend queue. */
static int nh_transfer(int fd,void *bytes,unsigned n,int write,uint64_t end){
    while(n){struct timespec ts;clock_gettime(CLOCK_MONOTONIC,&ts);uint64_t now=(uint64_t)ts.tv_sec*1000000000+ts.tv_nsec;if(now>=end)return ETIMEDOUT;
        struct pollfd p={fd,write?POLLOUT:POLLIN,0};int r=poll(&p,1,(int)((end-now+999999)/1000000));if(r<0&&errno==EINTR)continue;if(r<=0)return r?errno:ETIMEDOUT;
        ssize_t k=write?send(fd,bytes,n,MSG_DONTWAIT):recv(fd,bytes,n,MSG_DONTWAIT);if(k<0&&(errno==EINTR||errno==EAGAIN))continue;if(k<=0)return k?errno:ECONNRESET;bytes=(uint8_t *)bytes+k;n-=(unsigned)k;
    }return 0;
}
PtNetHost *pt_net_host_socket(const char *path,PtEvent *wake,_Atomic uint32_t *kick,uint8_t mac[6],int *status){
    PTNetBackend *h=[PTNetBackend new];h->wake=wake;h->kick=kick;int error=0;struct sockaddr_un server={.sun_family=AF_UNIX};
    if(!path||path[0]!='/'||strlen(path)>=sizeof server.sun_path){if(status)*status=EINVAL;return NULL;}strcpy(server.sun_path,path);
    char directory[sizeof server.sun_path];strcpy(directory,path);char *slash=strrchr(directory,'/');*slash=0;
    struct stat parent,sock;char canonical[PATH_MAX];
    if(!realpath(directory,canonical)||strcmp(canonical,directory)||lstat(directory,&parent)||!S_ISDIR(parent.st_mode)||
       !((parent.st_uid==getuid()&&!(parent.st_mode&077))||(parent.st_uid==0&&(parent.st_mode&0777)==0711))||
       lstat(path,&sock)||!S_ISSOCK(sock.st_mode)||sock.st_uid!=getuid()||(sock.st_mode&077)){if(status)*status=EACCES;return NULL;}
    h->fd=socket(AF_UNIX,SOCK_STREAM,0);if(h->fd<0)error=errno;int one=1;
    if(!error&&(fcntl(h->fd,F_SETFL,O_NONBLOCK)||fcntl(h->fd,F_SETFD,FD_CLOEXEC)||setsockopt(h->fd,SOL_SOCKET,SO_NOSIGPIPE,&one,sizeof one)))error=errno;
    struct timespec ts;clock_gettime(CLOCK_MONOTONIC,&ts);uint64_t end=(uint64_t)ts.tv_sec*1000000000+ts.tv_nsec+5000000000ull;
    if(!error&&connect(h->fd,(struct sockaddr *)&server,sizeof server)){if(errno!=EINPROGRESS)error=errno;else {struct pollfd p={h->fd,POLLOUT,0};int ready=poll(&p,1,5000);socklen_t n=sizeof error;if(ready<=0)error=ready?errno:ETIMEDOUT;else if(getsockopt(h->fd,SOL_SOCKET,SO_ERROR,&error,&n))error=errno;}}
    uid_t uid;gid_t gid;if(!error&&(getpeereid(h->fd,&uid,&gid)||(uid!=0&&uid!=getuid())))error=EACCES;
    uint8_t hello[12]={'P','T','N','E','T','2',0,0};pt_write_le(hello+8,4,(unsigned)getpid());
    if(!error)error=nh_transfer(h->fd,hello,sizeof hello,1,end);
    if(!error){uint8_t reply[16];error=nh_transfer(h->fd,reply,sizeof reply,0,end);
        if(!error){unsigned mtu=(unsigned)pt_read_le(reply+12,4);if(memcmp(reply,"PTNET2",6)||(reply[6]&1)||!memcmp(reply+6,"\0\0\0\0\0\0",6)||mtu<576||mtu>PT_NET_FRAME-18)error=EPROTO;else memcpy(h->mac,reply+6,6);}}
    if(error){if(status)*status=error;return NULL;}
    atomic_store(&h->up,1);dispatch_sync(h->queue,^{[h arm];});memcpy(mac,h->mac,6);if(status)*status=0;return (__bridge_retained PtNetHost *)h;
}
void pt_net_host_notify(PtNetHost *ref,void (*notify)(void *),void *opaque){PTNetBackend *h=(__bridge PTNetBackend *)ref;dispatch_sync(h->queue,^{h->notify=notify;h->notifyOpaque=opaque;});}
int pt_net_host_close(PtNetHost *ref){if(!ref)return 0;PTNetBackend *h=(__bridge_transfer PTNetBackend *)ref;dispatch_semaphore_t done=dispatch_semaphore_create(0);__block int error=0;
    dispatch_sync(h->queue,^{h->closing=YES;atomic_store(&h->up,0);h->wake=NULL;h->kick=NULL;h->notify=NULL;h->notifyOpaque=NULL;
        if(h->pump){dispatch_source_cancel(h->pump);h->pump=nil;}if(h->reader){dispatch_source_cancel(h->reader);h->reader=nil;}
        if(h->fd>=0){shutdown(h->fd,SHUT_RDWR);close(h->fd);h->fd=-1;dispatch_semaphore_signal(done);}
        else if(h->iface){vmnet_return_t result=vmnet_stop_interface(h->iface,h->queue,^(vmnet_return_t r){error=r==VMNET_SUCCESS?0:EIO;h->iface=NULL;dispatch_semaphore_signal(done);});if(result!=VMNET_SUCCESS){error=EIO;dispatch_semaphore_signal(done);}}
        else dispatch_semaphore_signal(done);
    });
    if(dispatch_semaphore_wait(done,dispatch_time(DISPATCH_TIME_NOW,10*NSEC_PER_SEC)))return ETIMEDOUT;return error;
}
