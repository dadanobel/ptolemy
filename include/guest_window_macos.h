/* SPDX-License-Identifier: GPL-2.0-or-later */
#import "display_macos.h"
@protocol PTGuestWindowActions <NSObject>
/* Main thread -> the machine adapter's queue; never call devices here. */
- (void)pauseGuest:(BOOL)pause;
- (void)resetGuest;      /* ask Tiger to restart / shut down (Ptolemy Tools), as its Apple menu does */
- (void)shutdownGuest;
- (void)forceStopGuest:(BOOL)restart;   /* stop at once, as a power switch; Tiger's unsaved writes are lost */
- (void)chooseMediaImage;
- (void)ejectMedia:(NSInteger)slot;   /* 0 CD/DVD, 1 USB disk */
- (void)exchangeFiles:(NSArray<NSURL *> *)files;
- (void)exportExchangeFiles;
- (void)setGuestScaling:(NSString *)mode;
- (void)resizeGuestLayer:(CAMetalLayer *)layer points:(CGSize)points scale:(CGFloat)scale fullscreen:(BOOL)fullscreen;
@optional
- (void)guestWindowFocused:(BOOL)focused;   /* YES = key with the pointer over it, NO = key lost (clipboard sync) */
@end
@interface PTGuestWindow : NSWindowController<NSWindowDelegate>
@property(nonatomic,readonly) PTDisplayView *displayView;
@property(nonatomic,readonly) NSMenu *machineMenu,*mediaMenu,*viewMenu;
/* Actions must outlive the window; invalidate on the AppKit thread before
 * destroying machine storage, after the core/GPU report fully stopped. */
- (instancetype)initWithTitle:(NSString *)title input:(PtInputSink)sink actions:(id<PTGuestWindowActions>)actions;
- (void)setGuestPaused:(BOOL)paused;
- (void)setGuestActivity:(NSString *)activity;   /* shown after the title, nil clears it */
- (void)guestStopped;
- (void)updateCaptureSuggestion;
@end
