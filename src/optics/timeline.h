/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef PT_R2_TIMELINE_H
#define PT_R2_TIMELINE_H
#include <stddef.h>
#include <stdint.h>
typedef struct PtR2Timeline PtR2Timeline;
typedef struct {uint64_t epoch,id;} PtR2TimelineToken;
typedef enum {PT_R2_PENDING,PT_R2_READY,PT_R2_FAILED,PT_R2_STALE,PT_R2_INVALID,PT_R2_FULL} PtR2TimelineResult;
/* CPU/device owner ledger. GPU worker messages are immutable inputs, not
 * calls from arbitrary completion threads. Mailbox storage is a separate cap. */
PtR2Timeline *pt_r2_timeline_create(uint32_t batches,uint32_t visibility,uint32_t fences);
void pt_r2_timeline_destroy(PtR2Timeline *);
int pt_r2_timeline_reset(PtR2Timeline *);
uint64_t pt_r2_timeline_issue(PtR2Timeline *);
/* A submitted batch covers the prefix after the last submission through last.
 * Keep an explicit mapping between this token and the renderer's GPU ticket. */
PtR2TimelineResult pt_r2_timeline_submit(PtR2Timeline *,uint64_t last,PtR2TimelineToken *);
PtR2TimelineResult pt_r2_timeline_complete(PtR2Timeline *,PtR2TimelineToken,int success);
PtR2TimelineResult pt_r2_visibility_create(PtR2Timeline *,uint64_t producing_sequence,PtR2TimelineToken *);
/* Success only after the required bytes were actually applied by the I/O owner.
 * CPU apply/GPU notification may arrive in either order; fences check BOTH. */
PtR2TimelineResult pt_r2_visibility_finish(PtR2Timeline *,PtR2TimelineToken,int success);
PtR2TimelineResult pt_r2_visibility_release(PtR2Timeline *,PtR2TimelineToken);
/* <=16 exact visibility dependencies; never join unrelated future work. */
PtR2TimelineResult pt_r2_fence_create(PtR2Timeline *,uint64_t after,const PtR2TimelineToken *,size_t,PtR2TimelineToken *);
PtR2TimelineResult pt_r2_fence_poll(PtR2Timeline *,PtR2TimelineToken);
PtR2TimelineResult pt_r2_fence_release(PtR2Timeline *,PtR2TimelineToken);
uint64_t pt_r2_timeline_gpu_done(const PtR2Timeline *);
#endif
