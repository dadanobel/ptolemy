/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef PT_NET_H
#define PT_NET_H
#include "dma.h"
enum {PT_NET_FRAME=2048,PT_NET_QUEUE=64};
/* send copies the packet; receive returns a complete frame or zero. CPU owner.
 * EAGAIN is backpressure, never a successful transmit. */
typedef struct {void *opaque;int (*send)(void *,const uint8_t *,unsigned);int (*receive)(void *,uint8_t *,unsigned);int (*link)(void *);} PtNetIo;
typedef struct {
    PtRam ram;PtNetIo io;uint8_t reg[256],mac[6],pending[4];uint32_t producer;int bus_master,link_up;
    void (*irq)(void *,int);void *opaque;
    uint64_t tx,rx,dropped,faults;
} PtNetRtl;
void pt_net_rtl_init(PtNetRtl *,PtRam,const uint8_t mac[6],PtNetIo,void (*)(void *,int),void *);
void pt_net_rtl_pci(uint8_t config[256],uint8_t mask[256]);
int pt_net_rtl_access(PtNetRtl *,unsigned offset,uint8_t *,unsigned,int write);
void pt_net_rtl_service(PtNetRtl *);
typedef struct PtNetHost PtNetHost;
/* Control thread opens/closes; worker never touches guest RAM. status is a
 * vmnet status for direct mode, or errno for the local helper connection. */
PtNetHost *pt_net_host_open(PtEvent *,_Atomic uint32_t *kick,uint8_t mac[6],int *status);
PtNetHost *pt_net_host_socket(const char *path,PtEvent *,_Atomic uint32_t *kick,uint8_t mac[6],int *status);
PtNetIo pt_net_host_io(PtNetHost *);
/* Optional helper wakeup; callback runs on the backend queue, never touches guest RAM. */
void pt_net_host_notify(PtNetHost *,void (*)(void *),void *);
int pt_net_host_close(PtNetHost *);
#endif
