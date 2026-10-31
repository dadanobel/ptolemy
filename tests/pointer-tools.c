/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "pt_agent.h"
#include "pointer_mailbox.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
static unsigned calls,last_state;static int visible,refuse;
static int ptagent_call(uint32_t op,uint32_t token,uint32_t state,uint32_t abi,uint32_t unused,uint32_t reply[3]){
    (void)reply;assert(op==PTA_INPUT&&token==42&&abi==PTA_INPUT_ABI&&!unused);calls++;last_state=state;return refuse?PTA_INVALID:PTA_OK;
}
#include "pointer_observer.h"
static int cursor(void){return visible;}
int main(void){
    PtCursorMailbox box={0};assert(pt_cursor_read(&box,0)==PT_CURSOR_UNKNOWN);
    pt_cursor_publish(&box,PT_CURSOR_HIDDEN,2000000);assert(pt_cursor_read(&box,2000000)==PT_CURSOR_HIDDEN);
    assert(pt_cursor_read(&box,1000000)==PT_CURSOR_UNKNOWN&&pt_cursor_read(&box,1002001000)==PT_CURSOR_UNKNOWN);
    pt_cursor_publish(&box,99,3000000);assert(pt_cursor_read(&box,3000000)==PT_CURSOR_UNKNOWN);
    pt_pointer_guest_begin(0);pt_pointer_guest_visible=cursor;pt_pointer_guest_tick(42,100);assert(!calls);
    pt_pointer_guest_cap=1;visible=1;pt_pointer_guest_tick(42,100);assert(calls==1&&last_state==1);
    visible=0;pt_pointer_guest_tick(42,110);assert(calls==1);pt_pointer_guest_tick(42,150);assert(calls==2&&last_state==2);
    refuse=1;pt_pointer_guest_tick(42,200);pt_pointer_guest_tick(42,250);assert(calls==3&&!pt_pointer_guest_cap);
    char dir[]="/private/tmp/pt-input-test.XXXXXX",path[108];assert(mkdtemp(dir));snprintf(path,sizeof path,"%s/control",dir);
    PtAgent *a=pt_agent_open(path);assert(a);uint32_t token;assert(pt_agent_register_caps(a,PTA_VERSION+1,0,&token)==PTA_INVALID);assert(!pt_agent_register_caps(a,PTA_VERSION,PTA_CAP_POINTER,&token));
    assert(pt_agent_input(a,0,1)==PTA_DENIED&&pt_agent_input(a,token,3)==PTA_INVALID);
    PtGuestPointer observed;uint8_t wire[24]={0};wire[3]=1;wire[7]=1;wire[11]=100;wire[15]=80;wire[16]=0;wire[17]=0;wire[18]=4;wire[19]=0;wire[20]=0;wire[21]=0;wire[22]=3;wire[23]=0;
    assert(!pt_agent_pointer_read(a,&observed));assert(pt_agent_pointer(a,0,wire)==PTA_DENIED);assert(!pt_agent_pointer(a,token,wire));assert(pt_agent_pointer_read(a,&observed)&&observed.x==100&&observed.y==80&&observed.width==1024&&observed.height==768);
    wire[3]=2;assert(pt_agent_pointer(a,token,wire)==PTA_INVALID);wire[3]=1;wire[18]=0;assert(pt_agent_pointer(a,token,wire)==PTA_INVALID);
    assert(!pt_agent_input(a,token,2));assert(!pt_agent_unregister(a,token));assert(pt_agent_input(a,token,2)==PTA_DENIED);
    pt_agent_close(a);rmdir(dir);
    puts("PASS: cursor mailbox freshness/clock reversal, observer capability/rate limit/old host, authenticated state and token revocation");
}
