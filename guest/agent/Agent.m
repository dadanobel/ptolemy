/* SPDX-License-Identifier: GPL-2.0-or-later
 * Ptolemy Agent for Tiger and Leopard (Cocoa, manual retain/release). The agent starts enabled; its status and Stop stay visible. */
#import <Cocoa/Cocoa.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
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
    NSMenu *games;
    unsigned frontTries;
}
- (void)toggle:(id)sender;
- (void)guestSleep:(id)sender;
- (void)openGame:(id)sender;
- (void)append:(NSData *)data;
- (void)recentGame:(id)sender;
- (void)show:(id)sender;
- (void)antiochus:(id)sender;
- (void)rosettast:(id)sender;
- (void)poll:(NSTimer *)sender;
@end
@implementation PtolemyAgent
/* DrawSprocket games (guest/dsp/ptdsp.c): started from this menu with the shim of the installed Tools in their
 * environment. The session's environment cannot carry it: Tiger's loginwindow drops DYLD_* from
 * ~/.MacOSX/environment.plist, and in Leopard a library named there and missing stops Finder and the Dock (u215). */
static NSString *shim_path(void) {
    return [NSHomeDirectory() stringByAppendingPathComponent:@"Library/Application Support/Ptolemy/current/ptdsp.dylib"];
}
/* The submenu: Open..., a separator, then the games started from it, the latest first. */
- (void)listGames {
    while ([games numberOfItems] > 2)
        [games removeItemAtIndex:2];
    NSEnumerator *e = [[[NSUserDefaults standardUserDefaults] arrayForKey:@"DrawSprocketGames"] objectEnumerator];
    NSString *path;
    while ((path = [e nextObject])) {
        NSMenuItem *m = [games addItemWithTitle:[[NSFileManager defaultManager] displayNameAtPath:path]
                                         action:@selector(recentGame:)
                                  keyEquivalent:@""];
        [m setTarget:self];
        [m setRepresentedObject:path];
    }
}
/* A program started by NSTask does not come to the front by itself: once its process is known to the window server. */
- (void)front:(NSTimer *)t {
    ProcessSerialNumber psn;
    if (GetProcessForPID([[t userInfo] intValue], &psn) == noErr) {
        SetFrontProcess(&psn);
        [t invalidate];
    } else if (++frontTries > 40)
        [t invalidate];
}
- (void)play:(NSString *)path {
    NSFileManager *files = [NSFileManager defaultManager];
    BOOL folder = NO;
    NSString *program = path;
    if ([files fileExistsAtPath:path isDirectory:&folder] && folder)
        program = [[NSBundle bundleWithPath:path] executablePath];
    NSData *head = program ? [[NSFileHandle fileHandleForReadingAtPath:program] readDataOfLength:8] : nil;
    NSMutableDictionary *env = [NSMutableDictionary dictionaryWithDictionary:[[NSProcessInfo processInfo] environment]];
    NSTask *game = [[[NSTask alloc] init] autorelease];
    [env setObject:shim_path() forKey:@"DYLD_INSERT_LIBRARIES"];
    [game setEnvironment:env];
    if ([head length] == 8 && !memcmp([head bytes], "Joy!peff", 8)) { /* CFM: Apple's loader runs it, as Finder does */
        [game setLaunchPath:@"/System/Library/Frameworks/Carbon.framework/Versions/A/Support/LaunchCFMApp"];
        [game setArguments:[NSArray arrayWithObject:program]];
    } else if (program)
        [game setLaunchPath:program];
    @try {
        if (!program || ![files fileExistsAtPath:shim_path()])
            [NSException raise:@"Ptolemy" format:@"%@", program ? @"The shim of Ptolemy Tools is missing" : @"Not a program"];
        [game launch];
    } @catch (NSException *x) {
        [NSApp activateIgnoringOtherApps:YES];
        NSRunAlertPanel([[files displayNameAtPath:path] stringByAppendingString:@" could not be started"], @"%@", @"OK",
                        nil, nil, [x reason]);
        return;
    }
    NSMutableArray *recent = [NSMutableArray arrayWithObject:path];
    NSEnumerator *e = [[[NSUserDefaults standardUserDefaults] arrayForKey:@"DrawSprocketGames"] objectEnumerator];
    NSString *other;
    while ((other = [e nextObject]) && [recent count] < 6)
        if (![other isEqualToString:path])
            [recent addObject:other];
    [[NSUserDefaults standardUserDefaults] setObject:recent forKey:@"DrawSprocketGames"];
    [self listGames];
    frontTries = 0;
    [NSTimer scheduledTimerWithTimeInterval:0.25
                                     target:self
                                   selector:@selector(front:)
                                   userInfo:[NSNumber numberWithInt:[game processIdentifier]]
                                    repeats:YES];
    [self append:[[NSString stringWithFormat:@"DrawSprocket game started with the shim: %@\n", path]
                         dataUsingEncoding:NSUTF8StringEncoding]];
}
- (void)openGame:(id)sender {
    (void)sender;
    NSOpenPanel *panel = [NSOpenPanel openPanel];
    [NSApp activateIgnoringOtherApps:YES];
    [panel setTitle:@"Start a DrawSprocket Game"];
    if ([panel runModalForDirectory:@"/Applications" file:nil types:nil] == NSOKButton)
        [self play:[panel filename]];
}
- (void)recentGame:(id)sender {
    [self play:[sender representedObject]];
}
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
    /* Antiochus, Ptolemy's own benchmark, and its demonstration: the applications the Tools installer put beside it. */
    NSMenu *tools = [[[NSMenu alloc] initWithTitle:@"Antiochus"] autorelease];
    /* The ellipsis as UTF-8 bytes: GCC 4.0 does not take a non-ASCII character in an @"" literal. */
    [[tools addItemWithTitle:[NSString stringWithUTF8String:"Run Antiochus\xe2\x80\xa6"]
                      action:@selector(antiochus:)
               keyEquivalent:@""] setTarget:self];
    [[tools addItemWithTitle:@"rosettast" action:@selector(rosettast:) keyEquivalent:@""] setTarget:self];
    [[menu addItemWithTitle:@"Antiochus" action:NULL keyEquivalent:@""] setSubmenu:tools];
    [menu addItem:[NSMenuItem separatorItem]];
    [menu addItemWithTitle:@"Quit Ptolemy Agent" action:@selector(terminate:) keyEquivalent:@"q"];
    [item setMenu:menu];
    if ([[NSFileManager defaultManager] fileExistsAtPath:shim_path()]) {
        games = [[NSMenu alloc] initWithTitle:@"DrawSprocket Games"];
        [[games addItemWithTitle:[NSString stringWithUTF8String:"Open\xe2\x80\xa6"]
                          action:@selector(openGame:)
                   keyEquivalent:@""] setTarget:self];
        [games addItem:[NSMenuItem separatorItem]];
        NSMenuItem *g = [[[NSMenuItem alloc] initWithTitle:@"DrawSprocket Games (Cro-Mag Rally)" action:NULL
                                             keyEquivalent:@""] autorelease];
        [g setSubmenu:games];
        [menu insertItem:g atIndex:[menu indexOfItemWithTitle:@"Antiochus"] + 1]; /* above the separator and Quit */
        [self listGames];
    }
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
    /* 0.5 s: reads the worker's log lines (at most 4 KiB per job) and notices its exit; 0.1 s cost more guest CPU at
     * idle than the worker itself (t952). */
    timer = [[NSTimer scheduledTimerWithTimeInterval:0.5 target:self selector:@selector(poll:) userInfo:nil
                                             repeats:YES] retain];
    [self toggle:nil];
}
- (void)show:(id)sender {
    (void)sender;
    [window makeKeyAndOrderFront:nil];
    [NSApp activateIgnoringOtherApps:YES];
}
/* Opens one of the Tools' applications by the installer's link (guest/tools/install.py); says so when it is not there. */
- (void)openTool:(NSString *)name {
    NSString *path = [[NSHomeDirectory() stringByAppendingPathComponent:@"Applications/Ptolemy Tools/Antiochus"]
            stringByAppendingPathComponent:name];
    if ([[NSFileManager defaultManager] fileExistsAtPath:path] && [[NSWorkspace sharedWorkspace] launchApplication:path])
        return;
    [NSApp activateIgnoringOtherApps:YES];
    NSRunAlertPanel([name stringByAppendingString:@" could not be opened"],
                    @"It is installed with Ptolemy Tools, in Applications/Ptolemy Tools/Antiochus in your home folder. "
                    @"Run the Ptolemy Tools installer again to put it back.",
                    @"OK", nil, nil);
}
- (void)antiochus:(id)sender {
    (void)sender;
    [self openTool:@"Antiochus.app"];
}
- (void)rosettast:(id)sender {
    (void)sender;
    [self openTool:@"rosettast.app"];
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
