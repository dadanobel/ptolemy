/* SPDX-License-Identifier: GPL-2.0-or-later */
#import "frame_mailbox.h"
#import <os/lock.h>
@implementation PtR2FrameMailbox {
    os_unfair_lock _lock;PtR2Frame *_pending;
    uint64_t _epoch,_newest,_pendingNumber,_replaced,_stale,_taken;
}
- (instancetype)init {if((self=[super init])){_lock=OS_UNFAIR_LOCK_INIT;_epoch=1;}return self;}
- (BOOL)publish:(PtR2Frame *)frame scanout:(uint64_t)n runEpoch:(uint64_t)epoch {
    if(!frame||!frame.texture||!n)return NO;
    PtR2Frame *__attribute__((objc_precise_lifetime)) old=nil;
    os_unfair_lock_lock(&_lock);
    if(epoch!=_epoch||n<=_newest){_stale++;os_unfair_lock_unlock(&_lock);return NO;}
    old=_pending;if(old)_replaced++;_pending=frame;_newest=_pendingNumber=n;
    os_unfair_lock_unlock(&_lock);return YES; /* Release old outside the mailbox lock. */
}
- (PtR2Frame *)takeLatest:(uint64_t *)number {
    os_unfair_lock_lock(&_lock);PtR2Frame *frame=_pending;_pending=nil;
    if(frame){_taken++;if(number)*number=_pendingNumber;}
    os_unfair_lock_unlock(&_lock);return frame;
}
- (BOOL)resetToEpoch:(uint64_t)e {
    PtR2Frame *__attribute__((objc_precise_lifetime)) old=nil;
    os_unfair_lock_lock(&_lock);if(e<=_epoch){os_unfair_lock_unlock(&_lock);return NO;}
    old=_pending;_pending=nil;_epoch=e;_newest=_pendingNumber=0;os_unfair_lock_unlock(&_lock);return YES;
}
- (NSDictionary *)diagnostics {
    os_unfair_lock_lock(&_lock);NSDictionary *d=@{@"runEpoch":@(_epoch),@"newestScanout":@(_newest),@"replaced":@(_replaced),@"stale":@(_stale),@"taken":@(_taken),@"pending":@(_pending!=nil)};
    os_unfair_lock_unlock(&_lock);return d;
}
@end
