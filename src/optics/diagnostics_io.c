/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "diagnostics.h"
#include "io.h"
void optics_status_io(OpticsStatus *s,const PtR2Io *io) {
    if(!s||!io)return;PtR2IoStats t;pt_r2_io_stats(io,&t);s->features|=OPTICS_MEMORY_IO;if(t.fatal)s->state=OPTICS_FAILED;
    const unsigned index[]={OPTICS_IO_READS,OPTICS_IO_WRITES,OPTICS_INVALIDATIONS,OPTICS_IO_BUSY,OPTICS_GUARD_CALLS,OPTICS_WATCHED_PAGES,OPTICS_IO_SNAPSHOT_BYTES};
    const uint64_t values[]={t.cpu_reads,t.cpu_writes,t.invalidations,t.busy,t.guard_calls,t.pages,t.snapshot_bytes};
    for(unsigned i=0;i<sizeof(index)/sizeof(*index);i++){s->metric[index[i]]=values[i];s->known|=UINT64_C(1)<<index[i];}
}
