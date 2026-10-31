// hvprobe: checks three premises of the design (PPC code translated to ARM64, run inside a hardware ARM64 VM):
//  1) whether an Apple silicon guest can run big-endian (then no REV is needed on every memory access);
//  2) whether the 4K translation granule exists (PowerPC pages are 4 KB);
//  3) what a VM exit on a device access costs (the path that serves Radeon/IDE/USB MMIO).
#include <Hypervisor/Hypervisor.h>
#include <mach/mach_time.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>

#define CK(x) do { hv_return_t r_ = (x); if (r_ != HV_SUCCESS) { printf("FAIL %s = 0x%x\n", #x, r_); exit(1); } } while (0)
enum { CODE = 0x10000, DATA = 0x18000, MMIO = 0x40000000, N = 200000 };

static const uint32_t prog[] = {
    0xD5380704, // mrs  x4, ID_AA64MMFR0_EL1
    0xB9400020, // ldr  w0, [x1]      ; bytes 01 02 03 04
    0xD4000002, // hvc  #0            ; report
    0xB9000040, // loop: str w0, [x2] ; unmapped address -> exits as MMIO
    0xF1000463, // subs x3, x3, #1
    0x54FFFFC1, // b.ne loop
    0xD4000022, // hvc  #1
};

static uint64_t reg(hv_vcpu_t v, hv_reg_t r) { uint64_t x; CK(hv_vcpu_get_reg(v, r, &x)); return x; }

// Runs the program from the start; returns x0 and x4 at hvc #0 and the time per MMIO exit.
static void run(hv_vcpu_t v, hv_vcpu_exit_t *e, uint64_t iters, uint64_t *x0, uint64_t *x4, double *ns) {
    CK(hv_vcpu_set_reg(v, HV_REG_PC, CODE));
    CK(hv_vcpu_set_reg(v, HV_REG_CPSR, 0x3c5));   // EL1h, interrupts masked
    CK(hv_vcpu_set_reg(v, HV_REG_X1, DATA));
    CK(hv_vcpu_set_reg(v, HV_REG_X2, MMIO));
    CK(hv_vcpu_set_reg(v, HV_REG_X3, iters));
    uint64_t mmio = 0, t0 = 0;
    mach_timebase_info_data_t tb; mach_timebase_info(&tb);
    for (;;) {
        CK(hv_vcpu_run(v));
        if (e->reason != HV_EXIT_REASON_EXCEPTION) { printf("exit reason %d\n", e->reason); exit(1); }
        uint32_t ec = (uint32_t)(e->exception.syndrome >> 26) & 0x3f;
        if (ec == 0x16) {                                   // HVC: PC already points at the next instruction
            if ((e->exception.syndrome & 0xffff) == 0) { *x0 = reg(v, HV_REG_X0); *x4 = reg(v, HV_REG_X4); t0 = mach_absolute_time(); continue; }
            *ns = mmio ? (double)(mach_absolute_time() - t0) * tb.numer / tb.denom / mmio : 0;
            return;
        }
        if (ec == 0x24 && e->exception.physical_address == MMIO) { mmio++; CK(hv_vcpu_set_reg(v, HV_REG_PC, reg(v, HV_REG_PC) + 4)); continue; }
        printf("unexpected EC 0x%x ESR 0x%llx PC 0x%llx\n", ec, e->exception.syndrome, reg(v, HV_REG_PC)); exit(1);
    }
}

int main(void) {
    CK(hv_vm_create(NULL));
    uint8_t *mem = mmap(NULL, 0x10000, PROT_READ | PROT_WRITE, MAP_ANON | MAP_PRIVATE, -1, 0);
    for (unsigned i = 0; i < sizeof prog / 4; i++) ((uint32_t *)mem)[i] = prog[i];
    mem[0x8000] = 1; mem[0x8001] = 2; mem[0x8002] = 3; mem[0x8003] = 4;
    CK(hv_vm_map(mem, CODE, 0x10000, HV_MEMORY_READ | HV_MEMORY_WRITE | HV_MEMORY_EXEC));
    hv_vcpu_t v; hv_vcpu_exit_t *e;
    CK(hv_vcpu_create(&v, &e, NULL));

    uint64_t x0, mm, sctlr; double ns;
    run(v, e, N, &x0, &mm, &ns);
    printf("ID_AA64MMFR0_EL1 = 0x%016llx\n", mm);
    printf("  BigEnd(EL1)=%llu BigEndEL0=%llu  TGran4=%s TGran16=%s TGran64=%s  PARange=%llu\n",
           (mm >> 8) & 15, (mm >> 16) & 15,
           ((mm >> 28) & 15) == 15 ? "no" : "yes", ((mm >> 20) & 15) ? "yes" : "no", ((mm >> 24) & 15) == 15 ? "no" : "yes", mm & 15);
    printf("ldr w0 with SCTLR.EE=0: 0x%08llx (0x04030201 = little-endian)\n", x0 & 0xffffffff);
    printf("MMIO exit: %.0f ns per access (%d accesses)\n", ns, N);

    CK(hv_vcpu_get_sys_reg(v, HV_SYS_REG_SCTLR_EL1, &sctlr));
    CK(hv_vcpu_set_sys_reg(v, HV_SYS_REG_SCTLR_EL1, sctlr | (1ull << 25)));   // EE=1: EL1 data big-endian
    uint64_t back; CK(hv_vcpu_get_sys_reg(v, HV_SYS_REG_SCTLR_EL1, &back));
    run(v, e, 1, &x0, &mm, &ns);
    printf("SCTLR_EL1 0x%llx -> write EE=1 -> 0x%llx; ldr w0: 0x%08llx (0x01020304 = big-endian works)\n", sctlr, back, x0 & 0xffffffff);
    return 0;
}
