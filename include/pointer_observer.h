/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef PT_POINTER_OBSERVER_H
#define PT_POINTER_OBSERVER_H
#include "pt_agent.h"
#include <dlfcn.h>
/* Included by the visible Tiger worker. No new daemon, shell or task. The
 * deprecated Quartz observation is valid on Tiger; absence means unknown. */
static int pt_pointer_guest_cap;
static int (*pt_pointer_guest_visible)(void);
static uint64_t pt_pointer_guest_at;
static void pt_pointer_guest_begin(unsigned caps){
    pt_pointer_guest_cap=!!(caps&PTA_CAP_INPUT);pt_pointer_guest_at=0;
    if(pt_pointer_guest_cap){void *quartz=dlopen("/System/Library/Frameworks/ApplicationServices.framework/ApplicationServices",RTLD_LAZY|RTLD_LOCAL);
        if(quartz)pt_pointer_guest_visible=(int (*)(void))dlsym(quartz,"CGCursorIsVisible");}
}
static void pt_pointer_guest_tick(uint32_t token,uint64_t now_ms){
    if(!pt_pointer_guest_cap||!pt_pointer_guest_visible||(pt_pointer_guest_at&&now_ms>=pt_pointer_guest_at&&now_ms-pt_pointer_guest_at<50))return;
    pt_pointer_guest_at=now_ms;int visible=pt_pointer_guest_visible();uint32_t reply[3];
    int rc=ptagent_call(PTA_INPUT,token,visible?1:2,PTA_INPUT_ABI,0,reply);
    if(rc==PTA_INVALID)pt_pointer_guest_cap=0; /* Older host: stop trying. */
}
#endif
