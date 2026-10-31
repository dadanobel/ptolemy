/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "devices.h"
#include <assert.h>
#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
int main(void) {
    char path[]="/tmp/pt-media-XXXXXX";int fd=mkstemp(path);assert(fd>=0 && !ftruncate(fd,8192));close(fd);
    PtDevices *d=calloc(1,sizeof *d);uint8_t ram[65536]={0};PtBus bus={0};PtPic pic;PtEvent event;_Atomic uint32_t kick=0;
    pt_pic_init(&pic);pt_event_init(&event);assert(!pt_devices_init(d,(PtRam){ram,sizeof ram,NULL},&bus,&pic,&event,&kick,NULL,0,0,0));
    PtBlock *cd=pt_block_open(path,0,&event),*usb=pt_block_open(path,0,&event),*old=NULL;assert(cd && usb);
    pt_block_set_kick(cd,&kick);pt_block_set_kick(usb,&kick);
    assert(!pt_devices_optical(d,cd,&old) && !old);
    d->optical.locked=1;assert(pt_devices_optical(d,NULL,&old)==EPERM && d->optical.block==cd);d->optical.locked=0;   /* a locked tray keeps its disc */
    assert(pt_devices_optical(d,cd,&old)==EALREADY && pt_devices_storage(d,cd,1,&old)==EINVAL);
    assert(!pt_devices_storage(d,usb,1,&old) && !old);
    d->optical.unit_attention=0;uint8_t cdb[10]={0x28};cdb[8]=1;assert(!pt_scsi_command(&d->optical,cdb,10));
    pt_scsi_service(&d->optical);assert(d->optical.pending);
    assert(!pt_devices_close(d));free(d);pt_event_destroy(&event);pt_pic_destroy(&pic);assert(!unlink(path));
    puts("media lifetime: pending private-buffer read drained before device destruction");
}
