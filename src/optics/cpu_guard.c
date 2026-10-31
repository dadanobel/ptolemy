/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Fast VRAM: the CPU-owner route for HVF stage-2 faults on the CPU view of the framebuffer aperture. The board calls it
 * from epicycle/hvf.c vm_enter before the device route, with the vCPU stopped in place. The frontend exports GPU results,
 * converts the faulting 16 KiB granule to the CPU's byte order and opens it (read-only for a read); the access then
 * re-runs natively. */
#include "frontend.h"
#include "cpu_access.h"

static int fault(void *device,uint64_t offset,uint64_t far,int write,uint64_t pc,uint32_t in) {
    (void)far; OpticsA64Memory m;
    if(in && optics_a64_memory_decode(in,&m)) write=!m.load;   /* else the syndrome's WnR */
    static uint64_t last_pc,last_offset; static unsigned repeat;   /* a fault a grant cannot clear is a bug: stop */
    if(pc==last_pc && offset==last_offset){ if(++repeat>64) return -1; } else { last_pc=pc; last_offset=offset; repeat=0; }
    return optics_frontend_cpu_grant(device,offset,write);
}
__attribute__((constructor)) static void install(void) { optics_frontend_set_cpu_fault(fault); }
