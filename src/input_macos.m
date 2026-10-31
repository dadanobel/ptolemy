/* SPDX-License-Identifier: GPL-2.0-or-later */
#import "input_macos.h"
#import <CoreGraphics/CoreGraphics.h>
#include <dlfcn.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "input_keymap.inc"
#include "pointer_math.h"
#include "pointer_calibration.h"
#include "pointer_mailbox.h"
@interface PTHidInputView ()
- (void)sendAbsolutePoint:(NSPoint)point wheel:(int32_t)wheel;
- (BOOL)routeGuestKeyEvent:(NSEvent *)event;
- (BOOL)cursorHidden;
- (void)applyCursorPolicy;
- (void)lostFocus:(NSNotification *)note;
- (void)updateHostKeys;
@end
@implementation PTHidInputView {
    PtHid *_keyboard,*_mouse,*_tablet;PtInputSink _sink;
    BOOL _captured,_modernPointer,_previousCoalescing;
    CGPoint _captureOrigin;
    uint8_t _modifiers[8],_buttons;
    double _dx,_dy,_wheel;
    NSTrackingArea *_tracking;
    BOOL _agentCursorKnown,_agentCursorHidden;
    BOOL _inputPolicyEnabled,_autoBlocked,_automaticCapture,_sawGuestCursor,_autoEnabled;
    unsigned _menuTracking;
    uint8_t _keys[256];
    NSEventModifierFlags _ignoredModifiers;
    BOOL _hostKeysOff,_keyboardReleased,_screenshotKeys;
    id _eventMonitor;
    NSPoint _lastAbsolute;
    PtPointerCalibration _calibration;double _sentX,_sentY;uint64_t _sentAt,_lastSample;unsigned _guestWidth,_guestHeight;int _observedX,_observedY;uint64_t _observedAt;
}
/* While Tiger has the keyboard, the WindowServer's global hot keys (Spotlight and input sources, Command+Tab, Force Quit) are off for this
 * process's connection, so those keys reach Tiger. The setting belongs to the connection: if the process dies, the Mac has
 * them back. Holding Command+Shift alone turns them on again, so the Mac's screenshots (Command+Shift+3/4/5) still work.
 * Control+Command+Escape gives the keyboard back to the Mac until the next click in Tiger. Private SkyLight entry points,
 * looked up at run time; when absent nothing changes. Mode 2: disabled except Universal Access. */
static void host_hot_keys(BOOL on){
    static int (*connection)(void);static int (*mode)(int,int);static int looked;
    if(!looked){looked=1;connection=(int (*)(void))dlsym(RTLD_DEFAULT,"CGSMainConnectionID");mode=(int (*)(int,int))dlsym(RTLD_DEFAULT,"CGSSetGlobalHotKeyOperatingMode");
        if(!connection||!mode)fprintf(stderr,"input: system shortcuts stay with the Mac (hot key control unavailable)\n");}
    if(connection&&mode)mode(connection(),on?0:2);
}
static NSCursor *blank_cursor(void){
    static NSCursor *blank;
    if(!blank) blank=[[NSCursor alloc] initWithImage:[NSImage imageWithSize:NSMakeSize(16,16) flipped:NO drawingHandler:^BOOL(NSRect r){(void)r;return YES;}] hotSpot:NSZeroPoint];
    return blank;
}
- (void)configureInputPolicy {
    pt_pointer_calibration_reset(&_calibration);
    const char *modern=getenv("PT_POINTER_V2");_modernPointer=modern&&!strcmp(modern,"1");
    const char *flag=getenv("PT_INPUT_UX");_inputPolicyEnabled=flag?!strcmp(flag,"1"):_modernPointer;
    flag=getenv("PT_INPUT_AUTO_CAPTURE");_autoEnabled=!(flag && !strcmp(flag,"0"));
    if(_inputPolicyEnabled)fprintf(stderr,"input: UX policy enabled (auto capture %s; toggle Ctrl+Cmd+G; release Ctrl+Cmd+Escape or Ctrl+Option; Mac shortcuts go to Tiger while it has the keyboard)\n",_autoEnabled?"on":"off");
}
- (instancetype)initWithFrame:(NSRect)frame keyboard:(PtHid *)keyboard
                       mouse:(PtHid *)mouse tablet:(PtHid *)tablet {
    if((self=[super initWithFrame:frame])) { _keyboard=keyboard; _mouse=mouse; _tablet=tablet; [self configureInputPolicy]; }
    return self;
}
- (instancetype)initWithFrame:(NSRect)frame eventSink:(PtInputSink)sink {
    if((self=[super initWithFrame:frame])){_sink=sink;[self configureInputPolicy];}return self;
}
- (BOOL)inputPolicyEnabled { return _inputPolicyEnabled; }
- (BOOL)isRelativeCaptured { return _captured; }
- (BOOL)acceptsFirstResponder { return _sink.send || _keyboard; }
- (BOOL)acceptsFirstMouse:(NSEvent *)event { return _inputPolicyEnabled?YES:[super acceptsFirstMouse:event]; }
- (void)sendKey:(unsigned)usage pressed:(int)pressed {
    if(_inputPolicyEnabled){
        if(!usage || usage>=sizeof _keys || _keys[usage]==!!pressed)return;
        _keys[usage]=!!pressed;
    }
    if(_sink.send){PtInputEvent e={.type=PT_INPUT_KEY,.usage=usage,.pressed=pressed};_sink.send(_sink.opaque,&e);}
    else if(_keyboard)pt_hid_key(_keyboard,usage,pressed);
}
- (BOOL)isFlipped { return YES; }
- (void)releaseKeys {
    if(_sink.send){PtInputEvent e={.type=PT_INPUT_RELEASE};_sink.send(_sink.opaque,&e);}
    if(_keyboard) pt_hid_release(_keyboard);
    if(_mouse) pt_hid_release(_mouse);
    if(_tablet) pt_hid_release(_tablet);
    memset(_modifiers,0,sizeof _modifiers); _buttons=0; _dx=_dy=_wheel=0;
    if(_inputPolicyEnabled){
        memset(_keys,0,sizeof _keys);
        /* A key still physically held after release must not be re-pressed
         * in Tiger by the other half of its flagsChanged sequence. */
        _ignoredModifiers|=NSEvent.modifierFlags & (NSEventModifierFlagControl|NSEventModifierFlagOption|
            NSEventModifierFlagShift|NSEventModifierFlagCommand);
    }
}
- (BOOL)setRelativeCapture:(BOOL)capture {
    NSAssert([NSThread isMainThread],@"input requires the AppKit thread");
    if(capture && _pointerSuspended)return NO;
    if(_inputPolicyEnabled && !capture){_autoBlocked=YES;_automaticCapture=NO;}
    if(capture==_captured){
        if(_inputPolicyEnabled && !capture){[self.window invalidateCursorRectsForView:self];[NSCursor.arrowCursor set];}
        return YES;
    }
    if(capture && self.window.firstResponder!=self) [self.window makeFirstResponder:self];   /* menus and full-screen changes can leave the window itself first responder */
    if(capture && ((!_sink.send && !_mouse) || !self.window.isKeyWindow || self.window.firstResponder!=self)) {
        fprintf(stderr,"input: mouse capture refused (key window %d, first responder %d)\n",self.window.isKeyWindow,self.window.firstResponder==self); return NO;
    }
    if(_modernPointer&&capture){
        CGEventRef sample=CGEventCreate(NULL);if(!sample)return NO;_captureOrigin=CGEventGetLocation(sample);CFRelease(sample);
        NSRect image=NSIsEmptyRect(_guestRect)?self.bounds:_guestRect;
        NSPoint center=[self.window convertPointToScreen:[self convertPoint:NSMakePoint(NSMidX(image),NSMidY(image)) toView:nil]];
        center.y=NSMaxY(NSScreen.screens.firstObject.frame)-center.y;
        if(CGWarpMouseCursorPosition(CGPointMake(center.x,center.y))!=kCGErrorSuccess)return NO;
    }
    CGError error=CGAssociateMouseAndMouseCursorPosition(!capture);
    if(_modernPointer&&error!=kCGErrorSuccess){if(capture)CGWarpMouseCursorPosition(_captureOrigin);return NO;}
    if(_modernPointer){if(capture){_previousCoalescing=NSEvent.isMouseCoalescingEnabled;NSEvent.mouseCoalescingEnabled=NO;}
        else {NSEvent.mouseCoalescingEnabled=_previousCoalescing;CGWarpMouseCursorPosition(_captureOrigin);}}

    if(error!=kCGErrorSuccess && (capture || !_inputPolicyEnabled)) return NO;
    if(error!=kCGErrorSuccess)fprintf(stderr,"input: cursor reassociation failed (%d)\n",(int)error);
    _captured=capture;
    if(capture) [NSCursor hide]; else [NSCursor unhide];
    [self releaseKeys];
    if(_inputPolicyEnabled){
        if(capture)_autoBlocked=NO;
        [self.window invalidateCursorRectsForView:self];
        if(!capture){
            [NSCursor.arrowCursor set];
            if(self.window.isKeyWindow && self.window.firstResponder==self && !_pointerSuspended)
                [self sendAbsolutePoint:[self convertPoint:self.window.mouseLocationOutsideOfEventStream fromView:nil] wheel:0];
        }
        if(self.captureChanged)self.captureChanged(capture);
    }
    return error==kCGErrorSuccess;
}
- (BOOL)pointerInsideImage {
    if(!self.window || !self.window.isVisible)return NO;
    NSPoint p=[self convertPoint:self.window.mouseLocationOutsideOfEventStream fromView:nil];
    NSRect image=NSIsEmptyRect(_guestRect)?self.bounds:_guestRect;
    return NSPointInRect(p,self.bounds) && NSPointInRect(p,image);
}
- (BOOL)tryAutomaticCapture {
    if(!_inputPolicyEnabled || !_autoEnabled || _captured || _autoBlocked || !(_agentCursorKnown || _sawGuestCursor) || ![self cursorHidden] ||
       _pointerSuspended || _menuTracking || !NSApp.isActive || !self.window.isKeyWindow ||
       NSWorkspace.sharedWorkspace.isVoiceOverEnabled ||
       self.window.attachedSheet || NSApp.modalWindow || self.window.firstResponder!=self || ![self pointerInsideImage])return NO;
    if(![self setRelativeCapture:YES])return NO;
    _automaticCapture=YES;return YES;
}
/* Called by a LOCAL monitor before AppKit key equivalents (Cmd+Q, Esc).
 * System-reserved combinations remain host-owned; no global event tap. */
- (BOOL)routeGuestKeyEvent:(NSEvent *)e {
    /* A key event provides a second way out if the Ctrl+Option modifier
     * transition is intercepted before reaching this app. App-local only;
     * keep this before responder/menu checks, like the modifier escape. */
    NSEventModifierFlags panic=NSEventModifierFlagControl|NSEventModifierFlagCommand;
    if(_inputPolicyEnabled && (_captured || self.window.isKeyWindow) &&
       (e.type==NSEventTypeKeyDown || e.type==NSEventTypeKeyUp) && e.keyCode==53 &&
       (e.modifierFlags&panic)==panic && !(e.modifierFlags&NSEventModifierFlagOption)){
        if(e.type==NSEventTypeKeyDown && !e.isARepeat){
            [self setRelativeCapture:NO];[self releaseKeys];_ignoredModifiers|=e.modifierFlags;
            _keyboardReleased=YES;[self updateHostKeys];
        }
        return YES;
    }
    /* Emergency release must also work if a sheet/responder stole focus
     * before its ordinary focus-loss cleanup ran. Still app-local. */
    NSEventModifierFlags escape=NSEventModifierFlagControl|NSEventModifierFlagOption;
    if(_inputPolicyEnabled && _captured && e.type==NSEventTypeFlagsChanged && (e.modifierFlags&escape)==escape){
        [self flagsChanged:e];return YES;
    }
    if(!_inputPolicyEnabled || !self.window.isKeyWindow || !NSApp.isActive || _menuTracking ||
       self.window.firstResponder!=self || self.window.attachedSheet || NSApp.modalWindow)return NO;
    if(e.window && e.window!=self.window)return NO;
    if(e.type==NSEventTypeFlagsChanged){
        NSEventModifierFlags all=NSEventModifierFlagCommand|NSEventModifierFlagShift|NSEventModifierFlagControl|NSEventModifierFlagOption;
        BOOL shot=(e.modifierFlags&all)==(NSEventModifierFlagCommand|NSEventModifierFlagShift);
        if(shot!=_screenshotKeys){_screenshotKeys=shot;[self updateHostKeys];}
        [self flagsChanged:e];return YES;
    }
    if(e.type!=NSEventTypeKeyDown && e.type!=NSEventTypeKeyUp)return NO;
    NSEventModifierFlags f=e.modifierFlags;
    BOOL command=!!(f&NSEventModifierFlagCommand),control=!!(f&NSEventModifierFlagControl);
    BOOL option=!!(f&NSEventModifierFlagOption),shift=!!(f&NSEventModifierFlagShift);
    /* Explicit capture also works with a software cursor (Radeon/Tiger).
     * Do not infer cursor visibility from an unused hardware register. */
    if(e.keyCode==5 && command && control && !option && !shift){
        if(e.type==NSEventTypeKeyDown && !e.isARepeat){
            [self releaseKeys];_ignoredModifiers|=e.modifierFlags;
            [self setRelativeCapture:!_captured];
        }
        return YES;
    }
    BOOL fullscreen=e.keyCode==3 && command && control && !option && !shift;
    BOOL screenshot=command && shift && !control && (e.keyCode==20 || e.keyCode==21 || e.keyCode==23);
    if(fullscreen){
        [self setRelativeCapture:NO];[self releaseKeys];
        if(e.type==NSEventTypeKeyDown && !e.isARepeat)[self.window toggleFullScreen:nil];
        return YES;
    }
    if(screenshot || (!_hostKeysOff && ((command && e.keyCode==48) || (command && option && e.keyCode==53) ||
       (command && e.keyCode==49) || (control && e.keyCode==49) || (command && control && e.keyCode==12)))){
        [self setRelativeCapture:NO];[self releaseKeys];return NO;
    }
    if(e.type==NSEventTypeKeyDown)[self keyDown:e];else [self keyUp:e];
    return YES;
}
- (void)menuBegan:(NSNotification *)note {(void)note;++_menuTracking;[self lostFocus:nil];}
- (void)menuEnded:(NSNotification *)note {(void)note;if(_menuTracking)--_menuTracking;[self updateHostKeys];}
- (void)lostFocus:(NSNotification *)note { (void)note; [self setRelativeCapture:NO]; [self releaseKeys]; if(_hostKeysOff){_hostKeysOff=NO;host_hot_keys(YES);} }
- (void)updateHostKeys {
    BOOL off=_inputPolicyEnabled && !_keyboardReleased && !_screenshotKeys && !_menuTracking && NSApp.isActive && self.window.isKeyWindow &&
        self.window.firstResponder==self && !self.window.attachedSheet && !NSApp.modalWindow;
    if(off!=_hostKeysOff){_hostKeysOff=off;host_hot_keys(!off);}
}
- (void)gainedFocus:(NSNotification *)note { (void)note; dispatch_async(dispatch_get_main_queue(),^{[self updateHostKeys];}); }
- (BOOL)becomeFirstResponder { BOOL ok=[super becomeFirstResponder]; if(ok)[self gainedFocus:nil]; return ok; }
- (BOOL)resignFirstResponder { [self lostFocus:nil]; return YES; }
- (void)viewWillMoveToWindow:(NSWindow *)window {
    [self lostFocus:nil];
    [[NSNotificationCenter defaultCenter] removeObserver:self];
    if(_eventMonitor){[NSEvent removeMonitor:_eventMonitor];_eventMonitor=nil;}
    if(window) {
        [[NSNotificationCenter defaultCenter] addObserver:self selector:@selector(lostFocus:)
            name:NSWindowDidResignKeyNotification object:window];
        [[NSNotificationCenter defaultCenter] addObserver:self selector:@selector(lostFocus:)
            name:NSApplicationDidResignActiveNotification object:NSApp];
        if(_inputPolicyEnabled){
            [[NSNotificationCenter defaultCenter] addObserver:self selector:@selector(gainedFocus:)
                name:NSWindowDidBecomeKeyNotification object:window];
            [[NSNotificationCenter defaultCenter] addObserver:self selector:@selector(gainedFocus:)
                name:NSApplicationDidBecomeActiveNotification object:NSApp];
            [[NSNotificationCenter defaultCenter] addObserver:self selector:@selector(lostFocus:)
                name:NSWindowWillExitFullScreenNotification object:window];
            [[NSNotificationCenter defaultCenter] addObserver:self selector:@selector(menuBegan:)
                name:NSMenuDidBeginTrackingNotification object:nil];
            [[NSNotificationCenter defaultCenter] addObserver:self selector:@selector(menuEnded:)
                name:NSMenuDidEndTrackingNotification object:nil];
            __weak PTHidInputView *weakSelf=self;
            _eventMonitor=[NSEvent addLocalMonitorForEventsMatchingMask:
                NSEventMaskKeyDown|NSEventMaskKeyUp|NSEventMaskFlagsChanged handler:^NSEvent *(NSEvent *event){
                    PTHidInputView *view=weakSelf;return view && [view routeGuestKeyEvent:event]?nil:event;
                }];
        }
    }
    [super viewWillMoveToWindow:window];
}
- (void)updateTrackingAreas {
    if(_tracking) [self removeTrackingArea:_tracking];
    _tracking=[[NSTrackingArea alloc] initWithRect:NSZeroRect options:
        NSTrackingMouseMoved|NSTrackingActiveInKeyWindow|NSTrackingInVisibleRect
        owner:self userInfo:nil];
    [self addTrackingArea:_tracking]; [super updateTrackingAreas];
}
- (void)setGuestRect:(NSRect)r { if(NSEqualRects(r,_guestRect))return; _guestRect=r; [self.window invalidateCursorRectsForView:self]; }
- (void)setPointerSuspended:(BOOL)suspended {
    if(_pointerSuspended==suspended)return;
    if(suspended){
        [self setRelativeCapture:NO];[self releaseKeys];
    }
    _pointerSuspended=suspended;[self.window invalidateCursorRectsForView:self];
}
- (BOOL)cursorHidden { return _agentCursorKnown?_agentCursorHidden:_guestCursorHidden; }
- (void)applyCursorPolicy {
    [self.window invalidateCursorRectsForView:self];
    if(!_inputPolicyEnabled)return;
    if(![self cursorHidden]){if(_automaticCapture)[self setRelativeCapture:NO];}
    else [self tryAutomaticCapture];
}
- (void)observeGuestCursor:(unsigned)state {
    if(!_modernPointer)return;
    BOOL known=state==1||state==2,hidden=state==2;
    if(_agentCursorKnown==known&&_agentCursorHidden==hidden)return;
    BOOL expired=_agentCursorKnown&&!known;
    _agentCursorKnown=known;_agentCursorHidden=hidden;
    if(expired&&_automaticCapture)[self setRelativeCapture:NO];
    [self applyCursorPolicy];
}
- (BOOL)captureSuggested {return _inputPolicyEnabled && _agentCursorKnown && _agentCursorHidden && !_captured && !_pointerSuspended;}
- (void)observeGuestPointerX:(int)x y:(int)y width:(unsigned)w height:(unsigned)h at:(uint64_t)at {
    if(_guestWidth!=w||_guestHeight!=h){pt_pointer_calibration_reset(&_calibration);_guestWidth=w;_guestHeight=h;_sentAt=0;}
    if(_captured||_buttons||!_agentCursorKnown||_agentCursorHidden||!NSApp.isActive||!self.window.isKeyWindow||!_sentAt||at<=_lastSample||at<_sentAt||at-_sentAt<200000000ull)return;
    _lastSample=at;BOOL settled=_observedAt>=_sentAt && at>_observedAt && at-_observedAt>=40000000ull && abs(x-_observedX)<=1 && abs(y-_observedY)<=1;
    _observedAt=at;_observedX=x;_observedY=y;if(!settled)return;BOOL was=_calibration.valid;
    pt_pointer_calibration_add(&_calibration,_sentX,_sentY,(double)x/(w-1),(double)y/(h-1),w,h);
    if(!was&&_calibration.valid){NSLog(@"Pointer mapping calibrated: %.6f/%.6f, %.6f/%.6f",_calibration.scale[0],_calibration.scale[1],_calibration.bias[0],_calibration.bias[1]);_lastAbsolute=NSMakePoint(-1,-1);}
}
- (void)setGuestCursorHidden:(BOOL)hidden {
    if(_inputPolicyEnabled && !hidden)_sawGuestCursor=YES;
    if(_guestCursorHidden==hidden)return;_guestCursorHidden=hidden;[self applyCursorPolicy];
}
/* Over the image the host cursor is blank: the absolute tablet puts Tiger's cursor under it. */
- (BOOL)hostCursorBlank {
    static int old=-1; if(old<0){const char *v=getenv("PT_HOST_CURSOR");old=v&&atoi(v)!=0;}
    return (_sink.send || _tablet) && !_pointerSuspended && !(old && [self cursorHidden]) && !(_inputPolicyEnabled && _autoBlocked);
}
/* One cursor, never stuck at an edge, also after the pointer visited the Mac menu bar or Dock. Cursor rects are
 * re-applied only when the pointer crosses their edge, and a Dock or menu bar shown over
 * the window is no crossing. Every presenter frame: over the visible image of this key window (in full screen: anywhere
 * on its screen) keep the blank cursor, elsewhere the arrow. While this window is key, Tiger's cursor follows the Mac
 * pointer to the nearest image edge (sendAbsolutePoint clamps): a pointer that leaves the image fast, or that enters
 * the full-screen strip above the image (camera housing, where Tiger's cursor would stop below its menu bar), delivers
 * no further events to this view. */
- (void)pollPointer {
    if(_captured || !self.window)return;
    NSCursor *blank=blank_cursor();
    BOOL active=[self hostCursorBlank] && NSApp.isActive && self.window.isKeyWindow;
    NSPoint screen=NSEvent.mouseLocation;
    BOOL hide=active && ((self.window.styleMask&NSWindowStyleMaskFullScreen) ? NSPointInRect(screen,self.window.screen.frame) :
        [NSWindow windowNumberAtPoint:screen belowWindowWithWindowNumber:0]==self.window.windowNumber && [self pointerInsideImage]);
    if(hide){if(NSCursor.currentCursor!=blank)[blank set];}
    else if(NSCursor.currentCursor==blank)[NSCursor.arrowCursor set];
    if(!active || self.window.firstResponder!=self)return;
    NSPoint p=[self convertPoint:self.window.mouseLocationOutsideOfEventStream fromView:nil];
    if(NSEqualPoints(p,_lastAbsolute) || (_inputPolicyEnabled && [self pointerInsideImage] && [self tryAutomaticCapture]))return;
    [self sendAbsolutePoint:p wheel:0];
}
- (void)resetCursorRects {
    /* The tablet is absolute: the guest cursor sits under the host one, so the host cursor is hidden over the image
     * whenever no capture is needed. _guestCursorHidden is always true with Ptolemy's display driver (the presenter: the hardware
     * cursor also hides while typing), which left two cursors visible; PT_HOST_CURSOR=1 restores that rule. */
    if(![self hostCursorBlank]){
        if(_inputPolicyEnabled)[self addCursorRect:self.bounds cursor:NSCursor.arrowCursor];
        return;
    }
    [self addCursorRect:NSIsEmptyRect(_guestRect)?self.bounds:_guestRect cursor:blank_cursor()];
}
- (void)detach {
    [self lostFocus:nil];_keyboard=_mouse=_tablet=NULL;_sink=(PtInputSink){0};
    if(_eventMonitor){[NSEvent removeMonitor:_eventMonitor];_eventMonitor=nil;}
    self.captureChanged=nil;
}
- (void)dealloc {
    [[NSNotificationCenter defaultCenter] removeObserver:self];
    if(_eventMonitor)[NSEvent removeMonitor:_eventMonitor];
    if(_captured) { CGAssociateMouseAndMouseCursorPosition(true);if(_modernPointer)NSEvent.mouseCoalescingEnabled=_previousCoalescing; [NSCursor unhide]; }
}
- (void)keyDown:(NSEvent *)e {
    if((e.modifierFlags&(NSEventModifierFlagControl|NSEventModifierFlagOption))==
       (NSEventModifierFlagControl|NSEventModifierFlagOption) && e.keyCode==0x35) {
        [self setRelativeCapture:NO]; [self releaseKeys]; return;
    }
    if((_sink.send || _keyboard) && e.keyCode<256 && !e.isARepeat) [self sendKey:mac_to_usb[e.keyCode] pressed:1];
}
- (void)keyUp:(NSEvent *)e {
    if((_sink.send || _keyboard) && e.keyCode<256) [self sendKey:mac_to_usb[e.keyCode] pressed:0];
}
- (void)flagsChanged:(NSEvent *)e {
    if(_inputPolicyEnabled)_ignoredModifiers&=e.modifierFlags;
    if(_captured && (e.modifierFlags&(NSEventModifierFlagControl|NSEventModifierFlagOption))==(NSEventModifierFlagControl|NSEventModifierFlagOption)) {
        [self setRelativeCapture:NO]; [self releaseKeys];
        if(_inputPolicyEnabled)_ignoredModifiers|=e.modifierFlags;
        return;
    }
    if((!_sink.send && !_keyboard) || e.keyCode>=256) return;
    unsigned usage=mac_to_usb[e.keyCode];
    if(usage==57) { [self sendKey:57 pressed:1]; [self sendKey:57 pressed:0]; return; }
    if(usage<224 || usage>=232) return;
    static const NSEventModifierFlags masks[]={NSEventModifierFlagControl,NSEventModifierFlagShift,
        NSEventModifierFlagOption,NSEventModifierFlagCommand};
    unsigned index=usage-224,group=index&3;
    if(_inputPolicyEnabled && (_ignoredModifiers&masks[group]))return;
    if(!(e.modifierFlags&masks[group])) {
        _modifiers[group]=_modifiers[group+4]=0;
        [self sendKey:224+group pressed:0]; [self sendKey:228+group pressed:0];
    } else { _modifiers[index]^=1; [self sendKey:usage pressed:_modifiers[index]]; }
}
/* Keep fractional Retina/trackpad deltas; never round each tiny event away. */
static int32_t whole(double *v) {
    double n=trunc(*v); n=fmax(-2147483647.0,fmin(n,2147483647.0)); *v-=n; return (int32_t)n;
}
- (void)sendAbsolutePoint:(NSPoint)p wheel:(int32_t)wheel {
    NSRect r=NSIsEmptyRect(_guestRect)?self.bounds:_guestRect;
    if((!_sink.send && !_tablet) || r.size.width<=0 || r.size.height<=0)return;
    double x=(p.x-r.origin.x)/r.size.width,y=(p.y-r.origin.y)/r.size.height;
    /* Linear. Tiger maps this tablet differently per release (measured at 1024x768 and 1472x924, both HID models): the
     * DVD's 10.4 linearly, 10.4.11 with a 15% overscan (raw 0.075..0.925 = the screen). An overscan correction would leave
     * the outer 7.5% of 10.4 unreachable (Tiger's menu bar and Dock). Linear reaches
     * every edge on both; on 10.4.11 Tiger's cursor runs about 18% ahead of the Mac pointer until Ptolemy Tools
     * reports the guest's own mapping. */
    const char *r1=getenv("PT_R1_UX");BOOL calibrated=r1&&!strcmp(r1,"1");
    int32_t ax=(calibrated?pt_pointer_calibration_apply(&_calibration,0,x):fmax(0,fmin(1,x)))*32767;
    int32_t ay=(calibrated?pt_pointer_calibration_apply(&_calibration,1,y):fmax(0,fmin(1,y)))*32767;
    _sentX=ax/32767.0;_sentY=ay/32767.0;_sentAt=pt_cursor_now();
    _lastAbsolute=p;
    if(_sink.send){PtInputEvent event={.type=PT_INPUT_ABSOLUTE,.buttons=_buttons,.x=ax,.y=ay,.wheel=wheel};_sink.send(_sink.opaque,&event);}
    else pt_hid_pointer(_tablet,_buttons,ax,ay,wheel);
}
- (void)pointer:(NSEvent *)e motion:(BOOL)motion scroll:(BOOL)scroll {
    if(_pointerSuspended || self.window.firstResponder!=self) return;
    if(_inputPolicyEnabled && (!self.window.isKeyWindow ||
       (!_captured && !_buttons && (motion || scroll) && ![self pointerInsideImage])))return;
    if(_inputPolicyEnabled && motion && [self tryAutomaticCapture])return;
    if(scroll) _wheel+=_modernPointer?pt_pointer_scroll(e.scrollingDeltaY,e.hasPreciseScrollingDeltas):e.scrollingDeltaY;
    int32_t wheel=_modernPointer?pt_pointer_whole(&_wheel):whole(&_wheel);
    if(_captured && (_sink.send || _mouse)) {
        if(motion) { _dx+=e.deltaX; _dy+=e.deltaY; }
        int32_t x=_modernPointer?pt_pointer_whole(&_dx):whole(&_dx),y=_modernPointer?pt_pointer_whole(&_dy):whole(&_dy);
        if(_sink.send){PtInputEvent event={.type=PT_INPUT_RELATIVE,.buttons=_buttons,.x=x,.y=y,.wheel=wheel};_sink.send(_sink.opaque,&event);}
        else pt_hid_pointer(_mouse,_buttons,x,y,wheel);
    } else if((_sink.send || _tablet) && self.bounds.size.width>0 && self.bounds.size.height>0) {
        [self sendAbsolutePoint:[self convertPoint:e.locationInWindow fromView:nil] wheel:wheel];
    }
}
- (void)mouseMoved:(NSEvent *)e { [self pointer:e motion:YES scroll:NO]; }
- (void)mouseDragged:(NSEvent *)e { [self mouseMoved:e]; }
- (void)rightMouseDragged:(NSEvent *)e { [self mouseMoved:e]; }
- (void)otherMouseDragged:(NSEvent *)e { [self mouseMoved:e]; }
- (void)scrollWheel:(NSEvent *)e { [self pointer:e motion:NO scroll:YES]; }
- (void)mouseDown:(NSEvent *)e {
    [self.window makeFirstResponder:self];
    _keyboardReleased=NO;[self updateHostKeys];
    if(_pointerSuspended)return;
    if(_inputPolicyEnabled && !_captured && ![self pointerInsideImage])return;
    if(_inputPolicyEnabled && !_captured && [self pointerInsideImage]){
        _autoBlocked=NO;
        [self.window invalidateCursorRectsForView:self];
        if([self tryAutomaticCapture])return; /* Capture click must not shoot in the game. */
    }
    if(e.buttonNumber<5) _buttons|=1u<<e.buttonNumber;
    [self pointer:e motion:NO scroll:NO];
}
- (void)mouseUp:(NSEvent *)e {
    if(e.buttonNumber<5) _buttons&=~(1u<<e.buttonNumber);
    [self pointer:e motion:NO scroll:NO];
}
- (void)rightMouseDown:(NSEvent *)e { [self mouseDown:e]; }
- (void)otherMouseDown:(NSEvent *)e { [self mouseDown:e]; }
- (void)rightMouseUp:(NSEvent *)e { [self mouseUp:e]; }
- (void)otherMouseUp:(NSEvent *)e { [self mouseUp:e]; }
@end
