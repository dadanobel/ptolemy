/* SPDX-License-Identifier: GPL-2.0-or-later */
/* The test is the guest: it drives Ethernet/IPv4/TCP frames through transmit()/receive() and a real worker thread
 * against a TCP server on 127.0.0.1, then fuzzes the parser directly. */
#define PT_NET_LOOPBACK 1
#include "../src/net_user.c"
#include <assert.h>
#include <signal.h>
enum {BULK=1<<20,SMALL=20000,UP=2<<20};
static PtNetIo io;static int listener;static uint16_t port;static unsigned guest_mss=1460;
static const uint8_t guest_mac[6]={2,0x50,0x54,0,0,15};
typedef struct {uint16_t sport,dport;uint32_t seq,ack;unsigned flags,wnd,len,mss;uint8_t data[1500];} Seg;
static uint8_t pattern(unsigned i){return (uint8_t)(i^i>>8^i>>16);}
static uint32_t pseudo(unsigned n){return (GUEST>>16)+(GUEST&65535)+0x7f00+1+6+n;}
static void out(uint16_t sport,uint16_t dport,uint32_t seq,uint32_t ack,unsigned flags,unsigned wnd,const void *d,unsigned n){
    uint8_t f[1514]={0};memcpy(f+6,guest_mac,6);w16(f+12,0x800);uint8_t *ip=f+14,*t=ip+20;unsigned head=flags&TSYN?24:20;
    ip[0]=0x45;w16(ip+2,20+head+n);ip[8]=64;ip[9]=6;w32(ip+12,GUEST);w32(ip+16,0x7f000001);w16(ip+10,sum(ip,20,0));
    w16(t,sport);w16(t+2,dport);w32(t+4,seq);w32(t+8,ack);t[12]=head*4;t[13]=flags;w16(t+14,wnd);if(flags&TSYN)w32(t+20,0x02040000|guest_mss);
    if(n)memcpy(t+head,d,n);w16(t+16,sum(t,head+n,pseudo(head+n)));while(io.send(io.opaque,f,34+head+n)==EAGAIN)usleep(100);
}
/* Next frame for the guest; every one must be a well-formed TCP segment from 127.0.0.1 with valid checksums. */
static int in(Seg *s,unsigned timeout){uint8_t f[PT_NET_FRAME];int n;uint64_t end=ms()+timeout;
    while((n=io.receive(io.opaque,f,sizeof f))<=0){if(ms()>end)return 0;usleep(100);}
    const uint8_t *ip=f+14,*t=ip+20;unsigned total=u16(ip+2),head=(t[12]>>4)*4;
    assert(n>=54&&!memcmp(f,guest_mac,6)&&!memcmp(f+6,router,6)&&u16(f+12)==0x800&&ip[0]==0x45&&ip[9]==6&&!sum(ip,20,0)&&u32(ip+12)==0x7f000001&&u32(ip+16)==GUEST);
    assert(total+14==(unsigned)n&&head>=20&&total>=20+head&&!sum(t,total-20,pseudo(total-20)));
    *s=(Seg){u16(t),u16(t+2),u32(t+4),u32(t+8),t[13],u16(t+14),total-20-head,head==24&&t[20]==2&&t[21]==4?u16(t+22):0,{0}};memcpy(s->data,t+head,s->len);return 1;
}
/* Next frame of an IP protocol, skipping leftovers such as late TCP retransmissions. */
static int frame(uint8_t *g,unsigned proto){int n;for(uint64_t until=ms()+3000;ms()<until;usleep(100))if((n=io.receive(io.opaque,g,PT_NET_FRAME))>0&&g[23]==proto)return n;return 0;}
static void expect(unsigned flags,uint32_t ack){Seg s;do assert(in(&s,3000));while(s.flags!=flags||s.ack!=ack);}
/* Guest active open; lose drops that many SYN-ACKs so the timer must resend them. Returns the first byte the host side sends. */
static uint32_t handshake(uint16_t sport,uint32_t isn,unsigned wnd,unsigned lose){Seg s;out(sport,port,isn,0,TSYN,wnd,NULL,0);
    for(unsigned i=0;i<=lose;i++){assert(in(&s,2000)&&s.flags==(TSYN|TACK)&&s.ack==isn+1&&s.mss==1460&&s.wnd==65535&&s.sport==port&&s.dport==sport);}
    out(sport,port,isn+1,s.seq+1,TACK,wnd,NULL,0);return s.seq+1;
}
/* Receiver that queues out-of-order bytes like a real stack: exactly want bytes (then the FIN if fin) are delivered,
 * every drop-th segment is lost, every segment must sit inside the advertised window and carry nothing past want. */
static void drain(uint16_t sport,uint32_t *rcv,uint32_t snd,uint8_t *buf,unsigned want,int fin,unsigned wnd,unsigned drop){
    static uint8_t have[BULK+1];unsigned count=0;uint32_t start=*rcv;Seg s;memset(have,0,want+1);
    while(*rcv!=start+want+!!fin){assert(in(&s,5000)&&s.sport==port&&s.dport==sport&&!(s.flags&(TSYN|TRST)));
        if(!s.len&&!(s.flags&TFIN))continue;if(drop&&++count%drop==0)continue;
        assert((int32_t)(s.seq+s.len-*rcv-wnd)<=0&&s.len<=1460);
        for(unsigned i=0;i<s.len;i++){uint32_t at=s.seq+i-start;assert(at<want||(int32_t)at<0);if(at<want){buf[at]=s.data[i];have[at]=1;}}
        if(s.flags&TFIN){assert(fin&&s.seq+s.len==start+want);have[want]=1;}
        while(*rcv-start<=want&&have[*rcv-start])(*rcv)++;
        out(sport,port,snd,*rcv,TACK,wnd,NULL,0);}
}
static void *server(void *arg){int mode=(int)(intptr_t)arg,fd=accept(listener,NULL,NULL);assert(fd>=0);static uint8_t b[65536];ssize_t n=0;
    if(mode=='e'){unsigned got=0;while(got<4&&(n=recv(fd,b+got,4-got,0))>0)got+=n;assert(got==4&&!memcmp(b,"ping",4)&&send(fd,"pong",4,0)==4&&!recv(fd,b,1,0)&&send(fd,"bye",3,0)==3);}
    if(mode=='b'||mode=='w')for(unsigned at=0,size=mode=='b'?BULK:SMALL;at<size;at+=n){unsigned k=size-at<sizeof b?size-at:sizeof b;for(unsigned i=0;i<k;i++)b[i]=pattern(at+i);assert((n=send(fd,b,k,0))>0);}
    /* Shrink the NAT's host socket (found by its port) so the stalled server really makes host writes block. */
    if(mode=='u'){struct sockaddr_in peer,me;socklen_t pl=sizeof peer,ml=sizeof me;int tiny=4096,found=0;assert(!getpeername(fd,(struct sockaddr *)&peer,&pl));
        for(int k=3;k<1024;k++,ml=sizeof me)if(k!=fd&&!getsockname(k,(struct sockaddr *)&me,&ml)&&me.sin_family==AF_INET&&me.sin_port==peer.sin_port)found=!setsockopt(k,SOL_SOCKET,SO_SNDBUF,&tiny,sizeof tiny);
        assert(found);usleep(300000);uint32_t count=0,bad=0;while((n=recv(fd,b,sizeof b,0))>0)for(ssize_t i=0;i<n;i++,count++)bad|=b[i]!=pattern(count);uint8_t r[4];w32(r,bad?0:count);assert(!n&&send(fd,r,4,0)==4);}
    if(mode=='r'){struct linger l={1,0};assert(recv(fd,b,1,0)==1&&!setsockopt(fd,SOL_SOCKET,SO_LINGER,&l,sizeof l));}
    if(mode=='g')assert(recv(fd,b,1,0)==1&&b[0]=='x'&&send(fd,"k",1,0)==1&&recv(fd,b,1,0)<0&&errno==ECONNRESET);
    close(fd);return NULL;
}
static pthread_t serve(int mode){pthread_t t;assert(!pthread_create(&t,NULL,server,(void *)(intptr_t)mode));return t;}
static uint16_t closed_port(void){int fd=socket(AF_INET,SOCK_STREAM,0);struct sockaddr_in sa={.sin_family=AF_INET,.sin_addr={htonl(0x7f000001)}};socklen_t l=sizeof sa;
    assert(!bind(fd,(struct sockaddr *)&sa,sizeof sa)&&!getsockname(fd,(struct sockaddr *)&sa,&l));close(fd);return ntohs(sa.sin_port);}
static void fuzz(uint16_t closed){
    PtUserNet *z=calloc(1,sizeof *z);assert(z&&!pthread_mutex_init(&z->lock,NULL));memcpy(z->mac,guest_mac,6);for(unsigned i=0;i<FLOWS;i++)z->flow[i].fd=-1;
    uint32_t seed=7;
#define RAND (seed=seed*1664525+1013904223,seed>>16)
    for(unsigned i=0;i<30000;i++){unsigned mode=i%3,L=mode==2?20+RAND%60:RAND%80;uint8_t *f=malloc(34+L),*ip=f+14,*t=ip+20;assert(f);
        memset(f,0,34);memcpy(f+6,guest_mac,6);w16(f+12,0x800);ip[0]=0x45;w16(ip+2,20+L);ip[8]=64;ip[9]=6;w32(ip+12,GUEST);w32(ip+16,0x7f000001);w16(ip+10,sum(ip,20,0));
        for(unsigned j=0;j<L;j++)t[j]=RAND;
        if(mode==0&&L&&!sum(t,L,pseudo(L)))t[0]^=1;    /* bad checksum */
        if(mode==1&&L>=20){if((t[12]>>4)*4u>=20&&(t[12]>>4)*4u<=L)t[12]=RAND%5<<4;w16(t+16,0);w16(t+16,sum(t,L,pseudo(L)));}    /* bad data offset */
        if(mode==2){t[12]=(20+4*(RAND%((L-20)/4+1)))*4;t[13]=TSYN;w16(t+2,closed);w16(t+16,0);w16(t+16,sum(t,L,pseudo(L)));}    /* SYN, junk options */
        packet(z,f,34+L);free(f);if(mode<2)assert(z->rw==z->rr);
        for(unsigned k=0;k<TCPS;k++)if(z->tcp[k].state){assert(z->tcp[k].state==1&&z->tcp[k].mss>=1&&z->tcp[k].mss<=1460);close(z->tcp[k].fd);z->tcp[k].state=0;}
        z->rr=z->rw;}
    /* Echo replies as each host delivers them: Linux bare with its own identifier, macOS behind an IP header. */
    int pair[2];assert(!socketpair(AF_UNIX,SOCK_DGRAM,0,pair));Flow *f=&z->flow[0];*f=(Flow){pair[0],1,0x1234,0,0x08080808,0};
    uint8_t m[48]={0,0,0,0,0x99,0x99,0,7},macos[20+sizeof m]={0x45};memcpy(macos+20,m,sizeof m);w16(macos+24,0x1234);
    unsigned before=z->rw;assert(send(pair[1],m,sizeof m,0)==sizeof m);flow_input(z,f);assert(z->rw==before+1);
    uint8_t *r=z->rx[before%PT_NET_QUEUE].data;assert(z->rx[before%PT_NET_QUEUE].n==34+sizeof m&&r[23]==1&&u32(r+26)==0x08080808&&u32(r+30)==GUEST&&!sum(r+14,20,0)&&!r[34]&&u16(r+38)==0x1234&&u16(r+40)==7&&!sum(r+34,sizeof m,0));
    assert(send(pair[1],macos,sizeof macos,0)==sizeof macos);flow_input(z,f);assert(z->rw==before+2&&!memcmp(z->rx[(before+1)%PT_NET_QUEUE].data+34,r+34,sizeof m));
    w16(macos+24,0x4321);assert(send(pair[1],macos,sizeof macos,0)==sizeof macos);flow_input(z,f);m[0]=3;assert(send(pair[1],m,sizeof m,0)==sizeof m);flow_input(z,f);assert(z->rw==before+2);
    close(pair[0]);close(pair[1]);pthread_mutex_destroy(&z->lock);free(z);
}
int main(void){
    signal(SIGPIPE,SIG_IGN);int small=4096;struct sockaddr_in sa={.sin_family=AF_INET,.sin_addr={htonl(0x7f000001)}};socklen_t l=sizeof sa;listener=socket(AF_INET,SOCK_STREAM,0);
    assert(!setsockopt(listener,SOL_SOCKET,SO_RCVBUF,&small,sizeof small)&&!bind(listener,(struct sockaddr *)&sa,sizeof sa)&&!listen(listener,128)&&!getsockname(listener,(struct sockaddr *)&sa,&l));port=ntohs(sa.sin_port);
    uint8_t mac[6];int error;PtUserNet *h=pt_user_net_open(NULL,NULL,mac,&error);assert(h&&!error&&!memcmp(mac,guest_mac,6));io=pt_user_net_io(h);
    Seg s;uint8_t buf[16];uint16_t closed=closed_port();uint32_t snd,rcv;pthread_t th;
    /* Closed port: RST|ACK acknowledging the SYN. */
    out(40000,closed,1000,0,TSYN,65535,NULL,0);assert(in(&s,3000)&&s.flags==(TRST|TACK)&&s.ack==1001&&s.sport==closed&&s.dport==40000);
    /* Request/response, lost SYN-ACK, guest FIN first, host data after it (half-close), host FIN; then the flow is gone. */
    th=serve('e');snd=5000;rcv=handshake(40001,snd,65535,1);snd++;
    out(40001,port,snd,rcv,TACK|TPSH,65535,"ping",4);snd+=4;drain(40001,&rcv,snd,buf,4,0,65535,0);assert(!memcmp(buf,"pong",4));
    out(40001,port,snd,rcv,TFIN|TACK,65535,NULL,0);snd++;expect(TACK,snd);drain(40001,&rcv,snd,buf,3,1,65535,0);assert(!memcmp(buf,"bye",3));pthread_join(th,NULL);
    out(40001,port,snd,rcv,TACK,65535,NULL,0);do assert(in(&s,3000));while(s.flags!=TRST);assert(s.seq==rcv);
    /* 1 MiB host->guest with every 7th segment lost: retransmission delivers each byte once, in order, inside the window. */
    th=serve('b');snd=7000;rcv=handshake(40002,snd,65535,0);snd++;uint8_t *big=malloc(BULK);assert(big);uint64_t t0=ms();
    drain(40002,&rcv,snd,big,BULK,1,65535,7);for(unsigned i=0;i<BULK;i++)assert(big[i]==pattern(i));unsigned bulk_ms=(unsigned)(ms()-t0);
    out(40002,port,snd,rcv,TFIN|TACK,65535,NULL,0);expect(TACK,snd+1);pthread_join(th,NULL);
    /* Guest MSS 536 and a 1000-byte window whose right edge stops at 5000, then zero: only probes until a probe's answer reopens it. */
    guest_mss=536;th=serve('w');snd=9000;rcv=handshake(40003,snd,1000,0);guest_mss=1460;snd++;
    uint32_t base=rcv,edge=rcv+1000;unsigned probes=0;
    while(rcv!=base+5000){assert(in(&s,3000));if(!s.len)continue;assert(s.len<=536&&(int32_t)(s.seq+s.len-edge)<=0&&!(s.flags&TFIN));
        if(s.seq==rcv){memcpy(big+(rcv-base),s.data,s.len);rcv+=s.len;}edge=rcv-base+1000>5000?base+5000:rcv+1000;out(40003,port,snd,rcv,TACK,edge-rcv,NULL,0);}
    for(uint64_t until=ms()+1000;ms()<until;)if(in(&s,50)){assert(!s.len&&s.seq==rcv-1&&!(s.flags&TFIN));probes++;out(40003,port,snd,rcv,TACK,0,NULL,0);}
    assert(probes>=2);do assert(in(&s,1000));while(s.len);out(40003,port,snd,rcv,TACK,1000,NULL,0);
    drain(40003,&rcv,snd,big+5000,SMALL-5000,1,1000,0);for(unsigned i=0;i<SMALL;i++)assert(big[i]==pattern(i));
    out(40003,port,snd,rcv,TFIN|TACK,1000,NULL,0);expect(TACK,snd+1);pthread_join(th,NULL);
    /* 2 MiB guest->host into a server that stalls first: out-of-order and overlapping segments, ACK only what the host took. */
    th=serve('u');snd=11000;rcv=handshake(40004,snd,65535,0);snd++;uint8_t *up=malloc(UP);assert(up);for(unsigned i=0;i<UP;i++)up[i]=pattern(i);
    out(40004,port,snd+1460,rcv,TACK,65535,up+1460,100);expect(TACK,snd);
    out(40004,port,snd,rcv,TACK,65535,up,100);expect(TACK,snd+100);out(40004,port,snd+50,rcv,TACK,65535,up+50,100);expect(TACK,snd+150);
    uint32_t una=snd+150,nxt=una,top=una,end=snd+UP;unsigned resent=0;uint64_t last=ms();
    while(una!=end){while(nxt!=end){unsigned n=end-nxt<1460?end-nxt:1460;if(nxt+n-una>65535)break;out(40004,port,nxt,rcv,TACK,65535,up+(nxt-snd),n);nxt+=n;if((int32_t)(nxt-top)>0)top=nxt;}
        if(in(&s,20)){assert(s.flags==TACK&&!s.len&&(int32_t)(s.ack-top)<=0);if((int32_t)(s.ack-una)>0){una=s.ack;last=ms();if((int32_t)(nxt-una)<0)nxt=una;}}
        if(ms()-last>100){nxt=una;last=ms();resent++;}}
    out(40004,port,end,rcv,TFIN|TACK,65535,NULL,0);expect(TACK,end+1);drain(40004,&rcv,end+1,buf,4,1,65535,0);assert(u32(buf)==UP);pthread_join(th,NULL);
    /* Host RST reaches the guest; guest RST reaches the host. */
    th=serve('r');snd=13000;rcv=handshake(40005,snd,65535,0);snd++;out(40005,port,snd,rcv,TACK|TPSH,65535,"x",1);snd++;pthread_join(th,NULL);expect(TRST|TACK,snd);
    th=serve('g');snd=15000;rcv=handshake(40006,snd,65535,0);snd++;out(40006,port,snd,rcv,TACK|TPSH,65535,"x",1);snd++;
    drain(40006,&rcv,snd,buf,1,0,65535,0);assert(buf[0]=='k');out(40006,port,snd,rcv,TRST|TACK,0,NULL,0);pthread_join(th,NULL);
    /* Table full: TCPS open flows (left in the listen backlog), one more SYN is reset; resets free the table again. */
    uint8_t open[TCPS]={0};unsigned opened=0;for(unsigned i=0;i<TCPS;i++)out(41000+i,port,100,0,TSYN,65535,NULL,0);
    while(opened<TCPS){assert(in(&s,3000)&&s.flags==(TSYN|TACK)&&s.dport-41000u<TCPS);opened+=!open[s.dport-41000];open[s.dport-41000]=1;}
    for(int reset=0;!reset;){out(41000+TCPS,port,100,0,TSYN,65535,NULL,0);    /* resent like a guest would: the RST may meet a full queue */
        for(uint64_t until=ms()+500;!reset&&in(&s,500)&&ms()<until;)if(s.flags!=(TSYN|TACK)){assert(s.flags==(TRST|TACK)&&s.dport==41000+TCPS&&s.ack==101);reset=1;}}
    for(unsigned i=0;i<TCPS;i++)out(41000+i,port,101,0,TRST,0,NULL,0);out(42000,port,100,0,TSYN,65535,NULL,0);expect(TSYN|TACK,101);out(42000,port,101,0,TRST,0,NULL,0);
    /* UDP still relays through the shared flow path: guest datagram out, host reply back with a valid checksum. */
    int u=socket(AF_INET,SOCK_DGRAM,0);struct timeval tv={3,0};struct sockaddr_in ua=sa,from;ua.sin_port=0;socklen_t ul=sizeof ua;char word[8];uint8_t g[PT_NET_FRAME],d[46]={0},*dip=d+14,*du=dip+20;
    assert(!setsockopt(u,SOL_SOCKET,SO_RCVTIMEO,&tv,sizeof tv)&&!bind(u,(struct sockaddr *)&ua,sizeof ua)&&!getsockname(u,(struct sockaddr *)&ua,&ul));
    memcpy(d+6,guest_mac,6);w16(d+12,0x800);dip[0]=0x45;w16(dip+2,32);dip[8]=64;dip[9]=17;w32(dip+12,GUEST);w32(dip+16,0x7f000001);w16(dip+10,sum(dip,20,0));
    w16(du,5353);w16(du+2,ntohs(ua.sin_port));w16(du+4,12);memcpy(du+8,"ping",4);assert(!io.send(io.opaque,d,sizeof d));
    ul=sizeof from;assert(recvfrom(u,word,sizeof word,0,(struct sockaddr *)&from,&ul)==4&&!memcmp(word,"ping",4)&&sendto(u,"pong",4,0,(struct sockaddr *)&from,ul)==4);close(u);
    assert(frame(g,17)==46&&u32(g+26)==0x7f000001&&u16(g+34)==ntohs(ua.sin_port)&&u16(g+36)==5353&&!memcmp(g+42,"pong",4)&&!sum(g+34,12,pseudo(12)-6+17));
    /* Real echo through the host when unprivileged ICMP sockets are allowed (macOS; Linux only with ping_group_range). */
    int probe=socket(AF_INET,SOCK_DGRAM,IPPROTO_ICMP),icmp=probe>=0;if(icmp){close(probe);
        uint8_t f[14+20+16]={0},*ip=f+14,*m=ip+20;memcpy(f+6,guest_mac,6);w16(f+12,0x800);ip[0]=0x45;w16(ip+2,36);ip[8]=64;ip[9]=1;w32(ip+12,GUEST);w32(ip+16,0x7f000001);w16(ip+10,sum(ip,20,0));
        m[0]=8;w16(m+4,0xbeef);w16(m+6,1);memcpy(m+8,"pingpong",8);w16(m+2,sum(m,16,0));assert(!io.send(io.opaque,f,sizeof f));
        assert(frame(g,1)==(int)sizeof f&&!g[34]&&u16(g+38)==0xbeef&&u16(g+40)==1&&!memcmp(g+42,"pingpong",8)&&!sum(g+34,16,0));}
    pt_user_net_close(h);close(listener);free(big);free(up);fuzz(closed);
    printf("PASS: TCP NAT handshake, SYN-ACK retransmit, closed-port RST, request/response, half-close and FIN both ways, 1 MiB with 1/7 loss in %u ms,"
        " MSS/window/zero-window probes, 2 MiB upload with host backpressure (%u guest go-back-N rounds), RST both ways, %u-flow table, UDP relay, ICMP %s, fuzzed parsing\n",
        bulk_ms,resent,TCPS,icmp?"host echo + reply translation":"reply translation (host echo sockets unavailable)");
}
