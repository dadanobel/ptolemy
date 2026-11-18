/* SPDX-License-Identifier: GPL-2.0-or-later */
#import <Cocoa/Cocoa.h>
#import <ApplicationServices/ApplicationServices.h>
#include <OpenGL/gl.h>
#include <unistd.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "bench.h"
#include "image.h"
#include "presentation.h"
#include "panion_gl.h"

@interface AntDisplayView : NSOpenGLView
@end
@implementation AntDisplayView
- (void)drawRect:(NSRect)rect { (void)rect; }
@end

static int pump(void) {
    NSEvent *event;
    while((event=[NSApp nextEventMatchingMask:NSAnyEventMask untilDate:[NSDate distantPast]
                 inMode:NSDefaultRunLoopMode dequeue:YES])) {
        if([event type]==NSKeyDown && [[event charactersIgnoringModifiers] isEqual:@"\033"])return 0;
        [NSApp sendEvent:event];
    }
    return 1;
}
static void draw_frame(unsigned frame) {
    unsigned x,y;
    glViewport(0,0,ANT_SCREEN_W,ANT_SCREEN_H);
    glMatrixMode(GL_PROJECTION);glLoadIdentity();glOrtho(0,ANT_SCREEN_W,0,ANT_SCREEN_H,-1,1);
    glMatrixMode(GL_MODELVIEW);glLoadIdentity();glDisable(GL_DITHER);glDisable(GL_BLEND);
    glDisable(GL_DEPTH_TEST);glDisable(GL_TEXTURE_2D);glDisable(GL_SCISSOR_TEST);
    glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE);glClearColor(0,0,0,1);glClear(GL_COLOR_BUFFER_BIT);
    glBegin(GL_QUADS);
    for(y=0;y<16;++y)for(x=0;x<16;++x) {
        unsigned char v=ant_tile(x,y,frame);glColor4ub(v,v,v,255);
        glVertex2i(x*32,y*16);glVertex2i((x+1)*32,y*16);
        glVertex2i((x+1)*32,(y+1)*16);glVertex2i(x*32,(y+1)*16);
    }
    glEnd();
}
/* Read the scanout without capturing the display or bypassing WindowServer. */
static int screen_pixels(NSWindow *window,NSView *view,unsigned char *pixels,int w,int h) {
    CGDirectDisplayID display=CGMainDisplayID();
    const volatile unsigned char *base=CGDisplayBaseAddress(display);
    size_t stride=CGDisplayBytesPerRow(display),width=CGDisplayPixelsWide(display),height=CGDisplayPixelsHigh(display);
    ant_text("capture_method","CGDisplayBaseAddress, read-only big-endian xRGB32");
    ant_number("capture_bits",CGDisplayBitsPerPixel(display));ant_number("capture_stride",stride);
    if(!base || CGDisplayBitsPerPixel(display)!=32 || CGDisplayBitsPerSample(display)!=8 ||
       CGDisplaySamplesPerPixel(display)!=3 || stride<width*4)return 0;
    NSRect screen=[[[NSScreen screens] objectAtIndex:0] frame];
    NSPoint origin=[window convertBaseToScreen:[view convertPoint:NSMakePoint(0,0) toView:nil]];
    int left=(int)lround(origin.x-screen.origin.x),top=(int)lround(NSMaxY(screen)-origin.y-h);
    ant_number("capture_left",left);ant_number("capture_top",top);
    if(width!=(size_t)screen.size.width || height!=(size_t)screen.size.height || left<0 || top<0 ||
       left+w>(int)width || top+h>(int)height)return 0;
    unsigned x,y;
    for(y=0;y<(unsigned)h;++y)for(x=0;x<(unsigned)w;++x) {
        const volatile unsigned char *q=base+(top+h-1-y)*stride+4*(left+x);
        unsigned char *p=pixels+4*(y*w+x);
        p[0]=q[1];p[1]=q[2];p[2]=q[3];p[3]=255;
    }
    return 1;
}
static int check_pixels(const char *dir,const char *name,unsigned char *actual,unsigned char *expected,unsigned char *diff) {
    unsigned maximum;size_t bad=ant_compare_rgba(actual,expected,ANT_SCREEN_W*ANT_SCREEN_H,1,diff,&maximum);
    char key[80];snprintf(key,sizeof key,"%s_mismatched_pixels",name);ant_number(key,bad);
    snprintf(key,sizeof key,"%s_max_channel_error",name);ant_number(key,maximum);
    if(!ant_image(dir,name,actual,ANT_SCREEN_W,ANT_SCREEN_H) ||
       !ant_image(dir,"reference",expected,ANT_SCREEN_W,ANT_SCREEN_H))return 0;
    snprintf(key,sizeof key,"%s-difference",name);
    if(!ant_image(dir,key,diff,ANT_SCREEN_W,ANT_SCREEN_H))return 0;
    return !bad;
}
int ant_window(const char *id,const char *dir) {
    NSAutoreleasePool *pool=[[NSAutoreleasePool alloc] init];
    NSWindow *window=nil;AntDisplayView *view=nil;NSOpenGLContext *context=nil;NSOpenGLPixelBuffer *buffer=nil;
    unsigned char *actual=NULL,*expected=NULL,*diff=NULL;int ok=0,visible=!strcmp(id,"gpu-window");
    unsigned sample,frame,generation=0;double values[ANT_SAMPLES];
    if(visible) {
        UpdateSystemActivity(UsrActivity);
        ProcessSerialNumber psn={0,kCurrentProcess};
        OSStatus err=TransformProcessType(&psn,kProcessTransformToForegroundApplication);
        ant_number("foreground_status",err);
        if(err!=noErr){ant_text("reason","Cannot create foreground test process");goto done;}
        SetFrontProcess(&psn);
    }
    [NSApplication sharedApplication];[NSApp finishLaunching];
    NSOpenGLPixelFormatAttribute attributes[]={NSOpenGLPFAAccelerated,NSOpenGLPFANoRecovery,
        visible?NSOpenGLPFADoubleBuffer:NSOpenGLPFAPixelBuffer,NSOpenGLPFAColorSize,24,NSOpenGLPFAAlphaSize,8,0};
    NSOpenGLPixelFormat *format=[[[NSOpenGLPixelFormat alloc] initWithAttributes:attributes] autorelease];
    if(!format){ant_text("reason","Requested display format unavailable");goto done;}
    context=[[NSOpenGLContext alloc] initWithFormat:format shareContext:nil];if(!context)goto done;
    if(visible) {
        NSRect usable=[[[NSScreen screens] objectAtIndex:0] visibleFrame];
        if(usable.size.width<ANT_SCREEN_W+32 || usable.size.height<ANT_SCREEN_H+64)goto done;
        window=[[NSWindow alloc] initWithContentRect:NSMakeRect(usable.origin.x+16,NSMaxY(usable)-ANT_SCREEN_H-40,
            ANT_SCREEN_W,ANT_SCREEN_H) styleMask:NSTitledWindowMask backing:NSBackingStoreBuffered defer:NO];
        [window setReleasedWhenClosed:NO];[window setTitle:@"Antiochus display test - keep visible (Esc stops test)"];
        [window setLevel:NSFloatingWindowLevel];[window setHidesOnDeactivate:NO];
        view=[[AntDisplayView alloc] initWithFrame:NSMakeRect(0,0,ANT_SCREEN_W,ANT_SCREEN_H) pixelFormat:format];
        [view setOpenGLContext:context];[window setContentView:view];[window makeKeyAndOrderFront:nil];
        [NSApp activateIgnoringOtherApps:YES];if(!pump())goto done;[context setView:view];[context update];
    } else {
        buffer=[[NSOpenGLPixelBuffer alloc] initWithTextureTarget:GL_TEXTURE_2D textureInternalFormat:GL_RGBA
                  textureMaxMipMapLevel:0 pixelsWide:ANT_SCREEN_W pixelsHigh:ANT_SCREEN_H];
        if(!buffer)goto done;
        [context setPixelBuffer:buffer cubeMapFace:0 mipMapLevel:0 currentVirtualScreen:[context currentVirtualScreen]];
    }
    [context makeCurrentContext];
    {long swap=0;[context setValues:&swap forParameter:NSOpenGLCPSwapInterval];}
    ant_text("gl_vendor",(const char *)glGetString(GL_VENDOR));ant_text("gl_renderer",(const char *)glGetString(GL_RENDERER));
    ant_text("gl_version",(const char *)glGetString(GL_VERSION));ant_text("drawable",visible?"visible window":"pbuffer control");
    ant_text("timing_scope","draw+finish and present+finish per frame; excludes pacing and screenshots");
    ant_number("frames_per_sample",ANT_SCREEN_FRAMES);ant_number("target_hz",60);ant_number("swap_interval",0);
    ant_number("width",ANT_SCREEN_W);ant_number("height",ANT_SCREEN_H);
    actual=malloc(ANT_SCREEN_W*ANT_SCREEN_H*4);expected=malloc(ANT_SCREEN_W*ANT_SCREEN_H*4);diff=malloc(ANT_SCREEN_W*ANT_SCREEN_H*4);
    if(!actual || !expected || !diff)goto done;
    for(sample=0;sample<ANT_SAMPLES;++sample) {
        if(visible)UpdateSystemActivity(UsrActivity);
        double start=ant_now(),wall=[NSDate timeIntervalSinceReferenceDate],draw=0,present=0;unsigned late=0;char key[80];
        NSAutoreleasePool *iteration=[[NSAutoreleasePool alloc] init];
        for(frame=0;frame<ANT_SCREEN_FRAMES;++frame) {
            double t=ant_now();draw_frame(++generation);glFinish();draw+=ant_now()-t;
            t=ant_now();if(visible)[context flushBuffer];else glFlush();glFinish();present+=ant_now()-t;
            if(glGetError()!=GL_NO_ERROR || !pump()){[iteration release];ant_text("reason","GL error or test cancelled");goto done;}
            double remaining=start+(frame+1)/60.0-ant_now();
            if(remaining>0)usleep((useconds_t)(remaining*1e6));else ++late;
        }
        double elapsed=ant_now()-start,wall_elapsed=[NSDate timeIntervalSinceReferenceDate]-wall;
        values[sample]=(draw+present)/ANT_SCREEN_FRAMES;
        if(!(values[sample]>0) || !isfinite(values[sample]) || fabs(elapsed-wall_elapsed)>0.05*elapsed){[iteration release];goto done;}
        snprintf(key,sizeof key,"sample_%u",sample);ant_number(key,values[sample]);
        snprintf(key,sizeof key,"draw_sample_%u",sample);ant_number(key,draw/ANT_SCREEN_FRAMES);
        snprintf(key,sizeof key,"present_sample_%u",sample);ant_number(key,present/ANT_SCREEN_FRAMES);
        snprintf(key,sizeof key,"elapsed_sample_%u",sample);ant_number(key,elapsed);
        snprintf(key,sizeof key,"late_frames_%u",sample);ant_number(key,late);
        /* A fresh back-buffer draw checks GL separately from the previously presented frame. */
        draw_frame(generation);glFinish();glReadBuffer(visible?GL_BACK:GL_FRONT);
        glPixelStorei(GL_PACK_ALIGNMENT,1);glReadPixels(0,0,ANT_SCREEN_W,ANT_SCREEN_H,GL_RGBA,GL_UNSIGNED_BYTE,actual);
        ant_screen_reference(expected,generation);ant_number("checked_frame",generation);
        if(glGetError()!=GL_NO_ERROR || !check_pixels(dir,"rendered",actual,expected,diff)){[iteration release];goto done;}
        if(visible) {
            if(!screen_pixels(window,view,actual,ANT_SCREEN_W,ANT_SCREEN_H)) {
                ant_text("reason","Screen-buffer access unavailable or unsupported pixel layout");[iteration release];goto done;
            }
            if(!check_pixels(dir,"displayed",actual,expected,diff)) {
                ant_text("reason","Composed screen differs; inspect rendering, presentation and window obstruction");[iteration release];goto done;
            }
        }
        [iteration release];
    }
    for(sample=1;sample<ANT_SAMPLES;++sample)for(frame=sample;frame && values[frame]<values[frame-1];--frame) {
        double t=values[frame];values[frame]=values[frame-1];values[frame-1]=t;
    }
    ant_number("median_seconds",values[ANT_SAMPLES/2]);ok=1;
done:
    [NSOpenGLContext clearCurrentContext];[context clearDrawable];[window close];
    [view release];[window release];[context release];[buffer release];free(actual);free(expected);free(diff);[pool release];return ok;
}


int ant_panorama(const char *id,const char *dir,int live) {
    NSAutoreleasePool *pool=[[NSAutoreleasePool alloc] init];
    NSWindow *window=nil;AntDisplayView *view=nil;NSOpenGLContext *context=nil;
    PanScene *scene=calloc(1,sizeof *scene);PanOracle *oracle=malloc(sizeof *oracle);PanGL gpu;
    unsigned char *actual=NULL,*reference=NULL,*difference=NULL,*screen=NULL;
    FILE *frames=NULL;int ok=0,stage=pan_stage(id),ready=0,w=PAN_WIDTH,h=PAN_HEIGHT;unsigned sample,frame=0;
    double samples[ANT_SAMPLES],fps_start=ant_now();unsigned fps_frames=0;char path[4096];
    memset(&gpu,0,sizeof gpu);
    if(stage<0 || !scene || !oracle)goto finish;
    UpdateSystemActivity(UsrActivity);
    {ProcessSerialNumber psn={0,kCurrentProcess};if(!live && TransformProcessType(&psn,kProcessTransformToForegroundApplication)!=noErr)goto finish;SetFrontProcess(&psn);}
    [NSApplication sharedApplication];[NSApp finishLaunching];
    if(live){NSImage *icon=[[NSImage alloc] initWithContentsOfFile:[[NSBundle mainBundle] pathForResource:@"Rosetta" ofType:@"icns"]];if(icon)[NSApp setApplicationIconImage:icon];[icon release];
        NSMenu *menu=[[[NSMenu alloc] initWithTitle:@""] autorelease],*appMenu=[[[NSMenu alloc] initWithTitle:@"Rosetta Stone"] autorelease];
        NSMenuItem *item=[[[NSMenuItem alloc] initWithTitle:@"Rosetta Stone" action:NULL keyEquivalent:@""] autorelease];
        [appMenu addItemWithTitle:@"Quit Rosetta Stone" action:@selector(terminate:) keyEquivalent:@"q"];[item setSubmenu:appMenu];[menu addItem:item];[NSApp setMainMenu:menu];}
    {NSOpenGLPixelFormatAttribute attrs[]={NSOpenGLPFAAccelerated,NSOpenGLPFANoRecovery,NSOpenGLPFADoubleBuffer,NSOpenGLPFAColorSize,24,NSOpenGLPFAAlphaSize,8,NSOpenGLPFADepthSize,24,0};
     NSOpenGLPixelFormat *format=[[[NSOpenGLPixelFormat alloc] initWithAttributes:attrs] autorelease];
     if(!format){ant_text("reason","Accelerated window format unavailable");goto finish;}
     NSRect usable=[[[NSScreen screens] objectAtIndex:0] visibleFrame];
     if(usable.size.width<w+32 || usable.size.height<h+70){ant_text("reason","A 800 x 502 visible desktop is required");goto finish;}
     window=[[NSWindow alloc] initWithContentRect:NSMakeRect(usable.origin.x+16,NSMaxY(usable)-h-40,w,h)
          styleMask:NSTitledWindowMask|NSClosableWindowMask|(live?NSResizableWindowMask|NSMiniaturizableWindowMask:0) backing:NSBackingStoreBuffered defer:NO];
     [window setReleasedWhenClosed:NO];[window setTitle:live?@"Rosetta Stone":[NSString stringWithUTF8String:pan_titles[stage]]];
     [window setMinSize:NSMakeSize(320,240)];if(!live)[window setLevel:NSFloatingWindowLevel];[window setHidesOnDeactivate:NO];
     view=[[AntDisplayView alloc] initWithFrame:NSMakeRect(0,0,w,h) pixelFormat:format];[view setAutoresizingMask:NSViewWidthSizable|NSViewHeightSizable];
     context=[[NSOpenGLContext alloc] initWithFormat:format shareContext:nil];if(!context)goto finish;[view setOpenGLContext:context];[window setContentView:view];[window makeKeyAndOrderFront:nil];
     [NSApp activateIgnoringOtherApps:YES];pump();[context setView:view];[context update];[context makeCurrentContext];}
    {long swap=0;[context setValues:&swap forParameter:NSOpenGLCPSwapInterval];}
    if(!pan_create(scene,stage))goto finish;ready=1;if(!pan_gl_create(&gpu,scene))goto finish;
    ant_text("gl_renderer",(const char *)glGetString(GL_RENDERER));ant_text("gl_version",(const char *)glGetString(GL_VERSION));
    ant_text("mode",live?"interactive, resizable, unscored":"fixed workload, visible, unpaced, synchronized frames");
    ant_number("width",w);ant_number("height",h);ant_number("swap_interval",0);ant_number("agents",scene->count);
    ant_number("infantry",scene->infantry);ant_number("cavalry",scene->cavalry);ant_number("elephants",scene->elephants);ant_number("trees",scene->trees);ant_number("dust_particles",scene->particles);
    ant_text("scene_provenance",stage?"Original artistic reconstruction; counts and terrain are not historical claims":"Original stylized model; marks are not a transcription of the decree");
    if(live) {
        double began=ant_now();
        while([window isVisible] && pump()) {
            NSAutoreleasePool *iteration=[[NSAutoreleasePool alloc] init];
            NSRect bounds=[view bounds];w=(int)bounds.size.width;h=(int)bounds.size.height;[context update];
            if(![window isMiniaturized] && w>0 && h>16) {
                unsigned pose=(unsigned)((ant_now()-began)*30);
                if(!pan_frame(scene,pose,w,h)||!pan_gl_draw(&gpu,scene,0xffffffffu,w,h)){[iteration release];goto finish;}
                [context flushBuffer];glFinish();++fps_frames;
                if(ant_now()-fps_start>=.5){[window setTitle:[NSString stringWithFormat:@"Rosetta Stone | %.1f FPS | %d x %d | unscored",fps_frames/(ant_now()-fps_start),w,h]];fps_start=ant_now();fps_frames=0;}
            } else usleep(50000);
            [iteration release];
        }
        ok=1;goto finish;
    }
    actual=malloc(w*h*4);reference=malloc(w*h*4);difference=malloc(w*h*4);screen=malloc(w*h*4);
    if(!actual||!reference||!difference||!screen)goto finish;
    if(snprintf(path,sizeof path,"%s/frames.tsv",dir)>=(int)sizeof path)goto finish;
    frames=fopen(path,"w");if(!frames)goto finish;
    fprintf(frames,"sample\tframe\tsimulation_seconds\tsubmit_seconds\tfinish_seconds\tpresent_seconds\tframe_seconds\n");
    /* Warm up the draw path, then reset simulation and pose before scored frames. */
    if(!pan_frame(scene,0,w,h)||!pan_gl_draw(&gpu,scene,0,w,h))goto finish;[context flushBuffer];glFinish();
    pan_destroy(scene);if(!pan_create(scene,stage))goto finish;
    for(sample=0;sample<ANT_SAMPLES;++sample) {
        double elapsed=0,start=ant_now(),wall=[NSDate timeIntervalSinceReferenceDate],cpu=0,submit=0,wait=0,present=0;unsigned i;char key[96];
        UpdateSystemActivity(UsrActivity);
        for(i=0;i<PAN_FRAMES;++i) {
            NSAutoreleasePool *iteration=[[NSAutoreleasePool alloc] init];double t=ant_now(),t1,t2,t3,t4;
            if(!pan_frame(scene,++frame,w,h)){[iteration release];goto finish;}t1=ant_now();
            if(!pan_gl_draw(&gpu,scene,frame,w,h)){[iteration release];goto finish;}t2=ant_now();glFinish();t3=ant_now();[context flushBuffer];glFinish();t4=ant_now();
            cpu+=t1-t;submit+=t2-t1;wait+=t3-t2;present+=t4-t3;elapsed+=t4-t;
            fprintf(frames,"%u\t%u\t%.9g\t%.9g\t%.9g\t%.9g\t%.9g\n",sample,frame,t1-t,t2-t1,t3-t2,t4-t3,t4-t);
            ++fps_frames;if(ant_now()-fps_start>=.5){[window setTitle:[NSString stringWithFormat:@"%@ | %.1f FPS",[NSString stringWithUTF8String:pan_titles[stage]],fps_frames/(ant_now()-fps_start)]];fps_start=ant_now();fps_frames=0;}
            if(glGetError()!=GL_NO_ERROR || ![window isVisible] || !pump()){[iteration release];ant_text("reason","GL error, closed window or cancelled");goto finish;}
            [iteration release];
        }
        {double dt=ant_now()-start,dw=[NSDate timeIntervalSinceReferenceDate]-wall;if(dt<=0 || fabs(dt-dw)>.05*dt)goto finish;}
        if(fflush(frames)||ferror(frames))goto finish;samples[sample]=elapsed/PAN_FRAMES;
        snprintf(key,sizeof key,"sample_%u",sample);ant_number(key,samples[sample]);
        snprintf(key,sizeof key,"simulation_sample_%u",sample);ant_number(key,cpu/PAN_FRAMES);
        snprintf(key,sizeof key,"submit_sample_%u",sample);ant_number(key,submit/PAN_FRAMES);
        snprintf(key,sizeof key,"finish_sample_%u",sample);ant_number(key,wait/PAN_FRAMES);
        snprintf(key,sizeof key,"present_sample_%u",sample);ant_number(key,present/PAN_FRAMES);
        ant_number("checked_frame",frame);ant_number("triangles",(scene->mesh[0].n+scene->mesh[1].n+scene->mesh[2].n+scene->mesh[3].n)/3);
        if(!pan_state_check(scene)){ant_text("reason","CPU scene state or spatial index mismatch");goto finish;}
        ant_number("state_hash",scene->state_hash);ant_number("neighbor_checks",scene->comparisons);
        if(!pan_gl_draw(&gpu,scene,frame,w,h))goto finish;glFinish();glReadBuffer(GL_BACK);glPixelStorei(GL_PACK_ALIGNMENT,1);glReadPixels(0,0,w,h,GL_RGBA,GL_UNSIGNED_BYTE,actual);
        if(glGetError()!=GL_NO_ERROR)goto finish;pan_reference(scene,oracle,w,h);int verified=pan_compare(oracle,actual,w,h,reference,difference);
        ant_number("oracle_tested",oracle->tested);ant_number("oracle_actor_tested",oracle->actor_tested);ant_number("oracle_bad",oracle->bad);ant_number("oracle_max_error",oracle->maximum);
        snprintf(key,sizeof key,"frame-%u-rendered",frame);if(!ant_image(dir,key,actual,w,h))goto finish;
        snprintf(key,sizeof key,"frame-%u-reference-probes",frame);if(!ant_image(dir,key,reference,w,h))goto finish;
        snprintf(key,sizeof key,"frame-%u-difference-probes",frame);if(!ant_image(dir,key,difference,w,h))goto finish;
        if(!verified || !pan_marker_check(actual,w,frame)){ant_text("reason","Independent interior-pixel oracle or frame marker failed");goto finish;}
        /* Presentation may lag the GL back buffer. Bound the wait and record it separately. */
        {double began=ant_now();int seen=0;do {usleep(20000);if(!pump() || !screen_pixels(window,view,screen,w,h))goto finish;seen=pan_marker_check(screen,w,frame);}while(!seen && ant_now()-began<.5);
         ant_number("display_settle_seconds",ant_now()-began);snprintf(key,sizeof key,"frame-%u-displayed",frame);if(!ant_image(dir,key,screen,w,h))goto finish;
         if(!seen){ant_text("reason","Displayed frame marker missing or stale after 500 ms");goto finish;}}
        {unsigned maximum=0;size_t bad=0,p;int k;
         for(p=0;p<(size_t)w*h;++p){int mismatch=0;for(k=0;k<3;++k){unsigned delta=abs((int)screen[p*4+k]-actual[p*4+k]);if(delta>maximum)maximum=delta;if(delta>4)mismatch=1;}bad+=mismatch;}
         ant_number("display_rgb_differences_diagnostic",bad);ant_number("display_rgb_max_diagnostic",maximum);}
        fps_start=ant_now();fps_frames=0;
    }
    for(sample=1;sample<ANT_SAMPLES;++sample){unsigned j;for(j=sample;j && samples[j]<samples[j-1];--j){double x=samples[j];samples[j]=samples[j-1];samples[j-1]=x;}}
    ant_number("median_seconds",samples[ANT_SAMPLES/2]);ant_number("fps_from_active_time",1/samples[ANT_SAMPLES/2]);ok=1;
finish:
    if(frames && fclose(frames))ok=0;if(ready){pan_gl_destroy(&gpu);pan_destroy(scene);}
    if(context){[NSOpenGLContext clearCurrentContext];[context clearDrawable];}[window close];[view release];[window release];[context release];
    free(scene);free(oracle);free(actual);free(reference);free(difference);free(screen);[pool release];return ok;
}
