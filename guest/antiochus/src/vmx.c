/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "bench.h"
#ifndef __APPLE_ALTIVEC__
#include <altivec.h>
#endif
#include <stdlib.h>
#include <string.h>
enum { VMX_BYTES = 262144 };
typedef struct {
    unsigned kind; void *raw[3]; unsigned char *a, *b, *c;
    vector unsigned short saved_vscr;
} VData;
static volatile unsigned char observed;
static void run(void *opaque) {
    VData *d = opaque; unsigned i;
    vector unsigned char mask = (vector unsigned char)(0,7,14,21,28,3,10,17,24,31,6,13,20,27,2,9);
    for (i = 0; i < VMX_BYTES; i += 16) {
        if (d->kind == 2) {
            vector float a = vec_ld(i, (float *)d->a), b = vec_ld(i, (float *)d->b);
            vector float r = vec_madd(a, b, a); vec_st(r, i, (float *)d->c);
        } else {
            vector unsigned char a = vec_ld(i, d->a), b = vec_ld(i, d->b), r;
            r = d->kind == 0 ? vec_adds(a, b) : vec_perm(a, b, mask); vec_st(r, i, d->c);
        }
    }
    observed = d->c[0];
}
static int check(void *opaque) {
    VData *d = opaque; unsigned i;
    if (d->kind == 2) {
        const float *a = (const float *)d->a, *b = (const float *)d->b, *c = (const float *)d->c;
        for (i = 0; i < VMX_BYTES / 4; ++i) if (c[i] != (float)((double)a[i] * b[i] + a[i])) return 0;
    } else for (i = 0; i < VMX_BYTES; ++i) {
        unsigned expected;
        if (d->kind == 0) { expected = (unsigned)d->a[i] + d->b[i]; if (expected > 255) expected = 255; }
        else { unsigned at = (i % 16) * 7 % 32; expected = at < 16 ? d->a[(i & ~15u) + at] : d->b[(i & ~15u) + at - 16]; }
        if (d->c[i] != expected) return 0;
    }
    if (d->kind == 0) {
        union { vector unsigned short v; unsigned int words[4]; } state;
        state.v = vec_mfvscr(); if (!(state.words[3] & 1)) return 0;
    }
    return 1;
}
static void destroy(void *opaque) {
    VData *d = opaque; unsigned i; vec_mtvscr(d->saved_vscr);
    for (i = 0; i < 3; ++i) free(d->raw[i]); free(d);
}
int ant_vmx(const char *id, AntWork *w) {
    VData *d; unsigned i; unsigned char **p[3]; uint32_t seed = 0x564d5831u;
    unsigned kind = !strcmp(id, "vmx-saturate") ? 0 : !strcmp(id, "vmx-permute") ? 1 : !strcmp(id, "vmx-madd") ? 2 : 3;
    if (kind == 3) return 0;
    d = calloc(1, sizeof *d); if (!d) return 0; d->kind = kind; d->saved_vscr = vec_mfvscr();
    p[0] = &d->a; p[1] = &d->b; p[2] = &d->c;
    for (i = 0; i < 3; ++i) { d->raw[i] = malloc(VMX_BYTES + 15); if (!d->raw[i]) { destroy(d); return 0; }
        *p[i] = (unsigned char *)(((uintptr_t)d->raw[i] + 15) & ~(uintptr_t)15); }
    for (i = 0; i < VMX_BYTES; ++i) { seed = seed * 1664525u + 1013904223u; d->a[i] = seed >> 24;
        seed = seed * 1664525u + 1013904223u; d->b[i] = seed >> 24; }
    if (kind == 2) for (i = 0; i < VMX_BYTES / 4; ++i) {
        ((float *)d->a)[i] = (float)((int)(i % 127) - 63) / 16;
        ((float *)d->b)[i] = (float)((int)(i % 97) - 48) / 8;
    }
    {
        vector unsigned int zero = (vector unsigned int)(0,0,0,0);
        vec_mtvscr((vector unsigned short)zero);
    }
    w->data = d; w->run = run; w->check = check; w->destroy = destroy;
    ant_number("bytes", VMX_BYTES); return 1;
}
