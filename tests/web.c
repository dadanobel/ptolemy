/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "web.h"
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static int parse(const char *text,PtWebRequest *r){static char b[PT_WEB_HEAD];size_t n=strlen(text);memcpy(b,text,n);return pt_web_parse(b,n,r);}
int main(void){
    PtWebRequest r;
    assert(!parse("GET /a?b=1 HTTP/1.1\r\nHost: example.com\r\nuser-agent:  Safari/412\r\n\r\n",&r));
    assert(!strcmp(r.method,"GET")&&!strcmp(r.target,"/a?b=1")&&!strcmp(r.version,"HTTP/1.1")&&r.headers==2);
    assert(!strcmp(pt_web_header(&r,"HOST"),"example.com")&&!strcmp(pt_web_header(&r,"User-Agent"),"Safari/412")&&!pt_web_header(&r,"Cookie"));
    assert(!parse("GET http://example.com/ HTTP/1.0\n\n",&r)&&!strcmp(r.target,"http://example.com/")&&!r.headers);
    assert(parse("GET / HTTP/2\r\n\r\n",&r)==EINVAL&&parse("GET /\r\n\r\n",&r)==EINVAL&&parse(" / HTTP/1.1\r\n\r\n",&r)==EINVAL&&parse("get / HTTP/1.1\r\n\r\n",&r)==EINVAL);
    assert(parse("GET / HTTP/1.1\r\nno colon\r\n\r\n",&r)==EINVAL&&parse("GET / HTTP/1.1\r\n: v\r\n\r\n",&r)==EINVAL);
    char many[PT_WEB_HEAD]="GET / HTTP/1.1\r\n";for(unsigned i=0;i<=PT_WEB_HEADERS;i++)strcat(many,"X: y\r\n");strcat(many,"\r\n");
    assert(parse(many,&r)==E2BIG);
    char nul[]="GET / HTTP/1.1\r\nA: b\0c\r\n\r\n";assert(pt_web_parse(nul,sizeof nul-1,&r)==EINVAL);
    uint8_t page[]="<a href=\"https://a.org/x\">https://https://</a>{\"u\":\"https:\\/\\/b.org\"} https:/";
    size_t n=pt_web_downgrade(page,sizeof page-1);
    const char *want="<a href=\"http://a.org/x\">http://http://</a>{\"u\":\"http:\\/\\/b.org\"} https:/";
    assert(n==strlen(want)&&!memcmp(page,want,n));
    uint32_t seed=7;   /* random heads: never crash, never read past n (ASan) */
    for(unsigned i=0;i<20000;i++){size_t len=1+i%300;char *b=malloc(len);for(size_t j=0;j<len;j++){seed=seed*1664525+1013904223;b[j]=" :\r\nGET/HTTP1.ab"[seed>>28];}
        (void)pt_web_parse(b,len,&r);(void)pt_web_downgrade((uint8_t *)b,len);free(b);}
    puts("PASS: web gateway request heads (origin/absolute form, limits, NUL), https:// downgrade, fuzzed heads");
    return 0;
}
