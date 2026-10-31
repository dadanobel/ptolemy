/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "../epicycle/jit.c"
#include <assert.h>
#include <pthread.h>
int sys_call(CPU *c) { (void)c; abort(); }
int main(void) {
    uint32_t *code=mmap(NULL,65536,PROT_READ|PROT_WRITE|PROT_EXEC,MAP_PRIVATE|MAP_ANONYMOUS|MAP_JIT,-1,0);
    uint8_t *mem=calloc(1,4096);assert(code!=MAP_FAILED&&mem);
    unsigned count=0;
    for(unsigned sh=0;sh<32;sh++)for(unsigned mb=0;mb<32;mb++)for(unsigned kind=0;kind<4;kind++) {
        unsigned me=(mb+kind*9)&31, out=kind==3?4:3;
        uint32_t first=31u<<26|3u<<21|19u<<1;
        uint32_t second=21u<<26|3u<<21|out<<16|sh<<11|mb<<6|me<<1|(kind&1);
        CPU c={0};c.pc=0x100;c.npreg=1;c.preg[0]=(PRegion){0,4096,mem,0};
        for(unsigned f=0;f<8;f++)cr_set_field(&c,f,(sh+f*7+mb*3)&15);
        CPU expected=c;wbe32(mem+0x100,first);wbe32(mem+0x104,second);
        assert(!interp_step(&expected)&&!interp_step(&expected));
        gm=mem;pg_end=4096;check=0;cr_fuse=1;cur_n=0;skip_next=kill_cr0=0;cwr=0;memset(cmap,-1,sizeof cmap);
        pthread_jit_write_protect_np(0);
        cp=code;E(0xa9bf7bfb);E(0xa9bf77fc);E(0xaa0003fb);ldrx(H_CR0,ENV,OCR(0));
        assert(tr(first,0x100)==0);assert(skip_next==(out==3));
        if(!skip_next)assert(tr(second,0x104)==0);
        strw(3,ENV,OGPR(3));if(out==4)strw(4,ENV,OGPR(4));strx(H_CR0,ENV,OCR(0));
        E(0xa8c177fc);E(0xa8c17bfb);E(0xd65f03c0);
        pthread_jit_write_protect_np(1);__builtin___clear_cache((char *)code,(char *)cp);
        ((void (*)(CPU *))(void *)code)(&c);
        assert(c.gpr[3]==expected.gpr[3]&&c.gpr[out]==expected.gpr[out]&&!memcmp(c.cr,expected.cr,sizeof c.cr));count++;
    }
    printf("mfcr/rlwinm: %u masks, rotations, Rc and alias checks\n",count);return 0;
}
