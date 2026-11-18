/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Two guest CPUs on two host threads: who may run what, when.
 *
 * One engine lock. A CPU's thread holds it whenever it is outside its vCPU's run: translating, handling an exit,
 * running a device model, walking page tables. It gives the lock up to run guest code and to sleep. So the engine's
 * host-side state (the translator and its tables, the device models as the CPUs call them) is touched by one thread at
 * a time, exactly as with one CPU. Only guest code runs in parallel; what it shares is guest memory and the JIT code.
 *
 * What the lock alone does not give is a moment when no other CPU is inside JIT code: for a flush, which uses the
 * code's memory again, and for a change that another CPU must not run across (a page out of its tables after a tlbie
 * here, the timebase's coefficients). That is a stop of the world: every other CPU is at a block boundary, in its
 * checkpoint or on its way to it, or asleep. A CPU leaves a block at its interrupt check, in microseconds.
 * With one CPU every call returns at once: nothing changes for a machine with one.
 *
 * serial (diagnostic): the lock is kept during the vCPU's run too and handed over at checkpoints. The CPUs then take
 * turns and nothing runs in parallel: the same machine without its concurrency, to tell one kind of defect from the other. */
#ifndef PT_SMP_H
#define PT_SMP_H
#include <stdint.h>
enum { PT_SMP_MAX = 2 };
/* boundary(cpu): make that CPU leave its block at the next interrupt check (any thread). */
void pt_smp_init(int cpus, int serial, void (*boundary)(int cpu));
/* How long a CPU looks for the lock before it sleeps for it (30 us at first; 0: it sleeps at once). The holder is
 * mostly microseconds from handing over - the end of an exit's handling - and a sleep costs a wake-up of the thread. */
void pt_smp_look(uint64_t ns);
int pt_smp_cpus(void);
void pt_smp_attach(int cpu); /* this thread is that CPU; returns with the lock held */
int pt_smp_self(void);
void pt_smp_run_begin(void); /* lock held -> the vCPU may run; the lock is released (kept when serial) */
void pt_smp_run_end(int boundary); /* the vCPU left its run; returns with the lock. boundary: the block is over (its exit,
                                      or a fault that abandons it), not an access in its middle that it will resume */
void pt_smp_sleep_begin(void); /* the CPU idles (at a block boundary): lock released */
void pt_smp_sleep_end(void);
void pt_smp_checkpoint(void); /* lock held, at a block boundary, before a run: where a stop by another CPU is waited out */
void pt_smp_stop_world(void); /* lock held, at a block boundary; returns with every other CPU at one, or asleep */
void pt_smp_start_world(void);
#endif
