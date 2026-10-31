/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef PT_BOARD_H
#define PT_BOARD_H
#include "platform.h"
#include "nvram.h"
typedef struct { uint32_t aperture,gart,control; uint64_t generation; } PtAgp;
/* CPU owner. Send copies of this snapshot through the GPU queue; the GPU must
 * never race writes to PCI config space. gart includes the hardware flags. */
void pt_agp_init(uint8_t config[256],uint8_t mask[256],PtAgp *);
void pt_agp_update(PtAgp *,const uint8_t config[256],unsigned reg,unsigned size);
int pt_board_ram(unsigned mb,uint64_t *bytes);
/* Apply only explicit machine configuration. Existing OSX bank stays intact. */
int pt_board_boot(PtNvram *,int automatic,const char *boot_device);
#endif
