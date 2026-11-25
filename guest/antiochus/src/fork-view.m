/* SPDX-License-Identifier: GPL-2.0-or-later */
#import <Cocoa/Cocoa.h>
#import <ApplicationServices/ApplicationServices.h>
#include "bench.h"
#include "fork-view.h"
static NSAutoreleasePool *pool;
static NSWindow *window;
static NSImageView *image;
static NSTextField *label;
static NSView *canvas;
static NSOpenGLContext *context;
static NSOpenGLPixelFormat *format;
static NSString *lastNote;
static void pump(void) {
    NSEvent *e;
    while((e=[NSApp nextEventMatchingMask:NSAnyEventMask untilDate:[NSDate distantPast]
                                   inMode:NSDefaultRunLoopMode dequeue:YES])) [NSApp sendEvent:e];
    [window displayIfNeeded];
}
int ant_view_open(const char *title,int width,int height) {
    ProcessSerialNumber psn={0,kCurrentProcess};
    pool=[[NSAutoreleasePool alloc] init];
    if(TransformProcessType(&psn,kProcessTransformToForegroundApplication)!=noErr) return 0;
    [NSApplication sharedApplication]; [NSApp finishLaunching];
    if(![NSScreen mainScreen]) return 0;
    window=[[NSWindow alloc] initWithContentRect:NSMakeRect(0,0,540,310) styleMask:NSTitledWindowMask
        backing:NSBackingStoreBuffered defer:NO];
    if(!window) return 0;
    [window setReleasedWhenClosed:NO]; [window setTitle:[NSString stringWithFormat:@"Antiochus · Piglit fork · %s",title]];
    [window center];
    canvas=[[[NSView alloc] initWithFrame:NSMakeRect(10,45,width,height)] autorelease];
    image=[[[NSImageView alloc] initWithFrame:NSMakeRect(275,45,250,250)] autorelease];
    [image setImageScaling:NSScaleToFit];
    label=[[[NSTextField alloc] initWithFrame:NSMakeRect(10,8,520,32)] autorelease];
    [label setEditable:NO]; [label setSelectable:NO]; [label setBezeled:NO]; [label setDrawsBackground:NO];
    [[window contentView] addSubview:canvas]; [[window contentView] addSubview:image]; [[window contentView] addSubview:label];
    [window makeKeyAndOrderFront:nil]; SetFrontProcess(&psn); [NSApp activateIgnoringOtherApps:YES];
    ant_view_note("Preparing test");
    ant_number("window_number",[window windowNumber]); ant_number("window_visible",[window isVisible]);
    return [window isVisible];
}
void ant_view_note(const char *text) {
    if(!window) return;
    CGLContextObj saved=CGLGetCurrentContext();
    [lastNote release];lastNote=[[NSString alloc] initWithUTF8String:text];
    [label setStringValue:lastNote]; pump(); CGLSetCurrentContext(saved);
}
void ant_view_frame(const unsigned char *rgba,int w,int h) {
    if(!window) return;
    CGLContextObj saved=CGLGetCurrentContext();
    NSBitmapImageRep *rep=[[[NSBitmapImageRep alloc] initWithBitmapDataPlanes:NULL pixelsWide:w pixelsHigh:h
        bitsPerSample:8 samplesPerPixel:3 hasAlpha:NO isPlanar:NO colorSpaceName:NSDeviceRGBColorSpace
        bytesPerRow:w*3 bitsPerPixel:24] autorelease];
    if(!rep) return;
    int x,y; unsigned char *pixels=[rep bitmapData];
    for(y=0;y<h;++y) for(x=0;x<w;++x) memcpy(pixels+3*(y*w+x),rgba+4*((h-1-y)*w+x),3);
    NSImage *frame=[[[NSImage alloc] initWithSize:NSMakeSize(w,h)] autorelease];
    [frame addRepresentation:rep]; [image setImage:frame]; pump(); CGLSetCurrentContext(saved);
}
int ant_view_gl(int depth,CGLContextObj *ctx,CGLPixelFormatObj *pf) {
    NSOpenGLPixelFormatAttribute a[]={NSOpenGLPFAWindow,NSOpenGLPFAAccelerated,NSOpenGLPFANoRecovery,
        NSOpenGLPFADoubleBuffer,NSOpenGLPFAColorSize,24,NSOpenGLPFAAlphaSize,8,0,0,0};
    if(depth){a[8]=NSOpenGLPFADepthSize;a[9]=24;}
    format=[[NSOpenGLPixelFormat alloc] initWithAttributes:a]; if(!format) return -1;
    context=[[NSOpenGLContext alloc] initWithFormat:format shareContext:nil]; if(!context) return 0;
    [context setView:canvas]; [context update]; [context makeCurrentContext];
    *ctx=[context CGLContextObj]; *pf=[format CGLPixelFormatObj]; return *ctx && *pf;
}
void ant_view_present(void) { if(context) [context flushBuffer]; }
void ant_view_close(const char *verdict) {
    if(window) {
        NSString *summary=[NSString stringWithFormat:@"%s — %@",verdict,lastNote?lastNote:@""];
        [label setStringValue:summary];pump();
        NSDate *end=[NSDate dateWithTimeIntervalSinceNow:1.0];
        while([end timeIntervalSinceNow]>0) { pump(); [NSThread sleepUntilDate:[NSDate dateWithTimeIntervalSinceNow:.025]]; }
        [context clearDrawable]; [NSOpenGLContext clearCurrentContext]; [context release]; [format release];
        [window close]; [window release];
    }
    window=nil; context=nil; format=nil; [lastNote release];lastNote=nil; [pool drain]; pool=nil;
}
