/* SPDX-License-Identifier: GPL-2.0-or-later
 * Included by the existing Tiger development worker. No fork, shell, new agent,
 * token exposure to children, root requirement or system-library replacement. */
#ifndef OPTICS_AGENT_BUILTIN_H
#define OPTICS_AGENT_BUILTIN_H
#include "diagnostics.h"
#include <stdio.h>
#include <string.h>
#include <stdint.h>
static int optics_agent_builtin(uint32_t token,uint32_t job,char **argv,unsigned *status) {
    if(strcmp(argv[0],OPTICS_AGENT_COMMAND))return 0;
    char output[4096];size_t n=0;uint32_t reply[3]={0};int rc;
    if(argv[1]&&(strcmp(argv[1],"status")||argv[2])) {
        strcpy(output,"{\"component\":\"optics\",\"error\":\"usage: status\"}\n");*status=2;n=strlen(output);
    } else {
        if(ptagent_call(PTA_CHECK,token,job,0,0,reply)||reply[0]){*status=125;return 1;}
        uint8_t space[OPTICS_DIAG_BYTES+8192],*wire=(uint8_t *)(((uintptr_t)space+4095)&~(uintptr_t)4095)+4093;OpticsStatus value;
        ((volatile uint8_t *)wire)[0]=((volatile uint8_t *)wire)[OPTICS_DIAG_BYTES-1]=0; /* Touch both actual pages. */
        rc=ptagent_call(OPTICS_AGENT_OPCODE,token,(uint32_t)(uintptr_t)wire,OPTICS_DIAG_BYTES,OPTICS_DIAG_ABI,reply);
        if(rc==PTA_FAULT){((volatile uint8_t *)wire)[0]=((volatile uint8_t *)wire)[OPTICS_DIAG_BYTES-1]=0;rc=ptagent_call(OPTICS_AGENT_OPCODE,token,(uint32_t)(uintptr_t)wire,OPTICS_DIAG_BYTES,OPTICS_DIAG_ABI,reply);}
        if(!rc&&optics_status_decode(wire,OPTICS_DIAG_BYTES,&value)) {n=optics_status_json(&value,output,sizeof output);*status=n?0:2;}
        else {*status=2;n=(size_t)snprintf(output,sizeof output,"{\"component\":\"optics\",\"error\":\"host diagnostics unavailable or ABI mismatch\",\"status\":%d}\n",rc);}
    }
    if(n) {
        fwrite(output,1,n,stdout);
        if(ptagent_call(PTA_OUTPUT,token,job,(uint32_t)(uintptr_t)output,(uint32_t)n,reply))*status=2;
    }
    return 1;
}
#endif
