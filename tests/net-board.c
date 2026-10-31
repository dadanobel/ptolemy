/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "net.h"
#include "board.h"
#include "pci_address.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef struct {unsigned slot,reg;uint32_t size,flags,base;int probe;unsigned region;} PtBar;
static PtBar bars[7];static unsigned nbars;static uint64_t guest_ram_bytes=2ull<<30,pci_window_end=0xd0000000ull;
static PtBus bus;static PtAgp agp;static PtNetRtl network;static int devices_enabled=1,radeon_enabled,network_enabled=1,vram_remap;
static uint8_t config[32][256],cfgmask[32][256];static uint32_t cfg_index[3];static void *radeon;
static unsigned configured,unexpected_ports;
static void pt_radeon_config(void *p,const uint8_t *c,const PtAgp *a){(void)p;(void)c;(void)a;configured++;}
static int pt_radeon_mmio(void *p,unsigned o,uint8_t *b,unsigned n,int w){(void)p;(void)o;(void)b;(void)n;(void)w;unexpected_ports++;return 0;}
static int pt_radeon_port(void *p,unsigned o,uint8_t *b,unsigned n,int w){return pt_radeon_mmio(p,o,b,n,w);}
static int absent(void *p,uint32_t o,uint8_t *b,unsigned n,int w){(void)p;(void)o;(void)b;(void)n;(void)w;return -1;}
static void fatal(const char *why){fprintf(stderr,"%s\n",why);abort();}
static int isa_access(void *,uint32_t,uint8_t *,unsigned,int);
static int network_access(void *,uint32_t,uint8_t *,unsigned,int);
static void radeon_configure(void);
#include "network-board-functions.inc"
static uint32_t cfg(unsigned slot,unsigned reg,unsigned size,int write,uint32_t value){
    uint8_t b[4];pt_write_le(b,4,0x80000000u|slot<<11|(reg&~3u));assert(pci_access(0xf2800000,b,4,1));
    pt_write_le(b,size,value);assert(pci_access(0xf2c00000+(reg&3),b,size,write));return (uint32_t)pt_read_le(b,size);
}
int main(void){
    uint8_t mac[]={2,1,2,3,4,5},b[4];
    pt_net_rtl_init(&network,(PtRam){0},mac,(PtNetIo){0},NULL,NULL);
    for(unsigned gpu=0;gpu<2;gpu++){
        memset(&bus,0,sizeof bus);memset(bars,0,sizeof bars);radeon_enabled=gpu;radeon=gpu?(void *)1:NULL;nbars=gpu?5:2;
        bars[0]=(PtBar){12,0x10,0x80000,0,0x80000000,0,0};bars[1]=(PtBar){13,0x10,0x100,0,0x80080000,0,1};
        assert(!pt_bus_add(&bus,(PtRegion){0x80000000,0x80000,absent,NULL,"macio"}));assert(!pt_bus_add(&bus,(PtRegion){0x80080000,0x100,absent,NULL,"usb"}));
        if(gpu){bars[2]=(PtBar){14,0x10,0x10000000,8,0x90000000,0,2};bars[3]=(PtBar){14,0x18,0x10000,0,0x80090000,0,3};bars[4]=(PtBar){14,0x14,0x100,1,0x1000,0,UINT32_MAX};}
        pci_init();add_network_regions();assert(nbars==(gpu?7:4));assert(cfg(15,0,4,0,0)==0x813910ec);assert(cfg(15,0x3c,2,0,0)==0x011e);
        unsigned before=configured;cfg(15,0x10,4,1,UINT32_MAX);assert(cfg(15,0x10,4,0,0)==0xffffff01);cfg(15,0x10,4,1,0x3001);assert(cfg(15,0x10,4,0,0)==0x3001);assert(configured==before);
        cfg(15,0x14,4,1,UINT32_MAX);assert(cfg(15,0x14,4,0,0)==0xffffff00);cfg(15,0x14,4,1,0x800b0000);cfg(15,4,2,1,7);
        assert(!isa_access(NULL,0x3000,b,4,0)&&!memcmp(b,mac,4)&&!unexpected_ports);
        assert(!pt_bus_access(&bus,0x800b0000,b,4,0)&&!memcmp(b,mac,4));cfg(15,4,2,1,4);
        assert(isa_access(NULL,0x3000,b,4,0)==-1||gpu);assert(pt_bus_access(&bus,0x800b0000,b,4,0)==-1);
        cfg(15,4,2,1,7);cfg(15,0x14,4,1,0x80080000);assert(!(config[15][4]&2)&&cfg(15,0x14,4,0,0)==0x800b0000);assert(!vram_remap);
        if(gpu){cfg(15,4,2,1,7);cfg(15,0x10,4,1,0x1000);assert(!(config[15][4]&1)&&cfg(15,0x10,4,0,0)==0x3001);radeon_configure();assert(pt_read_le(config[14]+0x10,4)==0x90000008&&pt_read_le(config[14]+0x14,4)==0x1001);}
    }
    network_enabled=0;pci_init();assert(cfg(15,0,4,0,0)==UINT32_MAX);
    puts("PASS: exact product PCI enumeration, BAR probes/relocation/overlap, I/O and MMIO decode, Radeon isolation and network-off rollback");
}
