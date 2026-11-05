/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Host feasibility and real execution of leaf lowerings; not a PPC OS test. */
#include <Hypervisor/Hypervisor.h>
#include <assert.h>
#include <mach/mach_time.h>
#include <pthread.h>
#include <sched.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "g5_lower.h"
extern const char pt_g5_native_start[], pt_g5_native_end[];
#define HIGH (UINT64_C(16) << 30)
enum {
    SIZE = 65536,
    CODE = 0x4000,
    VECTORS = 0xc000
};
#define HV(call)                                     \
    do {                                             \
        hv_return_t r = (call);                      \
        if (r != HV_SUCCESS) {                       \
            fprintf(stderr, "%s: 0x%x\n", #call, r); \
            exit(1);                                 \
        }                                            \
    } while (0)
typedef struct {
    unsigned id, shared;
    _Atomic unsigned *ready, *go;
} Worker;
static void initialize(hv_vcpu_t *v, hv_vcpu_exit_t **e, unsigned id) {
    HV(hv_vcpu_create(v, e, NULL));
    HV(hv_vcpu_set_trap_debug_exceptions(*v, true));
    HV(hv_vcpu_set_sys_reg(*v, HV_SYS_REG_MAIR_EL1, 0xff));
    HV(hv_vcpu_set_sys_reg(*v, HV_SYS_REG_TCR_EL1, 28 | 1u << 8 | 1u << 10 | 3u << 12 | 1u << 23 | 2ull << 32));
    HV(hv_vcpu_set_sys_reg(*v, HV_SYS_REG_TTBR0_EL1, (uint64_t)(id + 1) << 48));
    HV(hv_vcpu_set_sys_reg(*v, HV_SYS_REG_VBAR_EL1, VECTORS));
    uint64_t ctl;
    HV(hv_vcpu_get_sys_reg(*v, HV_SYS_REG_SCTLR_EL1, &ctl));
    HV(hv_vcpu_set_sys_reg(*v, HV_SYS_REG_SCTLR_EL1, ctl | 1 | 1u << 2 | 1u << 12));
    HV(hv_vcpu_set_reg(*v, HV_REG_CPSR, 0x3c5));
}
static void run(hv_vcpu_t v, hv_vcpu_exit_t *e) {
    for (;;) {
        HV(hv_vcpu_run(v));
        if (e->reason == HV_EXIT_REASON_CANCELED)
            continue;
        if (e->reason != HV_EXIT_REASON_EXCEPTION || e->exception.syndrome >> 26 != 0x3c ||
            (e->exception.syndrome & 0xffff) != 1) {
            fprintf(stderr, "unexpected exit %u, syndrome %llx\n", e->reason,
                    (unsigned long long)e->exception.syndrome);
            exit(1);
        }
        break;
    }
}
static void *worker(void *arg) {
    Worker *w = arg;
    hv_vcpu_t v;
    hv_vcpu_exit_t *e;
    initialize(&v, &e, w->id);
    HV(hv_vcpu_set_reg(v, HV_REG_PC, CODE));
    HV(hv_vcpu_set_reg(v, HV_REG_X0, HIGH + (w->shared ? 0 : w->id * 4096)));
    HV(hv_vcpu_set_reg(v, HV_REG_X1, 100000));
    atomic_fetch_add_explicit(w->ready, 1, memory_order_release);
    while (!atomic_load_explicit(w->go, memory_order_acquire))
        sched_yield();
    run(v, e);
    HV(hv_vcpu_destroy(v));
    return NULL;
}
static uint64_t be64(const uint8_t *p) {
    uint64_t v;
    memcpy(&v, p, 8);
    return __builtin_bswap64(v);
}
static void *alloc(void) {
    void *p;
    assert(!posix_memalign(&p, 16384, SIZE));
    memset(p, 0, SIZE);
    return p;
}
static uint64_t random64(void) {
    static uint64_t x = 0x2359abca7881ull;
    x ^= x << 13;
    x ^= x >> 7;
    x ^= x << 17;
    return x;
}
int main(void) {
    alarm(30);
    uint32_t max;
    HV(hv_vm_get_max_vcpu_count(&max));
    assert(max >= 4);
    if (__builtin_available(macOS 13.0, *)) {
        uint32_t bits, def;
        HV(hv_vm_config_get_max_ipa_size(&bits));
        HV(hv_vm_config_get_default_ipa_size(&def));
        printf("{\"max_vcpus\":%u,\"max_ipa_bits\":%u,\"default_ipa_bits\":%u}\n", max, bits, def);
        assert(def >= 35);
    }
    uint8_t *low = alloc(), *high = alloc();
    size_t size = (size_t)(pt_g5_native_end - pt_g5_native_start);
    assert(size < 4096);
    memcpy(low + CODE, pt_g5_native_start, size);
    __builtin___clear_cache((char *)low + CODE, (char *)low + CODE + size);
    uint64_t *root = (void *)low, attrs = (1ull << 54) | (1u << 10) | (3u << 8) | 1;
    root[0] = attrs;
    root[16] = HIGH | attrs;
    for (unsigned i = 0; i < 16; i++)
        ((uint32_t *)(low + VECTORS))[i * 32] = 0xd4200000 | ((i + 16) << 5);
    HV(hv_vm_create(NULL));
    HV(hv_vm_map(low, 0, SIZE, HV_MEMORY_READ | HV_MEMORY_WRITE | HV_MEMORY_EXEC));
    HV(hv_vm_map(high, HIGH, SIZE, HV_MEMORY_READ | HV_MEMORY_WRITE));
    for (unsigned shared = 0; shared < 2; shared++) {
        memset(high, 0, SIZE);
        pthread_t t[4];
        Worker w[4];
        _Atomic unsigned ready = 0, go = 0;
        for (unsigned i = 0; i < 4; i++) {
            w[i] = (Worker){i, shared, &ready, &go};
            assert(!pthread_create(&t[i], NULL, worker, &w[i]));
        }
        while (atomic_load_explicit(&ready, memory_order_acquire) != 4)
            sched_yield();
        atomic_store_explicit(&go, 1, memory_order_release);
        for (unsigned i = 0; i < 4; i++)
            assert(!pthread_join(t[i], NULL));
        if (shared)
            assert(be64(high) == 400000);
        else
            for (unsigned i = 0; i < 4; i++)
                assert(be64(high + i * 4096) == 100000);
        printf("{\"case\":\"%s_be64_high_ipa\",\"vcpus\":4,\"ipa\":%llu,\"updates\":400000,\"pass\":true}\n",
               shared ? "shared" : "independent", (unsigned long long)HIGH);
    }
    /* Each leaf runs for real on ARM EL1 and is checked against the integer executor. */
    static const unsigned ops[] = {266, 40, 233, 9, 73, 28, 60, 316, 444, 986, 58};
    uint32_t *code = (void *)(low + 0x5000);
    for (unsigned k = 0; k < sizeof ops / sizeof *ops; k++) {
        uint32_t in = 31u << 26 | 3 << 21 | 4 << 16 | ((ops[k] == 986 || ops[k] == 58) ? 0 : 5u << 11) | ops[k] << 1;
        assert(pt_g5_lower_leaf(in, &code[k * 2]) == PT_G5_OK);
        code[k * 2 + 1] = 0xd4200020;
    }
    __builtin___clear_cache((char *)code, (char *)code + sizeof ops / sizeof *ops * 8);
    hv_vcpu_t v;
    hv_vcpu_exit_t *e;
    initialize(&v, &e, 0);
    unsigned count = 0;
    for (unsigned k = 0; k < sizeof ops / sizeof *ops; k++)
        for (unsigned i = 0; i < 256; i++) {
            uint64_t a = random64(), b = random64();
            PtG5Integer c = {.msr = PT_G5_MSR_SF};
            unsigned op = ops[k];
            bool logic = op == 28 || op == 60 || op == 316 || op == 444 || op == 986 || op == 58;
            c.gpr[logic ? 3 : 4] = a;
            c.gpr[5] = b;
            uint32_t in = 31u << 26 | 3 << 21 | 4 << 16 | ((op == 986 || op == 58) ? 0 : 5u << 11) | op << 1;
            assert(pt_g5_integer(&c, in) == PT_G5_OK);
            HV(hv_vcpu_set_reg(v, HV_REG_PC, 0x5000 + k * 8));
            HV(hv_vcpu_set_reg(v, HV_REG_X9, a));
            HV(hv_vcpu_set_reg(v, HV_REG_X10, b));
            run(v, e);
            uint64_t actual;
            HV(hv_vcpu_get_reg(v, HV_REG_X11, &actual));
            assert(actual == c.gpr[logic ? 4 : 3]);
            count++;
        }
    HV(hv_vcpu_destroy(v));
    printf("{\"case\":\"armv8_leaf_lowerings\",\"checks\":%u,\"pass\":true}\n", count);
    HV(hv_vm_unmap(HIGH, SIZE));
    HV(hv_vm_unmap(0, SIZE));
    HV(hv_vm_destroy());
    free(high);
    free(low);
    alarm(0);
}
