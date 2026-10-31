/* SPDX-License-Identifier: GPL-2.0-or-later */
/* The portable half of the web gateway (web_macos.m): request-head parsing of untrusted guest bytes and
 * the https:// -> http:// body edit. */
#include "web.h"
#include <errno.h>
#include <string.h>
#include <strings.h>
static char *line(char **at,char *end){   /* next line, NUL-terminated without its CR LF; NULL at the end */
    char *s=*at,*nl=s<end?memchr(s,'\n',(size_t)(end-s)):NULL;if(!nl)return NULL;
    *at=nl+1;if(nl>s&&nl[-1]=='\r')nl--;*nl=0;return s;
}
int pt_web_parse(char *head,size_t n,PtWebRequest *r){
    memset(r,0,sizeof *r);if(!head||!n||n>PT_WEB_HEAD||memchr(head,0,n))return EINVAL;
    char *at=head,*end=head+n,*l=line(&at,end);if(!l)return EINVAL;
    r->method=l;char *sp=strchr(l,' ');if(!sp)return EINVAL;*sp=0;r->target=sp+1;
    sp=strchr(r->target,' ');if(!sp)return EINVAL;*sp=0;r->version=sp+1;
    if(!*r->method||!*r->target||strncmp(r->version,"HTTP/1.",7))return EINVAL;
    for(const char *m=r->method;*m;m++)if(*m<'A'||*m>'Z')return EINVAL;   /* GET, POST, HEAD, CONNECT... */
    for(char *h;(h=line(&at,end))&&*h;){
        char *colon=strchr(h,':');if(!colon||colon==h)return EINVAL;if(r->headers==PT_WEB_HEADERS)return E2BIG;
        *colon=0;char *v=colon+1;while(*v==' '||*v=='\t')v++;
        r->name[r->headers]=h;r->value[r->headers++]=v;
    }
    return 0;
}
const char *pt_web_header(const PtWebRequest *r,const char *name){
    for(unsigned i=0;i<r->headers;i++)if(!strcasecmp(r->name[i],name))return r->value[i];return NULL;
}
size_t pt_web_downgrade(uint8_t *b,size_t n){
    size_t o=0;
    for(size_t i=0;i<n;){
        if(n-i>=8&&!memcmp(b+i,"https://",8)){memcpy(b+o,"http://",7);o+=7;i+=8;}
        else if(n-i>=10&&!memcmp(b+i,"https:\\/\\/",10)){memcpy(b+o,"http:\\/\\/",9);o+=9;i+=10;}
        else b[o++]=b[i++];
    }
    return o;
}
