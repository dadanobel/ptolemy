/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "g5_pic.h"
#include <string.h>
#define MASK UINT32_C(0x80000000)
#define ACT UINT32_C(0x40000000)
#define LEVEL UINT32_C(0x00400000)
#define MODE UINT32_C(0x20000000)
static unsigned priority(uint32_t v) {
    return (v >> 16) & 15;
}
static void reset(PtG5Pic *p) {
    memset(p->source, 0, sizeof p->source);
    memset(p->depth, 0, sizeof p->depth);
    memset(p->current, 0, sizeof p->current);
    p->init_lines = 0;
    p->gcr = 0;
    p->spurious = 255;
    p->frequency = 25000000;
    for (unsigned i = 0; i < PT_G5_PIC_SOURCES; i++)
        p->source[i].vpr = MASK;
    for (unsigned i = 0; i < 4; i++) {
        p->priority[i] = 15;
        p->base[i] = MASK;
    }
}
PtG5Result pt_g5_pic_init(PtG5Pic *p, unsigned n) {
    if (!p || !n || n > 4)
        return PT_G5_BAD_CONFIG;
    memset(p, 0, sizeof *p);
    if (pthread_mutex_init(&p->lock, NULL))
        return PT_G5_BAD_CONFIG;
    p->cpus = n;
    reset(p);
    return PT_G5_OK;
}
void pt_g5_pic_destroy(PtG5Pic *p) {
    pthread_mutex_destroy(&p->lock);
}
static unsigned threshold(PtG5Pic *p, unsigned c) {
    unsigned v = p->priority[c];
    if (p->depth[c] && p->stack[c][p->depth[c] - 1].priority > v)
        v = p->stack[c][p->depth[c] - 1].priority;
    return v;
}
static void route(PtG5Pic *p) {
    for (unsigned i = 0; i < PT_G5_PIC_IO; i++) {
        PtG5PicSource *s = &p->source[i];
        if (s->vpr & MASK) {
            s->pending = 0;
            continue;
        }
        bool event = (s->vpr & LEVEL) ? s->asserted : s->edge;
        if (!event) {
            s->pending = 0;
            continue;
        }
        if (s->pending || s->active)
            continue;
        /* Round-robin among eligible destinations: an I/O edge is never multicast. */
        for (unsigned n = 0; n < p->cpus; n++) {
            unsigned c = (s->rr + n) % p->cpus;
            if ((s->dest & (1u << c)) && !(p->init_lines & (1u << c)) && priority(s->vpr) > threshold(p, c)) {
                s->pending = (uint8_t)(1u << c);
                break;
            }
        }
    }
}
static int best(PtG5Pic *p, unsigned c) {
    if (!(p->gcr & MODE) || (p->init_lines & (1u << c)))
        return -1;
    int id = -1;
    unsigned pr = threshold(p, c);
    for (unsigned i = 0; i < PT_G5_PIC_SOURCES; i++) {
        PtG5PicSource *s = &p->source[i];
        unsigned v = priority(s->vpr);
        if (!(s->vpr & MASK) && (s->pending & (1u << c)) && !(s->active & (1u << c)) && v > pr) {
            id = (int)i;
            pr = v;
        }
    }
    return id;
}
static uint32_t acknowledge(PtG5Pic *p, unsigned c) {
    route(p);
    int i = best(p, c);
    if (i < 0)
        return p->spurious;
    PtG5PicSource *s = &p->source[i];
    if (p->depth[c] >= 16)
        return p->spurious;
    s->pending &= (uint8_t) ~(1u << c);
    s->active |= 1u << c;
    if (i < PT_G5_PIC_IO) {
        s->edge = false;
        s->rr = (uint8_t)((c + 1) % p->cpus);
    }
    p->stack[c][p->depth[c]++] = (PtG5PicFrame){(uint16_t)i, (uint8_t)priority(s->vpr)};
    return s->vpr & 255;
}
static void eoi(PtG5Pic *p, unsigned c) {
    if (p->depth[c]) {
        unsigned i = p->stack[c][--p->depth[c]].source;
        p->source[i].active &= (uint8_t) ~(1u << c);
    }
    route(p);
}
static PtG5Result source_reg(PtG5Pic *p, unsigned i, bool dest, uint32_t *value, bool write) {
    PtG5PicSource *s = &p->source[i];
    if (write) {
        if (dest) {
            if (s->active)
                return PT_G5_RETRY;
            s->dest = *value & ((1u << p->cpus) - 1);
        } else {
            uint32_t mask = MASK | 0xf00ff | (i < PT_G5_PIC_IO ? LEVEL : 0);
            if (s->active && ((*value ^ s->vpr) & ~MASK & mask))
                return PT_G5_RETRY;
            s->vpr = *value & mask;
        }
    } else
        *value = dest ? s->dest : s->vpr | (s->active ? ACT : 0);
    return PT_G5_OK;
}
static PtG5Result access(PtG5Pic *p, unsigned c, uint32_t reg, uint32_t *value, bool write) {
    if (reg >= 0x20000 && reg < 0x24000) {
        c = (reg - 0x20000) / 0x1000;
        reg &= 0xfff;
        if (c >= p->cpus)
            return PT_G5_BUS;
    }
    if (reg == 0)
        reg = 0x40;
    if (reg == 8)
        reg = 0x10a0;
    if (reg < 0x1000) {
        if (reg >= 0x40 && reg <= 0x70 && !(reg & 15)) {
            if (write)
                p->source[PT_G5_PIC_IPI + (reg - 0x40) / 16].pending |= *value & ((1u << p->cpus) - 1);
            else
                *value = 0;
            return PT_G5_OK;
        }
        switch (reg) {
        case 0x80:
            if (write)
                p->priority[c] = *value & 15;
            else
                *value = p->priority[c];
            return PT_G5_OK;
        case 0x90:
            if (write)
                return PT_G5_UNSUPPORTED;
            *value = c;
            return PT_G5_OK;
        case 0xa0:
            if (write)
                return PT_G5_UNSUPPORTED;
            *value = acknowledge(p, c);
            return PT_G5_OK;
        case 0xb0:
            if (write) {
                if (*value)
                    return PT_G5_BAD_CONFIG;
                eoi(p, c);
            } else
                *value = 0;
            return PT_G5_OK;
        default:
            return PT_G5_UNSUPPORTED;
        }
    }
    if (reg == 0x1000) {
        if (write)
            return PT_G5_UNSUPPORTED;
        *value = 127u << 16 | (p->cpus - 1) << 8 | 2;
        return PT_G5_OK;
    }
    if (reg == 0x1020) {
        if (write) {
            if (*value & MASK)
                reset(p);
            else
                p->gcr = *value & MODE;
        } else
            *value = p->gcr;
        return PT_G5_OK;
    }
    if (reg == 0x1080) {
        if (write)
            return PT_G5_UNSUPPORTED;
        *value = 0;
        return PT_G5_OK;
    }
    if (reg == 0x1090) {
        if (write) {
            p->init_lines = *value & ((1u << p->cpus) - 1);
            for (unsigned i = 0; i < p->cpus; i++)
                if (p->init_lines & (1u << i))
                    p->priority[i] = 15;
        } else
            *value = p->init_lines;
        return PT_G5_OK;
    }
    if (reg >= 0x10a0 && reg <= 0x10d0 && !(reg & 15))
        return source_reg(p, 128 + (reg - 0x10a0) / 16, false, value, write);
    if (reg == 0x10e0) {
        if (write)
            p->spurious = *value & 255;
        else
            *value = p->spurious;
        return PT_G5_OK;
    }
    if (reg == 0x10f0) {
        if (write)
            return PT_G5_UNSUPPORTED;
        *value = p->frequency;
        return PT_G5_OK;
    }
    if (reg >= 0x1100 && reg < 0x1200 && !(reg & 15)) {
        unsigned i = (reg - 0x1100) / 64, k = (reg & 63) / 16;
        if (k >= 2)
            return source_reg(p, 132 + i, k == 3, value, write);
        if (!k) {
            if (write)
                return PT_G5_UNSUPPORTED;
            *value = p->current[i];
            return PT_G5_OK;
        }
        if (write) {
            if (!(*value & MASK) && !(*value & ~MASK))
                return PT_G5_BAD_CONFIG;
            if ((p->base[i] & MASK) && !(*value & MASK))
                p->current[i] = *value & ~MASK;
            p->base[i] = *value;
        } else
            *value = p->base[i];
        return PT_G5_OK;
    }
    if (reg >= 0x10000 && reg < 0x11000 && !(reg & 15))
        return source_reg(p, (reg - 0x10000) / 32, reg & 16, value, write);
    return PT_G5_UNSUPPORTED;
}
PtG5Result pt_g5_pic_access(PtG5Pic *p, unsigned c, uint32_t reg, uint32_t *value, bool write) {
    if (c >= p->cpus || !value || (reg & 3))
        return PT_G5_BAD_CONFIG;
    pthread_mutex_lock(&p->lock);
    PtG5Result r = access(p, c, reg, value, write);
    route(p);
    pthread_mutex_unlock(&p->lock);
    return r;
}
PtG5Result pt_g5_pic_input(PtG5Pic *p, unsigned i, bool asserted) {
    if (i >= 128)
        return PT_G5_BAD_CONFIG;
    pthread_mutex_lock(&p->lock);
    PtG5PicSource *s = &p->source[i];
    if (asserted && !s->asserted)
        s->edge = true;
    s->asserted = asserted;
    route(p);
    pthread_mutex_unlock(&p->lock);
    return PT_G5_OK;
}
void pt_g5_pic_tick(PtG5Pic *p, uint64_t delta) {
    pthread_mutex_lock(&p->lock);
    for (unsigned i = 0; i < 4; i++)
        if (!(p->base[i] & MASK) && delta) {
            uint32_t count = p->current[i] & ~MASK, period = p->base[i] & ~MASK;
            if (!period)
                continue;
            if (delta < count)
                p->current[i] -= (uint32_t)delta;
            else {
                uint64_t elapsed = delta - count, wraps = 1 + elapsed / period;
                uint32_t toggle = (p->current[i] & MASK) ^ ((uint32_t)(wraps & 1) << 31);
                p->current[i] = toggle | (period - (uint32_t)(elapsed % period));
                PtG5PicSource *s = &p->source[132 + i];
                if (!(s->vpr & MASK))
                    s->pending |= s->dest;
            }
        }
    pthread_mutex_unlock(&p->lock);
}
unsigned pt_g5_pic_irqs(PtG5Pic *p) {
    pthread_mutex_lock(&p->lock);
    route(p);
    unsigned out = 0;
    for (unsigned c = 0; c < p->cpus; c++)
        if (best(p, c) >= 0)
            out |= 1u << c;
    pthread_mutex_unlock(&p->lock);
    return out;
}
unsigned pt_g5_pic_reset_lines(PtG5Pic *p) {
    pthread_mutex_lock(&p->lock);
    unsigned lines = p->init_lines;
    pthread_mutex_unlock(&p->lock);
    return lines;
}
