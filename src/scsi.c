/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "scsi.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
/* SPC/SBC/MMC byte-oriented target. Only the CPU owner publishes completion;
 * backend requests retain their private buffer across reset until take(). */
static void nd_scsi_sense(PtScsi *s,unsigned key,unsigned asc,unsigned ascq){
    s->status=2;s->key=key;s->asc=asc;s->ascq=ascq;s->phase=PT_SCSI_DONE;s->retry=0;
}
static void nd_scsi_data(PtScsi *s,unsigned size,unsigned allocation){
    s->position=0;s->length=size<allocation?size:allocation;s->expected=s->length;s->phase=s->length?PT_SCSI_IN:PT_SCSI_DONE;
}
static uint64_t nd_scsi_capacity(const PtScsi *s){return s->block?pt_block_size(s->block)/s->sector:0;}
/* Any image is DVD-ROM media and the drive a DVD-ROM drive. As a CD, Tiger splits the disc into tracks by its full TOC
 * and reads them with READ CD; neither is modelled (a CD-sized .iso never mounted in Tiger 10.4.11: "disk1: alignment
 * error"). DVD media is read as plain 2048-byte blocks, as the install DVD always was. */
static int nd_scsi_dvd(const PtScsi *s){return s->optical&&s->block;}
void pt_scsi_init(PtScsi *s,int optical){
    memset(s,0,sizeof *s);s->optical=!!optical;s->sector=optical?2048:512;s->read_only=!!optical;s->generation=1;
}
void pt_scsi_reset(PtScsi *s){
    s->generation++;s->native_cancelled=s->pending;s->retry=0;s->phase=PT_SCSI_IDLE;s->position=s->length=s->status=s->blocks=0;
    s->expected=s->transferred=0;
}
int pt_scsi_media(PtScsi *s,PtBlock *block,int read_only,PtBlock **old){
    if(s->pending||s->retry||!(s->phase==PT_SCSI_IDLE||s->phase==PT_SCSI_DONE))return EBUSY;
    if(block&&pt_block_size(block)%s->sector)return EINVAL;
    if(old)*old=s->block;s->block=block;s->present=block!=NULL;s->read_only=read_only||s->optical;
    s->generation++;s->unit_attention=1;s->event=block?2:3;return 0;
}
static int nd_scsi_rw(PtScsi *s,uint64_t lba,uint32_t blocks,int write){
    if(write&&s->read_only){nd_scsi_sense(s,7,0x27,0);return 0;}
    uint64_t capacity=nd_scsi_capacity(s);if(lba>capacity||blocks>capacity-lba){nd_scsi_sense(s,5,0x21,0);return 0;}
    s->lba=lba;s->blocks=blocks;s->expected=(uint64_t)blocks*s->sector;
    if(!blocks)return 0;s->chunk=blocks>sizeof s->data/s->sector?sizeof s->data/s->sector:blocks;
    if(write){s->phase=PT_SCSI_OUT;s->length=s->chunk*s->sector;}else {s->phase=PT_SCSI_READ_WAIT;s->retry=1;}return 0;
}
static void nd_scsi_toc(PtScsi *s,const uint8_t *c){
    unsigned format=c[2]&15,msf=c[1]&2,len=20;if(format>2){nd_scsi_sense(s,5,0x24,0);return;}
    s->data[2]=1;s->data[3]=1;
    if(format==1){len=12;s->data[5]=0x14;s->data[6]=1;}
    else {s->data[5]=0x14;s->data[6]=1;s->data[13]=0x14;s->data[14]=0xaa;
        uint32_t last=(uint32_t)nd_scsi_capacity(s);if(msf){last+=150;s->data[9]=0;s->data[10]=2;s->data[11]=0;s->data[17]=(uint8_t)(last/(60*75));s->data[18]=(uint8_t)(last/75%60);s->data[19]=(uint8_t)(last%75);}
        else pt_write_be(s->data+16,4,last);}
    pt_write_be(s->data,2,len-2);nd_scsi_data(s,len,(unsigned)pt_read_be(c+7,2));
}
static int nd_scsi_command(PtScsi *s,const uint8_t *c,unsigned n){
    if(s->pending||s->retry||s->phase==PT_SCSI_IN||s->phase==PT_SCSI_OUT)return EBUSY;
    if(!c||!n)return EINVAL;unsigned group=c[0]>>5,need=group==0?6:group==4?16:group==5?12:10;
    if(n<need)return EINVAL;
    s->status=0;s->position=s->length=s->blocks=0;s->expected=s->transferred=0;s->native_fua=0;s->phase=PT_SCSI_DONE;memset(s->data,0,sizeof s->data);
    if(c[0]!=0x12&&c[0]!=3&&c[0]!=0x4a&&s->unit_attention){s->unit_attention=0;nd_scsi_sense(s,6,0x28,0);return 0;}
    /* Drive properties are answered without a medium: a Tiger booted with an empty drive that got NOT READY for GET
     * CONFIGURATION and MODE SENSE took the drive for CD-only and could never read DVD media inserted later. */
    if(c[0]!=0x12&&c[0]!=3&&c[0]!=0x4a&&c[0]!=0x1b&&c[0]!=0x46&&c[0]!=0x5a&&c[0]!=0x1a&&c[0]!=0x1e&&(!s->present||!s->block)){nd_scsi_sense(s,2,0x3a,0);return 0;}
    switch(c[0]){
    case 0:case 0x2b:case 0x1d:break;
    case 3:{unsigned key=s->key,asc=s->asc,ascq=s->ascq;s->data[0]=0x70;s->data[2]=(uint8_t)key;s->data[7]=10;s->data[12]=(uint8_t)asc;s->data[13]=(uint8_t)ascq;nd_scsi_data(s,18,c[4]);s->key=s->asc=s->ascq=0;break;}
    case 0x12:
        if(c[1]&1){if(c[2]==0){s->data[1]=0;s->data[3]=1;s->data[4]=0;nd_scsi_data(s,5,c[4]);}else nd_scsi_sense(s,5,0x24,0);break;}
        s->data[0]=s->optical?5:0;s->data[1]=0x80;s->data[2]=5;s->data[3]=2;s->data[4]=31;
        memcpy(s->data+8,"PTOLEMY ",8);memset(s->data+16,' ',16);memcpy(s->data+16,s->optical?"DVD-ROM":"Transfer Disk",s->optical?7:13);memcpy(s->data+32,"0001",4);nd_scsi_data(s,36,c[4]);break;
    case 0x1b:
        if((c[4]&3)==2){if(s->locked){nd_scsi_sense(s,5,0x53,2);break;}s->present=0;s->event=3;if(s->eject)s->eject(s->opaque);}
        else if((c[4]&3)==3&&s->block)s->present=1;break;
    case 0x1e:s->locked=c[4]&1;break;
    case 0x25:{uint64_t capacity=nd_scsi_capacity(s);pt_write_be(s->data,4,capacity>0x100000000ull?0xffffffffu:(uint32_t)(capacity?capacity-1:0));pt_write_be(s->data+4,4,s->sector);nd_scsi_data(s,8,8);break;}
    case 0x9e:
        if((c[1]&31)!=0x10){nd_scsi_sense(s,5,0x24,0);break;}pt_write_be(s->data,8,nd_scsi_capacity(s)-1);pt_write_be(s->data+8,4,s->sector);nd_scsi_data(s,32,(unsigned)pt_read_be(c+10,4));break;
    case 8:case 0x0a:return nd_scsi_rw(s,((uint32_t)(c[1]&31)<<16)|(uint32_t)pt_read_be(c+2,2),c[4]?c[4]:256,c[0]==0x0a);
    case 0x28:case 0x2a:s->native_fua=!!(c[1]&8);return nd_scsi_rw(s,pt_read_be(c+2,4),(uint32_t)pt_read_be(c+7,2),c[0]==0x2a);
    case 0xa8:case 0xaa:s->native_fua=!!(c[1]&8);return nd_scsi_rw(s,pt_read_be(c+2,4),(uint32_t)pt_read_be(c+6,4),c[0]==0xaa);
    case 0x88:case 0x8a:s->native_fua=!!(c[1]&8);return nd_scsi_rw(s,pt_read_be(c+2,8),(uint32_t)pt_read_be(c+10,4),c[0]==0x8a);
    case 0x35:case 0x91:
        if(!s->read_only){s->phase=PT_SCSI_FLUSH_WAIT;s->retry=1;}break;
    case 0x1a:case 0x5a:{
        unsigned header=c[0]==0x5a?8:4,page=c[2]&63,pos=header;
        if(page==8||page==63){s->data[pos]=8;s->data[pos+1]=18;s->data[pos+2]=s->read_only?0:4;pos+=20;}
        if((page==0x2a||page==63)&&s->optical){s->data[pos]=0x2a;s->data[pos+1]=26;s->data[pos+2]=0x3b;s->data[pos+3]=3;s->data[pos+4]=0x71;   /* write: CD-R, CD-RW (above) */s->data[pos+5]=0xff;s->data[pos+6]=0x29;s->data[pos+7]=3;pt_write_be(s->data+pos+8,2,706);pt_write_be(s->data+pos+14,2,706);pos+=28;}
        if(pos==header){nd_scsi_sense(s,5,0x24,0);break;}
        if(header==8){pt_write_be(s->data,2,pos-2);s->data[3]=s->read_only?128:0;}else {s->data[0]=(uint8_t)(pos-1);s->data[2]=s->read_only?128:0;}
        nd_scsi_data(s,pos,header==8?(unsigned)pt_read_be(c+7,2):c[4]);break;}
    case 0x43:if(!s->optical)nd_scsi_sense(s,5,0x20,0);else nd_scsi_toc(s,c);break;
    case 0x46:{ /* GET CONFIGURATION: profile list and core/removable/random-readable. */
        /* Tiger publishes a drive to Disc Recording only if it lists CD-R/RW or DVD-R/RW/RAM (IODVDServices::start sets the
         * SCSITaskUserClientIniter match category from those), and games eject or look for "the" drive through it (SimCity 4
         * crashed in CheckCD: DRDeviceEjectMedia on element 0 of an empty DRCopyDeviceArray). The profiles are those of the
         * PowerBook5,8's Combo drive (CD-RW/DVD-ROM); nothing is ever written - only pressed images are inserted, which
         * READ DISC INFORMATION reports complete and not erasable. */
        if(!s->optical){nd_scsi_sense(s,5,0x20,0);break;}unsigned profile=nd_scsi_dvd(s)?0x10:0;pt_write_be(s->data+6,2,profile);unsigned pos=8,start=(unsigned)pt_read_be(c+2,2),rt=c[1]&3;
        const unsigned features[]={0,1,3,0x10};for(unsigned k=0;k<4;k++){unsigned f=features[k];if(f<start||(rt==2&&f!=start))continue;pt_write_be(s->data+pos,2,f);s->data[pos+2]=3;unsigned bytes=f==0?16:f==1?8:f==3?4:8;s->data[pos+3]=(uint8_t)bytes;
            if(f==0){static const uint16_t list[4]={0x10,0x0a,0x09,8};   /* DVD-ROM, CD-RW, CD-R, CD-ROM */
                for(unsigned j=0;j<4;j++){pt_write_be(s->data+pos+4+4*j,2,list[j]);s->data[pos+6+4*j]=list[j]==profile;}}
            if(f==1)pt_write_be(s->data+pos+4,4,2);if(f==3)s->data[pos+4]=0x29;if(f==0x10){pt_write_be(s->data+pos+4,4,2048);pt_write_be(s->data+pos+8,2,16);}pos+=4+bytes;}
        pt_write_be(s->data,4,pos-4);nd_scsi_data(s,pos,(unsigned)pt_read_be(c+7,2));break;}
    case 0x4a:
        if(!(c[1]&1)){nd_scsi_sense(s,5,0x24,0);break;}pt_write_be(s->data,2,6);s->data[2]=4;s->data[3]=0x10;s->data[4]=(uint8_t)s->event;s->data[5]=s->present?2:0;s->event=0;nd_scsi_data(s,8,(unsigned)pt_read_be(c+7,2));break;
    case 0x51:
        if(!s->optical){nd_scsi_sense(s,5,0x20,0);break;}pt_write_be(s->data,2,32);s->data[2]=0x0e;s->data[3]=1;s->data[4]=1;s->data[5]=1;s->data[6]=1;nd_scsi_data(s,34,(unsigned)pt_read_be(c+7,2));break;
    case 0xbb:break; /* SET CD SPEED: no mechanical speed transition. */
    case 0xad:{
        if(!s->optical){nd_scsi_sense(s,5,0x20,0);break;}if(!nd_scsi_dvd(s)){nd_scsi_sense(s,5,0x30,0);break;}
        unsigned format=c[7],size=2052;if((c[1]&15)||c[6]||(format!=0&&format!=1&&format!=4)){nd_scsi_sense(s,5,0x24,0);break;}
        if(format==1)size=8;pt_write_be(s->data,2,size-2);if(!format){s->data[4]=1;s->data[5]=15;s->data[6]=1;uint64_t cap=nd_scsi_capacity(s);pt_write_be(s->data+12,4,cap-1);pt_write_be(s->data+16,4,cap-1);}nd_scsi_data(s,size,(unsigned)pt_read_be(c+8,2));break;}
    default:nd_scsi_sense(s,5,0x20,0);break;
    }return 0;
}
/* PT_SCSI_TRACE=1: every optical command and its immediate result on stderr (diagnosis of guest media handling). */
int pt_scsi_command(PtScsi *s,const uint8_t *c,unsigned n){
    static int trace=-1;if(trace<0){const char *v=getenv("PT_SCSI_TRACE");trace=v&&*v=='1';}
    int e=nd_scsi_command(s,c,n);
    if(trace&&s->optical&&c&&n>2)fprintf(stderr,"scsi: %02x %02x %02x %02x -> %d status %u sense %x/%02x/%02x\n",c[0],c[1],c[2],n>9?c[9]:0,e,s->status,s->key,s->asc,s->ascq);
    return e;
}
int pt_scsi_service(PtScsi *s){
    if(s->pending){PtBlockRequest result;int got=pt_block_take(s->block,&result);if(got<=0)return got<0?1:0;s->pending=0;
        if(s->native_cancelled||result.generation!=s->generation){s->native_cancelled=0;s->phase=PT_SCSI_IDLE;return 0;}
        if(result.error){nd_scsi_sense(s,result.error==EROFS?7:3,result.error==EROFS?0x27:0x11,0);return 0;}
        if(s->phase==PT_SCSI_READ_WAIT){s->position=0;s->length=s->chunk*s->sector;s->lba+=s->chunk;s->blocks-=s->chunk;s->phase=PT_SCSI_IN;}
        else if(s->phase==PT_SCSI_WRITE_WAIT){s->lba+=s->chunk;s->blocks-=s->chunk;s->position=0;s->chunk=s->blocks>sizeof s->data/s->sector?sizeof s->data/s->sector:s->blocks;s->length=s->chunk*s->sector;s->phase=s->blocks?PT_SCSI_OUT:PT_SCSI_DONE;if(!s->blocks&&s->native_fua){s->phase=PT_SCSI_FLUSH_WAIT;s->retry=1;s->native_fua=0;}}
        else s->phase=PT_SCSI_DONE;
    }
    if(s->retry){int op=s->phase==PT_SCSI_READ_WAIT?PT_BLOCK_READ:s->phase==PT_SCSI_WRITE_WAIT?PT_BLOCK_WRITE:PT_BLOCK_FLUSH;
        if(op==PT_BLOCK_READ)s->chunk=s->blocks>sizeof s->data/s->sector?sizeof s->data/s->sector:s->blocks;
        PtBlockRequest request={.id=s->generation,.generation=s->generation,.operation=op,.offset=s->lba*s->sector,.buffer=s->data,.size=op==PT_BLOCK_FLUSH?0:(size_t)s->chunk*s->sector};
        int error=pt_block_submit(s->block,&request);if(error==EAGAIN)return 1;s->retry=0;if(error)nd_scsi_sense(s,error==EROFS?7:3,error==EROFS?0x27:0x11,0);else s->pending=1;
    }return 0;
}
int pt_scsi_read(PtScsi *s,void *out,unsigned size){
    pt_scsi_service(s);
    if(s->pending||s->retry)return -EAGAIN;if(s->phase!=PT_SCSI_IN)return 0;if(!out&&size)return -EINVAL;
    unsigned take=s->length-s->position;if(take>size)take=size;if(take)memcpy(out,s->data+s->position,take);s->position+=take;s->transferred+=take;
    if(s->position==s->length){if(s->blocks){s->phase=PT_SCSI_READ_WAIT;s->retry=1;}else s->phase=PT_SCSI_DONE;}return (int)take;
}
int pt_scsi_write(PtScsi *s,const void *in,unsigned size){
    pt_scsi_service(s);
    if(s->pending||s->retry)return -EAGAIN;if(s->phase!=PT_SCSI_OUT)return 0;if(!in&&size)return -EINVAL;
    unsigned take=s->length-s->position;if(take>size)take=size;if(take)memcpy(s->data+s->position,in,take);s->position+=take;s->transferred+=take;
    if(s->position==s->length){s->phase=PT_SCSI_WRITE_WAIT;s->retry=1;}return (int)take;
}
