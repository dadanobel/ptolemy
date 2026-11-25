/* SPDX-License-Identifier: GPL-2.0-or-later */
#import <Cocoa/Cocoa.h>
#import <ApplicationServices/ApplicationServices.h>
#include <sys/sysctl.h>
#include <sys/stat.h>
#include <signal.h>
#include <unistd.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "bench.h"
#include "score.h"

static NSMutableDictionary *ReadTSV(NSString *path) {
    NSData *bytes = [NSData dataWithContentsOfFile:path]; NSMutableDictionary *d = [NSMutableDictionary dictionary];
    if (!bytes || [bytes length] > 1048576) return d;
    NSString *text = [[[NSString alloc] initWithData:bytes encoding:NSUTF8StringEncoding] autorelease];
    if (!text) return d;
    NSEnumerator *e = [[text componentsSeparatedByString:@"\n"] objectEnumerator]; NSString *line;
    while ((line = [e nextObject])) { NSRange tab = [line rangeOfString:@"\t"];
        if (tab.location != NSNotFound) [d setObject:[line substringFromIndex:tab.location+1] forKey:[line substringToIndex:tab.location]];
    }
    return d;
}
static BOOL Positive(NSString *text, double *value) {
    if (![text isKindOfClass:[NSString class]]) return NO;
    NSScanner *scanner = [NSScanner scannerWithString:text];
    return [scanner scanDouble:value] && [scanner isAtEnd] && isfinite(*value) && *value > 0;
}
static unsigned long Big32(const unsigned char *p) { return (unsigned long)p[0]<<24 | (unsigned long)p[1]<<16 | (unsigned long)p[2]<<8 | p[3]; }
static BOOL UnpackClassic(NSString *binary, NSString *path) {
    NSData *data = [NSData dataWithContentsOfFile:binary]; const unsigned char *p = [data bytes];
    if ([data length] < 128 || [data length] > 16777216 || p[0] || p[1] < 1 || p[1] > 63 || memcmp(p+65,"APPL",4)) return NO;
    unsigned long n = Big32(p+83), r = Big32(p+87), offset;
    if (n < 8 || !r || n > 8388608 || r > 8388608) return NO;
    offset = 128 + ((n+127)&~127ul); if (offset+r > [data length]) return NO;
    if (memcmp(p+128,"Joy!peff",8)) return NO;
    if (![[NSData dataWithBytes:p+128 length:n] writeToFile:path atomically:YES]) return NO;
    if (![[NSData dataWithBytes:p+offset length:r] writeToFile:[path stringByAppendingString:@"/..namedfork/rsrc"] atomically:NO]) return NO;
    NSDictionary *attributes = [NSDictionary dictionaryWithObjectsAndKeys:
        [NSNumber numberWithUnsignedLong:0x4150504c], NSFileHFSTypeCode,
        [NSNumber numberWithUnsignedLong:0x416e436c], NSFileHFSCreatorCode,
        [NSNumber numberWithUnsignedLong:0700], NSFilePosixPermissions, nil];
    return [[NSFileManager defaultManager] changeFileAttributes:attributes atPath:path];
}
@interface Antiochus : NSObject {
    NSWindow *window; NSTextField *status, *numbers; NSButton *start, *classic, *reveal;
    NSTask *graphicsTask; NSMenuItem *graphicsMenu;
    NSTask *task; NSTimer *timer; NSDate *began; NSString *report, *caseDirectory, *nonce;
    NSMutableArray *records; NSMutableDictionary *classicResult; AntResult results[ANT_CASE_COUNT];
    unsigned index; BOOL running, stopping, cancelled, timedOut, classicRunning, reportFailed; double stopTime;
}
- (void)begin:(id)sender;
- (void)poll:(NSTimer *)timer;
- (void)next;
- (void)finish;
- (void)reveal:(id)sender;
- (void)save:(BOOL)complete;
@end
@implementation Antiochus
- (NSTextField *)label:(NSRect)frame text:(NSString *)text {
    NSTextField *field = [[[NSTextField alloc] initWithFrame:frame] autorelease];
    [field setEditable:NO]; [field setSelectable:YES]; [field setBezeled:NO]; [field setDrawsBackground:NO]; [field setStringValue:text];
    [[window contentView] addSubview:field]; return field;
}
- (NSButton *)button:(NSRect)frame title:(NSString *)title action:(SEL)action {
    NSButton *b = [[[NSButton alloc] initWithFrame:frame] autorelease]; [b setTitle:title];
    [b setBezelStyle:NSRoundedBezelStyle]; [b setTarget:self]; [b setAction:action]; [[window contentView] addSubview:b]; return b;
}
- (void)applicationDidFinishLaunching:(NSNotification *)note {
    (void)note;
    window = [[NSWindow alloc] initWithContentRect:NSMakeRect(0,0,480,250)
        styleMask:NSTitledWindowMask|NSClosableWindowMask|NSMiniaturizableWindowMask backing:NSBackingStoreBuffered defer:NO];
    [window setTitle:@"Antiochus"]; [window setReleasedWhenClosed:NO]; [window center]; [window setDelegate:self];
    numbers = [self label:NSMakeRect(24,105,432,105) text:@"CPU        -\nGPU        -\nOverall    -"];
    [numbers setFont:[NSFont boldSystemFontOfSize:23]];
    status = [self label:NSMakeRect(24,66,432,36) text:@"CPU and graphics benchmark | Tiger / Leopard"];
    classic = [self button:NSMakeRect(24,32,170,24) title:@"Include Classic" action:NULL];
    [classic setButtonType:NSSwitchButton]; [classic setState:NSOnState];
    start = [self button:NSMakeRect(324,20,132,32) title:@"Run" action:@selector(begin:)];
    reveal = [self button:NSMakeRect(190,20,126,32) title:@"Show report" action:@selector(reveal:)]; [reveal setEnabled:NO];
    [window makeKeyAndOrderFront:nil]; [NSApp activateIgnoringOtherApps:YES];
    NSMenu *menu = [[[NSMenu alloc] initWithTitle:@""] autorelease]; NSMenuItem *appItem = [[[NSMenuItem alloc] initWithTitle:@"Antiochus" action:NULL keyEquivalent:@""] autorelease];
    NSMenu *appMenu = [[[NSMenu alloc] initWithTitle:@"Antiochus"] autorelease];
    [appMenu setAutoenablesItems:NO];
    graphicsMenu=[appMenu addItemWithTitle:@"Run on-screen OpenGL checks (Piglit fork)" action:@selector(graphics:) keyEquivalent:@"g"];
    [graphicsMenu setTarget:self];
    [appMenu addItemWithTitle:@"Quit Antiochus" action:@selector(terminate:) keyEquivalent:@"q"];
    [appItem setSubmenu:appMenu]; [menu addItem:appItem]; [NSApp setMainMenu:menu];
}
- (void)graphics:(id)sender {
    (void)sender;
    if(running || graphicsTask) return;
    char path[4096];
    NSString *desktop=[NSSearchPathForDirectoriesInDomains(NSDesktopDirectory,NSUserDomainMask,YES) objectAtIndex:0];
    NSString *pattern=[desktop stringByAppendingPathComponent:@"Antiochus-Graphics-XXXXXX"];
    if(![pattern getFileSystemRepresentation:path maxLength:sizeof path] || !mkdtemp(path)) {
        [status setStringValue:@"Cannot create the graphics report directory."]; return;
    }
    [report release]; report=[[NSString stringWithUTF8String:path] copy];
    NSString *res=[[NSBundle mainBundle] resourcePath];
    graphicsTask=[[NSTask alloc] init]; [graphicsTask setLaunchPath:@"/usr/bin/perl"];
    [graphicsTask setArguments:[NSArray arrayWithObjects:[res stringByAppendingPathComponent:@"graphics-report.pl"],
        @"--run",report,[res stringByAppendingPathComponent:@"antiochus-worker"],nil]];
    @try { [graphicsTask launch]; }
    @catch(NSException *e) { [graphicsTask release];graphicsTask=nil;[status setStringValue:[e reason]];return; }
    [graphicsMenu setEnabled:NO]; [classic setEnabled:NO];[reveal setEnabled:YES];[start setTitle:@"Stop checks"];
    [numbers setStringValue:@"OpenGL checks"];[status setStringValue:@"Starting the visible graphics suite."];
    timer=[[NSTimer scheduledTimerWithTimeInterval:0.5 target:self selector:@selector(graphicsPoll:) userInfo:nil repeats:YES] retain];
}
- (void)graphicsPoll:(NSTimer *)sender {
    (void)sender;
    if([graphicsTask isRunning]) {
        NSString *s=[NSString stringWithContentsOfFile:[report stringByAppendingPathComponent:@"current.tsv"]
            encoding:NSUTF8StringEncoding error:NULL];
        if(s) [status setStringValue:s]; return;
    }
    int rc=[graphicsTask terminationStatus]; [graphicsTask release]; graphicsTask=nil;
    [timer invalidate];[timer release];timer=nil;
    NSString *summary=[NSString stringWithContentsOfFile:[report stringByAppendingPathComponent:@"summary.tsv"]
        encoding:NSUTF8StringEncoding error:NULL];
    [numbers setStringValue:@"OpenGL checks"];[status setStringValue:rc==0 && summary?
        [[summary componentsSeparatedByString:@"\n"] componentsJoinedByString:@"   "]:@"Run incomplete. See the saved report."];
    [start setTitle:@"Run"];[classic setEnabled:YES];[graphicsMenu setEnabled:YES];
}
- (void)begin:(id)sender {
    (void)sender;
    if(graphicsTask) { if([graphicsTask isRunning]) [graphicsTask terminate]; return; }
    if (running) {
        if (classicRunning) { [status setStringValue:@"Close the Classic test window to cancel it."]; return; }
        stopping = YES; cancelled = YES; stopTime = [NSDate timeIntervalSinceReferenceDate]; [task terminate]; return;
    }
    NSFileManager *fm = [NSFileManager defaultManager];
    NSString *desktop = [NSSearchPathForDirectoriesInDomains(NSDesktopDirectory,NSUserDomainMask,YES) objectAtIndex:0];
    NSString *prefix = [desktop stringByAppendingPathComponent:@"Antiochus-XXXXXX"]; char path[4096];
    if (![prefix getFileSystemRepresentation:path maxLength:sizeof path] || !mkdtemp(path)) {
        [status setStringValue:@"Cannot create a report on the Desktop."]; return;
    }
    NSString *created = [fm stringWithFileSystemRepresentation:path length:strlen(path)];
    NSString *destination = [created stringByAppendingString:@".antiochus"];
    if (![fm movePath:created toPath:destination handler:nil]) { [status setStringValue:@"Cannot name the report."]; return; }
    [report release]; report = [destination copy]; reportFailed=NO; [records release]; records = [[NSMutableArray alloc] init];
    [classicResult release]; classicResult = [[NSMutableDictionary alloc] initWithObjectsAndKeys:@"pending",@"status",nil];
    CFUUIDRef uuid = CFUUIDCreate(NULL); [nonce release]; nonce = [(NSString *)CFUUIDCreateString(NULL,uuid) autorelease]; [nonce retain]; CFRelease(uuid);
    NSMutableDictionary *environment = [NSMutableDictionary dictionaryWithObjectsAndKeys:
        [[NSProcessInfo processInfo] operatingSystemVersionString],@"os",
        [NSDate date],@"started",@"1.0-draft4",@"version",nonce,@"nonce",nil];
    const char *keys[] = {"hw.model","hw.machine","hw.memsize","hw.cpufrequency","hw.ncpu","hw.optional.altivec"}; unsigned i;
    for(i=0;i<6;++i) { unsigned char bytes[256]; size_t n=sizeof bytes;
        if(!sysctlbyname(keys[i],bytes,&n,NULL,0)) {
            NSString *key=[NSString stringWithUTF8String:keys[i]];
            if(i<2) [environment setObject:[NSString stringWithUTF8String:(char *)bytes] forKey:key];
            else { uint64_t value=0; if(n==4){uint32_t v;memcpy(&v,bytes,4);value=v;}else if(n==8)memcpy(&value,bytes,8);
                [environment setObject:[NSNumber numberWithUnsignedLongLong:value] forKey:key]; }
        }
    }
    NSString *resource = [[NSBundle mainBundle] resourcePath];
    if(![fm copyPath:[resource stringByAppendingPathComponent:@"build-manifest.txt"] toPath:[report stringByAppendingPathComponent:@"build-manifest.txt"] handler:nil])reportFailed=YES;
    if(![environment writeToFile:[report stringByAppendingPathComponent:@"environment.plist"] atomically:YES])reportFailed=YES;
    memset(results,0,sizeof results); index=0;running=YES;stopping=NO;cancelled=NO;timedOut=NO;classicRunning=NO;
    [graphicsMenu setEnabled:NO];[classic setEnabled:NO];[start setTitle:@"Cancel"];[reveal setEnabled:YES];
    [numbers setStringValue:@"CPU        -\nGPU        -\nOverall    -"];
    [self save:NO];if(reportFailed){cancelled=YES;[self finish];return;}
    timer=[[NSTimer scheduledTimerWithTimeInterval:0.2 target:self selector:@selector(poll:) userInfo:nil repeats:YES] retain];
    [self next];
}
- (void)next {
    if(index==ANT_CASE_COUNT) {
        if([classic state]!=NSOnState) { [classicResult setObject:@"not_requested" forKey:@"status"];[self finish];return; }
        SInt32 version=0; Gestalt(gestaltSystemVersion,&version);
        if(version>=0x1050) { [classicResult setObject:@"unsupported_os" forKey:@"status"];[self finish];return; }
        NSString *binary=[[[NSBundle mainBundle] resourcePath] stringByAppendingPathComponent:@"AntiochusClassic.bin"];
        if(![[NSFileManager defaultManager] fileExistsAtPath:binary]) {
            [classicResult setObject:@"missing_binary" forKey:@"status"];[self finish];return;
        }
        NSString *executable=[report stringByAppendingPathComponent:@"Antiochus Classic"];
        NSString *request=[report stringByAppendingPathComponent:@"ClassicRequest.txt"];
        NSData *body=[[NSString stringWithFormat:@"ANTIOCHUS_REQUEST_1\nnonce\t%@\n",nonce] dataUsingEncoding:NSASCIIStringEncoding];
        if(!UnpackClassic(binary,executable) || ![body writeToFile:request atomically:YES]) {
            [classicResult setObject:@"unpack_error" forKey:@"status"];[self finish];return;
        }
        LSLaunchURLSpec spec; memset(&spec,0,sizeof spec);
        spec.appURL=(CFURLRef)[NSURL fileURLWithPath:executable];
        spec.itemURLs=(CFArrayRef)[NSArray arrayWithObject:[NSURL fileURLWithPath:request]];
        spec.launchFlags=kLSLaunchDefaults|kLSLaunchAsync|kLSLaunchInClassic;
        OSStatus error=LSOpenFromURLSpec(&spec,NULL);
        if(error) { [classicResult setObject:@"launch_error" forKey:@"status"];[classicResult setObject:[NSNumber numberWithInt:error] forKey:@"error"];[self finish];return; }
        classicRunning=YES;[began release];began=[[NSDate date] retain];[status setStringValue:@"Classic: starting and checking QuickDraw..."];
        [self save:NO];return;
    }
    NSString *identifier=[NSString stringWithUTF8String:ant_cases[index].id];
    stopping=NO;timedOut=NO;
    [caseDirectory release];caseDirectory=[[report stringByAppendingPathComponent:identifier] copy];
    if(![[NSFileManager defaultManager] createDirectoryAtPath:caseDirectory attributes:nil]) {
        cancelled=YES;[status setStringValue:@"Report write failed."];[self finish];return;
    }
    NSString *out=[caseDirectory stringByAppendingPathComponent:@"result.tsv"],*err=[caseDirectory stringByAppendingPathComponent:@"stderr.txt"];
    [[NSFileManager defaultManager] createFileAtPath:out contents:[NSData data] attributes:nil];
    [[NSFileManager defaultManager] createFileAtPath:err contents:[NSData data] attributes:nil];
    NSFileHandle *output=[NSFileHandle fileHandleForWritingAtPath:out],*errors=[NSFileHandle fileHandleForWritingAtPath:err];
    if(!output || !errors){cancelled=YES;[self finish];return;}
    [task release];task=[[NSTask alloc] init];
    [task setLaunchPath:[[[NSBundle mainBundle] resourcePath] stringByAppendingPathComponent:@"antiochus-worker"]];
    [task setArguments:[NSArray arrayWithObjects:identifier,caseDirectory,nil]];[task setCurrentDirectoryPath:caseDirectory];
    [task setStandardOutput:output];[task setStandardError:errors];
    [began release];began=[[NSDate date] retain];
    [status setStringValue:[NSString stringWithFormat:@"%u / %u  %@",index+1,ANT_CASE_COUNT,identifier]];
    @try { [task launch]; } @catch(NSException *e) { (void)e; cancelled=YES; }
    [output closeFile];[errors closeFile];[self save:NO];
}
- (void)poll:(NSTimer *)sender {
    (void)sender;
    if(!running)return;
    if(classicRunning) {
        NSMutableDictionary *r=ReadTSV([report stringByAppendingPathComponent:@"ClassicResult.tsv"]);
        if([[r objectForKey:@"nonce"] isEqual:nonce] && [[r objectForKey:@"protocol"] isEqual:@"antiochus-classic-1"] &&
            ([[r objectForKey:@"status"] isEqual:@"pass"] || [[r objectForKey:@"status"] isEqual:@"fail"])) {
            [classicResult setDictionary:r];[self finish];return;
        }
        if(-[began timeIntervalSinceNow]>600){[classicResult setObject:@"timeout" forKey:@"status"];[self finish];}
        return;
    }
    if([task isRunning]) {
        if(!stopping && -[began timeIntervalSinceNow]>(index>=29?600:180)) {stopping=YES;timedOut=YES;stopTime=[NSDate timeIntervalSinceReferenceDate];[task terminate];}
        if(stopping && [NSDate timeIntervalSinceReferenceDate]-stopTime>3) kill([task processIdentifier],SIGKILL);
        return;
    }
    NSMutableDictionary *r=ReadTSV([caseDirectory stringByAppendingPathComponent:@"result.tsv"]);
    NSString *identifier=[NSString stringWithUTF8String:ant_cases[index].id];
    BOOL valid=[[r objectForKey:@"id"] isEqual:identifier] && [[r objectForKey:@"protocol"] isEqual:@"antiochus-1"] && [[r objectForKey:@"version"] isEqual:@"1.0-draft4"];
    int exitCode=-1; @try {exitCode=[task terminationStatus];} @catch(NSException *e){(void)e;}
    BOOL pass=valid && exitCode==0 && [[r objectForKey:@"status"] isEqual:@"pass"];
    double seconds=0;
    if(pass && ant_cases[index].group!=ANT_GATE) {
        double samples[ANT_SAMPLES];unsigned i,j;
        for(i=0;i<ANT_SAMPLES;++i)if(!Positive([r objectForKey:[NSString stringWithFormat:@"sample_%u",i]],&samples[i]))pass=NO;
        if(pass) {for(i=1;i<ANT_SAMPLES;++i)for(j=i;j && samples[j]<samples[j-1];--j){double t=samples[j];samples[j]=samples[j-1];samples[j-1]=t;}
            seconds=samples[ANT_SAMPLES/2];}
    }
    if(ant_cases[index].group==ANT_GATE && ![[r objectForKey:@"state_failures"] isEqual:@"0"])pass=NO;
    results[index].attempted=1;results[index].passed=pass;results[index].seconds=seconds;
    results[index].unsupported=valid && exitCode==0 && [[r objectForKey:@"status"] isEqual:@"unsupported"];
    [r setObject:identifier forKey:@"id"];[r setObject:[NSNumber numberWithInt:exitCode] forKey:@"exit_code"];
    [r setObject:[NSNumber numberWithBool:pass] forKey:@"accepted"];[r setObject:[NSNumber numberWithDouble:seconds] forKey:@"accepted_seconds"];
    if(!pass && !results[index].unsupported) [r setObject:timedOut?@"timeout":cancelled?@"cancelled":@"failed" forKey:@"outcome"];
    [records addObject:r];++index;[task release];task=nil;
    if(cancelled){[self finish];return;}[self save:NO];if(reportFailed){cancelled=YES;[self finish];return;}[self next];
}
- (void)save:(BOOL)complete {
    AntScore score=ant_score(results);
    NSDictionary *summary=[NSDictionary dictionaryWithObjectsAndKeys:@"antiochus-score-4",@"profile",@"1.0-draft4",@"version",
        [NSNumber numberWithBool:complete && score.complete && !reportFailed],@"complete",[NSNumber numberWithDouble:score.cpu],@"cpu",
        [NSNumber numberWithDouble:score.gpu],@"gpu",[NSNumber numberWithDouble:score.total],@"total",
        [NSNumber numberWithInt:score.failed],@"failed",[NSNumber numberWithInt:score.optional_failed],@"optional_failed",
        [NSNumber numberWithDouble:score.gpu_speed],@"gpu_speed",
        [NSNumber numberWithDouble:score.combined],@"combined",
        [NSNumber numberWithInt:score.gpu_passed],@"gpu_passed",[NSNumber numberWithInt:score.gpu_checked],@"gpu_checked",
        [NSNumber numberWithInt:score.diagnostic_failed],@"diagnostic_failed",
        records,@"cases",classicResult,@"classic",nonce,@"nonce",nil];
    if(![summary writeToFile:[report stringByAppendingPathComponent:@"summary.plist"] atomically:YES]) {
        reportFailed=YES;[status setStringValue:@"Report write failed; results are incomplete."];
    }
}
- (void)finish {
    [timer invalidate];[timer release];timer=nil;running=NO;classicRunning=NO;
    AntScore score=ant_score(results);[self save:!cancelled];
    if(!cancelled && !reportFailed && score.complete) {
        [numbers setStringValue:[NSString stringWithFormat:@"CPU        %.0f\nGPU        %.0f\nOverall    %.0f",score.cpu,score.gpu,score.total]];
        [status setStringValue:[NSString stringWithFormat:@"GPU checks %d/%d; speed (valid tests) %.0f\nFailures %d, diagnostics %d; Classic: %@",score.gpu_passed,score.gpu_checked,score.gpu_speed,score.failed+score.optional_failed,score.diagnostic_failed,[classicResult objectForKey:@"status"]]];
    } else [status setStringValue:reportFailed?@"Report write failed. No accepted overall score.":@"Incomplete run. Partial data saved; no overall score."];
    [graphicsMenu setEnabled:YES];[start setTitle:@"Run again"];[classic setEnabled:YES];
}
- (void)reveal:(id)sender { (void)sender;if(report)[[NSWorkspace sharedWorkspace] selectFile:report inFileViewerRootedAtPath:@""]; }
- (BOOL)windowShouldClose:(id)sender { (void)sender;[NSApp terminate:nil];return NO; }
- (NSApplicationTerminateReply)applicationShouldTerminate:(NSApplication *)app {
    (void)app;
    if(graphicsTask && [graphicsTask isRunning]) {
        if(NSRunAlertPanel(@"Stop OpenGL checks?",@"Completed evidence will be kept.",@"Stop",@"Continue",nil)!=NSAlertDefaultReturn)
            return NSTerminateCancel;
        [graphicsTask terminate];
    }
    if(running) {
        int answer=NSRunAlertPanel(@"Stop Antiochus?",@"The current run will be incomplete. Its report will be kept.",@"Stop",@"Continue",nil);
        if(answer!=NSAlertDefaultReturn)return NSTerminateCancel;
        if([task isRunning]){[task terminate];kill([task processIdentifier],SIGKILL);}
        [self save:NO];
    }
    return NSTerminateNow;
}
@end
int main(int argc,const char **argv) {
    (void)argc;(void)argv;NSAutoreleasePool *pool=[[NSAutoreleasePool alloc] init];
    [NSApplication sharedApplication];Antiochus *delegate=[[Antiochus alloc] init];[NSApp setDelegate:delegate];[NSApp run];
    [delegate release];[pool release];return 0;
}
