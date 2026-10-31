/* SPDX-License-Identifier: GPL-2.0-or-later
 * /Ptolemy/machine: the guest's own view of the machine (sysctl), without fork or shell. A value the kernel does not
 * provide is reported with its errno, never as a numeric zero: an absent L3 is an error, not 0 bytes. */
#ifndef PT_MACHINE_BUILTIN_H
#define PT_MACHINE_BUILTIN_H
#include <sys/types.h>
#include <sys/sysctl.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
static int pt_machine_builtin(uint32_t token,uint32_t job,char **argv,unsigned *status) {
    static const char *const names[]={"hw.model","hw.machine","hw.cputype","hw.cpusubtype","hw.ncpu","hw.memsize",
        "hw.physmem","hw.cpufrequency","hw.busfrequency","hw.tbfrequency","hw.cachelinesize","hw.l1icachesize",
        "hw.l1dcachesize","hw.l2cachesize","hw.l3cachesize","hw.l2settings","hw.l3settings","hw.optional.altivec"};
    if(strcmp(argv[0],"/Ptolemy/machine"))return 0;
    char out[4096];size_t n=0;uint32_t reply[3]={0};
    if(argv[1]||ptagent_call(PTA_CHECK,token,job,0,0,reply)||reply[0]){*status=argv[1]?2:125;return 1;}
    n=(size_t)snprintf(out,sizeof out,"{\"component\":\"machine\"");
    for(unsigned i=0;i<sizeof names/sizeof *names&&n<sizeof out-320;i++) {
        unsigned char v[256];size_t len=sizeof v-1;
        if(sysctlbyname(names[i],v,&len,NULL,0))
            n+=(size_t)snprintf(out+n,sizeof out-n,",\"%s\":{\"errno\":%d}",names[i],errno);
        else if(len==4){uint32_t x;memcpy(&x,v,4);n+=(size_t)snprintf(out+n,sizeof out-n,",\"%s\":%lu",names[i],(unsigned long)x);}
        else if(len==8){uint64_t x;memcpy(&x,v,8);n+=(size_t)snprintf(out+n,sizeof out-n,",\"%s\":%llu",names[i],(unsigned long long)x);}
        else {
            if(len&&!v[len-1])len--;
            v[len]=0;for(size_t k=0;k<len;k++)if(v[k]=='"'||v[k]=='\\'||v[k]<32)v[k]='?';
            n+=(size_t)snprintf(out+n,sizeof out-n,",\"%s\":\"%s\"",names[i],(const char *)v);
        }
    }
    n+=(size_t)snprintf(out+n,sizeof out-n,"}\n");
    fwrite(out,1,n,stdout);*status=0;
    if(ptagent_call(PTA_OUTPUT,token,job,(uint32_t)(uintptr_t)out,(uint32_t)n,reply))*status=2;
    return 1;
}
#endif
