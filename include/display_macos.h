/* SPDX-License-Identifier: GPL-2.0-or-later */
#import "input_macos.h"
#import <QuartzCore/CAMetalLayer.h>
@interface PTDisplayView : PTHidInputView
@property(nonatomic,readonly) CAMetalLayer *metalLayer;
/* Main thread. Renderer owns viewport/scaling/fps, consumes layer on its own queue. */
@property(nonatomic,copy) void (^viewportChanged)(CAMetalLayer *,CGSize points,CGFloat scale,BOOL fullscreen);
@property(nonatomic,copy) void (^filesDropped)(NSArray<NSURL *> *);
@end
