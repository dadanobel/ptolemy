// PowerPC 750 MMU: BATs, segments, the hashed page table (HTAB) with R/C bits; memory access for the interpreter.
// Host mappings (HVF VM, Linux stand windows) are filled through pt_xlate.
#include "ppc.h"
#include <stdio.h>

uint32_t ppc_exc_ring[64][8]; unsigned ppc_exc_n;   /* recent exceptions, written by ppc_exception, printed below for a user DSI near 0 */

// EA -> RA for access acc (0 read, 1 write, 2 instruction fetch) in mode pr. The PTE R/C bits are set here.
// 0: success, with *ra and *wr (may be mapped writable: protection allows it and C is already set, or it is a BAT).
// Otherwise the reason: for data, DSISR (0x40000000 no translation, 0x08000000 protection, 0x04000000 T segment, | 0x02000000 store),
// for a fetch, SRR1 bits (0x40000000 no translation, 0x10000000 N or T segment or a G4 guarded page, 0x08000000 protection).
// On a 7447A with HID0[STEN] (software table search): PT_TLB_MISS, | PT_TLB_CHANGED for a store to a TLB entry with C clear.
uint32_t pt_xlate(CPU *c, uint32_t ea, int acc, int pr, uint32_t *ra, int *wr) {
    int st = acc == 1, fetch = acc == 2;
    uint32_t prot = fetch ? 0x08000000u : st ? 0x0A000000u : 0x08000000u;
    for (unsigned i = 0; i < ppc_bat_count(c); i++) {                    // BATs take priority over pages
        unsigned s = (fetch ? 528 : 536) + 2 * i + (i>=4?24:0);
        uint32_t u = c->spr[s], l = c->spr[s+1];
        uint32_t m = ppc_bat_mask(c,u);
        if (!(pr ? u & 1 : u & 2) || (ea & ~m) != (u & ~m & 0xFFFE0000u)) continue;
        if(fetch && ppc_has_vec(c) && (l&8))return 0x10000000u;
        int pp = l & 3;
        if (!pp || (st && pp != 2)) return prot;
        *ra = (l & ~m & 0xFFFE0000u) | (ea & m); *wr = pp == 2; return 0;
    }
    uint32_t sr = c->sr[ea >> 28];
    if (sr >> 31) return fetch ? 0x10000000u : 0x04000000u | (st ? 0x02000000u : 0);       // direct-store segment (T=1): not supported
    if (fetch && sr & 0x10000000u) return 0x10000000u;                                     // N: no execute
    int key = pr ? sr >> 29 & 1 : sr >> 30 & 1;
    if(ppc_7447a(c)) {
        unsigned set=ea>>12&63;
        for(unsigned way=0;way<2;way++) {
            PtPpcTlb *t=&c->tlb[fetch][set*2+way];
            if(!t->valid || t->vsid!=(sr&0xffffff) || t->page!=(ea&0x0ffff000))continue;
            if(fetch && (t->lo&8))return 0x10000000u;
            unsigned pp=t->lo&3;int read=!(key && !pp),write=key?pp==2:pp!=3;
            c->tlb_lru[fetch][set]=way^1;
            if(!read || (st && !write))return prot;
            if(st && !(t->lo&0x80)) {
                if(c->spr[1008]&HID0_STEN)return PT_TLB_MISS|PT_TLB_CHANGED;
                break; /* Hardware reload obtains/updates C from the HTAB. */
            }
            *ra=(t->lo&0xfffff000)|(ea&4095);*wr=write && (t->lo&0x80);return 0;
        }
        if(c->spr[1008]&HID0_STEN)return PT_TLB_MISS;
    }
    uint32_t vsid = sr & 0xFFFFFF, pi = ea >> 12 & 0xFFFF, h = (vsid & 0x7FFFF) ^ pi;
    uint32_t sdr1 = c->spr[SPR_SDR1], hi = sdr1 >> 16 & 0x1FF, mask = sdr1 & 0x1FF;
    for (int sec = 0; sec < 2; sec++, h = ~h & 0x7FFFF) {
        uint32_t pteg = (sdr1 & 0xFE000000u) | ((hi | (h >> 10 & mask)) << 16) | (h & 0x3FF) << 6;
        for (int k = 0; k < 8; k++) {
            uint8_t *p = pmem(c, pteg + 8 * k, 8);
            if (!p) return fetch ? 0x40000000u : 0x40000000u | (st ? 0x02000000u : 0);    // HTAB not in RAM
            if (be32(p) != (0x80000000u | vsid << 7 | (uint32_t)sec << 6 | pi >> 10)) continue;
            uint32_t w1 = be32(p + 4), pp = w1 & 3;
            if(fetch && ppc_has_vec(c) && (w1&8))return 0x10000000u;
            int r_ok = !(key && pp == 0), w_ok = key ? pp == 2 : pp != 3;
            if (!r_ok || (st && !w_ok)) return prot;                    // a fetch is checked as a read
            uint32_t n1 = w1 | 0x100 | (st ? 0x80 : 0);                 // R, C
            if (n1 != w1) {
                uint8_t *write_pte = pmem_write(c, pteg + 8 * k + 4, 4);
                if (write_pte) wbe32(write_pte, n1);
            }
            *ra = (w1 & 0xFFFFF000u) | (ea & 0xFFF); *wr = w_ok && n1 & 0x80; return 0;
        }
    }
    return fetch ? 0x40000000u : 0x40000000u | (st ? 0x02000000u : 0);
}

static int xl(CPU *c, uint32_t ea, int st, uint32_t *ra) {              // EA -> RA for data; 1 = DSI delivered
    *ra = ea;
    if (!(c->msr & MSR_DR)) return 0;
    int wr; uint32_t why = pt_xlate(c, ea, st, !!(c->msr & MSR_PR), ra, &wr);
    if (!why) return 0;
    ppc_mmu_fault(c,ea,st,why);
    return 1;
}
static void phys(CPU *c, uint32_t ra, uint8_t *buf, int n, int st) {   // RAM/ROM directly, otherwise a device
    uint8_t *p = st ? pmem_write(c, ra, n) : pmem(c, ra, n);
    if (p) { if (st) memcpy(p, buf, n); else memcpy(buf, p, n); }
    else if (c->env_mmio) c->env_mmio(c, ra, buf, n, st);
    else if (!st) memset(buf, 0xff, n);                                 // open bus
}
// n bytes between EA and buf (in memory order); 1 = exception delivered, the instruction did not complete.
// Across a page boundary with DR=1: byte by byte, but both pages are checked first, so a store is never partial.
static int acc(CPU *c, uint32_t ea, uint8_t *buf, int n, int st) {
    uint32_t ra, r2;
    if (!(c->msr & MSR_DR && (ea & 0xFFF) + n > 0x1000)) { if (xl(c, ea, st, &ra)) return 1; phys(c, ra, buf, n, st); return 0; }
    if (xl(c, ea, st, &ra) || xl(c, (ea | 0xFFF) + 1, st, &r2)) return 1;
    for (int i = 0; i < n; i++) { xl(c, ea + i, st, &ra); phys(c, ra, buf + i, 1, st); }
    return 0;
}
static uint64_t get(const uint8_t *p, int n) { uint64_t x = 0; for (int i = 0; i < n; i++) x = x << 8 | p[i]; return x; }
int ppc_ld(CPU *c, uint32_t ea, int n, uint64_t *v) { uint8_t b[8]; if (acc(c, ea, b, n, 0)) return 1; *v = get(b, n); return 0; }
int ppc_st(CPU *c, uint32_t ea, int n, uint64_t v) {
    uint8_t b[8]; for (int i = n - 1; i >= 0; i--, v >>= 8) b[i] = (uint8_t)v;
    return acc(c, ea, b, n, 1);
}
int ppc_ifetch(CPU *c, uint32_t pc, uint32_t *in) {                     // 1 = ISI delivered
    uint32_t ra = pc; int wr;
    if (c->msr & MSR_IR) {
        uint32_t why = pt_xlate(c, pc, 2, !!(c->msr & MSR_PR), &ra, &wr);
        if (why) { c->pc=pc;ppc_mmu_fault(c,pc,2,why); return 1; }
    }
    uint8_t *p = pmem(c, ra, 4);
    if (!p) { ppc_exception(c, 0x200, pc, 0); return 1; }               // fetch outside memory: machine check
    *in = be32(p); return 0;
}

/* MPC7450UM §§2.4.3,4.6.15: functional software reload, not timing/cache simulation.
 * pt_xlate may be called speculatively by the JIT; publish exception state only here. */
void ppc_mmu_fault(CPU *c,uint32_t ea,int access,uint32_t why) {
    if(why&PT_TLB_MISS) {
        unsigned fetch=access==2,set=ea>>12&63;
        uint32_t old=c->msr,sr=c->sr[ea>>28];
        unsigned key=old&MSR_PR?sr>>29&1:sr>>30&1;
        c->spr[980]=(ea&~1u)|c->tlb_lru[fetch][set];
        c->spr[981]=0x80000000u|((sr&0xffffff)<<7)|(ea>>22&63);
        ppc_exception(c,fetch?0x1000:access?0x1200:0x1100,c->pc,0);
        c->spr[SPR_SRR1]=(old&0x0200ffffu)|(key<<19)|((why&PT_TLB_CHANGED)?0x100000u:0);
    }else if(access==2)ppc_exception(c,0x400,c->pc,why);
    else {
        if(ea<0x1000 && (c->msr&MSR_PR)) {   /* diagnostics for a user DSI near 0 (seen in Doom 3: DAR 0x1 on stfs 8(r3)) */
            static unsigned shown; if(shown++<4) {
                const uint32_t *r=c->gpr;
                fprintf(stderr,"ptolemy: DSI ea %08x pc %08x dsisr %08x msr %08x lr %08x ctr %08x\n",ea,c->pc,why,c->msr,c->lr,c->ctr);
                for(int g=0;g<32;g+=8)fprintf(stderr,"ptolemy:   r%-2d %08x %08x %08x %08x %08x %08x %08x %08x\n",g,r[g],r[g+1],r[g+2],r[g+3],r[g+4],r[g+5],r[g+6],r[g+7]);
                for(unsigned k=ppc_exc_n>64?ppc_exc_n-64:0;k<ppc_exc_n;k++){const uint32_t *e=ppc_exc_ring[k&63];
                    fprintf(stderr,"ptolemy:   exc -%-2u vec %4x srr0 %08x msr %08x dar %08x dsisr %08x r1 %08x r3 %08x r31 %08x\n",ppc_exc_n-k,e[0],e[1],e[2],e[3],e[4],e[5],e[6],e[7]);}
            }
        }
        c->spr[SPR_DAR]=ea;c->spr[SPR_DSISR]=why;ppc_exception(c,0x300,c->pc,0);
    }
}
void ppc_tlb_load(CPU *c,uint32_t ea,int instruction) {
    PtPpcTlb *t=&c->tlb[instruction][(ea>>12&63)*2+(ea&1)];
    *t=(PtPpcTlb){c->spr[981]>>7&0xffffff,ea&0x0ffff000,c->spr[982],c->spr[981]>>31};
    c->tlb_lru[instruction][ea>>12&63]=(ea&1)^1;
    c->xgen++; /* Retire old JIT code and ARM mappings for either replaced way. */
}
void ppc_tlb_invalidate(CPU *c,uint32_t ea) {
    if(!ppc_7447a(c))return;
    unsigned set=ea>>12&63,changed=0;
    for(unsigned kind=0;kind<2;kind++)for(unsigned way=0;way<2;way++) {
        PtPpcTlb *t=&c->tlb[kind][set*2+way];changed|=t->valid;t->valid=0;
    }
    if(changed)c->xgen++; /* tlbie is index-based, also invalidates an alias VSID. */
}
int ppc_external(CPU *c,uint32_t ea,int store,uint32_t *value) {
    uint32_t why=0,pa=ea;int wr;
    if(!(c->spr[282]&0x80000000u))why=0x100000u|(store?0x02000000u:0);
    else if(ea&3) {
        c->spr[SPR_DAR]=ea;c->spr[SPR_DSISR]=store?0x02000000u:0;
        ppc_exception(c,0x600,c->pc,0);return 1;
    }else if(c->msr&MSR_DR)why=pt_xlate(c,ea,store,!!(c->msr&MSR_PR),&pa,&wr);
    if(why){ppc_mmu_fault(c,ea,store,why);return 1;}
    uint8_t bytes[4];wbe32(bytes,store?*value:UINT32_MAX);
    if(c->env_external)c->env_external(c,c->spr[282]&15,pa,bytes,store);
    if(!store)*value=be32(bytes);
    return 0;
}
