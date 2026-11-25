/* SPDX-License-Identifier: GPL-2.0-or-later */
/* cc -Iinclude tests/boot-mode.c && ./a.out : CVT-RB generator vs the catalogue, strict parsing. */
#include "pt_boot_mode.h"
#include <assert.h>
int main(void) {
    const char *names[] = {"1920x1080", "1920x1200", "2560x1440", "2560x1600"};
    for (unsigned i = 0; i < 4; i++) {
        const PtBootMode *m = pt_boot_mode(names[i]);
        uint8_t d[15];
        assert(m && pt_cvt_rb(m->width, m->height, d) && !memcmp(d, m->dtd, 12));
    }
    const PtBootMode *m = pt_boot_mode("1464x923");
    assert(m && m->width == 1464 && m->height == 923 && (m->dtd[2] | (m->dtd[4] >> 4) << 8) == 1464 &&
           (m->dtd[5] | (m->dtd[7] >> 4) << 8) == 923);
    assert(!pt_boot_mode("1470x923") && !pt_boot_mode("1464x923 ") && !pt_boot_mode("1464x") && !pt_boot_mode(NULL));
    /* up to the R300 CRTC: 4096 wide, 4096 lines; 5K and wider go to the presenter's scaling instead */
    m = pt_boot_mode("3840x2160");
    assert(m && m->width == 3840 && m->height == 2160 && (m->dtd[2] | (m->dtd[4] >> 4) << 8) == 3840);
    assert(pt_boot_mode("4096x2160") && pt_boot_mode("3440x1440") && pt_boot_mode("3840x2400"));
    assert(!pt_boot_mode("4104x2160") && !pt_boot_mode("3840x4104") && !pt_boot_mode("5120x2880"));
    m = pt_boot_mode("1464x923");
    printf("boot-mode PASS (1464x923: %.2f MHz, vblank %u)\n", (m->dtd[0] | m->dtd[1] << 8) / 100.0, m->dtd[6]);
    return 0;
}
