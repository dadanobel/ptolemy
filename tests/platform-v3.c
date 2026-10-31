/* SPDX-License-Identifier: GPL-2.0-or-later
 * Targeted protocol checks; deterministic virtual time and private fixtures. */
#include "devices.h"
#include "nvram.h"
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
static uint8_t ram[512*1024];
static void desc(unsigned a,unsigned n,unsigned cmd,unsigned data) {
    memset(ram+a,0,16);pt_write_le(ram+a,2,n);pt_write_le(ram+a+2,2,cmd);pt_write_le(ram+a+4,4,data);
}
static void start(PtDma *d,unsigned at) { pt_dma_write(d,0,((uint32_t)PT_DMA_RUN<<16));pt_dma_write(d,12,at);pt_dma_write(d,0,((uint32_t)PT_DMA_RUN<<16)|PT_DMA_RUN); }
static void audio_mouse_dma(void) {
    PtHid h;pt_hid_init(&h,PT_HID_MOUSE);h.configuration=1;uint8_t report[8];
    for(unsigned i=0;i<1000;i++)pt_hid_pointer(&h,0,1,-1,0);
    assert(h.count==1 && !h.overflows); int x=0,y=0;
    while(pt_hid_packet(&h,1,PT_USB_IN,report,8,0)!=PT_USB_NAK) { x+=(int8_t)report[1];y+=(int8_t)report[2]; }
    assert(x==1000 && y==-1000);
    pt_hid_pointer(&h,1,2,0,0);pt_hid_pointer(&h,0,3,0,0);
    assert(h.count==2);assert(pt_hid_packet(&h,1,PT_USB_IN,report,8,0)==4 && report[0]==1);
    assert(pt_hid_packet(&h,1,PT_USB_IN,report,8,0)==4 && !report[0]);
    pt_hid_pointer(&h,0,INT32_MAX,0,0);pt_hid_pointer(&h,0,INT32_MAX,0,0);
    assert(h.events[h.head].x==INT32_MAX);pt_hid_release(&h);
    assert(h.count==1 && !h.events[h.head].x);pt_hid_destroy(&h);
    PtDma dma;pt_dma_init(&dma,(PtRam){ram,sizeof ram,NULL},NULL,NULL,NULL);
    desc(0x100,8,0x1000,0x2000);desc(0x110,0,0x7000,0);start(&dma,0x100);PtDmaSlice slice;
    assert(pt_dma_next(&dma,&slice)==1);assert(!pt_dma_advance(&dma,slice.generation,3));
    pt_dma_write(&dma,0,PT_DMA_FLUSH<<16|PT_DMA_FLUSH);
    assert(dma.regs[3]==0x100 && pt_read_le(ram+0x10c,2)==5);
    assert(pt_dma_next(&dma,&slice)==1 && slice.address==0x2003 && slice.length==5);
}
static void aw(PtAta *a,unsigned reg,unsigned v) { uint8_t b=v;assert(!pt_ata_mmio(a,reg,&b,1,1)); }
static void ata_command(PtAta *a,unsigned cmd,unsigned count,unsigned lba) {
    aw(a,0x60,0xe0);aw(a,0x20,count);aw(a,0x30,lba);aw(a,0x40,lba>>8);aw(a,0x50,0);aw(a,0x70,cmd);
}
static void ata_large(void) {
    char path[]="/tmp/ptolemy-v3-ata-XXXXXX";int fd=mkstemp(path);assert(fd>=0);assert(!ftruncate(fd,256*1024));
    uint8_t *data=malloc(128*1024);assert(data);for(unsigned i=0;i<128*1024;i++)data[i]=i*23+(i>>10);
    assert(pwrite(fd,data,128*1024,0)==128*1024);
    PtEvent event;pt_event_init(&event);PtBlock *block=pt_block_open(path,1,&event);assert(block);
    PtDma dma;pt_dma_init(&dma,(PtRam){ram,sizeof ram,NULL},NULL,NULL,NULL);PtAta ata;pt_ata_init(&ata,block,&dma,NULL,NULL);
    for(unsigned i=0;i<4;i++)desc(0x100+i*16,32768,i==3?0x3000:0x2000,0x20000+i*32768);
    desc(0x140,0,0x7000,0);start(&dma,0x100);
    uint64_t g=pt_event_generation(&event);ata_command(&ata,0xc8,0,0);pt_ata_service(&ata);
    /* A fast worker may finish the first slice inside service; accept "in flight or already signalled". */
    assert(ata.queued || pt_event_generation(&event)!=g);
    assert(pt_event_wait(&event,g,5000000000ull)==1);
    /* A mutex trylock can race the final worker loop; only retry that case. */
    for(unsigned i=0;i<100 && ata.queued;i++) { pt_ata_service(&ata);if(ata.queued)usleep(100); }
    assert(!ata.busy && !ata.queued && !memcmp(ram+0x20000,data,128*1024));
    unsigned power[]={0xe0,0xe1,0xe2,0xe3,0xe6,0x94,0x95,0x96,0x97,0x99};
    for(unsigned i=0;i<sizeof power/sizeof power[0];i++) {
        aw(&ata,0x70,power[i]);assert(!(ata.status&1));unsigned expected=ata.power;
        aw(&ata,0x70,0xe5);assert(ata.count==expected && !(ata.status&1));
    }
    ata_command(&ata,0x40,1,511);assert(!(ata.status&1) && ata.lba_next==512 && !ata.sectors);
    ata_command(&ata,0x41,1,512);assert(ata.status&1 && ata.error==0x10);
    pt_block_close(block);pt_event_destroy(&event);free(data);close(fd);assert(!unlink(path));
}
static int via_line,gpio_line;
static void via_irq(void *o,int v) { (void)o;via_line=v; }
static void gpio_irq(void *o,unsigned pin,int v) { (void)o;if(pin==1)gpio_line=v; }
static uint8_t transfer(PtPmu *p,uint8_t value,int output,uint64_t now) {
    pt_pmu_write(p,11*512,output?0x1c:0x0c,now);
    if(output)pt_pmu_write(p,10*512,value,now);
    uint8_t b=pt_pmu_read(p,0,now);assert(b&8);
    pt_pmu_write(p,0,b&~16u,now);assert(!(pt_pmu_read(p,0,now)&8));
    assert(pt_pmu_read(p,13*512,now)&4);uint8_t result=pt_pmu_read(p,10*512,now);
    pt_pmu_write(p,0,pt_pmu_read(p,0,now)|16,now);assert(pt_pmu_read(p,0,now)&8);return result;
}
static unsigned command(PtPmu *p,uint8_t cmd,const uint8_t *in,unsigned len,uint8_t *out,unsigned reply,int variable,uint64_t now) {
    transfer(p,cmd,1,now);
    if(cmd==0x6f || cmd==0x8f)transfer(p,len,1,now);
    for(unsigned i=0;i<len;i++)transfer(p,in[i],1,now);
    if(variable) { unsigned n=transfer(p,0,0,now);assert(n<=reply);reply=n; }
    for(unsigned i=0;i<reply;i++)out[i]=transfer(p,0,0,now);
    return reply;
}
static void pmu_gpio(void) {
    PtGpio gpio;pt_gpio_init(&gpio,gpio_irq,NULL);PtPmu p;pt_pmu_init(&p,&gpio,1000000000,100,via_irq,NULL);
    assert(pt_gpio_read(&gpio,9)&2 && !gpio_line);
    pt_pmu_write(&p,2*512,0x10,1000000000); /* TREQ output, TACK input */
    pt_pmu_write(&p,14*512,0x84,1000000000);uint8_t out[32],in[4];
    command(&p,0x38,NULL,0,out,4,0,1000000000);assert(pt_read_be(out,4)==2082844900u && !via_line);
    pt_write_be(in,4,2082845000u);command(&p,0x30,in,4,out,0,0,1000000000);
    command(&p,0x38,NULL,0,out,4,0,2000000000);assert(pt_read_be(out,4)==2082845001u && gpio_line);
    assert(command(&p,0x78,NULL,0,out,32,1,2000000000)==1 && out[0]&128 && !gpio_line);
    in[0]=1;assert(command(&p,0x6f,in,1,out,32,1,2000000000)==10 && out[0]==5 && out[1]==1);
    p.battery=1;p.charge=4000;p.capacity=5000;p.voltage=12000;p.current=-200;
    command(&p,0x6f,in,1,out,32,1,2000000000);assert(out[1]==5 && pt_read_be(out+2,2)==4000 && pt_read_be(out+6,2)==(uint16_t)-200);
    /* VIA IRQ line is independently masked/acked, not the GPIO event line. */
    pt_pmu_write(&p,14*512,0xc0,2000000000);pt_pmu_write(&p,11*512,0x40,2000000000);
    pt_pmu_write(&p,4*512,8,2000000000);pt_pmu_write(&p,5*512,0,2000000000);
    pt_pmu_service(&p,2000020000);assert(via_line && pt_pmu_read(&p,13*512,2000020000)&64);
    pt_pmu_read(&p,4*512,2000020000);assert(!via_line);
    command(&p,0x7e,(const uint8_t *)"MATT",4,out,1,0,2000020000);assert(pt_pmu_take_power(&p)==PT_POWER_OFF);
    assert(pt_pmu_take_power(&p)==PT_POWER_NONE);
}
static void nvram(void) {
    PtNvram n;pt_nvram_init(&n);assert(!memcmp(n.data+8196,"common",6));
    assert(!memcmp(n.data+8192+0x804,"APL,MacOS75",11));
    uint8_t *free=n.data+8192+0x900;assert(pt_read_be(free+20,4)==2);
    unsigned a=0,b=0;for(unsigned i=20;i<8192-0x900;i++){a=(a+free[i])%65521;b=(b+a)%65521;}
    assert(pt_read_be(free+16,4)==((b<<16)|a));
    char directory[]="/tmp/ptolemy-v3-nvram-XXXXXX";assert(mkdtemp(directory));char path[256];snprintf(path,sizeof path,"%s/nvram.bin",directory);
    assert(!pt_nvram_open(&n,path));PtNvram other;pt_nvram_init(&other);assert(pt_nvram_open(&other,path)!=0);
    uint8_t value=0x81;assert(!pt_nvram_access(&n,0x3000,&value,1,1));
    int e=pt_nvram_service(&n);assert(!e || e==EAGAIN);
    value=0x42;assert(!pt_nvram_access(&n,0x3001,&value,1,1));assert(!pt_nvram_close(&n));
    pt_nvram_init(&other);assert(!pt_nvram_open(&other,path));assert(other.data[0x3000]==0x81 && other.data[0x3001]==0x42);
    assert(!pt_nvram_close(&other));int fd=open(path,O_WRONLY);assert(fd>=0 && !ftruncate(fd,1));close(fd);
    assert(pt_nvram_open(&n,path)==EINVAL);struct stat st;assert(!stat(path,&st) && st.st_size==1);
    char lock[sizeof path+8];snprintf(lock,sizeof lock,"%s.lock",path);assert(!unlink(path) && !unlink(lock) && !rmdir(directory));
}
int main(void) {
    audio_mouse_dma();ata_large();pmu_gpio();nvram();
    puts("PASS: motion coalescing, partial FLUSH, large ATA, power/VERIFY, PMU wire/RTC/GPIO/VIA, persistent NVRAM");
}
