/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "g5_bridge.h"
#include "platform.h"
#include <assert.h>
#include <stdio.h>
int main(void) {
    PtG5Bridge s = {0};
    uint8_t b[4] = {0};
    assert(!pt_g5_bridge_access(&s, 0, b, 4, 0) && b[0] == 0 && b[3] == 0x30);
    pt_write_be(b, 4, 0x12345678);
    assert(!pt_g5_bridge_access(&s, 0x40, b, 4, 1));
    assert(!pt_g5_bridge_access(&s, 0x40, b, 4, 0) && pt_read_be(b, 4) == 0x12345678);
    assert(pt_g5_bridge_access(&s, 0, b, 4, 1));
    assert(pt_g5_bridge_access(&s, 0x40, b, 2, 0));
    assert(pt_g5_bridge_access(&s, 0x30, b, 4, 1));
    assert(pt_g5_bridge_access(&s, 0x23030, b, 4, 1));
    pt_write_be(b, 4, 0);
    assert(!pt_g5_bridge_access(&s, 0x23030, b, 4, 1));
    assert(pt_g5_bridge_access(&s, 0x70000, b, 4, 0));
    puts("PASS G5 bridge: BE version, retained tuning, no silent sleep/HT/second-CPU success");
}
