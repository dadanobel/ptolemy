/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef PT_PCI_ADDRESS_H
#define PT_PCI_ADDRESS_H
#include <stdint.h>
/* UniNorth type-0 one-hot IDSEL and type-1 bus/devfn cycles. The board also
 * accepts conventional enabled PCI config addresses used by its firmware. */
static inline uint32_t pt_pci_address(uint32_t latch,uint32_t lane){
    if(latch&1)return (latch&0x00fffffcu)|(lane&7);
    uint32_t idsel=latch&0xfffff800u;
    if((latch&0x80000000u)&&(idsel&0x7ffff800u))return (latch&0x00fffffcu)|(lane&3);
    if(!idsel||(idsel&(idsel-1)))return UINT32_MAX;
    unsigned slot=0;while((idsel>>slot)!=1)slot++;
    return slot<<11|(latch&0x700)|(latch&0xfcu)|(lane&7);
}
#endif
