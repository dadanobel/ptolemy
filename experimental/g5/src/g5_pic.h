/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef PT_G5_PIC_H
#define PT_G5_PIC_H
#include "g5.h"
enum {
    PT_G5_PIC_IO = 128,
    PT_G5_PIC_IPI = 128,
    PT_G5_PIC_TIMER = 132,
    PT_G5_PIC_SOURCES = 136
};
typedef struct {
    uint32_t vpr, dest;
    uint8_t pending, active, rr;
    bool asserted, edge;
} PtG5PicSource;
typedef struct {
    uint16_t source;
    uint8_t priority;
} PtG5PicFrame;
typedef struct {
    pthread_mutex_t lock;
    unsigned cpus;
    PtG5PicSource source[PT_G5_PIC_SOURCES];
    PtG5PicFrame stack[4][16];
    unsigned depth[4], priority[4];
    uint32_t init_lines, gcr, spurious, frequency, base[4], current[4];
} PtG5Pic;
PtG5Result pt_g5_pic_init(PtG5Pic *, unsigned);
void pt_g5_pic_destroy(PtG5Pic *);
PtG5Result pt_g5_pic_access(PtG5Pic *, unsigned, uint32_t, uint32_t *, bool);
PtG5Result pt_g5_pic_input(PtG5Pic *, unsigned, bool);
void pt_g5_pic_tick(PtG5Pic *, uint64_t);
unsigned pt_g5_pic_irqs(PtG5Pic *);
/* Board polls this after writes and routes transitions to reset lines. No callbacks
 * execute while holding the PIC lock. MMIO adapter handles byte order and width. */
unsigned pt_g5_pic_reset_lines(PtG5Pic *);
#endif
