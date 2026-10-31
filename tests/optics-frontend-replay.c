/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Drive the Radeon frontend with a recorded core-API trace (wire format v1; the parser below is its specification)
 * and compare what the guest could observe: MMIO/VRAM/port read results, device write-backs into guest RAM and
 * scanout frame hashes. Guest RAM is rebuilt from the DMA reads recorded during each command. The trace is a
 * baseline, not hardware truth: timing-dependent reads (CRTC_VLINE) are reported separately, not as failures.
 * OPTICS_REPLAY_CP_THREAD=1: the product's CP thread (config.cp_thread); the replay waits for it before every command and
 * frame as it waits for completions, so the results must equal the synchronous run. */
#include "frontend.h"
int optics_draw_reference(void *,PtRadeon *,const OpticsDraw *,const char **);
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

/* Compact SHA-256 (FIPS 180-4), for comparing FRAME records. */
typedef struct { uint32_t h[8]; uint64_t n; uint8_t b[64]; unsigned k; } Sha;
static const uint32_t K[64]={0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,0xd807aa98,
0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,
0x4a7484aa,0x5cb0a9dc,0x76f988da,0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,0x27b70a85,
0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,
0xd6990624,0xf40e3585,0x106aa070,0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,0x748f82ee,
0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2};
#define ROR(x,n) ((x)>>(n)|(x)<<(32-(n)))
static void sha_block(Sha *s,const uint8_t *p) {
    uint32_t w[64],a=s->h[0],b=s->h[1],c=s->h[2],d=s->h[3],e=s->h[4],f=s->h[5],g=s->h[6],h=s->h[7];
    for(int i=0;i<16;i++) w[i]=(uint32_t)p[4*i]<<24|p[4*i+1]<<16|p[4*i+2]<<8|p[4*i+3];
    for(int i=16;i<64;i++) w[i]=w[i-16]+(ROR(w[i-15],7)^ROR(w[i-15],18)^w[i-15]>>3)+w[i-7]+(ROR(w[i-2],17)^ROR(w[i-2],19)^w[i-2]>>10);
    for(int i=0;i<64;i++) {
        uint32_t t1=h+(ROR(e,6)^ROR(e,11)^ROR(e,25))+((e&f)^(~e&g))+K[i]+w[i],t2=(ROR(a,2)^ROR(a,13)^ROR(a,22))+((a&b)^(a&c)^(b&c));
        h=g;g=f;f=e;e=d+t1;d=c;c=b;b=a;a=t1+t2;
    }
    s->h[0]+=a;s->h[1]+=b;s->h[2]+=c;s->h[3]+=d;s->h[4]+=e;s->h[5]+=f;s->h[6]+=g;s->h[7]+=h;
}
static void sha(const uint8_t *p,size_t n,uint8_t out[32]) {
    Sha s={{0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19},0,{0},0};
    size_t i=0; for(;i+64<=n;i+=64) sha_block(&s,p+i);
    uint8_t t[128]={0}; size_t r=n-i; memcpy(t,p+i,r); t[r]=0x80; size_t L=r<56?64:128; uint64_t bits=(uint64_t)n*8;
    for(int k=0;k<8;k++) t[L-1-k]=(uint8_t)(bits>>8*k);
    sha_block(&s,t); if(L==128) sha_block(&s,t+64);
    for(int k=0;k<8;k++){out[4*k]=(uint8_t)(s.h[k]>>24);out[4*k+1]=(uint8_t)(s.h[k]>>16);out[4*k+2]=(uint8_t)(s.h[k]>>8);out[4*k+3]=(uint8_t)s.h[k];}
}

enum {CONFIG=2,ACCESS,RESULT,TICK,RESET,DMAREAD,RAMREF,WB_INTENT,WB_APPLY,IRQ,SUBMIT,GPU_DONE,DONE_QUERY,RESET_NOTICE,FRAME,END,CLOCK};
typedef struct { const uint8_t *p; uint32_t len; uint16_t type; uint64_t call,parent,now; const uint8_t *pl; uint32_t pn; } Rec;
static uint32_t u32(const uint8_t *p){uint32_t v;memcpy(&v,p,4);return v;}
static uint64_t u64(const uint8_t *p){uint64_t v;memcpy(&v,p,8);return v;}
static int is_command(uint16_t t){return t==CONFIG||t==ACCESS||t==TICK||t==RESET;}

/* The frontend's write-backs in order, per physical address, compared with the recorded sequence (timing-independent). */
typedef struct { uint64_t pa; uint32_t v; } Wb;
static Wb *ours; static size_t nours,capours;
static void on_wb(void *o,uint64_t pa,uint32_t v) { (void)o; if(nours==capours){capours=capours?2*capours:4096;ours=realloc(ours,capours*sizeof *ours);} ours[nours++]=(Wb){pa,v}; }
static int cmpwb(const void *a,const void *b) { const Wb *x=a,*y=b; return x->pa<y->pa?-1:x->pa>y->pa?1:0; }
int main(int argc,char **argv) {
    optics_frontend_set_writeback_hook(on_wb,NULL);
    static const OpticsExecutor reference={.draw=optics_draw_reference};   /* stateless CPU reference: no GPU ownership */
    if(getenv("OPTICS_REFERENCE")) optics_frontend_set_executor(&reference); Wb *refs=NULL; size_t nrefs=0,caprefs=0;
    if(argc<2){fprintf(stderr,"usage: %s TRACE.bin [--verbose]\n",argv[0]);return 2;}
    int verbose=argc>2;
    int fd=open(argv[1],O_RDONLY); struct stat st; if(fd<0||fstat(fd,&st)){perror(argv[1]);return 2;}
    const uint8_t *m=mmap(NULL,(size_t)st.st_size,PROT_READ,MAP_PRIVATE,fd,0); if(m==MAP_FAILED){perror("mmap");return 2;}
    if(memcmp(m,"PTGT",4)||u32(m+4)!=1){fprintf(stderr,"not a v1 trace\n");return 2;}
    uint32_t width=u32(m+24),height=u32(m+28); uint64_t ram_size=u64(m+32),vram_size=u64(m+40);
    uint8_t *ram=mmap(NULL,ram_size,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0); if(ram==MAP_FAILED){perror("ram");return 2;}
    /* Index records. */
    size_t cap=1<<20,n=0; Rec *r=malloc(cap*sizeof *r); size_t o=48;
    while(o+44<=(size_t)st.st_size) {
        uint32_t len=u32(m+o); if(len<44||o+len>(size_t)st.st_size) break;
        if(n==cap){cap*=2;r=realloc(r,cap*sizeof *r);}
        r[n]=(Rec){m+o,len,(uint16_t)(m[o+4]|m[o+5]<<8),u64(m+o+16),u64(m+o+24),u64(m+o+32),m+o+40,len-44};
        n++; o+=len;
    }
    PtRadeonConfig cfg={.ram={ram,ram_size,NULL},.vram_mb=(unsigned)(vram_size>>20),   /* no host boot mode: the recorded run had none */
        .cp_thread=getenv("OPTICS_REPLAY_CP_THREAD") && atoi(getenv("OPTICS_REPLAY_CP_THREAD"))};
    PtRadeon *d; if(pt_radeon_create(&d,&cfg)){fprintf(stderr,"create failed\n");return 1;}
    uint64_t commands=0,reads=0,read_diff=0,vline=0,wb=0,wb_diff=0,frames=0,frame_same=0,frame_diff=0,frame_none=0;
    uint8_t *shot=malloc((size_t)width*height*4+16); uint64_t first_frame_diff=0; unsigned shown=0;
    uint64_t pending_call=0; uint8_t pending_bytes[8]; unsigned pending_n=0; uint32_t pending_off=0; int pending_space=0; uint64_t last_now=0;
    for(size_t i=0;i<n;i++) {
        Rec *x=&r[i];
        if(is_command(x->type) || (x->type==FRAME && cfg.cp_thread)) {
            /* In the recorded run each fence finished before the guest's next access: let completions catch up (the
             * GPU executor's scratch/RBBM fences wait for real Metal completion; the CPU reference never waits). */
            for(unsigned k=0;k<200000 && !optics_frontend_idle(d);k++){ pt_radeon_service(d,last_now); usleep(50); }
        }
        if(is_command(x->type)) {
            if(x->type==TICK) last_now=x->now;
            /* Apply this command's DMA reads first: they are the RAM contents recorded during it. */
            for(size_t j=i+1;j<n && !is_command(r[j].type);j++) if(r[j].type==DMAREAD && r[j].parent==x->call) {
                uint64_t a=u64(r[j].pl); uint32_t q=u32(r[j].pl+8); int32_t e=(int32_t)u32(r[j].pl+12);
                if(!e && a<=ram_size && q<=ram_size-a) memcpy(ram+a,r[j].pl+16,q);
            }
            commands++;
            if(x->type==CONFIG){PtAgp g={u32(x->pl+256),u32(x->pl+260),u32(x->pl+264),u64(x->pl+268)};pt_radeon_config(d,x->pl,&g);}
            else if(x->type==TICK) pt_radeon_service(d,x->now);
            else if(x->type==RESET) pt_radeon_reset(d);
            else {
                unsigned space=x->pl[0],wr=x->pl[1],len=x->pl[2]|x->pl[3]<<8; uint32_t off=u32(x->pl+4);
                uint8_t b[256]={0}; if(wr) memcpy(b,x->pl+8,len);
                int e=space==0?pt_radeon_mmio(d,off,b,len,wr):space==1?pt_radeon_vram(d,off,b,len,wr):pt_radeon_port(d,off,b,len,wr);
                (void)e; if(!wr){pending_call=x->call;pending_n=len<8?len:8;memcpy(pending_bytes,b,pending_n);pending_off=off;pending_space=(int)space;}
            }
        } else if(x->type==RESULT && x->parent==pending_call) {
            reads++; const uint8_t *want=x->pl+4;
            if(memcmp(want,pending_bytes,pending_n)) {
                if(pending_space==0 && pending_off==0x210) vline++;
                else {read_diff++; if(shown++<20) printf("read differs: space %d off %#x want %02x%02x%02x%02x got %02x%02x%02x%02x\n",pending_space,pending_off,
                    want[3],want[2],want[1],want[0],pending_bytes[3],pending_bytes[2],pending_bytes[1],pending_bytes[0]);}
            }
            pending_call=0;
        } else if(x->type==WB_INTENT) {
            uint64_t a=u64(x->pl+8); uint32_t q=u32(x->pl+16); wb++;
            if(q==4){if(nrefs==caprefs){caprefs=caprefs?2*caprefs:4096;refs=realloc(refs,caprefs*sizeof *refs);}refs[nrefs++]=(Wb){a,u32(x->pl+20)};}
            if(a>ram_size||q>ram_size-a||memcmp(ram+a,x->pl+20,q)) {wb_diff++; if(verbose&&wb_diff<10) printf("write-back differs at %#llx\n",(unsigned long long)a);}
        } else if(x->type==FRAME) {
            frames++; uint32_t w,h,bpp,line; size_t k=optics_frontend_scanout(d,shot,(size_t)width*height*4+16,&w,&h,&bpp,&line);
            if(!k){frame_none++;continue;}
            unsigned lane=optics_frontend_cpu_view_xor(d);   /* recorded frames hash the CPU view */
            if(lane==3) for(size_t q=0;q+4<=k;q+=4){uint8_t t0=shot[q],t1=shot[q+1];shot[q]=shot[q+3];shot[q+1]=shot[q+2];shot[q+2]=t1;shot[q+3]=t0;}
            uint8_t hash[32]; sha(shot,k,hash);
            if(!memcmp(hash,x->pl+56,32)) frame_same++; else {if(!frame_diff)first_frame_diff=frames;frame_diff++;}
        }
    }
    for(unsigned k=0;cfg.cp_thread && k<200000 && !optics_frontend_idle(d);k++){ pt_radeon_service(d,last_now); usleep(50); }
    /* Stable sort by address keeps program order within an address: compare value sequences. */
    size_t seq_same=0,seq_diff=0;
    { Wb *a=malloc(nours*sizeof *a),*b=malloc(nrefs*sizeof *b); memcpy(a,ours,nours*sizeof *a); memcpy(b,refs,nrefs*sizeof *b);
      for(size_t i=1;i<nours;i++){Wb t=a[i];size_t j=i;while(j>0&&cmpwb(&a[j-1],&t)>0){a[j]=a[j-1];j--;}a[j]=t;}
      for(size_t i=1;i<nrefs;i++){Wb t=b[i];size_t j=i;while(j>0&&cmpwb(&b[j-1],&t)>0){b[j]=b[j-1];j--;}b[j]=t;}
      size_t k=nours<nrefs?nours:nrefs; for(size_t i=0;i<k;i++){if(a[i].pa==b[i].pa&&a[i].v==b[i].v)seq_same++;else{if(seq_diff++<5)printf("write-back sequence differs at #%zu: ours %#llx=%08x ref %#llx=%08x\n",i,(unsigned long long)a[i].pa,a[i].v,(unsigned long long)b[i].pa,b[i].v);}}
      seq_diff+=(nours>nrefs?nours-nrefs:nrefs-nours);
      printf("write-back sequences: ours %zu, reference %zu, same %zu, differ %zu\n",nours,nrefs,seq_same,seq_diff); free(a); free(b); }
    OpticsFrontendStats s; optics_frontend_stats(d,&s);
    printf("commands %llu; reads %llu (differ %llu, CRTC_VLINE timing %llu); write-backs %llu (differ %llu); frames %llu: same %llu, differ %llu (first #%llu), no scanout %llu\n",
        (unsigned long long)commands,(unsigned long long)reads,(unsigned long long)read_diff,(unsigned long long)vline,(unsigned long long)wb,(unsigned long long)wb_diff,
        (unsigned long long)frames,(unsigned long long)frame_same,(unsigned long long)frame_diff,(unsigned long long)first_frame_diff,(unsigned long long)frame_none);
    pt_radeon_stats(d,stdout);
    if(getenv("OPTICS_DUMP")) {   /* raw native dumps of interesting surfaces for inspection */
        static const uint32_t at[][3]={{0,1024,768},{0x300000,1024,768},{0x452000,832,64}};
        for(unsigned i=0;i<3;i++){size_t n=(size_t)at[i][1]*at[i][2]*4;uint8_t *b=malloc(n);char name[256];
            snprintf(name,sizeof name,"%s-%08x-%ux%u.raw",getenv("OPTICS_DUMP"),at[i][0],at[i][1],at[i][2]);
            if(b && optics_frontend_gpu_read(d,at[i][0],b,n)){FILE *f=fopen(name,"wb");if(f){fwrite(b,1,n,f);fclose(f);}}free(b);}
    }
    pt_radeon_close(d);
    return read_diff||seq_diff?1:0;
}
