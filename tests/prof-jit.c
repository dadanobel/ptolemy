/* SPDX-License-Identifier: GPL-2.0-or-later
 * PT_PROF over a session (jit.c): samples at translated blocks are counted by guest image and pc - the Mach-O header
 * below a user pc names the image -, supervisor code and user code without a header apart, ticks outside the VM
 * apart; a stall reported by Optics counts the ticks inside it again; the file holds both tables. No JIT code runs. */
#include "jit-test.h"
#include <unistd.h>
enum { GUEST = 1 << 21, LIB = 0x40000, HEAP = 0x180000 };
static uint8_t *memory;
static uint64_t code_at(CPU *c, uint32_t pc, uint32_t msr) { /* a block at pc: its host code address */
    c->pc = pc;
    c->msr = msr;
    kpr = !!(msr & MSR_PR);
    pthread_jit_write_protect_np(0);
    uint32_t *t = block(c);
    pthread_jit_write_protect_np(1);
    assert(t);
    return (uint64_t)t;
}
int main(void) {
    uint32_t *code = mmap(NULL, 1 << 22, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANON | MAP_JIT, -1, 0);
    memory = mmap(NULL, GUEST, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
    assert(code != MAP_FAILED && memory != MAP_FAILED);
    trampoline(code);
    buf = code;
    bufend = code + (1 << 20);
    jmsr = MSR_750;
    zbase = 0;
    maxblk = 128;
    flush_all();
    pthread_jit_write_protect_np(1);
    CPU c = {0};
    c.mem = c.vbase = memory;
    c.jit_jc = jc;
    c.npreg = 1;
    c.preg[0] = (PRegion){0, GUEST, memory, 0};
    /* a dylib: mach_header, LC_SEGMENT __TEXT (vmaddr 0x90010000, 0x6000 bytes), LC_UUID, LC_ID_DYLIB; a data page
     * with the magic number above it must not be taken for a header of the pc beyond its __TEXT */
    const uint32_t hdr[] = {0xfeedface, 18, 0, 6, 3, 0, 0, /* LC_SEGMENT */ 1, 56, 0x5f5f5445, 0x58540000, 0, 0, 0x90010000, 0x6000,
                            0, 0x6000, 7, 5, 0, 0, /* LC_UUID */ 0x1b, 24, 0x01020304, 0x05060708, 0x090a0b0c, 0x0d0e0f10,
                            /* LC_ID_DYLIB */ 0xd, 48, 24, 0, 0, 0};
    for (unsigned i = 0; i < sizeof hdr / 4; i++)
        wbe32(memory + LIB + 4 * i, hdr[i]);
    strcpy((char *)memory + LIB + 4 * 27 + 24, "/usr/lib/libfoo.dylib");
    for (uint32_t a = 0x1000; a < GUEST; a += 0x1000) /* blr everywhere a block may start */
        if (a != LIB)
            wbe32(memory + a, 0x4e800020u), wbe32(memory + a + 0x100, 0x4e800020u);
    wbe32(memory + HEAP - 0x2000, 0xfeedface);
    wbe32(memory + HEAP - 0x2000 + 4, 18);
    uint64_t lib1 = code_at(&c, LIB + 0x1000, MSR_PR), lib2 = code_at(&c, LIB + 0x5100, MSR_PR),
             heap = code_at(&c, HEAP, MSR_PR), kern = code_at(&c, 0x3000, 0);
    unlink("/tmp/prof-jit.txt");
    setenv("PT_PROF_FILE", "/tmp/prof-jit.txt", 1);
    jit_prof_stall(200000000); /* without a tick: profiling is off, nothing counted */
    c.msr = MSR_PR;
    for (int i = 0; i < 5; i++)
        jit_prof_sample(&c, lib1);
    jit_prof_sample(&c, heap);
    usleep(20000); /* the stall below is the last 10 ms: only what follows */
    jit_prof_sample(&c, lib2);
    jit_prof_sample(&c, lib2);
    jit_prof_sample(&c, (uint64_t)code); /* the trampoline: JIT code outside blocks */
    jit_prof_outside();
    c.msr = 0;
    jit_prof_sample(&c, kern);
    jit_prof_stall(10000000);
    jit_prof_stall(10000000); /* the same ticks are not a second stall's */
    c.msr = MSR_PR;
    jit_prof_sample(&c, lib1);
    assert(npim == 3 && pim[2].base == LIB && pim[2].size == 0x6000 && pim[2].vmaddr == 0x90010000 && pim[2].type == 6 &&
           !strcmp(pim[2].name, "/usr/lib/libfoo.dylib") && pim[2].uuid[15] == 0x10);
    assert(pim[2].n == 8 && pim[2].stall == 2 && pim[1].n == 1 && pim[1].stall == 0 && pim[0].n == 1 && pim[0].stall == 1);
    assert(pse_n[0] == 10 && pse_n[1] == 1 && pse_n[2] == 1 && pst_n[0] == 3 && pst_n[1] == 1 && pst_n[2] == 1 && pst_count == 2);
    jit_prof_session();
    FILE *f = fopen("/tmp/prof-jit.txt", "r");
    assert(f);
    char line[512];
    int seen = 0, stalls = 0;
    while (fgets(line, sizeof line, f)) {
        stalls |= !strncmp(line, "stalls\t5 ticks", 14);
        seen += !stalls && !strcmp(line, "pc\t2\t6\t00041000\t00001000\n");
        seen += !stalls && !strcmp(line, "pc\t2\t2\t00045100\t00005100\n");
        seen += !stalls && !strncmp(line, "session\t12 ticks\tblocks 83.3%", 29);
        seen += stalls && !strcmp(line, "pc\t2\t2\t00045100\t00005100\n");
        seen += stalls && !strcmp(line, "pc\t0\t1\t00003000\t00003000\n");
        seen += stalls && strstr(line, "\t0102030405060708090a0b0c0d0e0f10\t/usr/lib/libfoo.dylib\n") != NULL;
    }
    fclose(f);
    assert(seen == 6);
    puts("prof-jit: session and stall tables by guest image and pc PASS");
    return 0;
}
