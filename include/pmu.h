/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef PT_PMU_H
#define PT_PMU_H
#include "platform.h"
typedef struct { uint8_t levels[8],pins[36]; void (*irq)(void *,unsigned,int); void *opaque; } PtGpio;
void pt_gpio_init(PtGpio *,void (*)(void *,unsigned,int),void *);
void pt_gpio_input(PtGpio *,unsigned pin,int high);
uint8_t pt_gpio_read(PtGpio *,unsigned offset);
void pt_gpio_write(PtGpio *,unsigned offset,uint8_t);
typedef struct { uint16_t latch,initial; uint64_t loaded,next; int armed,fired; } PtViaTimer;
enum { PT_POWER_NONE,PT_POWER_OFF,PT_POWER_RESET,PT_POWER_SLEEP };
enum {
    PT_PMU_BAD_PINS, PT_PMU_IDLE_DIRECTION, PT_PMU_COMMAND_DIRECTION,
    PT_PMU_RESPONSE_DIRECTION, PT_PMU_RESPONSE_OVERRUN, PT_PMU_INPUT_TOO_LONG,
    PT_PMU_BATTERY_PAYLOAD, PT_PMU_OFF_PAYLOAD, PT_PMU_SLEEP_PAYLOAD,
    PT_PMU_EVENTS_PAYLOAD, PT_PMU_ERROR_COUNT
};
typedef struct {
    uint64_t count,first_ns;
    unsigned need,received,sent,reply_size;
    uint8_t cmd,state,port_b,ddrb,acr,sr,input[8];
} PtPmuFault;
typedef struct {
    unsigned native_pending_power; int native_ack,native_irq_level;
    uint16_t native_events[2]; uint64_t native_i2c_refused;
    PtGpio *gpio; void (*irq)(void *,int); void *opaque;
    uint8_t via[16],cmd,input[128],response[128],intbits,intmask;
    uint8_t state,variable_input,variable_response,bad_request,unknown[32];
    unsigned need,received,reply_size,sent,power_request;
    uint64_t now,origin,tick_at,protocol_errors,unhandled_commands;
    PtPmuFault faults[PT_PMU_ERROR_COUNT]; /* CPU owner; printed only after stop. */
    uint32_t rtc_base; PtViaTimer timer[2];
    /* Explicit virtual power information: default AC, no battery, lid open.
     * These fields are not a reading of the host's battery. CPU owner only. */
    uint8_t ac,battery,lid,volume,brightness,power[2];
    uint16_t charge,capacity,voltage; int16_t current;
} PtPmu;
void pt_pmu_init(PtPmu *,PtGpio *,uint64_t now_ns,uint32_t unix_seconds,void (*)(void *,int),void *);
void pt_pmu_service(PtPmu *,uint64_t now_ns);
uint64_t pt_pmu_deadline(PtPmu *);
uint8_t pt_pmu_read(PtPmu *,unsigned offset,uint64_t now_ns);
void pt_pmu_write(PtPmu *,unsigned offset,uint8_t,uint64_t now_ns);
/* Request is delivered after the PMU wire response finishes, not mid-packet. */
unsigned pt_pmu_take_power(PtPmu *);
const char *pt_pmu_error_name(unsigned reason);
#endif
