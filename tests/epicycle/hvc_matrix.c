// Diagnostic, not an engine test: the first VM exit after `hvc #0` for several x0 values (including three SMCCC function IDs).
// Code in the VM: hvc #0; mov x2,x0; brk #0. EC 0x16: an HVC exit; EC 0x3c: HVF handled the HVC itself and the first exit was the BRK.
// Observed on M4/macOS 27.0.1: c1a86500 -> x0=ffffffff without an HVC exit; 80000000 and ffffffff exit on HVC unchanged.
// Unverified: the mechanism (SMCCC classification?). This is why the engine leaves the VM with BRK (jit.c), not HVC.
//   clang -O2 hvc_matrix.c -framework Hypervisor -o hvc_matrix && codesign -s - -f --entitlements ../../epicycle/hv.entitlements hvc_matrix
#include <Hypervisor/Hypervisor.h>
#include <stdio.h>
#include <sys/mman.h>
#define CK(e) do { hv_return_t r = (e); if (r) { printf("HV error %x\n", r); return 1; } } while (0)
int main(void) {
    CK(hv_vm_create(NULL));
    uint32_t *mem = mmap(NULL, 0x4000, PROT_READ | PROT_WRITE, MAP_ANON | MAP_PRIVATE, -1, 0);
    mem[0] = 0xD4000002; mem[1] = 0xAA0003E2; mem[2] = 0xD4200000;
    CK(hv_vm_map(mem, 0x10000, 0x4000, HV_MEMORY_READ | HV_MEMORY_WRITE | HV_MEMORY_EXEC));
    hv_vcpu_t v; hv_vcpu_exit_t *e; CK(hv_vcpu_create(&v, &e, NULL));
    CK(hv_vcpu_set_trap_debug_exceptions(v, true));
    uint64_t values[] = {0x12345678, 0x7fffffff, 0x80000000, 0xc1a86500, 0xffffffff,
                         0x80000001, 0x84000000, 0xc1000000};          // SMCCC_ARCH_FEATURES, PSCI_VERSION, CPU service
    for (unsigned i = 0; i < sizeof values / sizeof *values; i++) {
        CK(hv_vcpu_set_reg(v, HV_REG_PC, 0x10000));
        CK(hv_vcpu_set_reg(v, HV_REG_CPSR, 0x3c5));
        CK(hv_vcpu_set_reg(v, HV_REG_X0, values[i]));
        CK(hv_vcpu_set_reg(v, HV_REG_X2, 0x11111111));
        CK(hv_vcpu_run(v));
        uint64_t x0, x2, pc; CK(hv_vcpu_get_reg(v, HV_REG_X0, &x0)); CK(hv_vcpu_get_reg(v, HV_REG_X2, &x2)); CK(hv_vcpu_get_reg(v, HV_REG_PC, &pc));
        printf("x0=%08llx: EC=%02llx x0=%016llx x2=%016llx pc=%llx\n", values[i], e->exception.syndrome >> 26 & 0x3f, x0, x2, pc);
    }
    CK(hv_vcpu_destroy(v)); CK(hv_vm_destroy());
}
