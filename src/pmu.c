/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "pmu.h"
#include <string.h>
#include <limits.h>
/* W65C22 register behavior; PMU wire commands follow the published PMU client
 * definitions. No physical battery, ADB peripheral or I2C target is fabricated. */
enum { ND_B,ND_A,ND_DDRB,ND_DDRA,ND_T1L,ND_T1H,ND_T1LL,ND_T1LH,ND_T2L,ND_T2H,ND_SR,ND_ACR,ND_PCR,ND_IFR,ND_IER,ND_ANH };
static void nd_pmu_irq(PtPmu *p){
    int level=!!(p->via[ND_IFR]&p->via[ND_IER]&0x7f);
    if(level!=p->native_irq_level){p->native_irq_level=level;if(p->irq)p->irq(p->opaque,level);}
    if(p->gpio)pt_gpio_input(p->gpio,1,!(p->intbits&p->intmask));
}
void pt_gpio_init(PtGpio *g,void (*irq)(void *,unsigned,int),void *opaque){
    memset(g,0,sizeof *g);g->irq=irq;g->opaque=opaque;
}
void pt_gpio_input(PtGpio *g,unsigned pin,int high){
    if(pin>=36)return;
    g->pins[pin]=(uint8_t)((g->pins[pin]&~2u)|(high?2:0));
    if(pin<8&&g->levels[pin]!=!high){g->levels[pin]=(uint8_t)!high;if(g->irq)g->irq(g->opaque,pin,!high);}
}
uint8_t pt_gpio_read(PtGpio *g,unsigned offset){return offset>=8&&offset<44?g->pins[offset-8]:0;}
void pt_gpio_write(PtGpio *g,unsigned offset,uint8_t v){if(offset>=8&&offset<44)g->pins[offset-8]=(g->pins[offset-8]&2)|(v&~2u);}
static void nd_pmu_fault(PtPmu *p,unsigned reason){
    p->protocol_errors++;PtPmuFault *f=&p->faults[reason];
    if(!f->count){f->first_ns=p->now;f->need=p->need;f->received=p->received;f->sent=p->sent;f->reply_size=p->reply_size;
        f->cmd=p->cmd;f->state=p->state;f->port_b=p->via[ND_B];f->ddrb=p->via[ND_DDRB];f->acr=p->via[ND_ACR];f->sr=p->via[ND_SR];memcpy(f->input,p->input,8);}
    f->count++;p->bad_request=1;
}
static void nd_pmu_reply(PtPmu *p){
    uint8_t *r=p->response;unsigned n=p->received;p->reply_size=p->sent=0;memset(r,0,sizeof p->response);
    if(p->need>sizeof p->input){nd_pmu_fault(p,PT_PMU_INPUT_TOO_LONG);goto ready;}
    switch(p->cmd){
    case 0x10:case 0x11:p->power[p->cmd&1]=p->input[0];break;
    case 0x18:case 0x19:r[0]=p->power[p->cmd&1];p->reply_size=1;break;
    case 0x20:p->intbits|=0x10;break; /* Complete an ADB transaction with no peripheral data. */
    case 0x21:break;
    case 0x30:p->rtc_base=(uint32_t)pt_read_be(p->input,4);p->origin=p->now;break;
    case 0x38:pt_write_be(r,4,p->rtc_base+(p->now-p->origin)/1000000000);p->reply_size=4;break;
    case 0x40:p->volume=p->input[0];break;case 0x41:p->brightness=p->input[0];break;
    case 0x48:r[0]=p->volume;p->reply_size=1;break;case 0x49:case 0xd9:r[0]=p->brightness;p->reply_size=1;break;
    case 0x6b:r[0]=p->battery?1:0;r[1]=p->ac?1:0;p->reply_size=8;break;
    case 0x6f:
        if(n!=1||p->input[0]!=1){nd_pmu_fault(p,PT_PMU_BATTERY_PAYLOAD);break;}
        r[0]=5;r[1]=(p->ac?1:0)|(p->battery?4:0);pt_write_be(r+2,2,p->charge);pt_write_be(r+4,2,p->capacity);
        pt_write_be(r+6,2,(uint16_t)p->current);pt_write_be(r+8,2,p->voltage);p->reply_size=10;break;
    case 0x70:p->intmask=p->input[0];break;
    case 0x78:r[0]=p->intbits;p->reply_size=1;if(p->intbits&0x10)p->reply_size=3;p->intbits=0;break;
    case 0x7e:case 0x7f:
        p->reply_size=1;if(memcmp(p->input,"MATT",4)){nd_pmu_fault(p,p->cmd==0x7e?PT_PMU_OFF_PAYLOAD:PT_PMU_SLEEP_PAYLOAD);break;}
        p->native_pending_power=p->cmd==0x7e?PT_POWER_OFF:PT_POWER_SLEEP;r[0]=p->cmd==0x7f?0x70:0;break;
    case 0x8f:{
        unsigned op=n?p->input[0]:255;
        if((op==0||op==3)&&n==1){pt_write_be(r,2,p->native_events[op==3]);p->reply_size=2;}
        else if((op==1||op==2||op==4||op==5)&&n==3){unsigned at=op>=3;uint16_t bits=(uint16_t)pt_read_be(p->input+1,2);if(op==1||op==4)p->native_events[at]|=bits;else p->native_events[at]&=(uint16_t)~bits;}
        else if(op==7&&n==1){r[0]=0;p->reply_size=1;} /* Virtual platform has no recorded shutdown fault. */
        else nd_pmu_fault(p,PT_PMU_EVENTS_PAYLOAD);break;}
    case 0x9a:
        /* The transport exists; no I2C target is attached. NACK is finite,
         * never an endless BUSY and never a fabricated successful read. */
        r[0]=0xff;p->reply_size=1;p->native_i2c_refused++;break;
    case 0xd0:p->native_pending_power=PT_POWER_RESET;break;
    case 0xdc:r[0]=p->lid?1:0;p->reply_size=1;break;
    case 0xdf:break;
    case 0xe8:
        /* Diagnostic address/count exchange over zero-initialized virtual PMU
         * RAM. This is not the ROM image of a physical PMU microcontroller. */
        p->reply_size=p->input[2]>sizeof p->response?sizeof p->response:p->input[2];break;
    case 0xea:r[0]=0x30;p->reply_size=1;break;
    default:p->unhandled_commands++;p->unknown[p->cmd>>3]|=(uint8_t)(1u<<(p->cmd&7));break;
    }
ready:
    p->state=p->reply_size||p->variable_response?2:0;nd_pmu_irq(p);
}
static void nd_pmu_start(PtPmu *p,uint8_t command){
    p->cmd=command;p->received=p->sent=p->reply_size=p->bad_request=0;memset(p->input,0,sizeof p->input);
    p->state=1;p->variable_input=0;p->variable_response=0;p->need=0;
    switch(command){
    case 0x10:case 0x11:case 0x40:case 0x41:case 0x70:case 0xd9:p->need=1;break;
    case 0x30:case 0x7e:case 0x7f:p->need=4;break;
    case 0xe8:p->need=3;p->variable_response=1;break;
    case 0x20:p->variable_input=1;break;
    case 0x6f:case 0x8f:case 0x9a:case 0xdf:p->variable_input=p->variable_response=1;break;
    case 0x78:p->variable_response=1;break;
    case 0x18:case 0x19:case 0x21:case 0x38:case 0x48:case 0x49:case 0x6b:case 0xd0:case 0xdc:case 0xea:break;
    default:p->variable_input=1;p->variable_response=!!(command&8);break;
    }if(!p->need&&!p->variable_input)nd_pmu_reply(p);
}
static void nd_pmu_transfer(PtPmu *p){
    int output=!!(p->via[ND_ACR]&0x10);uint8_t value=p->via[ND_SR];
    if((p->via[ND_DDRB]&0x18)!=0x10){nd_pmu_fault(p,PT_PMU_BAD_PINS);return;}
    if(!p->state){if(!output){nd_pmu_fault(p,PT_PMU_IDLE_DIRECTION);return;}nd_pmu_start(p,value);}
    else if(p->state==1){
        if(!output){nd_pmu_fault(p,PT_PMU_COMMAND_DIRECTION);return;}
        if(p->variable_input){p->need=value;p->variable_input=0;if(!value)nd_pmu_reply(p);}
        else {if(p->received<sizeof p->input)p->input[p->received]=value;p->received++;if(p->received==p->need)nd_pmu_reply(p);}
    }else{
        if(output){nd_pmu_fault(p,PT_PMU_RESPONSE_DIRECTION);return;}
        if(p->variable_response){p->via[ND_SR]=(uint8_t)p->reply_size;p->variable_response=0;if(!p->reply_size)p->state=0;}
        else if(p->sent<p->reply_size){p->via[ND_SR]=p->response[p->sent++];if(p->sent==p->reply_size)p->state=0;}
        else nd_pmu_fault(p,PT_PMU_RESPONSE_OVERRUN);
    }
}
static uint64_t nd_via_period(uint16_t latch){return ((uint64_t)latch+2)*1000000000/783360;}
void pt_pmu_service(PtPmu *p,uint64_t now){
    if(now<p->now)return;p->now=now;
    if(now>=p->tick_at){p->intbits|=0x80;p->tick_at+=((now-p->tick_at)/1000000000+1)*1000000000;}
    for(unsigned i=0;i<2;i++){PtViaTimer *t=&p->timer[i];if(t->armed&&now>=t->next){p->via[ND_IFR]|=(uint8_t)(i?0x20:0x40);t->fired=1;
        if(!i&&(p->via[ND_ACR]&0x40)){uint64_t period=nd_via_period(t->latch);t->next+=((now-t->next)/period+1)*period;}
        else t->armed=0;}}
    nd_pmu_irq(p);
}
uint64_t pt_pmu_deadline(PtPmu *p){uint64_t at=p->tick_at;for(unsigned i=0;i<2;i++)if(p->timer[i].armed&&p->timer[i].next<at)at=p->timer[i].next;return at;}
void pt_pmu_init(PtPmu *p,PtGpio *gpio,uint64_t now,uint32_t seconds,void (*irq)(void *,int),void *opaque){
    memset(p,0,sizeof *p);p->gpio=gpio;p->irq=irq;p->opaque=opaque;p->native_ack=1;
    p->via[ND_B]=0x18;p->now=p->origin=now;p->tick_at=now+1000000000;p->rtc_base=seconds+2082844800u;p->ac=1;p->brightness=255;p->intmask=0xff;nd_pmu_irq(p);
}
uint8_t pt_pmu_read(PtPmu *p,unsigned offset,uint64_t now){
    if(offset>=8192||offset%512)return 0xff;
    pt_pmu_service(p,now);unsigned reg=offset/512;uint8_t result=p->via[reg];
    if(reg==ND_B)result=(uint8_t)((result&~8u)|(p->native_ack?8:0));
    if(reg==ND_IFR)result=(uint8_t)((result&0x7f)|(p->native_irq_level?0x80:0));
    if(reg==ND_IER)result|=0x80;
    if(reg==ND_SR){p->via[ND_IFR]&=~4u;nd_pmu_irq(p);}
    if(reg==ND_T1L||reg==ND_T1H||reg==ND_T2L||reg==ND_T2H){unsigned i=reg>=ND_T2L;PtViaTimer *t=&p->timer[i];uint64_t ticks=(uint64_t)(((__uint128_t)(p->now-t->loaded)*783360)/1000000000);if(!i&&(p->via[ND_ACR]&0x40))ticks%=((uint64_t)t->latch+2);uint16_t count=(uint16_t)(t->initial-ticks);result=(uint8_t)(count>>(reg&1?8:0));
        if(!(reg&1)){p->via[ND_IFR]&=(uint8_t)~(i?0x20:0x40);nd_pmu_irq(p);}}
    return result;
}
void pt_pmu_write(PtPmu *p,unsigned offset,uint8_t value,uint64_t now){
    if(offset>=8192||offset%512)return;
    pt_pmu_service(p,now);unsigned reg=offset/512;uint8_t old=p->via[reg];
    if(reg==ND_IER){if(value&0x80)p->via[reg]|=value&0x7f;else p->via[reg]&=(uint8_t)~value;}
    else if(reg==ND_IFR)p->via[reg]&=(uint8_t)~value;
    else {p->via[reg]=value;if(reg==ND_B){
        if((old&0x10)&&!(value&0x10)){p->native_ack=0;nd_pmu_transfer(p);p->via[ND_IFR]|=4;}
        if(!(old&0x10)&&(value&0x10)){p->native_ack=1;if(!p->state&&p->native_pending_power){p->power_request=p->native_pending_power;p->native_pending_power=0;}}}
        if(reg==ND_T1L||reg==ND_T1LL)p->timer[0].latch=(p->timer[0].latch&0xff00)|value;
        if(reg==ND_T1LH)p->timer[0].latch=(uint16_t)((p->timer[0].latch&255)|(value<<8));
        if(reg==ND_T2L)p->timer[1].latch=(p->timer[1].latch&0xff00)|value;
        if(reg==ND_T1H||reg==ND_T2H){unsigned i=reg==ND_T2H;PtViaTimer *t=&p->timer[i];t->initial=t->latch=(uint16_t)((t->latch&255)|value<<8);t->loaded=p->now;t->next=p->now+nd_via_period(t->latch);t->armed=1;t->fired=0;p->via[ND_IFR]&=(uint8_t)~(i?0x20:0x40);}}
    nd_pmu_irq(p);
}
unsigned pt_pmu_take_power(PtPmu *p){unsigned result=p->power_request;p->power_request=0;return result;}
const char *pt_pmu_error_name(unsigned reason){
    static const char *const names[]={"bad handshake pins","input in idle","input during command","output during response","response overrun","command exceeds input capacity","battery payload","power-off signature","sleep signature","power-events payload"};
    return reason<PT_PMU_ERROR_COUNT?names[reason]:"unknown";
}
