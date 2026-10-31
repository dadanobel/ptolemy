/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef PT_AGENT_LOG_ARGV_H
#define PT_AGENT_LOG_ARGV_H
#include "sha256.h"
#include <stdio.h>
/* The entire escaped preview, across all argv, is <=1024 bytes. Metadata
 * has a separate bound (64 arguments). One fwrite replaces per-byte IO. */
static void pt_log_argv(uint32_t id,char *const *argv){
    char line[12288];size_t at=(size_t)snprintf(line,sizeof(line),"\n[Job %u]",id),budget=1024;unsigned arg;
    static const char hex[]="0123456789abcdef";
    for(arg=0;argv[arg]&&arg<64;arg++){
        const unsigned char *s=(const unsigned char *)argv[arg];size_t n=strlen(argv[arg]),shown=0;
        line[at++]=' ';line[at++]='"';
        while(shown<n){unsigned char c=s[shown];size_t k=(c<32||c>=127||c=='"'||c=='\\')?4:1;if(k>budget)break;
            if(k==4){line[at++]='\\';line[at++]='x';line[at++]=hex[c>>4];line[at++]=hex[c&15];}else line[at++]=(char)c;
            budget-=k;shown++;}
        line[at++]='"';
        if(shown<n){PtSha256 hash;uint8_t d[32];char digest[65];pt_sha256_init(&hash);pt_sha256_update(&hash,s,n);pt_sha256_final(&hash,d);pt_sha256_hex(d,digest);
            at+=(size_t)snprintf(line+at,sizeof(line)-at," [bytes=%lu sha256=%s preview-truncated]",(unsigned long)n,digest);
        }else at+=(size_t)snprintf(line+at,sizeof(line)-at," [bytes=%lu]",(unsigned long)n);
    }
    line[at++]='\n';(void)fwrite(line,1,at,stdout);
}
#endif
