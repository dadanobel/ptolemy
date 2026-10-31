/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef OPTICS_SHADER_FAILURE_H
#define OPTICS_SHADER_FAILURE_H
#include "foundation/shader_ir.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
static void optics_shader_capture_error(void){static int warned;if(!warned){warned=1;fputs("optics: shader failure capture unavailable; check the private output directory and permissions\n",stderr);}}
/* Explicit diagnostics only: one bounded file per exact program key, never
 * per draw. No constants, textures or framebuffer contents are recorded. */
static void optics_shader_failure_dump(unsigned stage,const uint32_t *words,size_t count,uint32_t flow,const PtR2ShaderError *e){
    static unsigned attempts;const char *path=getenv("OPTICS_SHADER_FAILURES");if(!path||!path[0]||!words||!e||!count||count>PT_R2_SHADER_WORDS||attempts>=64)return;
    attempts++;int dir=open(path,O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);struct stat st;
    if(dir<0){optics_shader_capture_error();return;}if(fstat(dir,&st)||st.st_uid!=getuid()||(st.st_mode&022)){optics_shader_capture_error();close(dir);return;}
    uint64_t hash=14695981039346656037ull;for(size_t i=0;i<count+2;i++){uint32_t w=i==count?flow:i==count+1?stage:words[i];for(unsigned b=0;b<4;b++){hash^=(uint8_t)(w>>(8*b));hash*=1099511628211ull;}}
    char name[64];snprintf(name,sizeof name,"%s-%016llx.shader.json",stage==PT_R2_SHADER_PVS?"pvs":"us",(unsigned long long)hash);
    int fd=openat(dir,name,O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW|O_CLOEXEC,0600);if(fd<0){if(errno!=EEXIST)optics_shader_capture_error();close(dir);return;}FILE *f=fdopen(fd,"w");if(!f){close(fd);unlinkat(dir,name,0);close(dir);return;}
    fprintf(f,"{\"schema\":1,\"stage\":%u,\"profile\":%u,\"flow\":%u,\"status\":%u,\"word\":%u,\"reason\":\"",stage,PT_R2_SHADER_FP32_SEPARATE,flow,e->status,e->word);
    for(const unsigned char *p=(const unsigned char *)e->reason;*p;p++){if(*p=='\"'||*p=='\\')fputc('\\',f);if(*p>=32&&*p<127)fputc(*p,f);else fprintf(f,"\\u%04x",*p);}
    fputs("\",\"words\":[",f);for(size_t i=0;i<count;i++)fprintf(f,"%s%u",i?",":"",words[i]);fputs("]}\n",f);int bad=ferror(f);if(fclose(f))bad=1;if(bad){optics_shader_capture_error();unlinkat(dir,name,0);}close(dir);
}
#endif
