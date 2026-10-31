/* SPDX-License-Identifier: GPL-2.0-or-later
 * Execute the real block translator on Apple Silicon; compare every exit with
 * the interpreter, not just each instruction in isolation (which hides lazy bugs).
 * --bench times already translated FP loops. No Tiger, HVF or GPU is involved. */
#include "../epicycle/jit.c"
#include <assert.h>
#include <fenv.h>
#include <math.h>
#include <pthread.h>
#include <setjmp.h>
#include <signal.h>
#include <sys/ucontext.h>

int sys_call(CPU *c) { (void)c; abort(); }
static uint8_t *memory, *reference;
static uint32_t *code;
static unsigned checks, exits, deferred_ops, fsf_exits;
static uint64_t random_state = 0x9e3779b97f4a7c15ull;
static unsigned random32(void) {
    random_state ^= random_state << 13; random_state ^= random_state >> 7;
    random_state ^= random_state << 17; return (unsigned)random_state;
}
static uint32_t afp(unsigned single, unsigned xo, unsigned d, unsigned a, unsigned b, unsigned k) {
    return (single ? 59u : 63u) << 26 | d << 21 | a << 16 | b << 11 | k << 6 | xo << 1;
}
static uint32_t xfp(unsigned xo, unsigned d, unsigned a, unsigned b) {
    return 63u << 26 | d << 21 | a << 16 | b << 11 | xo << 1;
}
static void setup(void) {
    code = mmap(NULL, 1 << 20, PROT_READ|PROT_WRITE|PROT_EXEC,
        MAP_PRIVATE|MAP_ANONYMOUS|MAP_JIT, -1, 0);
    memory = mmap(NULL, 65536, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
    reference = calloc(1, 65536); assert(code != MAP_FAILED && memory != MAP_FAILED && reference);
    /* Match the engine's native trampoline, preserving the host ABI. x18 is
     * macOS-reserved; the VM may use it, this native fixture must not. */
    NPOOL = 8; memcpy(POOL, (int8_t[]){15,19,20,21,22,23,24,25}, 8);
    pthread_jit_write_protect_np(0); cp = code;
    E(0xA9800000u | (unsigned)(-12 & 0x7f) << 15 | 30u << 10 | 31u << 5 | 29);
    for (unsigned i=0;i<5;i++) E(0xA9000000u | (2+2*i) << 15 | (20+2*i) << 10 | 31u << 5 | (19+2*i));
    E(0xAA0003FBu); E(0xAA0103F1u); ldrx(MEM, ENV, OFF(vbase)); load_guest(); E(0xD61F0220u);
    exit_common = cp; store_guest(); ldrw(0, ENV, OFF(jit_exit));
    for (unsigned i=0;i<5;i++) E(0xA9400000u | (2+2*i) << 15 | (20+2*i) << 10 | 31u << 5 | (19+2*i));
    E(0xA8C00000u | 12u << 15 | 30u << 10 | 31u << 5 | 29); E(0xD65F03C0u);
    __builtin___clear_cache((char *)code, (char *)cp); pthread_jit_write_protect_np(1);
    enter = (int (*)(CPU *, void *))(void *)code;
    buf = code; bufend = code + (1 << 18);
    jmsr = MSR_750|MSR_FP|MSR_VEC; vm_vec = MSR_VEC;
    fp_old = 0; fma_fast = fma_single_direct = fp_round_fast = 1;
    fp_hoist_enabled = 1; cr_fuse = 0; maxblk = 128; zbase = 0;
    fp_fr_on = !getenv("PT_FP_FR") || atoi(getenv("PT_FP_FR"));   /* PT_FP_FR=0: --bench A/B of FR's cost */
    vmx_neon_enabled = vmx_int2_enabled = 1;
#ifdef PT_JIT_FSF
    fsf_enabled = !getenv("PT_FP_SGL") || atoi(getenv("PT_FP_SGL"));   /* the single-format cache, as in the window */
#endif
    pthread_jit_write_protect_np(0); fp_lazy_enabled = 1; settle_gen(); pthread_jit_write_protect_np(1);   /* JIT-side settle */
}
static CPU initial(unsigned g4) {
    CPU c = {0}; c.pc=0x1000; c.mem=c.vbase=memory;
    c.msr=MSR_FP|MSR_VEC; c.spr[287]=g4?0x80030000u:0x00080000u;
    c.jit_jc=jc;
    c.npreg=1; c.preg[0]=(PRegion){0,65536,memory,0};
    c.fpscr=0x82000000u; c.gpr[3]=0x8000; c.gpr[4]=MSR_FP|MSR_VEC;
    for (unsigned i=0;i<16;i++) c.crtab[i]=cr_make(i>>3&1,i>>2&1,i>>1&1,i&1);
    for (unsigned r=0;r<32;r++) c.fpr[r]=1.25+r*0.125;
    return c;
}
static void reset_code(unsigned lazy, unsigned cache) {
    pthread_jit_write_protect_np(0);
    fp_lazy_enabled=lazy; fcache_enabled=cache; check=0; nolink=0; kir=kpr=0;
    flush_all(); pthread_jit_write_protect_np(1);
#ifdef PT_JIT_FSF
    memset(nofsf,0,sizeof nofsf); memset(nofsf_n,0,sizeof nofsf_n);   /* each case learns again */
#endif
}
static void same(CPU *jit, CPU *expected, unsigned test) {
    /* The architectural FPSCR is fpscr plus the status the JIT owes (fs_kind): compare a settled copy, so
     * the JIT's own record still crosses this exit, as it does in the machine. */
    CPU settled = *jit, *actual = &settled; ppc_fp_settle(actual);
    if (diff(actual, expected, 0, actual->pc) || actual->msr != expected->msr ||
        actual->vscr != expected->vscr || memcmp(actual->vr,expected->vr,sizeof actual->vr) ||
        memcmp(memory,reference,65536)) {
        fprintf(stderr,"lazy FPSCR mismatch test=%u cache=%d blocks=%d exit=%u\n",
            test, fcache_enabled, nblk,actual->jit_exit); abort();
    }
    checks++;
}
static CPU run_case(CPU c, const uint32_t *ins, unsigned n, unsigned test) {
    uint32_t start=c.pc,end=start+n*4;
    memset(memory,0,65536);
    for(unsigned i=0;i<n;i++) wbe32(memory+start+i*4,ins[i]);
    memcpy(reference,memory,65536); CPU expected=c;
    expected.mem=expected.vbase=reference; expected.preg[0].host=reference;
    reset_code(1,test&1); jsys=c.sys; jmsr=ppc_msr_mask(&c);vm_vec=jmsr&MSR_VEC;
    for(unsigned step=0;c.pc<end;step++) {
        if(step>=2*n+8) {fprintf(stderr,"stuck test=%u pc=%08x exit=%u sys=%d msr=%08x\n",test,c.pc,c.jit_exit,c.sys,c.msr);abort();}
        pthread_jit_write_protect_np(0); uint32_t *target=block(&c);
        pthread_jit_write_protect_np(1); assert(target);
        enter(&c,target); exits++;
        assert(c.pc>=start && c.pc<=end);
        while(expected.pc<c.pc) assert(interp_step(&expected)==0);
        same(&c,&expected,test);
        if(c.jit_exit==X_IRQ) {c.irq=expected.irq=0;continue;} /* acknowledge mtmsr's mode check */
#ifdef PT_JIT_FSF
        if(c.jit_exit==X_FSF) {pthread_jit_write_protect_np(0);int k=jit_fsf_fail(&c);pthread_jit_write_protect_np(1);fsf_exits++;if(k)continue;c.jit_exit=X_INTERP;}   /* entry value not single */
#endif
        if(c.jit_exit==X_INTERP && c.pc<end) {
            assert(interp_step(&c)==0 && interp_step(&expected)==0); same(&c,&expected,test);
        }
    }
    return c;
}
static void differential(void) {
    static const unsigned xo[]={18,20,21,25,28,29,30,31};
    static const double special[]={0.,-0.,0x1p-149,0x1p-150,0x1p-1022,
        0x0.0000000000001p-1022,0x1p1000,INFINITY,-INFINITY,NAN};
    for(unsigned t=0;t<12000;t++) {
        CPU c=initial(t&1); uint32_t ins[48]; unsigned n=0;
        for(unsigned r=1;r<24;r++) {
            double v=ldexp(1.+(random32()&0x7fffff)*0x1p-23,(int)(random32()%41)-20);
            c.fpr[r]=random32()&1?v:-v;
            if(t&4) c.fpr[r]=nextafter(c.fpr[r],INFINITY);
        }
        unsigned len=2+random32()%30;
        for(unsigned i=0;i<len;i++) {
            unsigned single=random32()&1, op=xo[random32()%8];
            ins[n++]=afp(single,op,1+random32()%20,1+random32()%20,1+random32()%20,1+random32()%20);
            deferred_ops++;
        }
        /* Exceptional inputs, RN/enables, both XX and FX independently, and
         * source/destination aliases; compare partial guard exits as well. */
        if(t%3==0) c.fpr[1+random32()%20]=special[t%10];
        if(t%41==0) {uint64_t snan=0x7ff0000000000001ull;memcpy(&c.fpr[1],&snan,8);}
        if(t%7==0) c.fpscr=(uint32_t[]){0,0x02000000u,0x82071000u,0x02000001u,
            0x02000002u,0x02000003u,0x02000004u,0x02000008u,0x02000080u}[t%9];
        switch(t%9) {
        case 0: ins[n++]=xfp(583,25,0,0); break;             /* mffs observer */
        case 1: ins[n++]=xfp(0,8,1,2); break;               /* fcmpu */
        case 2: ins[n++]=xfp(64,8,0,0); break;              /* mcrfs (interpreter) */
        case 3: ins[n++]=xfp(21,1,1,2)|1u; break;           /* Rc -> CR1 */
        case 4: ins[n++]=54u<<26|1u<<21|3u<<16; break;      /* stfd: memory boundary */
        case 5: ins[n++]=63u<<26|1u<<17|24u<<11|711u<<1; c.fpr[24]=0; break; /* mtfsf */
        case 6: ins[n++]=31u<<26|4u<<21|146u<<1; c.sys=1; break; /* mtmsr */
        case 7: ins[n++]=4u<<26|1u<<21|2u<<16|3u<<11|128u; c.spr[287]=0x80030000u; break; /* vadduwm */
        default: ins[n++]=xfp(12,2,0,1); break;             /* frsp */
        }
        ins[n++]=afp(0,21,4,1,2,0);
        run_case(c,ins,n,t);
    }
    /* Registers only from +-Inf, +-0, 1.5 and tiny values (Xbench FP Basic's array after ~10 passes):
     * the lazy path's out-of-line Inf and zero-factor acceptance against the interpreter. */
    static const double mix[]={INFINITY,-INFINITY,0.,-0.,1.5,-1.5,0x1p-140,0x1p-1060};
    for(unsigned t=0;t<3000;t++) {
        CPU c=initial(t&1); uint32_t ins[24]; unsigned n=0;
        for(unsigned r=1;r<24;r++) c.fpr[r]=mix[random32()%8];
        unsigned len=2+random32()%20;
        for(unsigned i=0;i<len;i++) { ins[n++]=afp(random32()&1,xo[random32()%8],1+random32()%20,1+random32()%20,1+random32()%20,1+random32()%20); deferred_ops++; }
        ins[n++]=xfp(583,25,0,0);
        run_case(c,ins,n,20000+t);
    }
    /* Explicit sequencing: last operation alone determines FI/FPRF. Neither
     * XX nor FX may be cleared or spuriously raised when XX was already set. */
    for(unsigned kind=0;kind<4;kind++) {
        CPU c=initial(1); c.fpscr=kind&1?0x02000000u:0x82000000u;
        c.fpr[1]=1.;c.fpr[2]=0x1p-54;c.fpr[3]=2.;
        uint32_t ins[18];for(unsigned j=0;j<15;j++)ins[j]=afp(0,21,7,1,3,0);
        ins[15]=afp(0,21,4,1,2,0);ins[16]=afp(0,21,5,1,3,0);ins[17]=xfp(583,6,0,0);
        if(kind&2) {uint32_t swap=ins[15];ins[15]=ins[16];ins[16]=swap;}
        CPU actual=run_case(c,ins,18,12000+kind);
        uint32_t want=(kind&1?0x02000000u:0x82000000u)|0x4000u|(kind&2?0x20000u:0);
        uint64_t observed;memcpy(&observed,&actual.fpr[6],8);
        assert(actual.fpscr==want && observed==(0xfff8000000000000ull|want));
    }
    for(unsigned kind=0;kind<8;kind++) {
        CPU c=initial(kind&1);uint32_t ins[40];unsigned n=0;
        for(unsigned j=0;j<24;j++)ins[n++]=afp(0,29,1,2,3,4);
        c.fpr[9]=NAN;c.fpr[10]=INFINITY;c.fpr[11]=0x1p-1022;c.fpr[12]=0x1.0000000000001p0;
        if(kind<4)ins[n++]=kind==0?afp(0,20,1,2,2,0):kind==1?afp(0,29,1,9,2,3):
            kind==2?afp(0,25,1,11,0,11):afp(1,29,1,12,2,3);
        if(kind==4) {ins[n++]=0x41820008u;ins[n++]=afp(0,21,1,1,10,0);} /* taken bc skips Inf */
        if(kind==5)c.pc=0x1fc0;                            /* flush at 4K PPC page boundary */
        if(kind==6)maxblk=16;                             /* flush at block-size boundary */
        if(kind==7) {ins[n++]=xfp(70,6,0,0);ins[n++]=afp(0,21,1,1,2,0);} /* mtfsb0 XX */
        ins[n++]=xfp(583,31,0,0);
        run_case(c,ins,n,12100+kind);maxblk=128;
    }
    {   /* Largest supported block, including its cold fallback/link stubs. */
        CPU c=initial(1);uint32_t ins[256];
        for(unsigned i=0;i<256;i++)ins[i]=afp(i&1,29,1+i%8,10,11,12);
        maxblk=256;run_case(c,ins,256,12200);maxblk=128;
        for(int b=0;b<nblk;b++)assert(blk[b].n<=256 && blk[b].end-blk[b].code<65536);
    }
    printf("lazy FPSCR: %u full-state comparisons at %u exits, %u random FP operations PASS\n",checks,exits,deferred_ops);
#ifdef PT_JIT_FSF
    if(fsf_enabled){ assert(st_fsf_blocks && fsf_exits); printf("lazy FPSCR: %llu translations with single-format FPRs, %u entries not single (an interpreter step, the fourth at a pc: translated again)\n",(unsigned long long)st_fsf_blocks,fsf_exits); }
#endif
}
static void activation(void) {   /* a record only with PT_FP_LAZY, never with PT_FP_OLD or PT_CHECK */
    for(unsigned mode=0;mode<3;mode++) {
        CPU c=initial(1);memset(memory,0,65536);
        wbe32(memory+0x1000,afp(0,29,1,2,3,4));wbe32(memory+0x1004,0x48000000u);   /* fmadd; b . (not executed) */
        reset_code(1,1);jsys=0;fp_old=mode==1;check=mode==2;
        pthread_jit_write_protect_np(0);uint32_t *target=block(&c);pthread_jit_write_protect_np(1);
        c.fs_kind=0;enter(&c,target);
        assert(c.pc==0x1004 && (c.fs_kind==29)==(mode==0));
        check=fp_old=0;
    }
    /* fadd (owed), fcmpu (FPCC after it), mffs in the JIT: the settle routine keeps fcmpu's FPCC, the rest from fadd */
    CPU c=initial(1);c.fpr[1]=1.;c.fpr[2]=0x1p-60;c.fpr[3]=3.;
    uint32_t ins[4]={afp(0,21,4,1,2,0),xfp(0,0,1,3),xfp(583,6,0,0),afp(0,21,5,1,3,0)};
    CPU r=run_case(c,ins,4,15000);uint64_t got;memcpy(&got,&r.fpr[6],8);
    assert((uint32_t)got==0x82028000u);   /* FX XX (initial), FI (1 + 2^-60 inexact), C = 0 (+normal), FPCC 1 < 3 */
    puts("lazy FPSCR: PT_FP_LAZY/PT_FP_OLD/PT_CHECK gates, settle keeps fcmpu FPCC PASS");
}
static void firmware_exit(void) {
    CPU c=initial(0);c.pc=0xfff00000u;c.sys=1;c.preg[0].base=c.pc;
    memset(memory,0,65536);
    for(unsigned j=0;j<4;j++)wbe32(memory+4*j,afp(0,21,1,2,3,0));
    memcpy(reference,memory,65536);CPU expected=c;expected.preg[0].host=reference;expected.mem=reference;
    assert(!interp_step(&expected));
    reset_code(1,1);jsys=1;jmsr=ppc_msr_mask(&c);
    unsigned slot=(c.pc+4)>>2&1023;hot[slot].pc1=c.pc+5;hot[slot].n=256;nhot=1;
    pthread_jit_write_protect_np(0);uint32_t *target=block(&c);pthread_jit_write_protect_np(1);
    enter(&c,target);assert(c.jit_exit==X_INTERP);same(&c,&expected,14000);
    memset(hot,0,sizeof hot);nhot=0;jsys=0;
    puts("lazy FPSCR: firmware forced-interpreter exit restores pending flags PASS");
}
static sigjmp_buf fault_jump;
static CPU *fault_cpu;
static unsigned fault_count;
static void on_fault(int sig, siginfo_t *info, void *context) {
    (void)sig; (void)info;
    ucontext_t *u=context; uint64_t x[31];
    for(unsigned i=0;i<29;i++) x[i]=u->uc_mcontext->__ss.__x[i];
    x[29]=__darwin_arm_thread_state64_get_fp(u->uc_mcontext->__ss);
    x[30]=__darwin_arm_thread_state64_get_lr(u->uc_mcontext->__ss);
    if(!fault_cpu || !jit_restore(fault_cpu,x,__darwin_arm_thread_state64_get_pc(u->uc_mcontext->__ss))) _Exit(90);
    fault_count++; siglongjmp(fault_jump,1);
}
static void fault_boundary(void) {
    struct sigaction sa={0},old_bus,old_segv; sa.sa_sigaction=on_fault;sa.sa_flags=SA_SIGINFO;
    assert(!sigaction(SIGBUS,&sa,&old_bus) && !sigaction(SIGSEGV,&sa,&old_segv));
    for(unsigned cache=0;cache<2;cache++) for(unsigned store=0;store<2;store++) {
        static CPU c,expected; c=initial(1);c.gpr[3]=0x8000;
        memset(memory,0,65536);
        for(unsigned j=0;j<16;j++)wbe32(memory+0x1000+4*j,afp(0,29,1,1,2,3));
        wbe32(memory+0x1040,(store?54u:50u)<<26|4u<<21|3u<<16);
        memcpy(reference,memory,65536);expected=c;
        expected.mem=expected.vbase=reference;expected.preg[0].host=reference;
        for(unsigned j=0;j<16;j++)assert(!interp_step(&expected));
        reset_code(1,cache);jsys=0;
        pthread_jit_write_protect_np(0);uint32_t *target=block(&c);pthread_jit_write_protect_np(1);
        assert(!mprotect(memory+0x8000,0x4000,PROT_NONE));
        fault_cpu=&c;
        if(!sigsetjmp(fault_jump,1)) {enter(&c,target);abort();}
        fault_cpu=NULL;assert(!mprotect(memory+0x8000,0x4000,PROT_READ|PROT_WRITE));
        same(&c,&expected,13000+cache*2+store);
    }
    sigaction(SIGBUS,&old_bus,NULL);sigaction(SIGSEGV,&old_segv,NULL);
    printf("lazy FPSCR: %u actual protected-page load/store faults, exact state + PPC PC PASS\n",fault_count);
}
static int compare_double(const void *a,const void *b) {double x=*(const double *)a,y=*(const double *)b;return (x>y)-(x<y);}
static void bench(void) {
    static const unsigned lengths[]={2,4,8,16,32,96};
    for(unsigned single=0;single<2;single++) for(unsigned li=0;li<sizeof lengths/sizeof *lengths;li++) {
        unsigned len=lengths[li];
        uint32_t ins[128];
        for(unsigned i=0;i<len;i++) ins[i]=afp(single,29,1+i%8,1+i%8,10,9);
        ins[len]=0x42000000u | ((uint32_t)-(int)(4*len)&0xfffcu); /* bdnz to first op */
        memset(memory,0,65536); for(unsigned i=0;i<=len;i++) wbe32(memory+0x1000+4*i,ins[i]);
        double times[2][7];unsigned words[2]={0};CPU results[2];
        for(unsigned pair=0;pair<7;pair++) { for(unsigned order=0;order<2;order++) {
            unsigned lazy=order^(pair&1);CPU c=initial(1);c.ctr=200000;
            c.fpr[9]=single?0x1.fffffep-1:0x1.fffffffffffffp-1;c.fpr[10]=0x1p-20;
            reset_code(lazy,1);jsys=0;
            pthread_jit_write_protect_np(0);uint32_t *target=block(&c);pthread_jit_write_protect_np(1);
            words[lazy]=(unsigned)(blk[0].end-target);
            uint64_t t=jit_now(); enter(&c,target); times[lazy][pair]=(jit_now()-t)*1e-9;
            assert(c.pc==0x1000+4*(len+1) && c.ctr==0);
            results[lazy]=c;
            printf("FP sample single=%u len=%u pair=%u order=%u lazy=%u seconds=%.9f\n",
                single,len,pair,order,lazy,times[lazy][pair]);
        } assert(!diff(&results[0],&results[1],0,results[0].pc)); }
        for(unsigned mode=0;mode<2;mode++)qsort(times[mode],7,sizeof(double),compare_double);
        printf("FP loop %s len=%u iterations=200000 median seconds eager=%.6f lazy=%.6f speedup=%.3fx code_words=%u/%u\n",
            single?"single-FMA":"double-FMA",len,times[0][3],times[1][3],times[0][3]/times[1][3],words[0],words[1]);
    }
}
int main(int argc,char **argv) {
    setup();
    if(argc==2 && !strcmp(argv[1],"--bench")) bench();
    else {assert(argc==1);differential();fault_boundary();activation();firmware_exit();}
    munmap(code,1<<20);munmap(memory,65536);free(reference);return 0;
}
