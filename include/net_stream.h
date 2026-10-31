/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef PT_NET_STREAM_H
#define PT_NET_STREAM_H
#include "net.h"
#include <errno.h>
#include <string.h>
#include <sys/socket.h>
/* One bounded Ethernet frame, length BE16. No message can become an allocation.
 * The caller retains the same TX frame until send returns zero. */
typedef struct {uint8_t tx[PT_NET_FRAME+2],rx[PT_NET_FRAME+2];unsigned tx_at,tx_n,rx_at,rx_n;} PtNetStream;
static inline int pt_net_stream_send(int fd,PtNetStream *s,const uint8_t *frame,unsigned n){
    if(n<14||n>PT_NET_FRAME)return EMSGSIZE;
    if(!s->tx_n){pt_write_be(s->tx,2,n);memcpy(s->tx+2,frame,n);s->tx_n=n+2;}
    for(unsigned tries=0;s->tx_at<s->tx_n&&tries<4;tries++){
        ssize_t k=send(fd,s->tx+s->tx_at,s->tx_n-s->tx_at,MSG_DONTWAIT);
        if(k>0)s->tx_at+=(unsigned)k;else if(k<0&&errno==EINTR)continue;else return k<0?errno:EPIPE;
    }
    if(s->tx_at!=s->tx_n)return EAGAIN;s->tx_at=s->tx_n=0;return 0;
}
static inline int pt_net_stream_receive(int fd,PtNetStream *s,uint8_t *frame){
    for(unsigned tries=0;tries<4;tries++){
        unsigned end=s->rx_n?s->rx_n+2:2;
        if(s->rx_at==end){
            if(!s->rx_n){s->rx_n=(unsigned)pt_read_be(s->rx,2);if(s->rx_n<14||s->rx_n>PT_NET_FRAME)return -EPROTO;continue;}
            unsigned n=s->rx_n;memcpy(frame,s->rx+2,n);s->rx_at=s->rx_n=0;return (int)n;
        }
        ssize_t k=recv(fd,s->rx+s->rx_at,end-s->rx_at,MSG_DONTWAIT);
        if(k>0)s->rx_at+=(unsigned)k;else if(!k)return -ECONNRESET;
        else if(errno==EINTR)continue;else return errno==EAGAIN||errno==EWOULDBLOCK?0:-errno;
    }
    if(s->rx_n&&s->rx_at==s->rx_n+2){unsigned n=s->rx_n;memcpy(frame,s->rx+2,n);s->rx_at=s->rx_n=0;return (int)n;}
    return 0;
}
#endif
