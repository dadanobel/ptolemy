/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "pci_address.h"
#include <assert.h>
#include <stdio.h>
int main(void){
    for(unsigned slot=11;slot<32;slot++)for(unsigned fn=0;fn<8;fn++)for(unsigned lane=0;lane<8;lane++){
        uint32_t reg=(1u<<slot)|(fn<<8)|0xa0;assert(pt_pci_address(reg,lane)==(slot<<11|fn<<8|0xa0|lane));
    }
    for(unsigned bus=0;bus<256;bus++)assert(pt_pci_address(bus<<16|14<<11|3<<8|0x20|1,6)==(bus<<16|14<<11|3<<8|0x26));
    assert(pt_pci_address(0,0)==UINT32_MAX);assert(pt_pci_address((1u<<11)|(1u<<12),0)==UINT32_MAX);
    /* The current ROM uses PCI mechanism 1; Tiger uses UniNorth IDSEL. */
    assert(pt_pci_address(0x80000000u|14<<11|0x20,2)==(14<<11|0x22));
    puts("PASS: UniNorth IDSEL, type-1 bus/devfn, firmware mechanism-1 ABI and invalid address");
}
