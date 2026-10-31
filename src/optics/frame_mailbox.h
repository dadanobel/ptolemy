/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once
#import "renderer.h"
/* One completed pending frame. An older completed frame never blocks a newer
 * scanout. In-flight presenter leases remain the presenter's responsibility. */
@interface PtR2FrameMailbox : NSObject
- (BOOL)publish:(PtR2Frame *)frame scanout:(uint64_t)number runEpoch:(uint64_t)epoch;
- (PtR2Frame *)takeLatest:(uint64_t *)number;
/* Called at a quiesced run barrier; pending old frames are dropped. */
- (BOOL)resetToEpoch:(uint64_t)epoch;
- (NSDictionary *)diagnostics;
@end
