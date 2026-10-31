/* SPDX-License-Identifier: GPL-2.0-or-later */
#import "guest_window_macos.h"
@implementation PTGuestWindow {
    __weak id<PTGuestWindowActions> _actions;
    PTDisplayView *_displayView;NSMenu *_machineMenu,*_mediaMenu,*_viewMenu;
    NSButton *_captureButton;NSMenuItem *_pauseItem,*_captureItem;NSString *_baseTitle;BOOL _paused,_stopped;
}
@synthesize displayView=_displayView,machineMenu=_machineMenu,mediaMenu=_mediaMenu,viewMenu=_viewMenu;
- (NSMenuItem *)item:(NSString *)name selector:(SEL)selector menu:(NSMenu *)menu tag:(NSInteger)tag {
    NSMenuItem *item=[[NSMenuItem alloc] initWithTitle:name action:selector keyEquivalent:@""];
    item.target=self;item.tag=tag;[menu addItem:item];return item;
}
- (instancetype)initWithTitle:(NSString *)title input:(PtInputSink)sink actions:(id<PTGuestWindowActions>)actions {
    NSWindow *window=[[NSWindow alloc] initWithContentRect:NSMakeRect(0,0,1024,768)
        styleMask:NSWindowStyleMaskTitled|NSWindowStyleMaskClosable|NSWindowStyleMaskMiniaturizable|NSWindowStyleMaskResizable backing:NSBackingStoreBuffered defer:NO];
    if((self=[super initWithWindow:window])) {
        _actions=actions;_baseTitle=[title copy];window.title=title;window.delegate=self;window.releasedWhenClosed=NO;
        window.contentMinSize=NSMakeSize(320,200);window.collectionBehavior|=NSWindowCollectionBehaviorFullScreenPrimary;
        _displayView=[[PTDisplayView alloc] initWithFrame:window.contentView.bounds eventSink:sink];window.contentView=_displayView;
        _captureButton=[NSButton buttonWithTitle:@"Capture Mouse" target:self action:@selector(capture:)];
        _captureButton.bezelStyle=NSBezelStyleRounded;_captureButton.hidden=YES;_captureButton.translatesAutoresizingMaskIntoConstraints=NO;
        [_displayView addSubview:_captureButton];[NSLayoutConstraint activateConstraints:@[[_captureButton.trailingAnchor constraintEqualToAnchor:_displayView.trailingAnchor constant:-16],[_captureButton.topAnchor constraintEqualToAnchor:_displayView.topAnchor constant:16]]];
        __weak PTGuestWindow *weakSelf=self;
        _displayView.viewportChanged=^(CAMetalLayer *layer,CGSize points,CGFloat scale,BOOL fs){PTGuestWindow *owner=weakSelf;if(owner)[owner->_actions resizeGuestLayer:layer points:points scale:scale fullscreen:fs];};
        _displayView.filesDropped=^(NSArray<NSURL *> *files){PTGuestWindow *owner=weakSelf;if(owner)[owner->_actions exchangeFiles:files];};
        /* Mac text goes to Tiger only while this window is key and the pointer is over it. */
        [_displayView addTrackingArea:[[NSTrackingArea alloc] initWithRect:NSZeroRect
            options:NSTrackingMouseEnteredAndExited|NSTrackingActiveInKeyWindow|NSTrackingInVisibleRect owner:self userInfo:nil]];
        if(_displayView.inputPolicyEnabled){
            window.acceptsMouseMovedEvents=YES;
            _displayView.toolTip=@"Ctrl+Cmd+G: capture or release the mouse. Ctrl+Cmd+Escape (or Ctrl+Option): release the mouse. Ctrl+Cmd+F: full screen. Cmd+Tab: back to the Mac.";
            _displayView.captureChanged=^(BOOL captured){
                PTGuestWindow *owner=weakSelf;if(!owner)return;
                owner.window.title=captured?[owner->_baseTitle stringByAppendingString:@" — Mouse captured (⌃⌘Esc to release)"]:owner->_baseTitle;
                owner->_captureItem.title=captured?@"Release Mouse (Ctrl+Cmd+Escape)":@"Capture Mouse";
                owner->_captureItem.state=captured?NSControlStateValueOn:NSControlStateValueOff;
            };
        }
        _machineMenu=[[NSMenu alloc] initWithTitle:@"Machine"];
        _pauseItem=[self item:@"Pause" selector:@selector(pause:) menu:_machineMenu tag:0];
        [self item:@"Restart" selector:@selector(reset:) menu:_machineMenu tag:0];
        NSMenuItem *force=[self item:@"Force Restart" selector:@selector(force:) menu:_machineMenu tag:1];   /* Option */
        force.keyEquivalentModifierMask=NSEventModifierFlagOption;force.alternate=YES;
        [self item:@"Shut Down" selector:@selector(shutdown:) menu:_machineMenu tag:0];
        force=[self item:@"Force Shut Down" selector:@selector(force:) menu:_machineMenu tag:0];
        force.keyEquivalentModifierMask=NSEventModifierFlagOption;force.alternate=YES;
        _mediaMenu=[[NSMenu alloc] initWithTitle:@"Media"];
        [self item:@"Insert Disk Image…" selector:@selector(insert:) menu:_mediaMenu tag:0];
        [self item:@"Eject CD/DVD" selector:@selector(eject:) menu:_mediaMenu tag:0];
        [self item:@"Eject Disk Image" selector:@selector(eject:) menu:_mediaMenu tag:1];
        [self item:@"Send Files to Tiger…" selector:@selector(files:) menu:_mediaMenu tag:0];
        [self item:@"Get Files from Tiger…" selector:@selector(export:) menu:_mediaMenu tag:0];
        _viewMenu=[[NSMenu alloc] initWithTitle:@"View"];
        NSMenuItem *fullscreen=[self item:@"Full Screen" selector:@selector(fullscreen:) menu:_viewMenu tag:0];
        fullscreen.keyEquivalent=@"f";fullscreen.keyEquivalentModifierMask=NSEventModifierFlagControl|NSEventModifierFlagCommand;   /* the way out: full screen hides the Mac menu bar */
        _captureItem=[self item:_displayView.inputPolicyEnabled?@"Capture Mouse":@"Capture Mouse (Ctrl+Option to release)" selector:@selector(capture:) menu:_viewMenu tag:0];
        if(_displayView.inputPolicyEnabled){_captureItem.keyEquivalent=@"g";_captureItem.keyEquivalentModifierMask=NSEventModifierFlagControl|NSEventModifierFlagCommand;}
        [self item:@"Fit" selector:@selector(scaling:) menu:_viewMenu tag:0];
        [self item:@"Integer Scale" selector:@selector(scaling:) menu:_viewMenu tag:1];
        [self item:@"Stretch" selector:@selector(scaling:) menu:_viewMenu tag:2];
        [window center];
        /* Restart: the previous process handed over its frame; used once, the next restart sets it again. */
        const char *frame=getenv("PT_WINDOW_FRAME");
        if(frame){NSRect r=NSRectFromString(@(frame));if(!NSIsEmptyRect(r))[window setFrame:r display:NO];unsetenv("PT_WINDOW_FRAME");}
    }return self;
}
- (void)showWindow:(id)sender {[super showWindow:sender];[self.window makeFirstResponder:_displayView];}
- (void)pause:(id)sender {(void)sender;[_actions pauseGuest:!_paused];}
- (void)setGuestPaused:(BOOL)paused {_paused=paused;_pauseItem.title=paused?@"Resume":@"Pause";if(paused)[_displayView setRelativeCapture:NO];}
- (void)reset:(id)sender {(void)sender;[_displayView setRelativeCapture:NO];[_actions resetGuest];}
- (void)shutdown:(id)sender {(void)sender;[_displayView setRelativeCapture:NO];[_actions shutdownGuest];}
- (void)force:(NSMenuItem *)sender {[_displayView setRelativeCapture:NO];[_actions forceStopGuest:sender.tag==1];}
- (void)setGuestActivity:(NSString *)activity {self.window.title=activity?[NSString stringWithFormat:@"%@ — %@",_baseTitle,activity]:_baseTitle;}
- (void)insert:(id)sender {(void)sender;[_actions chooseMediaImage];}
- (void)eject:(NSMenuItem *)sender {[_actions ejectMedia:sender.tag];}
- (void)files:(id)sender {
    (void)sender;NSOpenPanel *panel=[NSOpenPanel openPanel];panel.allowsMultipleSelection=YES;panel.canChooseDirectories=YES;
    [panel beginSheetModalForWindow:self.window completionHandler:^(NSModalResponse response){if(response==NSModalResponseOK)[self->_actions exchangeFiles:panel.URLs];}];
}
- (void)export:(id)sender {(void)sender;[_actions exportExchangeFiles];}
- (void)fullscreen:(id)sender {[self.window toggleFullScreen:sender];}
- (void)capture:(id)sender {
    (void)sender;
    /* A menu key equivalent must not capture an inactive or modal window. */
    if(_displayView.inputPolicyEnabled && (!NSApp.isActive || !self.window.isKeyWindow ||
       self.window.attachedSheet || NSApp.modalWindow))return;
    if(_displayView.inputPolicyEnabled && NSApp.currentEvent.type==NSEventTypeKeyDown && NSApp.currentEvent.isARepeat)return;
    [_displayView setRelativeCapture:_displayView.inputPolicyEnabled?!_displayView.isRelativeCaptured:YES];
}
- (void)scaling:(NSMenuItem *)sender {if(sender.tag>=0 && sender.tag<3)[_actions setGuestScaling:@[@"fit",@"integer",@"stretch"][sender.tag]];}
- (BOOL)windowShouldClose:(NSWindow *)sender {(void)sender;if(_stopped)return YES;[self shutdown:nil];return NO;}
- (void)clipboardToGuest {
    NSPoint p=[_displayView convertPoint:self.window.mouseLocationOutsideOfEventStream fromView:nil];
    if(!_stopped && self.window.isKeyWindow && NSPointInRect(p,_displayView.bounds) &&
       [_actions respondsToSelector:@selector(guestWindowFocused:)])[_actions guestWindowFocused:YES];
}
- (void)mouseEntered:(NSEvent *)event {(void)event;[self clipboardToGuest];}
- (void)windowDidBecomeKey:(NSNotification *)note {(void)note;[self.window makeFirstResponder:_displayView];   /* keys, Esc included, go to the guest */
    [self clipboardToGuest];}
- (void)windowDidResignKey:(NSNotification *)note {(void)note;
    if(!_stopped && [_actions respondsToSelector:@selector(guestWindowFocused:)])[_actions guestWindowFocused:NO];}
- (void)windowDidEnterFullScreen:(NSNotification *)note {
    (void)note;[self.window makeFirstResponder:_displayView];if(_displayView.viewportChanged)_displayView.viewportChanged(_displayView.metalLayer,_displayView.bounds.size,self.window.backingScaleFactor,YES);
}
/* In full screen the bottom edge belongs to Tiger's Dock, so the Mac Dock stays hidden. The Mac menu bar still slides
 * in at the top: without it there is no way to reach Capture Mouse. */
- (NSApplicationPresentationOptions)window:(NSWindow *)window willUseFullScreenPresentationOptions:(NSApplicationPresentationOptions)proposed {
    (void)window;
    return (proposed&~NSApplicationPresentationAutoHideDock)|NSApplicationPresentationHideDock;
}
- (void)windowWillExitFullScreen:(NSNotification *)note {
    (void)note;if(_displayView.inputPolicyEnabled)[_displayView setRelativeCapture:NO];
}
- (void)windowDidExitFullScreen:(NSNotification *)note {
    (void)note;[self.window makeFirstResponder:_displayView];if(_displayView.viewportChanged)_displayView.viewportChanged(_displayView.metalLayer,_displayView.bounds.size,self.window.backingScaleFactor,NO);
}
- (void)updateCaptureSuggestion {const char *v=getenv("PT_R1_UX");_captureButton.hidden=!v||strcmp(v,"1")||_stopped||!_displayView.captureSuggested;}
- (void)guestStopped {
    _stopped=YES;for(NSTrackingArea *t in _displayView.trackingAreas.copy)if(t.owner==self)[_displayView removeTrackingArea:t];   /* owner is not retained */
    [_displayView detach];_displayView.viewportChanged=nil;_displayView.filesDropped=nil;_actions=nil;
    for(NSMenu *menu in @[_machineMenu,_mediaMenu,_viewMenu])for(NSMenuItem *item in menu.itemArray){item.enabled=NO;item.target=nil;item.action=NULL;}
    [self close];
}
@end
