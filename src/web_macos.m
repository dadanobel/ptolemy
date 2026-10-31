/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Web gateway, so that even Safari 2.0 shows something of today's sites. The guest's plain-HTTP connections (port 80,
 * diverted by net_user.c) end here. The Mac fetches the same address over HTTPS (plain HTTP when the site has none), https://
 * in pages, styles and scripts becomes http:// so the next click comes back here, HSTS and CSP are dropped, cookies lose
 * Secure, and WebP/AVIF/HEIC images become JPEG or PNG. Port 443 stays plain NAT: PowerFox and other current guest browsers
 * do their own TLS. Original code, inspired by the WebOne proxy. Hosts on the Mac's loopback are refused, as in the NAT.
 * Rewritten bodies are held whole in memory; other bodies stream. */
#import <Foundation/Foundation.h>
#import <ImageIO/ImageIO.h>
#include "web.h"
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <unistd.h>
static BOOL send_all(int fd,const void *p,size_t n){
    const uint8_t *b=p;while(n){ssize_t w=write(fd,b,n);if(w<0&&errno==EINTR)continue;if(w<=0)return NO;b+=w;n-=(size_t)w;}return YES;
}
static NSString *html(NSString *s){
    return [[[s stringByReplacingOccurrencesOfString:@"&" withString:@"&amp;"] stringByReplacingOccurrencesOfString:@"<" withString:@"&lt;"]
        stringByReplacingOccurrencesOfString:@">" withString:@"&gt;"];
}
static void answer(int fd,int code,NSString *text){
    NSData *body=[[NSString stringWithFormat:@"<html><head><title>Ptolemy</title></head><body><h3>Ptolemy web gateway</h3><p>%@</p></body></html>\n",html(text)]
        dataUsingEncoding:NSUTF8StringEncoding];
    NSString *head=[NSString stringWithFormat:@"HTTP/1.0 %d %@\r\nContent-Type: text/html; charset=utf-8\r\nContent-Length: %lu\r\nConnection: close\r\n\r\n",
        code,code==400?@"Bad Request":code==405?@"Method Not Allowed":code==411?@"Length Required":code==413?@"Payload Too Large":code==403?@"Forbidden":@"Bad Gateway",
        (unsigned long)body.length];
    NSData *h=[head dataUsingEncoding:NSUTF8StringEncoding];if(send_all(fd,h.bytes,h.length))send_all(fd,body.bytes,body.length);
}
static NSString *downgraded(NSString *s){return [s stringByReplacingOccurrencesOfString:@"https://" withString:@"http://"];}
/* The host names the Mac itself (loopback, any address): not reachable from Tiger, as net_user.c refuses 127/8. */
static BOOL local_host(const char *host){
    struct addrinfo hints={.ai_socktype=SOCK_STREAM},*list=NULL;if(getaddrinfo(host,NULL,&hints,&list))return NO;BOOL local=NO;
    for(struct addrinfo *a=list;a;a=a->ai_next){
        if(a->ai_family==AF_INET){uint32_t v=ntohl(((struct sockaddr_in *)a->ai_addr)->sin_addr.s_addr);local|=v>>24==127||v>>24==0;}
        else if(a->ai_family==AF_INET6){const struct in6_addr *v=&((struct sockaddr_in6 *)a->ai_addr)->sin6_addr;local|=IN6_IS_ADDR_LOOPBACK(v)||IN6_IS_ADDR_UNSPECIFIED(v)||
            (IN6_IS_ADDR_V4MAPPED(v)&&(v->s6_addr[12]==127||!v->s6_addr[12]));}
    }
    freeaddrinfo(list);return local;
}
/* WebP/AVIF/HEIC, which Tiger cannot decode: PNG when the image has alpha, else JPEG. */
static NSData *transcode(NSData *d,NSString **type){
    CGImageSourceRef src=CGImageSourceCreateWithData((__bridge CFDataRef)d,NULL);if(!src)return nil;
    CGImageRef image=CGImageSourceCreateImageAtIndex(src,0,NULL);CFRelease(src);if(!image)return nil;
    CGImageAlphaInfo a=CGImageGetAlphaInfo(image);BOOL alpha=!(a==kCGImageAlphaNone||a==kCGImageAlphaNoneSkipLast||a==kCGImageAlphaNoneSkipFirst);
    NSMutableData *out=[NSMutableData data];
    CGImageDestinationRef dst=CGImageDestinationCreateWithData((__bridge CFMutableDataRef)out,alpha?CFSTR("public.png"):CFSTR("public.jpeg"),1,NULL);
    if(dst){NSDictionary *o=alpha?@{}:@{(__bridge id)kCGImageDestinationLossyCompressionQuality:@0.85};CGImageDestinationAddImage(dst,image,(__bridge CFDictionaryRef)o);
        if(!CGImageDestinationFinalize(dst))out=nil;CFRelease(dst);}else out=nil;
    CGImageRelease(image);*type=alpha?@"image/png":@"image/jpeg";return out;
}
static NSURLSessionConfiguration *configuration(void){
    static NSURLSessionConfiguration *c;static dispatch_once_t once;
    dispatch_once(&once,^{c=[NSURLSessionConfiguration ephemeralSessionConfiguration];c.HTTPCookieStorage=nil;c.HTTPShouldSetCookies=NO;c.URLCache=nil;
        c.requestCachePolicy=NSURLRequestReloadIgnoringLocalCacheData;c.timeoutIntervalForRequest=600;});   /* the guest drains slowly (writes block the delegate) */
    return c;
}
@interface PtWebLink : NSObject <NSURLSessionDataDelegate>
- (instancetype)initWithSocket:(int)fd;
- (void)run;
@end
@implementation PtWebLink {
    int _fd;BOOL _plain,_buffer,_head,_short;NSMutableURLRequest *_request;NSMutableData *_body;NSHTTPURLResponse *_response;NSError *_error;dispatch_semaphore_t _done;long long _sent;
}
- (instancetype)initWithSocket:(int)fd {if((self=[super init])){_fd=fd;_body=[NSMutableData data];_done=dispatch_semaphore_create(0);}return self;}
- (BOOL)sendHead:(long long)length type:(NSString *)type {
    NSInteger code=_response.statusCode==308?301:_response.statusCode;   /* 308 is newer than Tiger */
    NSMutableString *h=[NSMutableString stringWithFormat:@"HTTP/1.0 %ld %@\r\n",(long)code,code<400?@"OK":@"Error"];
    static NSSet *drop;static dispatch_once_t once;
    dispatch_once(&once,^{drop=[NSSet setWithArray:@[@"content-encoding",@"content-length",@"transfer-encoding",@"connection",@"keep-alive",@"strict-transport-security",
        @"content-security-policy",@"content-security-policy-report-only",@"alt-svc",@"set-cookie",@"public-key-pins",@"expect-ct",@"upgrade"]];});
    [_response.allHeaderFields enumerateKeysAndObjectsUsingBlock:^(NSString *k,NSString *v,BOOL *stop){(void)stop;NSString *l=k.lowercaseString;
        if([drop containsObject:l]||[v rangeOfCharacterFromSet:NSCharacterSet.newlineCharacterSet].location!=NSNotFound)return;
        if(type&&[l isEqualToString:@"content-type"])v=type;
        if([l isEqualToString:@"location"]||[l isEqualToString:@"content-location"]||[l isEqualToString:@"refresh"]||[l isEqualToString:@"link"])v=downgraded(v);
        [h appendFormat:@"%@: %@\r\n",k,v];}];
    NSDateFormatter *f=[NSDateFormatter new];f.locale=[NSLocale localeWithLocaleIdentifier:@"en_US_POSIX"];f.timeZone=[NSTimeZone timeZoneWithAbbreviation:@"GMT"];
    f.dateFormat=@"EEE, dd MMM yyyy HH:mm:ss 'GMT'";
    for(NSHTTPCookie *c in [NSHTTPCookie cookiesWithResponseHeaderFields:_response.allHeaderFields forURL:_response.URL]){   /* one per line, without Secure/SameSite */
        [h appendFormat:@"Set-Cookie: %@=%@; path=%@",c.name,c.value,c.path.length?c.path:@"/"];
        if([c.domain hasPrefix:@"."])[h appendFormat:@"; domain=%@",c.domain];if(c.expiresDate)[h appendFormat:@"; expires=%@",[f stringFromDate:c.expiresDate]];
        if(c.HTTPOnly)[h appendString:@"; HttpOnly"];[h appendString:@"\r\n"];
    }
    if(length>=0)[h appendFormat:@"Content-Length: %lld\r\n",length];
    [h appendString:@"Connection: close\r\n\r\n"];NSData *d=[h dataUsingEncoding:NSUTF8StringEncoding];return send_all(_fd,d.bytes,d.length);
}
- (void)URLSession:(NSURLSession *)s task:(NSURLSessionTask *)t willPerformHTTPRedirection:(NSHTTPURLResponse *)r newRequest:(NSURLRequest *)q
    completionHandler:(void (^)(NSURLRequest *))handler {(void)s;(void)t;(void)r;(void)q;handler(nil);}   /* Tiger follows them itself (Location downgraded) */
- (void)URLSession:(NSURLSession *)s dataTask:(NSURLSessionDataTask *)t didReceiveResponse:(NSURLResponse *)r
    completionHandler:(void (^)(NSURLSessionResponseDisposition))handler {
    (void)s;(void)t;_response=(NSHTTPURLResponse *)r;NSString *type=_response.MIMEType.lowercaseString?:@"";
    /* Software Update's catalog is application/x-apple-plist and keeps https:// for every .dist: rewritten too, by type or name. */
    NSString *ext=_request.URL.pathExtension.lowercaseString?:@"";
    _buffer=!_head&&([type hasPrefix:@"text/"]||[type containsString:@"javascript"]||[type containsString:@"json"]||[type containsString:@"xml"]||[type containsString:@"plist"]||
        [@[@"sucatalog",@"dist",@"smd",@"plist"] containsObject:ext]||[@[@"image/webp",@"image/avif",@"image/heic",@"image/heif"] containsObject:type]);
    /* A body passed through unchanged keeps the site's length, unless the Mac decoded it (Content-Encoding); without it
     * Software Update's download of a 79 MB package failed (NSURLErrorDomain -1005, size "------"). */
    NSString *coding=[_response valueForHTTPHeaderField:@"Content-Encoding"];
    long long length=!coding.length||[coding caseInsensitiveCompare:@"identity"]==NSOrderedSame?_response.expectedContentLength:-1;
    handler(_buffer||[self sendHead:length type:nil]?NSURLSessionResponseAllow:NSURLSessionResponseCancel);
}
- (void)URLSession:(NSURLSession *)s dataTask:(NSURLSessionDataTask *)t didReceiveData:(NSData *)d {
    (void)s;if(_buffer){[_body appendData:d];return;}
    __block BOOL ok=YES;[d enumerateByteRangesUsingBlock:^(const void *p,NSRange r,BOOL *stop){if(!(ok=send_all(self->_fd,p,r.length)))*stop=YES;}];
    if(ok)_sent+=(long long)d.length;else{_short=YES;[t cancel];}
}
- (void)URLSession:(NSURLSession *)s task:(NSURLSessionTask *)t didCompleteWithError:(NSError *)e {
    (void)t;
    if(e&&!_response&&!_plain){   /* no HTTPS there (or it failed before any answer): the site as asked, over plain HTTP */
        _plain=YES;NSURLComponents *u=[NSURLComponents componentsWithURL:_request.URL resolvingAgainstBaseURL:NO];u.scheme=@"http";_request.URL=u.URL;
        [[s dataTaskWithRequest:_request] resume];return;
    }
    _error=e;dispatch_semaphore_signal(_done);
}
- (void)run {
    char *head=malloc(PT_WEB_HEAD);size_t n=0,end=0;if(!head)return;
    while(!end&&n<PT_WEB_HEAD){ssize_t r=read(_fd,head+n,PT_WEB_HEAD-n);if(r<0&&errno==EINTR)continue;if(r<=0)break;n+=(size_t)r;
        char *a=memmem(head,n,"\r\n\r\n",4),*b=memmem(head,n,"\n\n",2);if(a&&(!b||a<b))end=(size_t)(a-head)+4;else if(b)end=(size_t)(b-head)+2;}
    PtWebRequest q;
    if(!end||pt_web_parse(head,end,&q)){if(n)answer(_fd,400,@"This request could not be read.");free(head);return;}
    NSString *host=nil,*path=nil;const char *target=q.target;
    if(!strncasecmp(target,"http://",7)){const char *p=strchr(target+7,'/');host=p?[[NSString alloc] initWithBytes:target+7 length:(NSUInteger)(p-target-7) encoding:NSUTF8StringEncoding]:@(target+7);path=p?@(p):@"/";}
    else if(target[0]=='/'){const char *h=pt_web_header(&q,"Host");host=h?@(h):nil;path=@(target);}
    if(!strcmp(q.method,"CONNECT")){answer(_fd,405,@"Secure connections through the gateway are not supported: type http:// instead of https://.");free(head);return;}
    if(!host.length||!path){answer(_fd,400,@"The request names no web site.");free(head);return;}
    if([host hasSuffix:@":80"])host=[host substringToIndex:host.length-3];
    NSString *name=[host hasPrefix:@"["]?host:[host componentsSeparatedByString:@":"].firstObject;
    if(local_host([[name stringByTrimmingCharactersInSet:[NSCharacterSet characterSetWithCharactersInString:@"[]"]] UTF8String])){
        answer(_fd,403,@"The Mac itself is not reachable from Tiger.");free(head);return;}
    NSString *text=[NSString stringWithFormat:@"https://%@%@",host,path];NSURL *url=[NSURL URLWithString:text];
    if(!url){NSMutableCharacterSet *ok=[NSCharacterSet.URLQueryAllowedCharacterSet mutableCopy];[ok addCharactersInString:@"%#"];
        url=[NSURL URLWithString:[text stringByAddingPercentEncodingWithAllowedCharacters:ok]];}
    if(!url){answer(_fd,400,@"The address could not be read.");free(head);return;}
    _request=[NSMutableURLRequest requestWithURL:url];_request.HTTPMethod=@(q.method);_request.HTTPShouldHandleCookies=NO;_head=!strcmp(q.method,"HEAD");
    static NSSet *skip;static dispatch_once_t once;
    dispatch_once(&once,^{skip=[NSSet setWithArray:@[@"host",@"connection",@"proxy-connection",@"keep-alive",@"proxy-authorization",@"te",@"trailer",@"transfer-encoding",
        @"upgrade",@"content-length",@"accept-encoding",@"expect"]];});
    for(unsigned i=0;i<q.headers;i++){NSString *k=[[NSString alloc] initWithUTF8String:q.name[i]],*v=[[NSString alloc] initWithUTF8String:q.value[i]]?:[[NSString alloc] initWithCString:q.value[i] encoding:NSISOLatin1StringEncoding];
        NSString *l=k.lowercaseString;if(!k||!v||[skip containsObject:l])continue;
        if(([l isEqualToString:@"referer"]||[l isEqualToString:@"origin"])&&[v hasPrefix:@"http://"])v=[@"https://" stringByAppendingString:[v substringFromIndex:7]];
        [_request addValue:v forHTTPHeaderField:k];}
    const char *te=pt_web_header(&q,"Transfer-Encoding"),*cl=pt_web_header(&q,"Content-Length");
    if(te){answer(_fd,411,@"Send the form again (chunked uploads are not supported).");free(head);return;}
    if(cl){char *stop;unsigned long long want=strtoull(cl,&stop,10);if(*stop||want>(64u<<20)){answer(_fd,413,@"The upload is too large for the gateway.");free(head);return;}
        NSMutableData *b=[NSMutableData dataWithBytes:head+end length:MIN(n-end,(size_t)want)];
        while(b.length<want){uint8_t chunk[65536];ssize_t r=read(_fd,chunk,MIN(sizeof chunk,(size_t)(want-b.length)));if(r<0&&errno==EINTR)continue;if(r<=0)break;[b appendBytes:chunk length:(NSUInteger)r];}
        if(b.length<want){free(head);return;}_request.HTTPBody=b;}
    free(head);
    NSURLSession *session=[NSURLSession sessionWithConfiguration:configuration() delegate:self delegateQueue:nil];
    [[session dataTaskWithRequest:_request] resume];dispatch_semaphore_wait(_done,DISPATCH_TIME_FOREVER);[session finishTasksAndInvalidate];
    static int trace=-1;if(trace<0){const char *v=getenv("PT_WEB_TRACE");trace=v&&*v=='1';}
    if(trace||(_error&&_response&&!_short))   /* a failure after the head reaches the session log; PT_WEB_TRACE=1: every request */
        NSLog(@"web: %@ %@ -> %ld %@, %lld bytes streamed%@",_request.HTTPMethod,_request.URL.absoluteString,(long)_response.statusCode,_response.MIMEType?:@"-",_sent,
            _error?[@", error: " stringByAppendingString:_error.localizedDescription]:@"");
    if(!_response){answer(_fd,502,[NSString stringWithFormat:@"%@ could not be reached: %@",host,_error.localizedDescription?:@"no answer"]);return;}
    if(!_buffer)return;
    NSString *type=nil,*mime=_response.MIMEType.lowercaseString;NSData *body=_body;
    if([mime hasPrefix:@"image/"]){NSData *t=transcode(_body,&type);if(t)body=t;else type=nil;}
    else if(!(_body.length>=6&&!memcmp(_body.bytes,"bplist",6)))_body.length=pt_web_downgrade(_body.mutableBytes,_body.length);   /* a binary plist has counted strings */
    if([self sendHead:(long long)body.length type:type])send_all(_fd,body.bytes,body.length);
}
@end
uint16_t pt_web_start(void){
    int fd=socket(AF_INET,SOCK_STREAM,0);if(fd<0)return 0;
    struct sockaddr_in a={.sin_len=sizeof a,.sin_family=AF_INET,.sin_addr.s_addr=htonl(INADDR_LOOPBACK)};socklen_t l=sizeof a;
    if(fcntl(fd,F_SETFD,FD_CLOEXEC)||bind(fd,(struct sockaddr *)&a,sizeof a)||listen(fd,64)||getsockname(fd,(struct sockaddr *)&a,&l)){close(fd);return 0;}
    [NSThread detachNewThreadWithBlock:^{   /* lives as long as the engine */
        for(;;){int c=accept(fd,NULL,NULL);if(c<0){if(errno==EINTR||errno==ECONNABORTED)continue;break;}
            int one=1;struct timeval t={60,0};(void)fcntl(c,F_SETFD,FD_CLOEXEC);
            setsockopt(c,SOL_SOCKET,SO_NOSIGPIPE,&one,sizeof one);setsockopt(c,SOL_SOCKET,SO_RCVTIMEO,&t,sizeof t);setsockopt(c,SOL_SOCKET,SO_SNDTIMEO,&t,sizeof t);
            [NSThread detachNewThreadWithBlock:^{@autoreleasepool{[[[PtWebLink alloc] initWithSocket:c] run];}close(c);}];}   /* a thread each: links block */
    }];
    return ntohs(a.sin_port);
}
