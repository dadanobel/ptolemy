/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "../src/net_user.c"
#include <assert.h>
int main(void){
    PtUserNet *h=calloc(1,sizeof *h);assert(h);assert(!pthread_mutex_init(&h->lock,NULL));h->dns=0xc0a80101;const uint8_t mac[6]={2,3,4,5,6,7};memcpy(h->mac,mac,6);for(unsigned i=0;i<FLOWS;i++)h->flow[i].fd=-1;
    uint8_t arp[42]={0};memcpy(arp+6,mac,6);w16(arp+12,0x806);w16(arp+14,1);w16(arp+16,0x800);arp[18]=6;arp[19]=4;w16(arp+20,1);memcpy(arp+22,mac,6);w32(arp+28,GUEST);w32(arp+38,GATEWAY);
    packet(h,arp,sizeof arp);assert(h->rw==1);Frame *f=&h->rx[0];assert(f->n==42&&u16(f->data+20)==2&&u32(f->data+28)==GATEWAY&&!memcmp(f->data,mac,6));arp[19]=5;packet(h,arp,sizeof arp);assert(h->rw==1);
    uint8_t b[300]={1,1,6};memcpy(b+28,mac,6);w32(b+4,0xabc123);w32(b+236,0x63825363);b[240]=53;b[241]=1;b[242]=1;b[243]=255;
    dhcp(h,b,sizeof b);assert(h->rw==2);f=&h->rx[1];assert(u32(f->data+42+16)==GUEST&&f->data[42+242]==2&&u32(f->data+42+4)==0xabc123);assert(!sum(f->data+14,20,0));
    const uint8_t *ip=f->data+14,*udp=ip+20;unsigned n=u16(udp+4);uint32_t source=u32(ip+12),dest=u32(ip+16);assert(!sum(udp,n,(source>>16)+(source&65535)+(dest>>16)+(dest&65535)+17+n));
    b[242]=3;dhcp(h,b,sizeof b);assert(h->rx[2].data[42+242]==5);b[243]=50;b[244]=4;w32(b+245,GUEST+1);b[249]=255;dhcp(h,b,sizeof b);assert(h->rx[3].data[42+242]==6);
    b[236]=0;dhcp(h,b,sizeof b);assert(h->rw==4);b[236]=0x63;b[241]=255;dhcp(h,b,250);assert(h->rw==4);
    b[241]=1;b[242]=1;b[243]=255;
    uint8_t request[342]={0};memcpy(request+6,mac,6);w16(request+12,0x800);uint8_t *request_ip=request+14,*request_udp=request_ip+20;
    request_ip[0]=0x45;request_ip[8]=64;request_ip[9]=17;w16(request_ip+2,328);w32(request_ip+16,UINT32_MAX);w16(request_ip+10,sum(request_ip,20,0));
    w16(request_udp,68);w16(request_udp+2,67);w16(request_udp+4,308);memcpy(request_udp+8,b,300);packet(h,request,sizeof request);assert(h->rw==5);
    request_ip[10]^=1;packet(h,request,sizeof request);assert(h->rw==5);request_ip[10]^=1;
    w16(request_udp+6,123);packet(h,request,sizeof request);assert(h->rw==5);w16(request_udp+6,0);
    unsigned count=h->rw;for(unsigned n=0;n<42;n++)packet(h,arp,n);assert(h->rw==count);
    uint32_t seed=17;for(unsigned i=0;i<10000;i++){uint8_t fuzz[64];for(unsigned j=0;j<sizeof fuzz;j++){seed=seed*1664525+1013904223;fuzz[j]=seed>>24;}packet(h,fuzz,i%64);}assert(h->rw==count);
    uint8_t syn[54]={0};memcpy(syn+6,mac,6);w16(syn+12,0x800);uint8_t *si=syn+14,*st=si+20;si[0]=0x45;w16(si+2,40);si[8]=64;si[9]=6;w32(si+12,GUEST);w32(si+16,0x7f000001);w16(si+10,sum(si,20,0));
    w16(st,40000);w16(st+2,22);st[12]=0x50;st[13]=TSYN;w16(st+14,65535);w16(st+16,sum(st,20,(GUEST>>16)+(GUEST&65535)+0x7f00+1+6+20));
    packet(h,syn,sizeof syn);assert(h->rw==count&&!h->tcp[0].state);   /* a SYN to the host's loopback opens nothing */
    {   /* port 80 to anywhere goes to the web gateway's loopback port when one is set */
        int l=socket(AF_INET,SOCK_STREAM,0);struct sockaddr_in a={.sin_family=AF_INET,.sin_addr.s_addr=htonl(0x7f000001)};socklen_t al=sizeof a;
        assert(l>=0&&!bind(l,(struct sockaddr *)&a,sizeof a)&&!listen(l,1)&&!getsockname(l,(struct sockaddr *)&a,&al));pt_user_net_web(h,ntohs(a.sin_port));
        w32(si+16,0x0a010203);si[10]=si[11]=0;w16(si+10,sum(si,20,0));w16(st+2,80);st[16]=st[17]=0;w16(st+16,sum(st,20,(GUEST>>16)+(GUEST&65535)+0x0a01+0x0203+6+20));
        packet(h,syn,sizeof syn);assert(h->tcp[0].state==1&&h->tcp[0].remote==0x0a010203);
        struct pollfd p={l,POLLIN,0};assert(poll(&p,1,2000)==1);int c=accept(l,NULL,NULL);assert(c>=0);close(c);close(h->tcp[0].fd);close(l);
    }
    pthread_mutex_destroy(&h->lock);free(h);puts("PASS: rootless ARP/DHCP, loopback TCP refused, port 80 to the web gateway, lease options, NAK, IPv4/UDP checksums, malformed lengths and bounded frame parsing; no host network used");
}
