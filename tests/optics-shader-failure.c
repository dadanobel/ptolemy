/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "shader_failure.h"
#include <assert.h>
#include <dirent.h>
#include <string.h>
int main(void){char path[]="/private/tmp/pt-pvs-failure.XXXXXX";assert(mkdtemp(path));assert(!setenv("OPTICS_SHADER_FAILURES",path,1));
    uint32_t words[]={0,0,63u|2u<<8|15u<<20,0,0,0};PtR2ShaderIR ir;PtR2ShaderError e={0};assert(pt_r2_shader_decode(PT_R2_SHADER_PVS,PT_R2_SHADER_FP32_SEPARATE,words,6,0,&ir,&e)==PT_R2_SHADER_UNSUPPORTED);
    optics_shader_failure_dump(2,words,6,0,&e);optics_shader_failure_dump(2,words,6,0,&e);DIR *d=opendir(path);assert(d);struct dirent *item;unsigned count=0;char name[512];
    while((item=readdir(d)))if(item->d_name[0]!='.'){count++;snprintf(name,sizeof name,"%s/%s",path,item->d_name);FILE *f=fopen(name,"r");assert(f);char data[8192];size_t n=fread(data,1,sizeof data-1,f);data[n]=0;fclose(f);assert(strstr(data,"PVS opcode not implemented")&&strstr(data,"\"word\":2")&&strstr(data,"\"words\":[0,0,"));unlink(name);}closedir(d);assert(count==1);
    assert(!chmod(path,0777));optics_shader_failure_dump(2,words,6,0,&e);d=opendir(path);assert(d);count=0;while((item=readdir(d)))if(item->d_name[0]!='.')count++;closedir(d);assert(count==0);
    assert(!chmod(path,0700));char linkpath[512];snprintf(linkpath,sizeof linkpath,"%s-link",path);assert(!symlink(path,linkpath));assert(!setenv("OPTICS_SHADER_FAILURES",linkpath,1));optics_shader_failure_dump(2,words,6,0,&e);unlink(linkpath);
    d=opendir(path);assert(d);count=0;while((item=readdir(d)))if(item->d_name[0]!='.')count++;closedir(d);assert(count==0);rmdir(path);
    words[2]=3u|2u<<8|15u<<20;assert(pt_r2_shader_decode(PT_R2_SHADER_PVS,PT_R2_SHADER_FP32_SEPARATE,words,6,0,&ir,&e)==PT_R2_SHADER_OK);
    puts("PASS: precise PVS failure and raw words, deduplication, safe output, supported-opcode negative control");
}
