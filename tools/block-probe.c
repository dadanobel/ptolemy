/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "block.h"
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
int main(int argc,char **argv) {
    if(argc!=2)return 2;PtBlock *b=pt_block_open(argv[1],0,NULL);
    if(!b){perror("read-only backend");return 1;}uint8_t data[4096]={0};
    PtBlockRequest request={.operation=PT_BLOCK_READ,.size=sizeof data,.buffer=data};
    int e=pt_block_submit(b,&request),done=0;
    for(unsigned i=0;!e && i<50000;i++) {
        int result=pt_block_take(b,&request);
        if(result==1){e=request.error;done=1;break;}
        if(result<0 && result!=-EAGAIN){e=-result;break;}usleep(100);
    }
    printf("capacity=%llu; first_sector=%02x%02x; readonly_write_rejected=%s\n",(unsigned long long)pt_block_size(b),data[0],data[1],
        pt_block_submit(b,&(PtBlockRequest){.operation=PT_BLOCK_WRITE,.size=512,.buffer=data})==EROFS?"yes":"NO");
    pt_block_close(b);if(e)fprintf(stderr,"%s\n",strerror(e));return e || !done;
}
