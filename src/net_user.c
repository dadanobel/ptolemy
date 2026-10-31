/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "net_user.h"
#include <arpa/inet.h>
#include <sys/socket.h>
#include <poll.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#ifndef PT_NET_LOOPBACK
#define PT_NET_LOOPBACK 0   /* the host's own 127/8 services stay out of the guest's reach; r1-tcp's server sets 1 */
#endif
enum {FLOWS=32,TCPS=64,RING=65536,RTO=300,TFIN=1,TSYN=2,TRST=4,TPSH=8,TACK=16};
#define GATEWAY 0x0a4d0002u
#define GUEST 0x0a4d000fu
static const uint8_t router[6]={2,0x50,0x54,0,0,2};
typedef struct {unsigned n;uint8_t data[PT_NET_FRAME];} Frame;
typedef struct {int fd;uint8_t proto;uint16_t sport,dport;uint32_t remote;uint64_t used;} Flow;
/* Guest-initiated TCP. state: 0 free, 1 host connect pending, 2 SYN-ACK sent, 3 open. ring[i] holds host bytes
 * [una,top) indexed by sequence number, nxt is the next to send. fin: 1 guest FIN relayed, 2 host EOF (our FIN is top). */
typedef struct {int fd;unsigned state,fin,dups;uint16_t sport,dport,wnd,mss;uint32_t remote,una,nxt,top,ack,recover;uint64_t used,timer;} Tcp;
struct PtUserNet {
    pthread_mutex_t lock;pthread_t worker;_Atomic int stop;int pipe[2];
    unsigned tw,tr,rw,rr;Frame tx[PT_NET_QUEUE],rx[PT_NET_QUEUE];Flow flow[FLOWS];Tcp tcp[TCPS];
    PtEvent *wake;_Atomic uint32_t *kick;_Atomic uint16_t web;uint8_t mac[6];uint32_t dns;uint16_t identification;uint8_t ring[TCPS][RING];
};
static uint16_t u16(const uint8_t *p){return (uint16_t)p[0]<<8|p[1];}
static uint32_t u32(const uint8_t *p){return (uint32_t)u16(p)<<16|u16(p+2);}
static void w16(uint8_t *p,uint16_t v){p[0]=v>>8;p[1]=v;}
static void w32(uint8_t *p,uint32_t v){w16(p,v>>16);w16(p+2,v);}
static uint16_t sum(const uint8_t *p,unsigned n,uint32_t v){for(;n>1;n-=2,p+=2)v+=u16(p);if(n)v+=(unsigned)*p<<8;while(v>>16)v=(v&65535)+(v>>16);return (uint16_t)~v;}
static uint64_t ms(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return t.tv_sec*1000ull+t.tv_nsec/1000000;}
static int publish(PtUserNet *h,const uint8_t *p,unsigned n){
    pthread_mutex_lock(&h->lock);int ok=h->rw-h->rr<PT_NET_QUEUE;if(ok){Frame *f=&h->rx[h->rw++%PT_NET_QUEUE];f->n=n;memcpy(f->data,p,n);}pthread_mutex_unlock(&h->lock);
    if(h->kick)atomic_store_explicit(h->kick,1,memory_order_release);if(h->wake)pt_event_signal(h->wake);return ok;
}
/* Ethernet and IPv4 headers for n payload bytes from a remote host; returns the transport pseudo-header sum. */
static uint32_t ipv4(PtUserNet *h,uint8_t *p,uint32_t from,uint32_t to,unsigned proto,unsigned n){
    memcpy(p,to==UINT32_MAX?(const uint8_t *)"\xff\xff\xff\xff\xff\xff":h->mac,6);memcpy(p+6,router,6);w16(p+12,0x800);uint8_t *ip=p+14;
    ip[0]=0x45;w16(ip+2,n+20);w16(ip+4,++h->identification);ip[8]=64;ip[9]=proto;w32(ip+12,from);w32(ip+16,to);w16(ip+10,sum(ip,20,0));
    return (from>>16)+(from&65535)+(to>>16)+(to&65535)+proto+n;
}
static void udp_reply(PtUserNet *h,uint32_t remote,uint16_t sport,uint16_t dport,const uint8_t *bytes,unsigned n,int broadcast){
    if(n>1472)return;uint8_t packet[1514]={0},*udp=packet+34;uint32_t pseudo=ipv4(h,packet,remote,broadcast?UINT32_MAX:GUEST,17,n+8);
    w16(udp,sport);w16(udp+2,dport);w16(udp+4,n+8);memcpy(udp+8,bytes,n);uint16_t c=sum(udp,n+8,pseudo);w16(udp+6,c?c:65535);publish(h,packet,n+42);
}
static int sock(int type,int proto,uint32_t remote,uint16_t port){int fd=socket(AF_INET,type,proto),one=1;struct sockaddr_in sa={.sin_family=AF_INET,.sin_port=htons(port),.sin_addr={htonl(remote)}};
    if(fd>=0&&(fcntl(fd,F_SETFL,O_NONBLOCK)||fcntl(fd,F_SETFD,FD_CLOEXEC)||setsockopt(fd,SOL_SOCKET,SO_NOSIGPIPE,&one,sizeof one)||(connect(fd,(struct sockaddr *)&sa,sizeof sa)&&errno!=EINPROGRESS))){close(fd);fd=-1;}return fd;}
/* One segment to the guest; a SYN carries MSS 1460, data comes from the ring at seq. Fixed window, no scaling. */
static int segment(PtUserNet *h,const Tcp *c,unsigned flags,uint32_t seq,unsigned n){
    uint8_t p[1514]={0},*t=p+34;unsigned head=flags&TSYN?24:20;uint32_t pseudo=ipv4(h,p,c->remote,GUEST,6,head+n);
    w16(t,c->dport);w16(t+2,c->sport);w32(t+4,seq);w32(t+8,c->ack);t[12]=head*4;t[13]=flags;w16(t+14,65535);if(flags&TSYN)w32(t+20,0x020405b4);
    for(unsigned i=0;i<n;i++)t[head+i]=h->ring[c-h->tcp][(seq+i)%RING];w16(t+16,sum(t,head+n,pseudo));return publish(h,p,34+head+n);
}
/* Send what fits at seq (MSS, ring top, guest window); FIN rides after the last byte. Returns sequence space sent. */
static unsigned emit(PtUserNet *h,Tcp *c,uint32_t seq,uint64_t now){
    unsigned n=c->top-seq;int32_t room=(int32_t)(c->una+c->wnd-seq);if(n>c->mss)n=c->mss;if(room<0)room=0;if(n>(unsigned)room)n=room;
    unsigned fin=seq+n==c->top&&c->fin&2;if(!n&&!fin)return 0;if(seq==c->una)c->timer=now;
    return segment(h,c,TACK|(n?TPSH:0)|(fin?TFIN:0),seq,n)?n+fin:0;
}
static void push(PtUserNet *h,Tcp *c,uint64_t now){for(unsigned n;c->state==3&&c->nxt!=c->top+(c->fin>>1)&&(n=emit(h,c,c->nxt,now));)c->nxt+=n;}
/* how: 0 graceful close, 1 abort the host side, 2 also reset the guest. */
static void bye(PtUserNet *h,Tcp *c,int how){if(how>1)segment(h,c,TRST|TACK,c->nxt,0);struct linger l={1,0};if(how)setsockopt(c->fd,SOL_SOCKET,SO_LINGER,&l,sizeof l);close(c->fd);c->state=0;}
static void tcp(PtUserNet *h,uint32_t remote,const uint8_t *t,unsigned n){
    if(n<20||t[12]>>4<5||(t[12]>>4)*4u>n)return;unsigned off=(t[12]>>4)*4,flags=t[13],len=n-off,wnd=u16(t+14);
    uint16_t sport=u16(t),dport=u16(t+2);uint32_t seq=u32(t+4),ack=u32(t+8);uint64_t now=ms();Tcp *c=NULL,*spare=NULL;
    for(unsigned i=0;i<TCPS&&!c;i++){Tcp *x=&h->tcp[i];if(x->state&&x->sport==sport&&x->dport==dport&&x->remote==remote)c=x;else if(!x->state&&!spare)spare=x;}
    /* Plain HTTP goes to the web gateway for old browsers (web_macos.m) when it runs: the guest still sees its own peer. */
    uint16_t web=dport==80?atomic_load(&h->web):0;
    if(!c){Tcp r={.remote=remote,.sport=sport,.dport=dport,.ack=seq+len+!!(flags&TSYN)+!!(flags&TFIN)};if(flags&TRST)return;
        if((flags&(TSYN|TACK|TFIN))!=TSYN||!spare||(spare->fd=sock(SOCK_STREAM,0,web?0x7f000001:remote,web?web:dport))<0){segment(h,&r,flags&TACK?TRST:TRST|TACK,flags&TACK?ack:0,0);return;}
        unsigned mss=536;for(unsigned i=20;i<off&&t[i];){if(t[i]==1){i++;continue;}if(off-i<2||t[i+1]<2||t[i+1]>off-i)break;if(t[i]==2&&t[i+1]==4)mss=u16(t+i+2);i+=t[i+1];}
        uint32_t iss=arc4random();*spare=(Tcp){.fd=spare->fd,.state=1,.sport=sport,.dport=dport,.wnd=wnd,.mss=mss&&mss<1460?mss:1460,.remote=remote,.una=iss+1,.nxt=iss+1,.top=iss+1,.ack=seq+1,.used=now};return;}
    c->used=now;if(flags&TRST){bye(h,c,1);return;}
    if(flags&TSYN){if(c->state>1)segment(h,c,c->state==2?TSYN|TACK:TACK,c->state==2?c->una-1:c->nxt,0);return;}
    if(!(flags&TACK)||c->state<2||(c->state==2&&ack!=c->una))return;
    c->state=3;uint32_t a=ack-c->una,end=c->top+(c->fin>>1);
    /* New ACK: during fast recovery a partial ACK resends the next hole at once. Third duplicate: fast retransmit. */
    if(a&&a<=end-c->una){c->una=ack;c->timer=now;if((int32_t)(c->nxt-ack)<0)c->nxt=ack;if(c->dups<3||(int32_t)(c->recover-ack)<=0)c->dups=0;else emit(h,c,ack,now);}
    else if(!a&&!len&&!(flags&TFIN)&&wnd==c->wnd&&c->nxt!=c->una&&++c->dups==3){c->recover=c->nxt;emit(h,c,c->una,now);}
    c->wnd=wnd;
    /* In-order guest bytes are acknowledged only once the host socket took them; the guest retransmits the rest. */
    if(len||flags&TFIN){int32_t d=(int32_t)(c->ack-seq);
        if(d>=0&&(unsigned)d<=len&&!(c->fin&1)){ssize_t w=(unsigned)d<len?send(c->fd,t+off+d,len-d,0):0;if(w<0&&errno!=EAGAIN){bye(h,c,2);return;}
            if(w>0)c->ack+=w;if(flags&TFIN&&w==(ssize_t)(len-d)){c->ack++;c->fin|=1;shutdown(c->fd,SHUT_WR);}}
        segment(h,c,TACK,c->nxt,0);}
    if(c->fin==3&&c->una==c->top+1){bye(h,c,0);return;}
    push(h,c,now);
}
static void dhcp(PtUserNet *h,const uint8_t *b,unsigned n){
    if(n<240||b[0]!=1||b[1]!=1||b[2]!=6||memcmp(b+28,h->mac,6)||u32(b+236)!=0x63825363)return;
    unsigned type=0;uint32_t requested=0,server=0;for(unsigned i=240;i<n;){unsigned code=b[i++];if(code==255)break;if(!code)continue;if(i>=n)return;unsigned len=b[i++];if(len>n-i)return;
        if(code==53&&len==1)type=b[i];if(code==50&&len==4)requested=u32(b+i);if(code==54&&len==4)server=u32(b+i);i+=len;}
    if((type!=1&&type!=3)||(server&&server!=GATEWAY))return;
    uint8_t out[300]={0};out[0]=2;out[1]=1;out[2]=6;memcpy(out+4,b+4,4);memcpy(out+10,b+10,2);w32(out+16,GUEST);w32(out+20,GATEWAY);memcpy(out+28,h->mac,6);w32(out+236,0x63825363);
    unsigned at=240;out[at++]=53;out[at++]=1;out[at++]=type==1?2:(requested&&requested!=GUEST)?6:5;
    out[at++]=54;out[at++]=4;w32(out+at,GATEWAY);at+=4;
    const uint8_t codes[]={1,3,6,51};uint32_t values[]={0xffffff00,GATEWAY,h->dns,3600};
    for(unsigned i=0;i<4;i++){out[at++]=codes[i];out[at++]=4;w32(out+at,values[i]);at+=4;}out[at++]=255;
    udp_reply(h,GATEWAY,67,68,out,sizeof out,1);
}
static void packet(PtUserNet *h,const uint8_t *b,unsigned n){
    if(n<14||memcmp(b+6,h->mac,6))return;
    if(u16(b+12)==0x806){if(n<42||u16(b+14)!=1||u16(b+16)!=0x800||b[18]!=6||b[19]!=4||u16(b+20)!=1||memcmp(b+22,h->mac,6)||u32(b+38)!=GATEWAY)return;
        uint8_t arp[42];memcpy(arp,h->mac,6);memcpy(arp+6,router,6);w16(arp+12,0x806);memcpy(arp+14,b+14,8);w16(arp+20,2);memcpy(arp+22,router,6);w32(arp+28,GATEWAY);memcpy(arp+32,h->mac,6);memcpy(arp+38,b+28,4);publish(h,arp,sizeof arp);return;}
    if(u16(b+12)!=0x800||n<34)return;const uint8_t *ip=b+14;unsigned ihl=(ip[0]&15)*4,total=u16(ip+2),proto=ip[9];
    if(ip[0]>>4!=4||ihl<20||ihl>60||total<ihl+8||total>n-14||sum(ip,ihl,0)||u16(ip+6)&0x3fff||(proto!=1&&proto!=6&&proto!=17))return;
    uint32_t source=u32(ip+12),destination=u32(ip+16);const uint8_t *udp=ip+ihl;unsigned bytes=proto==17?u16(udp+4):total-ihl;if(bytes<8||bytes>total-ihl)return;
    uint32_t pseudo=(source>>16)+(source&65535)+(destination>>16)+(destination&65535)+proto+bytes;if((proto!=17||u16(udp+6))&&sum(udp,bytes,proto==1?0:pseudo))return;
    if(proto==17&&u16(udp)==68&&u16(udp+2)==67){dhcp(h,udp+8,bytes-8);return;}
    if(source!=GUEST||destination>>24==0||destination>>24>=224||destination==GATEWAY||destination>>8==GUEST>>8)return;
    if(proto==6){if(destination>>24!=127||PT_NET_LOOPBACK)tcp(h,destination,udp,bytes);return;}
    /* UDP and ICMP echo: one connected host datagram socket per flow; an echo flow is keyed by its identifier. */
    uint16_t sport=u16(udp+(proto==1?4:0)),dport=proto==1?0:u16(udp+2);if(bytes>1480||(proto==1?udp[0]!=8||udp[1]:!sport||!dport))return;
    uint64_t now=ms();Flow *slot=NULL;for(unsigned i=0;i<FLOWS;i++){Flow *f=&h->flow[i];if(f->fd>=0&&now-f->used>30000){close(f->fd);f->fd=-1;}
        if(f->fd>=0&&f->proto==proto&&f->sport==sport&&f->dport==dport&&f->remote==destination){slot=f;break;}}
    if(!slot)for(unsigned i=0;i<FLOWS;i++)if(h->flow[i].fd<0){slot=&h->flow[i];break;}if(!slot)return;
    if(slot->fd<0){if((slot->fd=sock(SOCK_DGRAM,proto==1?IPPROTO_ICMP:0,destination,dport))<0)return;slot->proto=proto;slot->sport=sport;slot->dport=dport;slot->remote=destination;}
    slot->used=now;(void)send(slot->fd,udp+(proto==1?0:8),bytes-(proto==1?0:8),0);
}
/* Unprivileged echo sockets: macOS prepends the IP header and shows every reply from the peer, Linux rewrites the identifier. */
static void flow_input(PtUserNet *h,Flow *f){uint8_t b[1541],q[1514]={0};ssize_t n=recv(f->fd,b,sizeof b,0);
    if(f->proto!=1){if(n>=0&&n<=1472)udp_reply(h,f->remote,f->dport,f->sport,b,(unsigned)n,0);return;}
    unsigned skip=n>=20&&b[0]>>4==4?(b[0]&15)*4:0;uint8_t *m=b+skip;if(n<(ssize_t)skip+8||n-skip>1480||m[0]||m[1]||(skip&&u16(m+4)!=f->sport))return;
    n-=skip;ipv4(h,q,f->remote,GUEST,1,n);memcpy(q+34,m,n);w16(q+38,f->sport);w16(q+36,0);w16(q+36,sum(q+34,n,0));publish(h,q,34+n);
}
static void *worker(void *opaque){PtUserNet *h=opaque;int wait=1000;while(!atomic_load(&h->stop)){
    struct pollfd p[1+FLOWS+TCPS]={{h->pipe[0],POLLIN,0}};for(unsigned i=0;i<FLOWS;i++)p[i+1]=(struct pollfd){h->flow[i].fd,POLLIN,0};
    for(unsigned i=0;i<TCPS;i++){Tcp *c=&h->tcp[i];short e=c->state==1?POLLOUT:c->state==3&&!(c->fin&2)&&c->top-c->una<RING?POLLIN:0;p[1+FLOWS+i]=(struct pollfd){e?c->fd:-1,e,0};}
    int ready=poll(p,1+FLOWS+TCPS,wait);if(ready<0&&errno!=EINTR)break;
    if(p[0].revents){uint8_t wake[128];while(read(h->pipe[0],wake,sizeof wake)>0){}}
    for(unsigned i=0;i<FLOWS;i++)if(p[i+1].revents&POLLIN)flow_input(h,&h->flow[i]);
    uint64_t now=ms();for(unsigned i=0;i<TCPS;i++){Tcp *c=&h->tcp[i];if(!p[1+FLOWS+i].revents)continue;int e=0;socklen_t l=sizeof e;
        if(c->state==1){if(getsockopt(c->fd,SOL_SOCKET,SO_ERROR,&e,&l)||e)bye(h,c,2);else{c->state=2;c->timer=now;segment(h,c,TSYN|TACK,c->una-1,0);}continue;}
        unsigned at=c->top%RING,room=RING-(c->top-c->una);ssize_t r=recv(c->fd,h->ring[i]+at,room<RING-at?room:RING-at,0);
        if(r>0){c->top+=r;c->used=now;}else if(!r)c->fin|=2;else if(errno!=EAGAIN){bye(h,c,2);continue;}push(h,c,now);}
    for(unsigned i=0;i<PT_NET_QUEUE;i++){Frame f;pthread_mutex_lock(&h->lock);int available=h->tr!=h->tw;if(available)f=h->tx[h->tr++%PT_NET_QUEUE];pthread_mutex_unlock(&h->lock);if(!available)break;packet(h,f.data,f.n);}
    now=ms();wait=1000;for(unsigned i=0;i<FLOWS;i++)if(h->flow[i].fd>=0&&now-h->flow[i].used>30000){close(h->flow[i].fd);h->flow[i].fd=-1;}
    /* Idle timeout, then retransmission: SYN-ACK, go-back-N from una, or a window probe below una while the guest window is zero. */
    for(unsigned i=0;i<TCPS;i++){Tcp *c=&h->tcp[i];if(!c->state)continue;if(now-c->used>(c->state==3&&!c->fin?7200000u:240000u)){bye(h,c,2);continue;}
        if(c->state==2||(c->state==3&&c->una!=c->top+(c->fin>>1))){
            if(now-c->timer>=RTO){c->timer=now;if(c->state==2)segment(h,c,TSYN|TACK,c->una-1,0);else{c->nxt=c->una;push(h,c,now);if(c->nxt==c->una)segment(h,c,TACK,c->una-1,0);}}
            if(c->timer+RTO-now<(uint64_t)wait)wait=(int)(c->timer+RTO-now);}}
    }return NULL;
}
static int transmit(void *opaque,const uint8_t *b,unsigned n){PtUserNet *h=opaque;if(!b||n<14||n>PT_NET_FRAME)return EMSGSIZE;pthread_mutex_lock(&h->lock);int rc=EAGAIN;
    if(h->tw-h->tr<PT_NET_QUEUE){Frame *f=&h->tx[h->tw++%PT_NET_QUEUE];f->n=n;memcpy(f->data,b,n);rc=0;}pthread_mutex_unlock(&h->lock);uint8_t wake=1;(void)!write(h->pipe[1],&wake,1);return rc;}
static int receive(void *opaque,uint8_t *b,unsigned n){PtUserNet *h=opaque;pthread_mutex_lock(&h->lock);int size=0;if(h->rw!=h->rr){Frame *f=&h->rx[h->rr%PT_NET_QUEUE];if(f->n>n)size=-EMSGSIZE;else {memcpy(b,f->data,f->n);size=f->n;h->rr++;}}pthread_mutex_unlock(&h->lock);return size;}
static int link_up(void *p){return !atomic_load(&((PtUserNet *)p)->stop);}
void pt_user_net_web(PtUserNet *h,uint16_t port){atomic_store(&h->web,port);}
PtNetIo pt_user_net_io(PtUserNet *h){return (PtNetIo){h,transmit,receive,link_up};}
PtUserNet *pt_user_net_open(PtEvent *wake,_Atomic uint32_t *kick,uint8_t mac[6],int *error){
    uint32_t dns=0;FILE *f=fopen("/etc/resolv.conf","r");if(f){char line[512],ip[64];while(fgets(line,sizeof line,f))if(sscanf(line,"nameserver %63s",ip)==1){struct in_addr a;if(inet_pton(AF_INET,ip,&a)==1){dns=ntohl(a.s_addr);break;}}fclose(f);}
    /* no nameserver (the Mac offline): the lease names none, the rest still works */
    PtUserNet *h=calloc(1,sizeof *h);if(!h){if(error)*error=ENOMEM;return NULL;}
    if(pthread_mutex_init(&h->lock,NULL)){free(h);if(error)*error=ENOMEM;return NULL;}if(pipe(h->pipe)){pthread_mutex_destroy(&h->lock);free(h);if(error)*error=errno;return NULL;}
    for(unsigned i=0;i<2;i++)if(fcntl(h->pipe[i],F_SETFL,O_NONBLOCK)||fcntl(h->pipe[i],F_SETFD,FD_CLOEXEC)){int e=errno;close(h->pipe[0]);close(h->pipe[1]);pthread_mutex_destroy(&h->lock);free(h);if(error)*error=e;return NULL;}
    atomic_init(&h->stop,0);for(unsigned i=0;i<FLOWS;i++)h->flow[i].fd=-1;
    const uint8_t address[6]={2,0x50,0x54,0,0,15};memcpy(h->mac,address,6);h->dns=dns;h->wake=wake;h->kick=kick;
    int rc=pthread_create(&h->worker,NULL,worker,h);if(rc){close(h->pipe[0]);close(h->pipe[1]);pthread_mutex_destroy(&h->lock);free(h);if(error)*error=rc;return NULL;}memcpy(mac,h->mac,6);if(error)*error=0;return h;
}
void pt_user_net_close(PtUserNet *h){if(!h)return;atomic_store(&h->stop,1);uint8_t b=1;(void)!write(h->pipe[1],&b,1);pthread_join(h->worker,NULL);
    for(unsigned i=0;i<FLOWS;i++)if(h->flow[i].fd>=0)close(h->flow[i].fd);for(unsigned i=0;i<TCPS;i++)if(h->tcp[i].state)close(h->tcp[i].fd);
    close(h->pipe[0]);close(h->pipe[1]);pthread_mutex_destroy(&h->lock);free(h);
}
