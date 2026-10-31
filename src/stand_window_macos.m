/* SPDX-License-Identifier: GPL-2.0-or-later
 * Window for the standalone board. AppKit owns the main thread; the machine (HVF vCPU,
 * devices) runs on its own thread. The frame is drawn by Metal straight from VRAM (no CPU copy): a fragment
 * shader decodes the VBE mode (8 bpp via DAC palette, 15/16/24/32 bpp, either byte order). */
#import "guest_window_macos.h"
#import <Metal/Metal.h>
#include "stand_window.h"
#include "pt_agent.h"
#include "pt_boot_mode.h"
#include <pthread.h>
#include <signal.h>
#include <math.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <errno.h>
#include <arpa/inet.h>
#import <Carbon/Carbon.h>

static NSString *const shader = @
"#include <metal_stdlib>\n"
"using namespace metal;\n"
"struct V { float4 pos [[position]]; };\n"
"vertex V vs(uint id [[vertex_id]]) { float2 p=float2((id<<1)&2, id&2); V v; v.pos=float4(p*2-1,0,1); return v; }\n"
"struct P { uint w,h,bpp,line,start,be,pad0,pad1; float2 scale,offset; int2 cursor; uint2 size; uint visible,gamma,pad3,pad4; };\n"
/* The guest's hardware cursor (PtHwCursor) over either kind of frame; colour 0..255, 0x00ffffff inverts. */
"float3 hwcursor(float3 c, float2 g, constant P &p, device const uint *cur) {\n"
"  int2 cp=int2(g)-p.cursor; if (!p.visible || any(cp<int2(0)) || any(cp>=int2(p.size))) return c;\n"
"  uint v=cur[cp.y*p.size.x+cp.x]; return v==0x00ffffffu ? 255.0-c : mix(c,float3((v>>16)&255,(v>>8)&255,v&255),float(v>>24)/255.0);\n"
"}\n"
/* The display driver's gamma ramp (PtHwCursor.gamma, 0x00RRGGBB per level) over the finished pixel, 0..255 in and out. */
"float4 finish(float3 c, constant P &p, constant uint *gam) {\n"
"  if (p.gamma) { uint3 i=uint3(clamp(c+0.5,0.0,255.0)); c=float3((gam[i.r]>>16)&255,(gam[i.g]>>8)&255,gam[i.b]&255); }\n"
"  return float4(c/255.0,1);\n"
"}\n"
"fragment float4 fs(V in [[stage_in]], constant P &p [[buffer(0)]], device const uchar *m [[buffer(1)]],\n"
"                   constant uchar4 *pal [[buffer(2)]], device const uint *cur [[buffer(3)]], constant uint *gam [[buffer(4)]]) {\n"
"  float2 g=(in.pos.xy-p.offset)/p.scale;\n"
"  if (g.x<0 || g.y<0 || g.x>=float(p.w) || g.y>=float(p.h)) return float4(0,0,0,1);\n"
"  uint x=uint(g.x), a=p.start+uint(g.y)*p.line; float3 c;\n"
"  if (p.bpp==32) { a+=x*4; c= p.be ? float3(m[a+1],m[a+2],m[a+3]) : float3(m[a+2],m[a+1],m[a]); }\n"
"  else if (p.bpp==24) { a+=x*3; c= p.be ? float3(m[a],m[a+1],m[a+2]) : float3(m[a+2],m[a+1],m[a]); }\n"
"  else if (p.bpp==16 || p.bpp==15) { a+=x*2; uint v= p.be ? (uint(m[a])<<8|uint(m[a+1])) : (uint(m[a+1])<<8|uint(m[a]));\n"
"    c= p.bpp==15 ? float3((v>>10&31)<<3,(v>>5&31)<<3,(v&31)<<3) : float3((v>>11&31)<<3,(v>>5&63)<<2,(v&31)<<3); }\n"
"  else { uchar4 q=pal[m[a+x]]; c=float3(q.x,q.y,q.z); }\n"
"  return finish(hwcursor(c,g,p,cur),p,gam);\n"
"}\n"
/* A native GPU frame of Ptolemy's own Radeon frontend (PtRadeonFrame.metal_texture). Its RGBA8 channels hold the
 * VRAM bytes 2,1,0,3 of each pixel, so this is the 32 bpp decode of fs above on the same bytes. */
"fragment float4 fst(V in [[stage_in]], constant P &p [[buffer(0)]], texture2d<float, access::read> t [[texture(0)]],\n"
"                    device const uint *cur [[buffer(3)]], constant uint *gam [[buffer(4)]]) {\n"
"  float2 g=(in.pos.xy-p.offset)/p.scale;\n"
"  if (g.x<0 || g.y<0 || g.x>=float(p.w) || g.y>=float(p.h)) return float4(0,0,0,1);\n"
"  float4 q=t.read(uint2(g)); return finish(hwcursor((p.be ? float3(q.g,q.r,q.a) : float3(q.r,q.g,q.b))*255.0,g,p,cur),p,gam);\n"
"}\n";

typedef struct { uint32_t w,h,bpp,line,start,be,pad0,pad1; float sx,sy,ox,oy; int32_t cx,cy;uint32_t cw,ch,visible,gamma,pad3,pad4; } Params;
static dispatch_semaphore_t ready;
static PtStandWindow *config;
static int exit_code,restart_requested;
static _Atomic int ready_called;
static void (^quiesce)(void);
void pt_stand_window_ready(void) { if (ready && !atomic_exchange(&ready_called,1)) dispatch_semaphore_signal(ready); }
void pt_stand_window_detach(void) {
    if(ready) dispatch_sync(dispatch_get_main_queue(), ^{if(quiesce)quiesce();});
}

/* Cancellation shuts down a still-owned descriptor; only the worker closes it. */
@interface PTAgentCancellation:NSObject
- (BOOL)attach:(int)fd;
- (void)detach;
- (void)cancel;
- (BOOL)cancelled;
@end
@implementation PTAgentCancellation {int _fd;BOOL _cancelled;}
- (instancetype)init {if((self=[super init]))_fd=-1;return self;}
- (BOOL)attach:(int)fd {@synchronized(self){if(_cancelled)return NO;_fd=fd;return YES;}}
- (void)detach {@synchronized(self){_fd=-1;}}
- (void)cancel {@synchronized(self){_cancelled=YES;if(_fd>=0)shutdown(_fd,SHUT_RDWR);}}
- (BOOL)cancelled {@synchronized(self){return _cancelled;}}
@end

/* One request over the stand's own development socket (the PTA1 protocol served by src/agent.c). */
static BOOL io_all(int fd,void *b,size_t n,BOOL out){
    for(uint8_t *p=b;n;){ssize_t k=out?send(fd,p,n,0):recv(fd,p,n,0);if(k<=0)return NO;p+=k;n-=(size_t)k;}return YES;
}
static int agent_request_cancel(NSString *sock,uint32_t op,NSData *body,unsigned seconds,NSString **output,PTAgentCancellation *operation){
    struct sockaddr_un sa={.sun_family=AF_UNIX};if(sock.length>=sizeof sa.sun_path)return -1;strcpy(sa.sun_path,sock.fileSystemRepresentation);
    int s=socket(AF_UNIX,SOCK_STREAM,0),one=1,rc=-1;if(s<0)return -1;
    if(operation&&![operation attach:s]){close(s);return -1;}
    struct timeval tv={.tv_sec=seconds+15};setsockopt(s,SOL_SOCKET,SO_RCVTIMEO,&tv,sizeof tv);setsockopt(s,SOL_SOCKET,SO_NOSIGPIPE,&one,sizeof one);
    uint32_t h[4]={0,htonl(op),htonl((uint32_t)body.length),htonl(seconds)},r[4];memcpy(h,"PTA1",4);
    if(!connect(s,(struct sockaddr *)&sa,sizeof sa)&&![operation cancelled]&&io_all(s,h,16,YES)&&io_all(s,(void *)body.bytes,body.length,YES)&&io_all(s,r,16,NO)&&!memcmp(r,"PTA1",4)){
        uint32_t kind=ntohl(r[1]),n=ntohl(r[3]);NSMutableData *text=[NSMutableData dataWithLength:n<=1048576?n:0];
        if(n<=1048576&&io_all(s,text.mutableBytes,n,NO)){if(output)*output=[[NSString alloc] initWithData:text encoding:NSUTF8StringEncoding];rc=kind==0?(int)ntohl(r[2]):-1;}
    }
    [operation detach];close(s);return rc;
}
static int agent_request(NSString *sock,uint32_t op,NSData *body,unsigned seconds,NSString **output){return agent_request_cancel(sock,op,body,seconds,output,nil);}
/* Right after the window comes back the agent may not have polled yet ("Tiger agent is off or not polling"), or another
 * short command holds it; try again for 3 s. */
static int agent_request_soon(NSString *sock,uint32_t op,NSData *body,unsigned seconds,NSString **output){
    int rc=-1;for(int k=0;k<12;k++){NSString *o=nil;rc=agent_request(sock,op,body,seconds,&o);if(output)*output=o;
        if(!rc||!([o containsString:@"not polling"]||[o containsString:@"another command is active"]))break;usleep(250000);}
    return rc;
}
static NSData *argv_body(NSArray<NSString *> *argv){
    NSMutableData *body=[NSMutableData data];uint32_t v=htonl((uint32_t)argv.count);[body appendBytes:&v length:4];
    for(NSString *a in argv){NSData *d=[a dataUsingEncoding:NSUTF8StringEncoding];v=htonl((uint32_t)d.length);[body appendBytes:&v length:4];[body appendData:d];}
    return body;
}
/* A dropped file goes to ~/Desktop in Tiger as it is, a folder as one NAME.tar file that is not unpacked.
 * Protocol-2 transfer: checked 64 KiB chunks, SHA-256 on both sides, published only when complete
 * and never over an existing file ("name 2.ext"). The folder's tar is a host temp file, not memory. */
static void drop_to_guest(NSString *sock,NSURL *url){
    NSString *src=url.path,*name=url.lastPathComponent,*tmp=nil,*out=nil;BOOL folder=NO;
    if([NSFileManager.defaultManager fileExistsAtPath:src isDirectory:&folder]&&folder){
        tmp=[NSTemporaryDirectory() stringByAppendingPathComponent:[NSString stringWithFormat:@"ptolemy-drop-%@.tar",NSUUID.UUID.UUIDString]];
        NSTask *tar=[NSTask new];tar.executableURL=[NSURL fileURLWithPath:@"/usr/bin/tar"];
        tar.arguments=@[@"--format",@"gnutar",@"-cf",tmp,@"-C",url.URLByDeletingLastPathComponent.path,name];
        NSMutableDictionary *env=[NSProcessInfo.processInfo.environment mutableCopy];env[@"COPYFILE_DISABLE"]=@"1";tar.environment=env;
        NSError *e=nil;if(![tar launchAndReturnError:&e]){NSLog(@"Drop: tar failed: %@",e);return;}
        [tar waitUntilExit];if(tar.terminationStatus){NSLog(@"Drop: tar status %d for %@",tar.terminationStatus,src);unlink(tmp.fileSystemRepresentation);return;}
        src=tmp;name=[name stringByAppendingString:@".tar"];
    }
    NSData *p=[src dataUsingEncoding:NSUTF8StringEncoding],*n=[name dataUsingEncoding:NSUTF8StringEncoding];
    NSMutableData *body=[NSMutableData data];uint32_t v=htonl((uint32_t)p.length);[body appendBytes:&v length:4];[body appendData:p];
    v=htonl((uint32_t)n.length);[body appendBytes:&v length:4];[body appendData:n];
    CFAbsoluteTime t0=CFAbsoluteTimeGetCurrent();int rc=agent_request(sock,2,body,600,&out);
    if(tmp)unlink(tmp.fileSystemRepresentation);
    NSLog(@"Drop: %@ -> %@ (%.1f s, rc=%d)",url.lastPathComponent,[out stringByTrimmingCharactersInSet:NSCharacterSet.whitespaceAndNewlineCharacterSet],CFAbsoluteTimeGetCurrent()-t0,rc);
}

@interface PTStandActions : NSObject<PTGuestWindowActions,NSApplicationDelegate>
@property(atomic) unsigned scaling;                       /* 0 fit, 1 integer, 2 stretch */
@property(nonatomic) BOOL stopped;
@property(nonatomic) BOOL asked;   /* Tiger accepted Restart/Shut Down and has not stopped yet */
- (void)syncInputSource;
@property(nonatomic,weak) PTGuestWindow *window;
@end
@implementation PTStandActions
- (void)pauseGuest:(BOOL)pause {   /* the CPU owner pauses at its next service (virtual time stops) */
    if(self.stopped)return;
    atomic_store(config->pause, pause?1:0);
    PtInputEvent release={.type=PT_INPUT_RELEASE};pt_input_send(config->input,&release);   /* no held keys across; wakes the CPU */
    [self.window setGuestPaused:pause];
}
/* Restart, Shut Down, closing the window and Quit ask Tiger through Ptolemy Tools, as its Apple menu does
 * (applications may ask to save or cancel), and the machine stops when Tiger turns it off or resets it. An immediate
 * stop is a pulled plug (Halo lost a 4 KiB block it had just written). Force Restart and Force Shut Down (Option in
 * the Machine menu) stop at once; they are offered when Tiger cannot be asked. */
- (void)forceStopGuest:(BOOL)restart {
    if(self.stopped)return;
    if(restart)restart_requested=1;
    atomic_store(config->stop, 1);
    PtInputEvent release={.type=PT_INPUT_RELEASE};pt_input_send(config->input,&release);
}
- (void)resetGuest {[self askGuest:YES];}
- (void)shutdownGuest {[self askGuest:NO];}
- (void)askGuest:(BOOL)restart {
    if(self.stopped)return;
    if(self.asked||!config->agent_socket){[self offerForce:restart detail:self.asked?@"Tiger was asked and is still running: an application may be waiting for you, or the request was cancelled in Tiger.":nil];return;}
    if(atomic_load(config->pause))[self pauseGuest:NO];   /* a paused Tiger cannot answer */
    self.asked=YES;[self.window setGuestActivity:restart?@"Restarting…":@"Shutting Down…"];
    NSString *path=@(config->agent_socket),*verb=restart?@"restart":@"shut down";
    /* Measured: Tiger restarted in 4 s and shut down in 5 s this way, with the Ptolemy Agent running; files written just
     * before were intact after both, while an immediate stop lost 2 of 73. */
    NSData *body=argv_body(@[@"/usr/bin/osascript",@"-e",[@"tell application \"System Events\" to " stringByAppendingString:verb]]);
    dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED,0),^{NSString *out=nil;int rc=agent_request_soon(path,1,body,60,&out);
        dispatch_async(dispatch_get_main_queue(),^{
            NSString *text=[out stringByTrimmingCharactersInSet:NSCharacterSet.whitespaceAndNewlineCharacterSet]?:@"";
            NSLog(@"Asked Tiger to %@: rc=%d %@",verb,rc,text);
            if(self.stopped)return;
            /* A refusal by the host end means nothing reached Tiger; a timeout or a lost reply may mean it is on its way down. */
            BOOL refused=rc<0&&(!out||[out containsString:@"not polling"]||[out containsString:@"another command"]||[out containsString:@"invalid"]);
            if(rc>0||refused){self.asked=NO;[self.window setGuestActivity:nil];
                [self offerForce:restart detail:rc>0?[NSString stringWithFormat:@"Tiger did not accept the request (%@).",text]:nil];}
        });});
}
- (void)offerForce:(BOOL)restart detail:(NSString *)detail {
    if(self.stopped||!self.window.window||self.window.window.attachedSheet)return;
    NSAlert *a=[NSAlert new];a.alertStyle=NSAlertStyleWarning;a.messageText=restart?@"Force Tiger to restart?":@"Force Tiger to shut down?";
    a.informativeText=[NSString stringWithFormat:@"%@ Forcing stops Tiger at once: what it has not yet written to its disk is lost. "
        "You can also choose %@ in Tiger’s Apple menu.",detail?:@"Tiger cannot be asked: Ptolemy Tools are not running in it.",restart?@"Restart":@"Shut Down"];
    [a addButtonWithTitle:@"Cancel"];[a addButtonWithTitle:restart?@"Force Restart":@"Force Shut Down"].hasDestructiveAction=YES;
    [a beginSheetModalForWindow:self.window.window completionHandler:^(NSModalResponse response){
        if(response==NSAlertSecondButtonReturn)[self forceStopGuest:restart];
        else if(self.asked){self.asked=NO;[self.window setGuestActivity:nil];}   /* the next request asks Tiger again */
    }];
}
- (NSApplicationTerminateReply)applicationShouldTerminate:(NSApplication *)app {
    (void)app;[self shutdownGuest];return NSTerminateCancel;
}
/* Text clipboard through the enabled agent, on window focus only (no polling, no new wakeups).
 * Focus in (the window is key and the pointer is over it): the host text goes to Tiger if the host pasteboard
 * changed since the last sync. Focus out: Tiger's text (pbpaste) replaces the host's unless that request was
 * superseded by a newer focus change, the host pasteboard changed meanwhile, the text is empty (Tiger cannot tell
 * empty from none) or it is what was just synced (no echo).
 * <= 1 MiB of UTF-8 each way; an agent that is off, busy or old answers at once and nothing changes. */
static dispatch_queue_t clip_queue;static NSInteger clip_seen=-1;static NSString *clip_last;static uint64_t clip_gen;
- (void)guestWindowFocused:(BOOL)focused {
    const char *sock=config?config->agent_socket:NULL,*env=getenv("PT_CLIPBOARD");   /* PT_CLIPBOARD=0: off (test jobs) */
    if(!sock||self.stopped||(env&&!atoi(env)))return;
    static dispatch_once_t once;dispatch_once(&once,^{clip_queue=dispatch_queue_create("ptolemy.clipboard",DISPATCH_QUEUE_SERIAL);});
    NSPasteboard *pb=NSPasteboard.generalPasteboard;NSInteger cc=pb.changeCount;uint64_t gen=++clip_gen;NSString *path=@(sock);
    if(focused){
        if(cc==clip_seen)return;
        clip_seen=cc;NSString *text=[pb stringForType:NSPasteboardTypeString];if(!text||[text isEqualToString:clip_last])return;
        NSData *d=[text dataUsingEncoding:NSUTF8StringEncoding];
        if(d.length>PTA_CLIP_MAX){NSLog(@"Clipboard: %lu bytes, over 1 MiB; not sent to Tiger",(unsigned long)d.length);return;}
        dispatch_async(clip_queue,^{NSString *out=nil;int rc=agent_request_soon(path,3,d,30,&out);
            dispatch_async(dispatch_get_main_queue(),^{if(!rc)clip_last=text;else if(clip_seen==cc)clip_seen=-1;   /* not sent: the next focus tries again */
                NSLog(@"Clipboard -> Tiger: %lu bytes, rc=%d %@",(unsigned long)d.length,rc,[out stringByTrimmingCharactersInSet:NSCharacterSet.whitespaceAndNewlineCharacterSet]);});});
    } else {
        NSData *body=argv_body(@[@"/bin/sh",@"-c",@"LANG=en_US.UTF-8 __CF_USER_TEXT_ENCODING=$(printf 0x%X $(id -u)):0x8000100:0x8000100 exec /usr/bin/pbpaste"]);
        dispatch_async(clip_queue,^{NSString *out=nil;int rc=agent_request_soon(path,1,body,30,&out);
            dispatch_async(dispatch_get_main_queue(),^{
                if(rc||!out.length||gen!=clip_gen||pb.changeCount!=cc||[out isEqualToString:clip_last])return;
                [pb clearContents];[pb setString:out forType:NSPasteboardTypeString];clip_seen=pb.changeCount;clip_last=out;
                NSLog(@"Clipboard <- Tiger: %lu characters",(unsigned long)out.length);});});
    }
}
- (void)syncInputSource {
    static NSString *sent;static BOOL pending;static CFAbsoluteTime next;static uint32_t owner;
    const char *flag=getenv("PT_R1_UX");if(!flag||strcmp(flag,"1")||self.stopped||!config->agent_socket)return;
    if(!self.window.window.isKeyWindow){sent=nil;return;}
    uint32_t session=0;if(!config->agent||!(pt_agent_capabilities(*config->agent,&session)&PTA_CAP_POINTER))return;
    if(owner!=session){owner=session;sent=nil;}
    CFAbsoluteTime now=CFAbsoluteTimeGetCurrent();if(pending||now<next)return;next=now+1;
    TISInputSourceRef input=TISCopyCurrentKeyboardLayoutInputSource();if(!input)return;
    NSString *source=(__bridge NSString *)TISGetInputSourceProperty(input,kTISPropertyInputSourceID);
    NSArray *languages=(__bridge NSArray *)TISGetInputSourceProperty(input,kTISPropertyInputSourceLanguages);
    NSString *language=languages.firstObject?:@"";source=[source copy];language=[[language componentsSeparatedByCharactersInSet:[NSCharacterSet characterSetWithCharactersInString:@"-_"]] firstObject];CFRelease(input);
    if(!source.length||[source isEqual:sent])return;pending=YES;NSString *sock=@(config->agent_socket);
    dispatch_async(dispatch_get_global_queue(QOS_CLASS_UTILITY,0),^{NSString *out=nil;int rc=agent_request(sock,1,argv_body(@[@"/Ptolemy/input",@"layout",source,language]),5,&out);
        dispatch_async(dispatch_get_main_queue(),^{pending=NO;if(!rc&&owner==session)sent=source;});});
}
/* An .iso/.cdr/.toast becomes the CD/DVD, any other disk image or a .zip a read-only USB disk (media_change in
 * firmware.c, which runs hdiutil): off the main thread, and a refusal is shown in the window. */
- (void)chooseMediaImage {
    if(!config->media||self.stopped)return;NSOpenPanel *panel=[NSOpenPanel openPanel];panel.prompt=@"Insert";
    panel.message=@"Choose a disk image (.dmg, .iso, .cdr, .img, .sparseimage) or a .zip archive to give to Tiger.";
    [panel beginSheetModalForWindow:self.window.window completionHandler:^(NSModalResponse response){
        if(response!=NSModalResponseOK||!panel.URL)return;NSString *path=panel.URL.path;
        dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED,0),^{int e=config->media(path.fileSystemRepresentation,0);
            if(e==EPERM)[self tellDiscInUse];
            else if(e)dispatch_async(dispatch_get_main_queue(),^{if(self.stopped)return;NSAlert *a=[NSAlert new];a.messageText=@"Tiger could not use this image";
                a.informativeText=[NSString stringWithFormat:@"%@: %s.",path.lastPathComponent,strerror(e)];[a beginSheetModalForWindow:self.window.window completionHandler:nil];});});}];
}
- (void)tellDiscInUse {   /* EPERM from media_change: Tiger has the disc in the CD/DVD drive mounted */
    dispatch_async(dispatch_get_main_queue(),^{if(self.stopped)return;NSAlert *a=[NSAlert new];a.messageText=@"Tiger is using the disc in the CD/DVD drive";
        a.informativeText=@"Eject it in Tiger first (drag its icon to the Trash), then try again.";[a beginSheetModalForWindow:self.window.window completionHandler:nil];});
}
/* Explains to the user what the web gateway (web_macos.m) does for Tiger's browsers. */
- (void)aboutWeb:(id)sender {
    (void)sender;NSAlert *a=[NSAlert new];a.messageText=@"Web pages in Tiger";
    a.informativeText=@"Tiger's own browsers can no longer open most of today's secure sites. To help them, your Mac fetches http:// pages "
        "for Tiger over a secure HTTPS connection whenever the site offers one, and adjusts them a little (links, newer picture formats) "
        "so Tiger can show them. All of this happens on this Mac and nothing is sent anywhere else: the sites are reached the same way "
        "as when you browse on your Mac. Browsers that handle https:// themselves, such as PowerFox, connect directly.";
    if(self.window.window)[a beginSheetModalForWindow:self.window.window completionHandler:nil];else [a runModal];
}
- (void)ejectMedia:(NSInteger)slot {
    if(!config->media||self.stopped)return;
    dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED,0),^{int e=config->media(NULL,(int)slot);if(e==EPERM)[self tellDiscInUse];else if(e)NSLog(@"Eject: %s",strerror(e));});
}
- (void)exchangeFiles:(NSArray<NSURL *> *)files {
    const char *sock=config->agent_socket;
    if(!sock){NSLog(@"File drop needs --agent-socket and an enabled Tiger agent: %@",files);return;}
    static dispatch_queue_t queue;static dispatch_once_t once;
    dispatch_once(&once,^{queue=dispatch_queue_create("ptolemy.drop",DISPATCH_QUEUE_SERIAL);});
    NSString *path=@(sock);for(NSURL *url in files)dispatch_async(queue,^{drop_to_guest(path,url);});
}
- (void)exportExchangeFiles {
    const char *sock=config->agent_socket;if(!sock||self.stopped)return;
    NSAlert *a=[NSAlert new];a.messageText=@"Receive a File from Tiger";a.informativeText=@"Enter the full path of a regular file in Tiger. It will be copied without replacing an existing Mac file.";
    NSTextField *path=[NSTextField textFieldWithString:@"/Users/"];path.frame=NSMakeRect(0,0,420,24);a.accessoryView=path;[a addButtonWithTitle:@"Choose Destination…"];[a addButtonWithTitle:@"Cancel"];
    NSString *socket=@(sock);
    [a beginSheetModalForWindow:self.window.window completionHandler:^(NSModalResponse response){
        if(response!=NSAlertFirstButtonReturn)return;NSString *guest=path.stringValue;
        if(![guest hasPrefix:@"/"]||guest.length>1000)return;
        NSOpenPanel *panel=[NSOpenPanel openPanel];panel.canChooseDirectories=YES;panel.canChooseFiles=NO;panel.prompt=@"Receive File";
        [panel beginSheetModalForWindow:self.window.window completionHandler:^(NSModalResponse result){
            if(result!=NSModalResponseOK)return;
            NSData *directory=[panel.URL.URLByResolvingSymlinksInPath.path dataUsingEncoding:NSUTF8StringEncoding],*source=[guest dataUsingEncoding:NSUTF8StringEncoding];
            NSMutableData *body=[NSMutableData data];uint32_t n=htonl((uint32_t)directory.length);[body appendBytes:&n length:4];[body appendData:directory];n=htonl((uint32_t)source.length);[body appendBytes:&n length:4];[body appendData:source];
            PTAgentCancellation *operation=[PTAgentCancellation new];NSAlert *progress=[NSAlert new];progress.messageText=@"Receiving File…";progress.informativeText=guest;[progress addButtonWithTitle:@"Cancel Transfer"];
            [progress beginSheetModalForWindow:self.window.window completionHandler:^(NSModalResponse response){if(response==NSAlertFirstButtonReturn)[operation cancel];}];
            dispatch_async(dispatch_get_global_queue(QOS_CLASS_UTILITY,0),^{NSString *output=nil;int status=agent_request_cancel(socket,4,body,600,&output,operation);
                dispatch_async(dispatch_get_main_queue(),^{if(self.stopped)return;[self.window.window endSheet:progress.window returnCode:NSModalResponseOK];
                    NSAlert *done=[NSAlert new];BOOL cancelled=operation.cancelled;done.messageText=cancelled?@"Transfer Cancelled":status?@"File Was Not Received":@"File Received";
                    done.informativeText=cancelled?@"No partial file is published. A file that completed before cancellation may remain.":status?@"Check the file path, available space, and Ptolemy Tools. Existing files were preserved.":[NSString stringWithFormat:@"%@ was saved to %@.",guest.lastPathComponent,panel.URL.path];
                    dispatch_async(dispatch_get_main_queue(),^{if(!self.stopped)[done beginSheetModalForWindow:self.window.window completionHandler:nil];});});
            });
        }];
    }];
}
- (void)setGuestScaling:(NSString *)mode { self.scaling=[mode isEqual:@"integer"] ? 1 : [mode isEqual:@"stretch"] ? 2 : 0; }
- (void)resizeGuestLayer:(CAMetalLayer *)layer points:(CGSize)points scale:(CGFloat)scale fullscreen:(BOOL)fs { (void)layer; (void)points; (void)scale; (void)fs; }
@end

@interface PTStandPresenter : NSObject
- (void)stop;
@end
@implementation PTStandPresenter {
    id<MTLDevice> _dev; id<MTLCommandQueue> _queue; id<MTLRenderPipelineState> _pipe,_pipeTexture;
    PTDisplayView *_view; PTStandActions *_actions;
    id<MTLCommandBuffer> _last; BOOL _gameModes;
    id<MTLBuffer> _cursor; uint32_t _cursorGen,_cursorW,_cursorH,_gammaGen,_gamma[256]; BOOL _gammaOn;
}
- (instancetype)initWithView:(PTDisplayView *)view actions:(PTStandActions *)actions {
    if (!(self=[super init])) return nil;
    const char *modes=getenv("PT_GAME_MODES");_gameModes=modes && !strcmp(modes,"1");
    _view=view; _actions=actions; _dev=MTLCreateSystemDefaultDevice(); _queue=[_dev newCommandQueue];
    const char *color=getenv("PT_COLOR_SRGB");if(color&&!strcmp(color,"1")){
        CGColorSpaceRef space=CGColorSpaceCreateWithName(kCGColorSpaceSRGB);view.metalLayer.colorspace=space;CGColorSpaceRelease(space);view.metalLayer.wantsExtendedDynamicRangeContent=NO;
    }
    view.metalLayer.device=_dev; view.metalLayer.framebufferOnly=YES;
    NSError *e=nil; id<MTLLibrary> lib=[_dev newLibraryWithSource:shader options:nil error:&e];
    if (!lib) { NSLog(@"Metal: %@", e); return nil; }
    MTLRenderPipelineDescriptor *d=[MTLRenderPipelineDescriptor new];
    d.vertexFunction=[lib newFunctionWithName:@"vs"]; d.fragmentFunction=[lib newFunctionWithName:@"fs"];
    d.colorAttachments[0].pixelFormat=view.metalLayer.pixelFormat;
    _pipe=[_dev newRenderPipelineStateWithDescriptor:d error:&e]; if (!_pipe) { NSLog(@"Metal: %@", e); return nil; }
    d.fragmentFunction=[lib newFunctionWithName:@"fst"];
    _pipeTexture=[_dev newRenderPipelineStateWithDescriptor:d error:&e]; if (!_pipeTexture) { NSLog(@"Metal: %@", e); return nil; }
    _cursor=[_dev newBufferWithLength:64*64*4 options:MTLResourceStorageModeShared];
    return self;
}
- (void)frame:(NSTimer *)timer {
    (void)timer;
    if(config->cursor)[_view observeGuestCursor:pt_cursor_read(config->cursor,pt_cursor_now())];
    if(config->agent&&*config->agent){PtGuestPointer p;if(pt_agent_pointer_read(*config->agent,&p)){[_view observeGuestCursor:p.state];[_view observeGuestPointerX:p.x y:p.y width:p.width height:p.height at:p.at];}}
    [_actions.window updateCaptureSuggestion];[_actions syncInputSource];[_view pollPointer];
    PtRadeonFrame rf={0};PtRadeon *card=*config->radeon;
    if(!card || !pt_radeon_frame_acquire(card,&rf))return;
    const BOOL leased=YES;
    const struct { int enabled; unsigned width,height,bpp,line,start; int big_endian; } m={1,rf.width,rf.height,rf.bpp,rf.line,0,rf.big_endian};
    id<CAMetalDrawable> drawable=[_view.metalLayer nextDrawable]; if (!drawable) {if(leased)pt_radeon_frame_release(card,&rf);return;}
    CGSize ds=_view.metalLayer.drawableSize;
    Params p={0};
    if (m.enabled) {
        double sx=ds.width/m.width, sy=ds.height/m.height, s=fmin(sx,sy);
        unsigned mode=_actions.scaling;
        if (mode==1 && s>=1) s=floor(s);
        /* Full screen with a near-native guest mode (PT_EDID_MODE=screen): fill, no bars for the <2.5% aspect
         * rest of the 64-pixel width rounding. */
        BOOL fill=!_gameModes && (_view.window.styleMask&NSWindowStyleMaskFullScreen) && fabs(sx/sy-1)<0.025;
        if (mode==2 || fill) { p.sx=sx; p.sy=sy; } else { p.sx=p.sy=s; }
        p.ox=(ds.width-m.width*p.sx)/2; p.oy=(ds.height-m.height*p.sy)/2;
        p.w=m.width; p.h=m.height; p.bpp=m.bpp; p.line=m.line; p.start=m.start; p.be=m.big_endian;
        static char last[128];char now[128];
        snprintf(now,sizeof now,"drawable %.0fx%.0f, guest %ux%u, scale %.3f/%.3f, offset %.1f/%.1f, fill %d",
                 ds.width,ds.height,m.width,m.height,p.sx,p.sy,p.ox,p.oy,fill?1:0);
        if(strcmp(now,last)){strcpy(last,now);NSLog(@"Present: %s",now);}
    }
    PtHwCursor *hc=config->hw_cursor;uint64_t pos=atomic_load(&hc->pos);   /* read at every presentation, not per frame */
    pthread_mutex_lock(&hc->lock);   /* a new image gets a new buffer: frames in flight keep the old one */
    if(hc->gen!=_cursorGen){_cursorGen=hc->gen;_cursorW=hc->w;_cursorH=hc->h;_cursor=[_dev newBufferWithBytes:hc->image length:sizeof hc->image options:MTLResourceStorageModeShared];}
    if(hc->gamma_gen!=_gammaGen){_gammaGen=hc->gamma_gen;_gammaOn=hc->gamma_on;memcpy(_gamma,hc->gamma,sizeof _gamma);}
    pthread_mutex_unlock(&hc->lock);
    p.cx=(int16_t)pos;p.cy=(int16_t)(pos>>16);p.cw=_cursorW;p.ch=_cursorH;p.visible=(pos>>32&1) && _cursorW;p.gamma=_gammaOn;
    /* Automatic capture follows the agent's CGCursorIsVisible only: the hardware cursor is also hidden while typing. */
    _view.guestCursorHidden=YES;
    CGFloat k=_view.metalLayer.contentsScale>0?_view.metalLayer.contentsScale:1;   /* pointer maps through the drawn image */
    _view.guestRect=m.enabled?NSMakeRect(p.ox/k,p.oy/k,m.width*p.sx/k,m.height*p.sy/k):NSZeroRect;
    _view.pointerSuspended=NO;
    MTLRenderPassDescriptor *rp=[MTLRenderPassDescriptor renderPassDescriptor];
    rp.colorAttachments[0].texture=drawable.texture; rp.colorAttachments[0].loadAction=MTLLoadActionClear;
    rp.colorAttachments[0].clearColor=MTLClearColorMake(0,0,0,1); rp.colorAttachments[0].storeAction=MTLStoreActionStore;
    id<MTLCommandBuffer> cb=[_queue commandBuffer]; id<MTLRenderCommandEncoder> enc=[cb renderCommandEncoderWithDescriptor:rp];
    if (m.enabled && leased && rf.metal_texture) {   /* immutable native frame, leased until this presentation completes */
        [enc setRenderPipelineState:_pipeTexture]; [enc setFragmentBytes:&p length:sizeof p atIndex:0];
        [enc setFragmentTexture:(__bridge id<MTLTexture>)rf.metal_texture atIndex:0]; [enc setFragmentBuffer:_cursor offset:0 atIndex:3];
        [enc setFragmentBytes:_gamma length:sizeof _gamma atIndex:4];
        [enc drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];
    } else if (m.enabled) {
        [enc setRenderPipelineState:_pipe];
        [enc setFragmentBytes:&p length:sizeof p atIndex:0]; [enc setFragmentBuffer:(__bridge id<MTLBuffer>)rf.metal_buffer offset:0 atIndex:1];
        [enc setFragmentBytes:rf.palette length:sizeof rf.palette atIndex:2];
        [enc setFragmentBuffer:_cursor offset:0 atIndex:3]; [enc setFragmentBytes:_gamma length:sizeof _gamma atIndex:4];
        [enc drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];
    }
    [enc endEncoding];
    if(leased)[cb addCompletedHandler:^(id<MTLCommandBuffer> done){(void)done;pt_radeon_frame_release(card,&rf);}];
    [cb presentDrawable:drawable]; [cb commit];_last=cb;
}
- (void)stop {
    [_last waitUntilCompleted];_last=nil;
}
@end

static void *machine_thread(void *arg) {
    (void)arg;@autoreleasepool {
        exit_code=config->machine();
        if(!atomic_load(&ready_called))dispatch_semaphore_signal(ready);
    }return NULL;
}

int pt_stand_window_main(PtStandWindow *w) {
    BOOL screenMode=NO;
    @autoreleasepool{
        /* Windowed launches only, before the machine thread creates fw_cfg or EDID. Default: the screen mode below.
         * PT_EDID_MODE=1024x768 keeps the classic EDID (scripted sessions click at fixed guest coordinates); any
         * other explicit mode wins. No mode switch when a running window changes monitor. */
        [NSApplication sharedApplication];NSScreen *screen=[NSScreen mainScreen];
        /* PT_EDID_MODE=screen: the guest mode is this screen's full-screen content size in points (below the
         * camera housing), width rounded to the nearest 64 pixels: Tiger's compositor blits with a 64-pixel
         * aligned pitch, and a CRTC pitch that differs (e.g. 1464) leaves small sheared sprites on screen.
         * The presenter fills the rest in full screen. */
        const char *wanted=getenv("PT_EDID_MODE");BOOL classic=wanted && !strcmp(wanted,"1024x768");
        if(classic)unsetenv("PT_EDID_MODE");
        if(!classic && (!wanted || !strcmp(wanted,"screen")) && screen) {
            NSRect f=screen.frame;NSEdgeInsets in={0,0,0,0};
            if(@available(macOS 12.0,*))in=screen.safeAreaInsets;
            char m[32];snprintf(m,sizeof m,"%ux%u",((unsigned)(f.size.width-in.left-in.right)+32)/64*64,(unsigned)(f.size.height-in.top-in.bottom));
            screenMode=pt_boot_mode(m)!=NULL;
            if(screenMode)setenv("PT_EDID_MODE",m,1);else unsetenv("PT_EDID_MODE");
            NSLog(@"Screen mode %s%s (screen %.0fx%.0f pt, safe-area top %.0f)",m,screenMode?"":" not representable, 1024x768",f.size.width,f.size.height,in.top);
        }
        if(!classic && !getenv("PT_EDID_MODE") && screen &&    /* screen mode not representable: 5K fallback */
           screen.frame.size.width*screen.backingScaleFactor>=5120 &&
           screen.frame.size.height*screen.backingScaleFactor>=2880)setenv("PT_EDID_MODE","2560x1440",1);
    }
    config=w;exit_code=0;restart_requested=0;atomic_store(&ready_called,0);ready=dispatch_semaphore_create(0);
    pthread_t thread; pthread_attr_t attr; pthread_attr_init(&attr); pthread_attr_setstacksize(&attr, 8u<<20);
    int error=pthread_create(&thread, &attr, machine_thread, NULL);pthread_attr_destroy(&attr);
    if (error) { ready=nil;config=NULL;return error; }
    dispatch_semaphore_wait(ready, DISPATCH_TIME_FOREVER);       /* devices and input queue exist from here on */
    if(!atomic_load(&ready_called)) {pthread_join(thread,NULL);ready=nil;config=NULL;return exit_code?exit_code:1;}
    @autoreleasepool {
        NSApplication *app=[NSApplication sharedApplication]; [app setActivationPolicy:NSApplicationActivationPolicyRegular];
        NSWindow.allowsAutomaticWindowTabbing=NO;   /* no Show Tab Bar / Show All Tabs in the View menu: one guest window */
        PTStandActions *actions=[PTStandActions new]; actions.scaling=screenMode?0:1;
        app.delegate=actions;
        PtInputSink sink={w->input, pt_input_send};
        PTGuestWindow *win=[[PTGuestWindow alloc] initWithTitle:@"Ptolemy" input:sink actions:actions]; actions.window=win;
        NSMenu *bar=[NSMenu new]; NSMenuItem *appItem=[NSMenuItem new]; [bar addItem:appItem];
        NSMenu *appMenu=[NSMenu new]; [appMenu addItemWithTitle:@"Quit Ptolemy" action:@selector(terminate:) keyEquivalent:win.displayView.inputPolicyEnabled?@"":@"q"]; appItem.submenu=appMenu;
        for (NSMenu *m in @[win.machineMenu, win.mediaMenu, win.viewMenu]) { NSMenuItem *it=[NSMenuItem new]; it.submenu=m; m.title=m.title; it.title=m.title; [bar addItem:it]; }
        NSMenuItem *helpItem=[NSMenuItem new];NSMenu *help=[[NSMenu alloc] initWithTitle:@"Help"];helpItem.submenu=help;helpItem.title=@"Help";[bar addItem:helpItem];
        [help addItemWithTitle:@"Web Pages in Tiger" action:@selector(aboutWeb:) keyEquivalent:@""].target=actions;app.helpMenu=help;
        app.mainMenu=bar;
        PTStandPresenter *presenter=[[PTStandPresenter alloc] initWithView:win.displayView actions:actions];
        [win showWindow:nil]; [app activateIgnoringOtherApps:YES];
        NSTimer *t=nil;
        if (presenter) { t=[NSTimer timerWithTimeInterval:1.0/60 target:presenter selector:@selector(frame:) userInfo:nil repeats:YES];
                         [[NSRunLoop mainRunLoop] addTimer:t forMode:NSRunLoopCommonModes]; }
        quiesce=^{actions.stopped=YES;[t invalidate];[win guestStopped];[presenter stop];};
        if(!presenter)[actions forceStopGuest:NO];
        /* SIGHUP (Ptolemy.app quitting while Tiger runs) asks Tiger to shut down, as Shut Down does. */
        signal(SIGHUP,SIG_IGN);
        dispatch_source_t hup=dispatch_source_create(DISPATCH_SOURCE_TYPE_SIGNAL,SIGHUP,0,dispatch_get_main_queue());
        dispatch_source_set_event_handler(hup,^{[actions shutdownGuest];});dispatch_resume(hup);
        dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0), ^{
            pthread_join(thread, NULL);
            dispatch_async(dispatch_get_main_queue(), ^{
                [NSApp stop:nil];
                [NSApp postEvent:[NSEvent otherEventWithType:NSEventTypeApplicationDefined location:NSZeroPoint modifierFlags:0
                    timestamp:0 windowNumber:0 context:nil subtype:0 data1:0 data2:0] atStart:NO];
            });
        });
        /* No App Nap while the guest runs: a napped (hidden/occluded) app gets its timers coalesced, the CPU thread
         * misses device deadlines (OHCI skipped 67752 of 443000 frames in a background run) and input/audio lag. */
        /* PT_NAP=1 / PT_HIDE=1: tests only (A/B of this activity; hidden app as the user would leave it). */
        id<NSObject> activity=getenv("PT_NAP")?nil:[[NSProcessInfo processInfo] beginActivityWithOptions:
            NSActivityUserInitiatedAllowingIdleSystemSleep|NSActivityLatencyCritical reason:@"Ptolemy guest machine"];
        if(getenv("PT_FULLSCREEN")) dispatch_after(dispatch_time(DISPATCH_TIME_NOW,(getenv("PT_PRODUCT_UI")&&!strcmp(getenv("PT_PRODUCT_UI"),"1"))?0:2*NSEC_PER_SEC),dispatch_get_main_queue(),^{[win.window toggleFullScreen:nil];}); /* tests */
        if(getenv("PT_HIDE")) dispatch_after(dispatch_time(DISPATCH_TIME_NOW,(getenv("PT_PRODUCT_UI")&&!strcmp(getenv("PT_PRODUCT_UI"),"1"))?0:2*NSEC_PER_SEC),dispatch_get_main_queue(),^{[NSApp hide:nil];});
        [app run];
        if(activity) [[NSProcessInfo processInfo] endActivity:activity];
        dispatch_source_cancel(hup);signal(SIGHUP,SIG_DFL);   /* a restart re-executes with the default disposition */
        quiesce=nil;app.delegate=nil;
        if(!presenter && !exit_code)exit_code=1;
        if(restart_requested && !exit_code)exit_code=PT_EXIT_RESTART;
        if(exit_code==PT_EXIT_RESTART){   /* main() re-executes the product: same window frame and mode */
            setenv("PT_WINDOW_FRAME",NSStringFromRect(win.window.frame).UTF8String,1);
            if(win.window.styleMask&NSWindowStyleMaskFullScreen)setenv("PT_FULLSCREEN","1",1);else unsetenv("PT_FULLSCREEN");
        }
    }
    ready=nil;config=NULL;
    return exit_code;
}
