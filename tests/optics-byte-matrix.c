/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Explicit byte permutations, not assumptions about SURFACE register mode 3. */
#include "io.h"
#include "pixel.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
static int protect(void *o,int ram,uint64_t a,PtR2IoGuard g){(void)o;(void)ram;(void)a;(void)g;return 1;}
static void written(void *o,int ram,uint64_t a,size_t n){(void)o;(void)ram;(void)a;(void)n;}
int main(void){
    uint8_t memory[8192],expected[8192];PtR2IoConfig c={.address={.fb_length=sizeof memory,.vram=memory,.vram_bytes=sizeof memory,.generation=1},
        .pages=16,.transfers=16,.snapshot_bytes=8192,.protect=protect,.written=written};PtR2Io *io=pt_r2_io_create(&c);assert(io);
    unsigned cases=0;
    for(unsigned bpp=2;bpp<=4;bpp+=2)for(unsigned lane=0;lane<4;lane++)for(unsigned at=4089;at<4102;at++)for(unsigned n=1;n<=bpp;n++) {
        for(unsigned j=0;j<sizeof memory;j++)memory[j]=(uint8_t)(j*23+(j>>8));
        uint8_t out[4]={0},data[4]={0x12,0x34,0x56,0x78};
        assert(pt_r2_io_cpu_read(io,at,out,n,lane)==PT_R2_IO_OK);
        for(unsigned j=0;j<n;j++)assert(out[j]==memory[(at+j)^lane]);
        memcpy(expected,memory,sizeof memory);for(unsigned j=0;j<n;j++)expected[(at+j)^lane]=data[j];
        assert(pt_r2_io_cpu_write(io,at,data,n,lane)==PT_R2_IO_OK);assert(!memcmp(memory,expected,sizeof memory));cases++;
    }
    assert(pt_r2_io_destroy(io)==PT_R2_IO_OK);
    for(unsigned format=1;format<=2;format++)for(uint32_t v=0;v<(format==1?65536u:16777216u);v++) {
        uint32_t w=format==1?v:(v<<8)|(v*73&255);uint8_t p[4],out[4];float z;uint8_t stencil;
        for(unsigned j=0;j<4;j++)p[j]=(uint8_t)(w>>(j*8));
        assert(optics_depth_unpack(format,p,&z,&stencil)&&optics_depth_pack(format,z,stencil,out));
        assert(!memcmp(p,out,optics_depth_bytes(format)));
    }
    uint8_t guard[4]={1,2,3,4};assert(!optics_depth_pack(2,NAN,0,guard)&&guard[0]==1);
    printf("PASS: %u endian/width/boundary byte cases; exhaustive D16 and D24S8 integer round trips\n",cases);return 0;
}
