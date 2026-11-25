/* Little-endian mode probe (Tiger, devkit ppc-cc). Virtual PC runs its x86 code with MSR[LE] = 1 on a G3/G4; this
 * starts a thread of its own in that mode (thread_set_state, SRR1 | LE) and checks the munged accesses from outside:
 * the thread stores, loads (one misaligned) and then counts in a loop, through timer interrupts, until it is suspended.
 * In LE mode an instruction word at A is the big-endian word at A ^ 4, so the code is laid out in swapped pairs. */
#include <mach/mach.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#define D(op, t, a, d) ((unsigned)(op) << 26 | (t) << 21 | (a) << 16 | ((d)&0xffff))

int main(void) {
    static const unsigned le[8] = {
            D(36, 4, 3, 0),  /* stw  r4,0(r3): 0x11223344 -> big-endian bytes 4..7 */
            D(34, 5, 3, 0),  /* lbz  r5,0(r3): 0x44 */
            D(40, 6, 3, 2),  /* lhz  r6,2(r3): 0x1122 */
            D(32, 7, 3, 1),  /* lwz  r7,1(r3): misaligned, 0x00112233 */
            D(32, 8, 3, 16), /* lwz  r8,16(r3): bytes 20..23, 0x04050607 */
            D(14, 9, 9, 1),  /* addi r9,r9,1 */
            D(36, 9, 3, 8),  /* stw  r9,8(r3): big-endian bytes 12..15 */
            0x4bfffff8,      /* b    .-8 */
    };
    vm_address_t page = 0;
    if (vm_allocate(mach_task_self(), &page, 2 * vm_page_size, TRUE) != KERN_SUCCESS)
        return 2;
    unsigned *code = (unsigned *)page, i;
    unsigned char *buf = (unsigned char *)page + vm_page_size;
    for (i = 0; i < 8; i++)
        code[i ^ 1] = le[i];
    for (i = 0; i < 8; i++)
        buf[16 + i] = i;
    for (i = 0; i < 32; i += 8)
        __asm__ volatile("dcbst 0,%0; sync; icbi 0,%0; isync" : : "r"(code + i) : "memory");

    thread_act_t t;
    unsigned st[PPC_THREAD_STATE_COUNT]; /* srr0, srr1, r0..r31, cr, xer, lr, ctr, mq, vrsave */
    mach_msg_type_number_t n = PPC_THREAD_STATE_COUNT;
    if (thread_create(mach_task_self(), &t) != KERN_SUCCESS ||
        thread_get_state(t, PPC_THREAD_STATE, (thread_state_t)st, &n) != KERN_SUCCESS)
        return 3;
    st[0] = (unsigned)code;
    st[1] |= 1; /* MSR[LE] */
    st[2 + 1] = (unsigned)buf + 2048; /* r1, unused */
    st[2 + 3] = (unsigned)buf;
    st[2 + 4] = 0x11223344;
    st[2 + 9] = 0;
    if (thread_set_state(t, PPC_THREAD_STATE, (thread_state_t)st, PPC_THREAD_STATE_COUNT) != KERN_SUCCESS)
        return 4;
    n = PPC_THREAD_STATE_COUNT;
    thread_get_state(t, PPC_THREAD_STATE, (thread_state_t)st, &n);
    if (!(st[1] & 1)) {
        printf("LE SKIP: the kernel does not keep MSR[LE] in a thread's state (srr1 %08x)\n", st[1]);
        return 5;
    }
    thread_resume(t);
    sleep(2);
    thread_suspend(t);
    n = PPC_THREAD_STATE_COUNT;
    thread_get_state(t, PPC_THREAD_STATE, (thread_state_t)st, &n);
    thread_terminate(t);
    unsigned counted = (unsigned)buf[12] << 24 | buf[13] << 16 | buf[14] << 8 | buf[15], r9 = st[2 + 9];
    static const unsigned char stored[8] = {0, 0, 0, 0, 0x11, 0x22, 0x33, 0x44};
    int ok = (st[1] & 1) && st[2 + 5] == 0x44 && st[2 + 6] == 0x1122 && st[2 + 7] == 0x00112233 &&
             st[2 + 8] == 0x04050607 && !memcmp(buf, stored, 8) && r9 > 1000 && r9 - counted <= 1;
    printf("LE %s: srr1 %08x pc %08x r5 %08x r6 %08x r7 %08x r8 %08x loop %u (stored %u) bytes %02x%02x%02x%02x%02x%02x%02x%02x\n",
           ok ? "PASS" : "FAIL", st[1], st[0], st[2 + 5], st[2 + 6], st[2 + 7], st[2 + 8], r9, counted, buf[0], buf[1],
           buf[2], buf[3], buf[4], buf[5], buf[6], buf[7]);
    return !ok;
}
