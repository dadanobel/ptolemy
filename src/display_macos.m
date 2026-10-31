/* SPDX-License-Identifier: GPL-2.0-or-later */
#import "display_macos.h"
#import <Metal/Metal.h>
#include <math.h>
@implementation PTDisplayView
- (instancetype)initWithFrame:(NSRect)frame eventSink:(PtInputSink)sink {
    if((self=[super initWithFrame:frame eventSink:sink])) {
        self.wantsLayer=YES;[self registerForDraggedTypes:@[NSPasteboardTypeFileURL]];
        self.metalLayer.pixelFormat=MTLPixelFormatBGRA8Unorm;self.metalLayer.opaque=YES;
        self.autoresizingMask=NSViewWidthSizable|NSViewHeightSizable;
    }return self;
}
- (CALayer *)makeBackingLayer {return [CAMetalLayer layer];}
- (CAMetalLayer *)metalLayer {return (CAMetalLayer *)self.layer;}
- (void)changed {
    CGFloat scale=self.window.backingScaleFactor;if(scale<=0)scale=1;
    self.metalLayer.contentsScale=scale;
    self.metalLayer.drawableSize=CGSizeMake(fmax(1,fmin(16384,self.bounds.size.width*scale)),fmax(1,fmin(16384,self.bounds.size.height*scale)));
    if(self.viewportChanged)self.viewportChanged(self.metalLayer,self.bounds.size,scale,!!(self.window.styleMask&NSWindowStyleMaskFullScreen));
}
- (void)viewDidMoveToWindow {[super viewDidMoveToWindow];[self changed];}
- (void)viewDidChangeBackingProperties {[super viewDidChangeBackingProperties];[self changed];}
- (void)setFrameSize:(NSSize)size {[super setFrameSize:size];[self changed];}
- (NSDragOperation)draggingEntered:(id<NSDraggingInfo>)sender {(void)sender;return self.filesDropped?NSDragOperationCopy:NSDragOperationNone;}
- (BOOL)performDragOperation:(id<NSDraggingInfo>)sender {
    NSArray *urls=[sender.draggingPasteboard readObjectsForClasses:@[NSURL.class] options:@{NSPasteboardURLReadingFileURLsOnlyKey:@YES}];
    if(!urls.count || !self.filesDropped)return NO;self.filesDropped(urls);return YES;
}
@end
