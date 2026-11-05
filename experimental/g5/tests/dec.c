/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "g5_dec.h"
#include <assert.h>
#include <stdio.h>
#define OK(x) assert((x) == PT_G5_OK)
int main(void) {
    PtG5Decrementer d = {0};
    uint32_t value;
    uint64_t distance;
    bool pending;
    assert(pt_g5_dec_read(&d, 0, &value) == PT_G5_BAD_CONFIG);
    pt_g5_dec_write(&d, 100, 1);
    OK(pt_g5_dec_next(&d, 100, &distance));
    assert(distance == 2);
    OK(pt_g5_dec_read(&d, 101, &value));
    assert(value == 0);
    OK(pt_g5_dec_pending(&d, 101, true, &pending));
    assert(!pending);
    OK(pt_g5_dec_pending(&d, 102, true, &pending));
    assert(pending);
    OK(pt_g5_dec_pending(&d, 102, true, &pending));
    assert(pending);
    OK(pt_g5_dec_pending(&d, 102, false, &pending));
    assert(!pending);
    OK(pt_g5_dec_pending(&d, 103, true, &pending));
    assert(pending);
    OK(pt_g5_dec_next(&d, 103, &distance));
    assert(distance == 0);
    pt_g5_dec_write(&d, 103, 5);
    OK(pt_g5_dec_pending(&d, 103, true, &pending));
    assert(!pending);
    pt_g5_dec_write(&d, 200, 0x80000000u);
    OK(pt_g5_dec_pending(&d, 200, true, &pending));
    assert(pending);
    OK(pt_g5_dec_pending(&d, 201, true, &pending));
    assert(!pending);
    OK(pt_g5_dec_read(&d, 200 + (UINT64_C(1) << 32), &value));
    assert(value == 0x80000000u);
    assert(pt_g5_dec_read(&d, 199, &value) == PT_G5_BAD_CONFIG);
    puts("PASS 970MP DEC: level reassertion, EE masking, positive rewrite, wrap, explicit clock epoch");
}
