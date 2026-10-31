#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#include <Hypervisor/Hypervisor.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/mman.h>
#include <sys/sysctl.h>
#include <unistd.h>

#define HV_OK(expr) do { hv_return_t r = (expr); if (r != HV_SUCCESS) { fprintf(stderr, "%s: %x\n", #expr, r); return 1; } } while (0)
#define REQUIRE(expr) do { if (!(expr)) { fprintf(stderr, "FAILED: %s\n", #expr); return 1; } } while (0)

int main(void) {
    size_t page = (size_t)getpagesize();
    uint8_t *mem = mmap(NULL, 2 * page, PROT_READ | PROT_WRITE, MAP_ANON | MAP_PRIVATE, -1, 0);
    REQUIRE(mem != MAP_FAILED);
    @autoreleasepool {
        id<MTLDevice> dev = MTLCreateSystemDefaultDevice(); REQUIRE(dev);
        printf("OS: %s\nGPU: %s\nhost page: %zu\n", NSProcessInfo.processInfo.operatingSystemVersionString.UTF8String, dev.name.UTF8String, page);
        printf("unified: %d\nmaxBufferLength: %lu\nrecommendedMaxWorkingSetSize: %llu\n", dev.hasUnifiedMemory, (unsigned long)dev.maxBufferLength, dev.recommendedMaxWorkingSetSize);
        for (NSInteger family = MTLGPUFamilyApple7; family <= MTLGPUFamilyApple10; family++)
            printf("Apple%ld: %d\n", (long)(family - MTLGPUFamilyApple1 + 1), [dev supportsFamily:family]);
        size_t line = 0, n = sizeof(line);
        if (sysctlbyname("hw.cachelinesize", &line, &n, NULL, 0) == 0) printf("host cache line: %zu\n", line);
        else printf("host cache line: unavailable\n");
        uint32_t ipa_bits; HV_OK(hv_vm_config_get_max_ipa_size(&ipa_bits)); printf("max IPA bits: %u\n", ipa_bits);
        if (@available(macOS 26.0, *)) {
            hv_ipa_granule_t granule; HV_OK(hv_vm_config_get_default_ipa_granule(&granule));
            printf("default stage-2 granule: %s\n", granule == HV_IPA_GRANULE_4KB ? "4KB" : granule == HV_IPA_GRANULE_16KB ? "16KB" : "other");
        }
        bool el2; HV_OK(hv_vm_config_get_el2_supported(&el2)); printf("guest EL2 supported: %d\n", el2);

        id<MTLBuffer> shared = [dev newBufferWithBytesNoCopy:mem + page length:page options:MTLResourceStorageModeShared deallocator:nil];
        REQUIRE(shared && shared.contents == mem + page);
        uint32_t *data = (uint32_t *)(mem + page);
        data[0] = 0x11223344;
        const uint32_t program[] = {0xB9400020, 0x4A020000, 0xB9000420, 0xD5033F9F, 0xD4200000};
        memcpy(mem, program, sizeof(program));
        const uint32_t readback[] = {0xB9400820, 0xB9001020, 0xD5033F9F, 0xD4200000};
        memcpy(mem + 32, readback, sizeof(readback));
        HV_OK(hv_vm_create(NULL));
        HV_OK(hv_vm_map(mem, 0x10000, 2 * page, HV_MEMORY_READ | HV_MEMORY_WRITE | HV_MEMORY_EXEC));
        hv_vcpu_t v; hv_vcpu_exit_t *vx; HV_OK(hv_vcpu_create(&v, &vx, NULL));
        HV_OK(hv_vcpu_set_trap_debug_exceptions(v, true));
        HV_OK(hv_vcpu_set_reg(v, HV_REG_CPSR, 0x3c5));
        HV_OK(hv_vcpu_set_reg(v, HV_REG_PC, 0x10000));
        HV_OK(hv_vcpu_set_reg(v, HV_REG_X1, 0x10000 + page));
        HV_OK(hv_vcpu_set_reg(v, HV_REG_X2, 0xa5a5a5a5));
        HV_OK(hv_vcpu_run(v));
        REQUIRE(vx->reason == HV_EXIT_REASON_EXCEPTION && (vx->exception.syndrome >> 26) == 0x3c);
        REQUIRE(data[1] == (data[0] ^ 0xa5a5a5a5u));
        NSError *error = nil;
        NSString *src = @"#include <metal_stdlib>\nusing namespace metal;\nkernel void step(device uint *p [[buffer(0)]]) { p[2] = p[1] + 0x01020304u; p[3] = p[0]; }";
        id<MTLLibrary> lib = [dev newLibraryWithSource:src options:nil error:&error];
        if (!lib) { fprintf(stderr, "%s\n", error.description.UTF8String); return 1; }
        id<MTLComputePipelineState> pipeline = [dev newComputePipelineStateWithFunction:[lib newFunctionWithName:@"step"] error:&error];
        REQUIRE(pipeline);
        id<MTLCommandQueue> queue = [dev newCommandQueue];
        id<MTLCommandBuffer> cb = [queue commandBuffer];
        id<MTLComputeCommandEncoder> encoder = [cb computeCommandEncoder];
        [encoder setComputePipelineState:pipeline]; [encoder setBuffer:shared offset:0 atIndex:0];
        [encoder dispatchThreadgroups:MTLSizeMake(1,1,1) threadsPerThreadgroup:MTLSizeMake(1,1,1)];
        [encoder endEncoding]; [cb commit]; [cb waitUntilCompleted];
        REQUIRE(cb.status == MTLCommandBufferStatusCompleted);
        REQUIRE(data[2] == ((0x11223344u ^ 0xa5a5a5a5u) + 0x01020304u) && data[3] == data[0]);
        HV_OK(hv_vcpu_set_reg(v, HV_REG_PC, 0x10000 + 32)); HV_OK(hv_vcpu_run(v));
        REQUIRE(vx->reason == HV_EXIT_REASON_EXCEPTION && (vx->exception.syndrome >> 26) == 0x3c && data[4] == data[2]);
        printf("same host buffer: YES\nhost seed: %08x\nARM VM wrote: %08x\nMetal wrote: %08x\nARM VM read back: %08x\nROUNDTRIP: PASS\n", data[0], data[1], data[2], data[4]);
        HV_OK(hv_vcpu_destroy(v)); HV_OK(hv_vm_unmap(0x10000, 2 * page)); HV_OK(hv_vm_destroy());
    }
    REQUIRE(munmap(mem, 2 * page) == 0);
    return 0;
}
