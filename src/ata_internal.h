/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef PT_NATIVE_ATA_INTERNAL_H
#define PT_NATIVE_ATA_INTERNAL_H
#include "ata.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
/* For a packet device status bit 4 is SERV, not DSC; an idle ATAPI device reads 40h, not 50h. */
enum { ND_ATA_READ=1,ND_ATA_WRITE,ND_ATA_DMA_READ,ND_ATA_DMA_WRITE,ND_ATA_IDENTIFY,ND_ATAPI_PACKET=16,ND_ATAPI_IN,ND_ATAPI_OUT,ND_ATAPI_WAIT };
static inline void nd_ata_irq(PtAta *a,int set){a->pending=set;if(a->irq)a->irq(a->opaque,set&&!(a->control&2));}
static inline void nd_ata_done(PtAta *a){a->busy=a->retry=0;a->phase=0;a->status=a->optical?0x40:0x50;a->error=0;nd_ata_irq(a,1);}
static inline void nd_ata_error(PtAta *a,unsigned error){a->busy=a->retry=0;a->phase=0;a->status=a->optical?0x41:0x51;a->error=(uint8_t)error;nd_ata_irq(a,1);}
static inline void nd_ata_signature(PtAta *a){a->busy=a->retry=a->native_reset=0;a->phase=0;a->native_dma=a->native_write=a->native_flush=0;a->status=a->optical?0x40:0x50;a->error=1;a->count=1;a->lba[0]=1;a->lba[1]=a->optical?0x14:0;a->lba[2]=a->optical?0xeb:0;}
/* IDENTIFY strings are byte-swapped ASCII words, space padded (serial words 10-19, model 27-46). */
static inline void nd_ata_string(uint8_t *p,unsigned bytes,const char *text){memset(p,' ',bytes);for(unsigned k=0;text[k]&&k<bytes;k++)p[k^1]=(uint8_t)text[k];}
#endif
