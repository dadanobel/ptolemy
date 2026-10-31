/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once
#import "renderer.h"
extern "C" {
#include "io.h"
}
typedef struct {
    PtR2IoToken token;
    uint32_t width,height;
    size_t pitch;
    uint8_t lanes[4];
} PtR2ExportDescriptor;
@class PtR2Export,PtR2ExportData;
/* CPU/I/O owner: reserve before the first GPU write to this resource version. */
PtR2IoResult pt_r2_export_reserve(PtR2Io *,uint64_t gpu_address,uint32_t width,uint32_t height,
    size_t pitch,const uint8_t lanes[4],PtR2ExportDescriptor *);
/* Renderer owner: request materialization at a real CPU/alias visibility boundary.
 * These calls never enter PtR2Io or guest RAM from the renderer thread. */
PtR2Export *pt_r2_export_encode(PtR2Renderer *,PtR2Target *,PtR2ExportDescriptor,uint32_t resolve,NSError **);
PtR2ExportData *pt_r2_export_collect(PtR2Renderer *,PtR2Export *,NSError **);
/* Immutable data may cross a BOUNDED mailbox to the CPU owner. */
@interface PtR2ExportData : NSObject
@property(nonatomic,readonly) size_t byteCount;
@end
/* CPU/I/O owner: only OK means bytes were materialized; STALE/cancel is not success. */
PtR2IoResult pt_r2_export_apply(PtR2Io *,PtR2ExportData *);
/* CPU owner -> renderer job: no second CPU copy, no dangling pointer on IO reset. */
NSData *pt_r2_snapshot_detach(PtR2Io *,PtR2IoToken,NSError **);
