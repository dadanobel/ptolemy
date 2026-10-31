/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef PT_NET_USER_H
#define PT_NET_USER_H
#include "net.h"
typedef struct PtUserNet PtUserNet;
/* Experimental rootless IPv4 NAT: ARP, DHCP, UDP, ICMP echo and guest-initiated TCP (no inbound connections,
 * IP fragments or TCP window scaling). Explicit --network-user.
 * One CPU producer/consumer; worker sockets never touch guest memory. */
PtUserNet *pt_user_net_open(PtEvent *,_Atomic uint32_t *kick,uint8_t mac[6],int *error);
PtNetIo pt_user_net_io(PtUserNet *);
void pt_user_net_web(PtUserNet *,uint16_t port);   /* new port-80 connections go to 127.0.0.1:port (0: off) */
void pt_user_net_close(PtUserNet *);
#endif
