/* SPDX-License-Identifier: GPL-2.0-or-later
 * Ptolemy Agent for Tiger and Leopard (Cocoa, manual retain/release). The agent starts enabled; its status and Stop stay visible. */
#import <Cocoa/Cocoa.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <dlfcn.h>
#include <sys/utsname.h>
#include "sleep_guard.h"

@interface PtolemyAgent : NSObject {
    NSWindow *window;
    NSTextField *status;
    NSButton *toggle;
    NSTextView *text;
    NSStatusItem *item;
    NSTask *task;
    NSPipe *pipe;
    NSTimer *timer;
    NSFileHandle *log;
    BOOL quitting, stopping;
    PtSleepGuard sleepGuard;
    void *powerLibrary;
    NSMenuItem *sleepItem;
}
- (void)toggle:(id)sender;
- (void)guestSleep:(id)sender;
- (void)show:(id)sender;
- (void)poll:(NSTimer *)sender;
@end
@implementation PtolemyAgent
- (void)guestSleep:(id)sender {
    BOOL enable = sender ? !sleepGuard.active : YES;
    int e = pt_sleep_guard_set(&sleepGuard, enable);
    [sleepItem setState:sleepGuard.active ? NSOnState : NSOffState];
    if (!e && sender)
        [[NSUserDefaults standardUserDefaults] setBool:enable forKey:@"PreventGuestIdleSleep"];
    NSString *message =
            e ? [NSString
                        stringWithFormat:@"Guest sleep guard failed (%d). Use Energy Saver: computer sleep Never.\n", e]
              : (enable ? @"Automatic guest sleep prevented while Ptolemy Agent runs. Display sleep and host sleep remain available. Manual guest sleep is unsupported.\n"
                        : @"Guest sleep guard released. Automatic guest sleep may hang this preview.\n");
    [self append:[message dataUsingEncoding:NSUTF8StringEncoding]];
}
- (void)append:(NSData *)data {
    if (![data length])
        return;
    NSString *s = [[[NSString alloc] initWithData:data encoding:NSUTF8StringEncoding] autorelease];
    if (!s)
        s = [[[NSString alloc] initWithData:data encoding:NSISOLatin1StringEncoding] autorelease];
    if (s)
        [[text textStorage] appendAttributedString:[[[NSAttributedString alloc] initWithString:s] autorelease]];
    unsigned n = [[text string] length];
    if (n > 65536)
        [[text textStorage] deleteCharactersInRange:NSMakeRange(0, n - 65536)];
    [text scrollRangeToVisible:NSMakeRange([[text string] length], 0)];
    if (log)
        @try {
            [log writeData:data];
        } @catch (NSException *e) {
            (void)e;
            [log closeFile];
            [log release];
            log = nil;
            [status setStringValue:@"Log write failed. Stop agent and check disk space."];
            if (task && !stopping) {
                stopping = YES;
                [task terminate];
            }
        }
}
- (void)applicationDidFinishLaunching:(NSNotification *)notification {
    (void)notification;
    window =
            [[NSWindow alloc] initWithContentRect:NSMakeRect(0, 0, 680, 400)
                                        styleMask:NSTitledWindowMask | NSClosableWindowMask | NSMiniaturizableWindowMask
                                          backing:NSBackingStoreBuffered
                                            defer:NO];
    [window setTitle:@"Ptolemy - visible development access"];
    [window setReleasedWhenClosed:NO];
    [window center];
    status = [[NSTextField alloc] initWithFrame:NSMakeRect(16, 354, 648, 28)];
    [status setEditable:NO];
    [status setBezeled:NO];
    [status setDrawsBackground:NO];
    [status setStringValue:@"OFF - Enable to allow local Mac tools to run commands as your guest user."];
    [[window contentView] addSubview:status];
    toggle = [[NSButton alloc] initWithFrame:NSMakeRect(16, 312, 220, 32)];
    [toggle setTitle:@"Enable development access"];
    [toggle setTarget:self];
    [toggle setAction:@selector(toggle:)];
    [[window contentView] addSubview:toggle];
    NSScrollView *scroll = [[[NSScrollView alloc] initWithFrame:NSMakeRect(16, 16, 648, 284)] autorelease];
    [scroll setHasVerticalScroller:YES];
    text = [[NSTextView alloc] initWithFrame:NSMakeRect(0, 0, 630, 284)];
    [text setEditable:NO];
    [text setFont:[NSFont userFixedPitchFontOfSize:11]];
    [scroll setDocumentView:text];
    [[window contentView] addSubview:scroll];
    item = [[[NSStatusBar systemStatusBar] statusItemWithLength:NSVariableStatusItemLength] retain];
    [item setTitle:@"Ptolemy: OFF"];
    NSMenu *menu = [[[NSMenu alloc] initWithTitle:@"Ptolemy"] autorelease];
    NSMenuItem *show = [menu addItemWithTitle:@"Show status and command log" action:@selector(show:) keyEquivalent:@""];
    [show setTarget:self];
    NSMenuItem *stop = [menu addItemWithTitle:@"Enable / Stop" action:@selector(toggle:) keyEquivalent:@""];
    [stop setTarget:self];
    [menu addItem:[NSMenuItem separatorItem]];
    [menu addItemWithTitle:@"Quit Ptolemy Agent" action:@selector(terminate:) keyEquivalent:@"q"];
    [item setMenu:menu];
    struct utsname os;
    if (!uname(&os) && atoi(os.release) >= 9) {
        powerLibrary = dlopen("/System/Library/Frameworks/IOKit.framework/IOKit", RTLD_LAZY | RTLD_LOCAL);
        if (powerLibrary) {
            sleepGuard.create = dlsym(powerLibrary, "IOPMAssertionCreate");
            sleepGuard.release = dlsym(powerLibrary, "IOPMAssertionRelease");
        }
        sleepItem = [menu addItemWithTitle:@"Prevent Automatic Guest Sleep"
                                    action:@selector(guestSleep:)
                             keyEquivalent:@""];
        [sleepItem setTarget:self];
        id choice = [[NSUserDefaults standardUserDefaults] objectForKey:@"PreventGuestIdleSleep"];
        if (!choice || [choice boolValue])
            [self guestSleep:nil];
    }
    NSMenu *main = [[[NSMenu alloc] initWithTitle:@""] autorelease];
    NSMenuItem *app = [[[NSMenuItem alloc] initWithTitle:@"Ptolemy Agent" action:NULL keyEquivalent:@""] autorelease];
    [app setSubmenu:menu];
    [main addItem:app];
    [NSApp setMainMenu:main];
    /* Start in the menu bar only; the window opens from "Show status and command log", so it never covers or steals
     * focus from games at login. Access state and Stop remain visible in the menu bar. */
    timer = [[NSTimer scheduledTimerWithTimeInterval:0.1 target:self selector:@selector(poll:) userInfo:nil
                                             repeats:YES] retain];
    [self toggle:nil];
}
- (void)show:(id)sender {
    (void)sender;
    [window makeKeyAndOrderFront:nil];
    [NSApp activateIgnoringOtherApps:YES];
}
- (void)toggle:(id)sender {
    (void)sender;
    if (task) {
        if (!stopping) {
            stopping = YES;
            [task terminate];
            [toggle setEnabled:NO];
            [status setStringValue:@"Stopping command group and disabling access..."];
        }
        return;
    }
    NSString *dir = [NSHomeDirectory() stringByAppendingPathComponent:@"Library/Application Support/Ptolemy"];
    NSString *path = [dir stringByAppendingPathComponent:@"agent.log"];
    NSDictionary *attr = [[NSFileManager defaultManager] fileAttributesAtPath:path traverseLink:NO];
    /* Keep earlier logs; never silently truncate an existing development trace. */
    /* attr is nil for a missing log: a 64-bit return from a message to nil is undefined on PPC. */
    if (attr && [[attr objectForKey:NSFileSize] unsignedLongLongValue] > 4 * 1024 * 1024) {
        NSString *old = [path stringByAppendingFormat:@".%@", [[NSProcessInfo processInfo] globallyUniqueString]];
        if (![[NSFileManager defaultManager] movePath:path toPath:old handler:nil]) {
            [status setStringValue:@"Cannot rotate command log; agent stays OFF."];
            return;
        }
    }
    if (![[NSFileManager defaultManager] fileExistsAtPath:path])
        [[NSFileManager defaultManager]
                createFileAtPath:path
                        contents:[NSData data]
                      attributes:[NSDictionary dictionaryWithObject:[NSNumber numberWithInt:0600]
                                                             forKey:NSFilePosixPermissions]];
    log = [[NSFileHandle fileHandleForWritingAtPath:path] retain];
    if (!log) {
        [status setStringValue:@"Cannot open command log; agent stays OFF."];
        return;
    }
    [log seekToEndOfFile];
    task = [[NSTask alloc] init];
    pipe = [[NSPipe pipe] retain];
    [task setLaunchPath:[[NSBundle mainBundle] pathForResource:@"ptolemy-agent-worker" ofType:nil]];
    [task setStandardOutput:pipe];
    [task setStandardError:pipe];
    int fd = [[pipe fileHandleForReading] fileDescriptor];
    fcntl(fd, F_SETFL, O_NONBLOCK);
    @try {
        [task launch];
        [[pipe fileHandleForWriting] closeFile];
    } @catch (NSException *e) {
        [status setStringValue:[e reason]];
        [task release];
        task = nil;
        [pipe release];
        pipe = nil;
        [log closeFile];
        [log release];
        log = nil;
        return;
    }
    stopping = NO;
    [toggle setTitle:@"Stop development access"];
    [item setTitle:@"Ptolemy: ON"];
    [status setStringValue:
                    @"ON - Commands and results are logged below and in ~/Library/Application Support/Ptolemy/agent.log"];
}
- (void)poll:(NSTimer *)sender {
    (void)sender;
    if (!task)
        return;
    unsigned char bytes[8192];
    int fd = [[pipe fileHandleForReading] fileDescriptor];
    for (unsigned i = 0; i < 32; i++) {
        ssize_t n = read(fd, bytes, sizeof bytes);
        if (n > 0)
            [self append:[NSData dataWithBytes:bytes length:n]];
        else
            break;
    }
    if (![task isRunning]) {
        int result = [task terminationStatus];
        [task release];
        task = nil;
        [pipe release];
        pipe = nil;
        [log closeFile];
        [log release];
        log = nil;
        [item setTitle:@"Ptolemy: OFF"];
        [toggle setTitle:@"Enable development access"];
        [toggle setEnabled:YES];
        stopping = NO;
        [status setStringValue:
                        [NSString
                                stringWithFormat:
                                        @"OFF - Worker exited (%d). Log retained. If it crashed during a command, restart the guest system before reconnecting.",
                                        result]];
        if (quitting)
            [NSApp terminate:nil];
    }
}
- (NSApplicationTerminateReply)applicationShouldTerminate:(NSApplication *)application {
    (void)application;
    if (task) {
        quitting = YES;
        [self toggle:nil];
        return NSTerminateCancel;
    }
    return NSTerminateNow;
}
- (void)dealloc {
    pt_sleep_guard_set(&sleepGuard, 0);
    if (powerLibrary)
        dlclose(powerLibrary);
    [timer invalidate];
    [timer release];
    [[NSStatusBar systemStatusBar] removeStatusItem:item];
    [item release];
    [window release];
    [text release];
    [status release];
    [toggle release];
    [super dealloc];
}
@end
int main(void) {
    NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];
    NSApplication *app = [NSApplication sharedApplication];
    PtolemyAgent *delegate = [[PtolemyAgent alloc] init];
    [app setDelegate:delegate];
    [app run];
    [delegate release];
    [pool drain];
    return 0;
}
