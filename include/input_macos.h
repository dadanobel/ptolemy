/* SPDX-License-Identifier: GPL-2.0-or-later */
#import <AppKit/AppKit.h>
#include "hid.h"
#include "input.h"
/* Main-thread-only adapter, ready to host the future CAMetalLayer.
 * HID storage must outlive the view. Call detach before destroying PtDevices.
 * No global event tap; input is delivered only while this view has focus. */
@interface PTHidInputView : NSView
/* Preferred product path. Detach before stopping/destroying the backend. */
- (instancetype)initWithFrame:(NSRect)frame eventSink:(PtInputSink)sink;
- (instancetype)initWithFrame:(NSRect)frame keyboard:(PtHid *)keyboard
                       mouse:(PtHid *)mouse tablet:(PtHid *)tablet;
/* Invoke from an explicit UI action. Under PT_INPUT_UX, Ctrl+Cmd+G toggles
 * capture even without a guest hardware cursor. Ctrl+Cmd+Escape and Ctrl+Option
 * release it. Automatic capture still requires an observed visible cursor. */
- (BOOL)setRelativeCapture:(BOOL)capture;
/* PT_POINTER_V2=1 adds capture warping, scoped event coalescing and precise scroll normalization.
 * It enables PT_INPUT_UX unless explicitly overridden. Native pointer HID polls at 1 ms.
 * PT_INPUT_UX=1 enables the staged input policy, which is off by default.
 * PT_INPUT_AUTO_CAPTURE=0 keeps manual capture with the new keyboard policy.
 * Capture changes, including focus loss, are delivered on the AppKit thread. */
@property(nonatomic,readonly) BOOL inputPolicyEnabled;
@property(nonatomic,readonly,getter=isRelativeCaptured) BOOL relativeCaptured;
@property(nonatomic,copy) void (^captureChanged)(BOOL captured);
/* Guest image in view points (flipped); empty = whole view. The absolute pointer maps through it, and
 * inside it the host cursor is hidden: the guest draws its own. Set by the presenter, main thread. */
@property(nonatomic) NSRect guestRect;
/* Keyboard stays active; used while a host-only surface owns the picture. */
@property(nonatomic) BOOL pointerSuspended;
/* The guest has hidden its cursor (a game): keep the host arrow visible over the guest image,
 * so a released pointer can still be seen. Set by the presenter, main thread. */
@property(nonatomic) BOOL guestCursorHidden;
/* Tools' authenticated Quartz observation: 0 unknown/expired, 1 visible, 2 hidden. */
- (void)observeGuestCursor:(unsigned)state;
/* Once per presenter frame: host cursor and Tiger's position after the pointer returns from the Mac menu bar or Dock. */
- (void)pollPointer;
- (void)observeGuestPointerX:(int)x y:(int)y width:(unsigned)width height:(unsigned)height at:(uint64_t)at;
@property(nonatomic,readonly) BOOL captureSuggested;
- (void)detach;
@end
