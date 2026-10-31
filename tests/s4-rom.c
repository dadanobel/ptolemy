/* SPDX-License-Identifier: GPL-2.0-or-later */
/* The build extracts the exact sound-node function from the fork into this mock. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef unsigned phandle_t;
#define __cpu_to_be32(x) __builtin_bswap32(x)
#define FW_CFG_SIGNATURE 0
static unsigned feature,created,current,pic_handle=42;static const char *signature="PTOL",*pending_name;
typedef struct {unsigned node,n;char name[32];uint8_t bytes[256];} Property;
static Property props[64];static unsigned nprops;
static void fw_cfg_read(unsigned selector,void *out,unsigned n){assert(!selector&&n==4);memcpy(out,signature,n);}
static unsigned fw_cfg_read_i32(unsigned selector){assert(selector==31);return feature;}
static void push_str(const char *text){pending_name=text;}
static void fword(const char *word){if(!strcmp(word,"new-device")){current=++created;assert(current<=2);}else if(!strcmp(word,"finish-device")){assert(current);current--;}
    else {assert(!strcmp(word,"device-name"));assert(pending_name);assert(!strcmp(pending_name,current==1?"davbus":"sound"));pending_name=NULL;}}
static phandle_t get_cur_dev(void){return current;}
static phandle_t dt_find_type(const char *type){assert(!strcmp(type,"open-pic"));return pic_handle;}
static void set_property(phandle_t node,const char *name,const char *value,unsigned n){assert(nprops<64&&n<=256&&strlen(name)<32);Property *p=&props[nprops++];p->node=node;p->n=n;strcpy(p->name,name);memcpy(p->bytes,value,n);}
static void set_int_property(phandle_t node,const char *name,uint32_t value){value=__cpu_to_be32(value);set_property(node,name,(char *)&value,4);}
#include "s4-rom-function.inc"
static Property *property(unsigned node,const char *name){for(unsigned k=0;k<nprops;k++)if(props[k].node==node&&!strcmp(props[k].name,name))return &props[k];abort();}
static uint32_t cell(Property *p,unsigned k){assert((k+1)*4<=p->n);uint32_t v;memcpy(&v,p->bytes+k*4,4);return __builtin_bswap32(v);}
int main(void){
    macio_ptolemy_sound_init();assert(!created);feature=1;signature="NONE";macio_ptolemy_sound_init();assert(!created);signature="PTOL";macio_ptolemy_sound_init();assert(created==2&&!current);
    const unsigned reg[]={0x14000,0x60,0x8800,0x80,0x8900,0x80},irq[]={0x17,0,9,0,10,0};
    for(unsigned k=0;k<6;k++){assert(cell(property(1,"reg"),k)==reg[k]);assert(cell(property(1,"interrupts"),k)==irq[k]);}
    assert(cell(property(1,"interrupt-parent"),0)==42);Property *p=property(2,"model");assert(p->n==8&&!memcmp(p->bytes,"343S0184",8));
    assert(cell(property(2,"#-outputs"),0)==1&&!cell(property(2,"#-inputs"),0));assert(cell(property(2,"sample-rates"),0)==(44100u<<16));
    p=property(2,"sound-objects");assert(!p->bytes[p->n-1]&&strstr((char *)p->bytes,"model OutputPort")&&strstr((char *)p->bytes,"port-connection 2 ")&&strstr((char *)p->bytes,"port-type 0x6973706b "));puts("PASS: exact fork sound-node builder, feature/signature gates, DMA offsets, IRQs and Apple probe bytes");
}
