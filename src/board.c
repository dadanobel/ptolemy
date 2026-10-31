/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "board.h"
#include <stdio.h>
#include <errno.h>
#include <string.h>
/* PCI AGP capability and the UniNorth register contract, including the explicit
 * invalidation pulse. Config writes stay on the CPU owner. */
void pt_agp_init(uint8_t config[256],uint8_t mask[256],PtAgp *a){
    memset(a,0,sizeof *a);config[6]|=16;config[0x34]=0x80;
    config[0x80]=2;config[0x81]=0;config[0x82]=0x20;config[0x83]=0;
    pt_write_le(config+0x84,4,0x1f000207);pt_write_le(config+0x88,4,0);
    for(unsigned at=0x8c;at<0x98;at++)config[at]=0;
    pt_write_le(config+0x98,4,1);memset(mask+0x80,0,0x1c);memset(mask+0x88,255,16);
}
void pt_agp_update(PtAgp *a,const uint8_t config[256],unsigned reg,unsigned size){
    if(reg>=256||size>256-reg||!size||reg>=0x98||reg+size<=0x88)return;
    uint32_t gart=(uint32_t)pt_read_le(config+0x8c,4),aperture=(uint32_t)pt_read_le(config+0x90,4),control=(uint32_t)pt_read_le(config+0x94,4);
    if(gart!=a->gart||aperture!=a->aperture||control!=a->control||(reg<0x98&&reg+size>0x94&&(control&1)))a->generation++;
    a->gart=gart;a->aperture=aperture;a->control=control;
}
int pt_board_ram(unsigned mb,uint64_t *out){
    if(!out||mb<512||mb>2048||(mb&63))return EINVAL;*out=(uint64_t)mb<<20;return 0;
}
int pt_board_boot(PtNvram *n,int automatic,const char *device){
    /* Stage all OF changes; a full partition must never leave half a boot policy.
     * init-program/go resumes the client without recursively executing boot-command. */
    PtNvram stage=*n;
    static const char command[]= "\" /\" find-device \" PowerBook5,8\" encode-string \" model\" property "
        "\" MacRISC\" encode-string \" MacRISC2\" encode-string encode+ "
        "\" Power Macintosh\" encode-string encode+ \" compatible\" property device-end init-program go";
    const char *names[]={"boot-command","auto-boot?","input-device","output-device","boot-device"};
    const char *values[]={command,automatic?"true":"false",automatic?"keyboard":"scca",automatic?"screen":"scca",device};
    for(unsigned k=0;k<5;k++)if(values[k]){int e=pt_nvram_set_of(&stage,names[k],values[k]);if(e)return e;}
    memcpy(n->data,stage.data,sizeof n->data);n->generation=stage.generation;return 0;
}
