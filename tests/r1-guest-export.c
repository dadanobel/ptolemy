/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "pt_agent.h"
#include "pt_sha256.h"
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <assert.h>
static volatile sig_atomic_t stopping;static unsigned phase,called,commits,faults;
static uint64_t ms(void){return 1;}
static void input_tick(uint32_t t,uint64_t n){(void)t;(void)n;}
static int ptagent_call(uint32_t op,uint32_t token,uint32_t job,uint32_t b,uint32_t n,uint32_t reply[3]){
    (void)b;(void)n;assert(token==7&&job==9);called++;
    if(op==PTA_CHECK){reply[0]=stopping;return PTA_OK;}
    if(op==PTA_EXPORT_BEGIN){if(phase==1)stopping=1;return PTA_OK;}
    if(op==PTA_EXPORT_CHUNK){if(phase==2){stopping=1;return PTA_AGAIN;}if(phase==5&&!faults){faults=1;return PTA_FAULT;}return PTA_OK;}   /* 5: one page fault, then retried */
    assert(op==PTA_EXPORT_COMMIT);commits++;if(phase==3){stopping=1;return PTA_AGAIN;}return PTA_OK;
}
#include "../guest/agent/export_builtin.h"
int main(void){char path[]="/private/tmp/pt-r1-worker.XXXXXX";int fd=mkstemp(path);assert(fd>=0&&write(fd,"hello",5)==5);close(fd);char *argv[]={"/Ptolemy/files",path,NULL};unsigned status;
    for(phase=0;phase<=5;phase++){stopping=phase==4;called=commits=0;assert(pt_export_builtin(7,9,argv,&status));if(!phase||phase==5)assert(!status&&commits==1);else assert(status);if(phase==1||phase==2||phase==4)assert(!commits);if(phase==4)assert(!called);if(phase==5)assert(faults==1);}
    unlink(path);puts("PASS: guest export success and Stop before BEGIN, after BEGIN, on backpressure and during COMMIT; one CHUNK fault retried");
}
