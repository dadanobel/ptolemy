/* SPDX-License-Identifier: GPL-2.0-or-later */
#import <Foundation/Foundation.h>
#include "net.h"
#include <stdio.h>
#include <string.h>
int main(int argc,char **argv){@autoreleasepool{
    uint8_t mac[6];int status=0;PtNetHost *host=NULL;
    if(argc==2&&!strcmp(argv[1],"--shared"))host=pt_net_host_open(NULL,NULL,mac,&status);
    else if(argc==3&&!strcmp(argv[1],"--socket"))host=pt_net_host_socket(argv[2],NULL,NULL,mac,&status);
    else {fprintf(stderr,"Usage: network-probe --shared | --socket PRIVATE-SOCKET\n");return 2;}
    if(!host){fprintf(stderr,"Network start failed (%d); shared vmnet needs an authorized com.apple.vm.networking signature or a separate privileged helper. Do not run the whole CPU/GPU engine as root.\n",status);return 1;}
    printf("vmnet ready: %02x:%02x:%02x:%02x:%02x:%02x\n",mac[0],mac[1],mac[2],mac[3],mac[4],mac[5]);return pt_net_host_close(host)?1:0;
}}
