/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "pmu.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
enum { B=0,DDRB=2,SR=10,ACR=11,IDLE=0,COMMAND=1,RESPONSE=2 };
static PtPmu p;static PtGpio gpio;static uint64_t now;
static void write_reg(unsigned r,unsigned value) {pt_pmu_write(&p,r*512,value,++now);}
static uint8_t byte(unsigned value,int output) {
    write_reg(ACR,output?0x1c:0x0c);if(output)write_reg(SR,value);
    write_reg(B,8);uint8_t result=pt_pmu_read(&p,SR*512,++now);write_reg(B,24);return result;
}
static void init(void) {
    now=1000;pt_gpio_init(&gpio,NULL,NULL);pt_pmu_init(&p,&gpio,now,0,NULL,NULL);write_reg(DDRB,16);
}
static void check(unsigned reason) {
    uint64_t count=0;for(unsigned i=0;i<PT_PMU_ERROR_COUNT;i++)count+=p.faults[i].count;
    assert(p.protocol_errors==1 && count==1 && p.faults[reason].count==1);
    assert(strcmp(pt_pmu_error_name(reason),"unknown") && p.faults[reason].first_ns>1000);
}
int main(void) {
    init();byte(0,0);check(PT_PMU_IDLE_DIRECTION);
    PtPmuFault first=p.faults[PT_PMU_IDLE_DIRECTION];byte(0,0);
    assert(p.protocol_errors==2 && p.faults[PT_PMU_IDLE_DIRECTION].count==2);
    const PtPmuFault *again=&p.faults[PT_PMU_IDLE_DIRECTION];
    assert(first.first_ns==again->first_ns && first.cmd==again->cmd && first.state==again->state && first.port_b==again->port_b);
    init();byte(0x40,1);byte(0,0);check(PT_PMU_COMMAND_DIRECTION);
    assert(p.faults[PT_PMU_COMMAND_DIRECTION].cmd==0x40 && p.faults[PT_PMU_COMMAND_DIRECTION].state==COMMAND);
    init();byte(0x38,1);byte(0,1);check(PT_PMU_RESPONSE_DIRECTION);
    assert(p.faults[PT_PMU_RESPONSE_DIRECTION].state==RESPONSE);
    init();write_reg(DDRB,24);write_reg(B,0);check(PT_PMU_BAD_PINS);
    init();p.state=RESPONSE;p.reply_size=0;byte(0,0);check(PT_PMU_RESPONSE_OVERRUN); /* Fault injection. */
    init();byte(0x6f,1);byte(0,1);check(PT_PMU_BATTERY_PAYLOAD);
    init();byte(0x8f,1);byte(1,1);byte(6,1);check(PT_PMU_EVENTS_PAYLOAD);
    init();byte(0x6f,1);byte(129,1);for(unsigned i=0;i<129;i++)byte(i,1);check(PT_PMU_INPUT_TOO_LONG);
    assert(p.faults[PT_PMU_INPUT_TOO_LONG].received==129 && p.faults[PT_PMU_INPUT_TOO_LONG].input[7]==7);
    const unsigned commands[]={0x7e,0x7f},reasons[]={PT_PMU_OFF_PAYLOAD,PT_PMU_SLEEP_PAYLOAD};
    for(unsigned j=0;j<2;j++) {
        init();byte(commands[j],1);for(unsigned i=0;i<4;i++)byte('x',1);check(reasons[j]);
        assert(p.faults[reasons[j]].cmd==commands[j] && !memcmp(p.faults[reasons[j]].input,"xxxx",4));
    }
    init();byte(0xe8,1);for(unsigned i=0;i<3;i++)byte(0,1);assert(byte(0,0)==0);
    assert(!p.protocol_errors && !p.unhandled_commands && p.state==IDLE);
    assert(!strcmp(pt_pmu_error_name(PT_PMU_ERROR_COUNT),"unknown"));
    puts("PASS: all 10 PMU fault reasons, aggregate/first context, unsupported opcode remains separate");
}
