/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "bench.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
enum { CRC_BYTES = 1048576, SORT_N = 32768, MATRIX_N = 48, FFT_N = 2048 };
typedef struct {
    unsigned kind, n; uint32_t crc_table[256], expected, result;
    uint32_t *a, *b, *c; double *x, *y, *z; uint64_t sum; uint32_t fp_status;
} Data;
static volatile uint32_t observed;
static void fp_start(int sticky) {
#if defined(__ppc__) || defined(__POWERPC__)
    union { uint64_t u; double f; } control;
    control.u=sticky?UINT64_C(0x82000000):0;
    __asm__ volatile("mtfsf 255,%0" : : "f"(control.f) : "memory");
#else
    (void)sticky;
#endif
}
static uint32_t fp_status(void) {
#if defined(__ppc__) || defined(__POWERPC__)
    union { uint64_t u; double f; } status;
    __asm__ volatile("mffs %0" : "=f"(status.f) : : "memory");return (uint32_t)status.u;
#else
    return 0;
#endif
}
static uint32_t random_word(uint32_t *s) { *s = *s * 1664525u + 1013904223u; return *s; }
static uint32_t crc_slow(const unsigned char *p, size_t n) {
    uint32_t c = ~0u; size_t i; int j;
    for (i = 0; i < n; ++i) { c ^= p[i]; for (j = 0; j < 8; ++j) c = (c >> 1) ^ (0xedb88320u & (0u - (c & 1))); }
    return ~c;
}
static int word_order(const void *a, const void *b) {
    uint32_t x = *(const uint32_t *)a, y = *(const uint32_t *)b; return (x > y) - (x < y);
}
static void sort_words(uint32_t *a, uint32_t *tmp, unsigned n) {
    unsigned width, base;
    for (width = 1; width < n; width *= 2) {
        for (base = 0; base < n; base += 2 * width) {
            unsigned i = base, j = base + width, k = base, mid = j, end = base + 2 * width;
            if (mid > n) mid = n; if (end > n) end = n;
            while (i < mid || j < end) tmp[k++] = j >= end || (i < mid && a[i] <= a[j]) ? a[i++] : a[j++];
        }
        memcpy(a, tmp, n * sizeof *a);
    }
}
static void fft(double *r, double *im, unsigned n) {
    unsigned i, j = 0, bit, size, base, k;
    for (i = 1; i < n; ++i) {
        bit = n >> 1; while (j & bit) { j ^= bit; bit >>= 1; } j ^= bit;
        if (i < j) { double t = r[i]; r[i] = r[j]; r[j] = t; t = im[i]; im[i] = im[j]; im[j] = t; }
    }
    for (size = 2; size <= n; size *= 2) {
        double angle = -6.2831853071795864769 / size, wr = cos(angle), wi = sin(angle);
        for (base = 0; base < n; base += size) {
            double ar = 1, ai = 0;
            for (k = 0; k < size / 2; ++k) {
                unsigned p = base + k, q = p + size / 2;
                double tr = ar * r[q] - ai * im[q], ti = ar * im[q] + ai * r[q], next;
                r[q] = r[p] - tr; im[q] = im[p] - ti; r[p] += tr; im[p] += ti;
                next = ar * wr - ai * wi; ai = ar * wi + ai * wr; ar = next;
            }
        }
    }
}
static void run(void *opaque) {
    Data *d = opaque; unsigned i, j, k;
    if (d->kind == 0) {
        const unsigned char *p = (const unsigned char *)d->a; uint32_t c = ~0u;
        for (i = 0; i < CRC_BYTES; ++i) c = (c >> 8) ^ d->crc_table[(c ^ p[i]) & 255];
        d->result = ~c;
    } else if (d->kind == 1) {
        memcpy(d->b, d->a, SORT_N * sizeof(uint32_t)); sort_words(d->b, d->c, SORT_N); d->result = d->b[SORT_N / 2];
    } else if (d->kind == 2 || d->kind == 10) {
        fp_start(d->kind == 10);
        for (i = 0; i < MATRIX_N; ++i) for (j = 0; j < MATRIX_N; ++j) {
            double sum = 0;
            for (k = 0; k < MATRIX_N; ++k) sum += d->x[i * MATRIX_N + k] * d->y[k * MATRIX_N + j];
            d->z[i * MATRIX_N + j] = sum;
        }
        memcpy(&d->result, &d->z[0], sizeof d->result);
        d->fp_status=fp_status();
    } else if (d->kind == 3) {
        memcpy(d->y, d->x, FFT_N * sizeof(double)); memset(d->z, 0, FFT_N * sizeof(double)); fft(d->y, d->z, FFT_N);
        memcpy(&d->result, &d->y[0], sizeof d->result);
    } else if (d->kind == 4 || d->kind == 5) {
        for (i = 0; i < d->n; ++i) d->c[i] = d->a[i] + 3u * d->b[i];
        d->result = d->c[d->n - 1];
    } else if (d->kind == 6) {
        uint32_t at = 0; uint64_t sum = 0;
        for (i = 0; i < d->n; ++i) { at = d->a[at]; sum += at; }
        d->sum = sum; d->result = at;
    } else {
        unsigned char *a = (unsigned char *)d->a, *b = (unsigned char *)d->b, *c = (unsigned char *)d->c;
        for (i = 0; i < 262144; ++i) {
            if (d->kind == 7) { unsigned v = (unsigned)a[i] + b[i]; c[i] = v > 255 ? 255 : v; }
            else if (d->kind == 8) { unsigned at = (i % 16) * 7 % 32; c[i] = at < 16 ? a[(i & ~15u) + at] : b[(i & ~15u) + at - 16]; }
            else break;
        }
        if (d->kind == 9) for (i = 0; i < 65536; ++i)
            ((float *)c)[i] = ((float *)a)[i] * ((float *)b)[i] + ((float *)a)[i];
        d->result = c[0];
    }
    observed = d->result;
}
static int check(void *opaque) {
    Data *d = opaque; unsigned i, j, k;
    if (d->kind == 0) return d->result == d->expected;
    if (d->kind == 1) {
        uint32_t *ref = malloc(SORT_N * sizeof *ref); int ok;
        if (!ref) return 0;
        memcpy(ref, d->a, SORT_N * sizeof *ref); qsort(ref, SORT_N, sizeof *ref, word_order);
        ok = !memcmp(ref, d->b, SORT_N * sizeof *ref); free(ref); return ok;
    }
    if (d->kind == 2 || d->kind == 10) {
#if defined(__ppc__) || defined(__POWERPC__)
        if((d->fp_status & 0x82000003u)!=(d->kind==10?0x82000000u:0))return 0;
#endif
        for (i = 0; i < MATRIX_N; ++i) for (j = 0; j < MATRIX_N; ++j) {
            int64_t sum = 0;
            for (k = 0; k < MATRIX_N; ++k) sum += (int64_t)d->x[i * MATRIX_N + k] * (int64_t)d->y[k * MATRIX_N + j];
            if (d->z[i * MATRIX_N + j] != (double)sum) return 0;
        }
        return 1;
    }
    if (d->kind == 3) {
        double input_energy = 0, output_energy = 0;
        for (i = 0; i < FFT_N; ++i) {
            if (!isfinite(d->y[i]) || !isfinite(d->z[i])) return 0;
            input_energy += d->x[i] * d->x[i]; output_energy += d->y[i] * d->y[i] + d->z[i] * d->z[i];
        }
        if (fabs(output_energy / FFT_N - input_energy) > 1e-8 * input_energy) return 0;
        /* Direct DFT at spread-out bins is independent of the butterfly algorithm. */
        for (k = 0; k < 17; ++k) {
            unsigned bin = k * 127 % FFT_N; double real = 0, imag = 0;
            for (j = 0; j < FFT_N; ++j) { double a = -6.2831853071795864769 * bin * j / FFT_N;
                real += d->x[j] * cos(a); imag += d->x[j] * sin(a); }
            if (fabs(real - d->y[bin]) > 1e-7 || fabs(imag - d->z[bin]) > 1e-7) return 0;
        }
        return 1;
    }
    if (d->kind == 4 || d->kind == 5) {
        for (i = 0; i < d->n; ++i)
            if (d->c[i] != (uint32_t)((uint64_t)d->a[i] + 3ull * d->b[i])) return 0;
        return 1;
    }
    if (d->kind == 6) return d->result == 0 && d->sum == (uint64_t)d->n * (d->n - 1) / 2;
    if (d->kind == 9) {
        for (i = 0; i < 65536; ++i) if (((float *)d->c)[i] !=
            (float)((double)((float *)d->a)[i] * ((float *)d->b)[i] + ((float *)d->a)[i])) return 0;
    } else for (i = 0; i < 262144; ++i) {
        const unsigned char *a = (const unsigned char *)d->a, *b = (const unsigned char *)d->b, *c = (const unsigned char *)d->c;
        unsigned expected;
        if (d->kind == 7) { expected = (unsigned)a[i] + b[i]; if (expected > 255) expected = 255; }
        else { unsigned at = (i % 16) * 7 % 32; expected = at < 16 ? a[(i & ~15u) + at] : b[(i & ~15u) + at - 16]; }
        if (c[i] != expected) return 0;
    }
    return 1;
}
static void destroy(void *opaque) {
    Data *d = opaque; free(d->a); free(d->b); free(d->c); free(d->x); free(d->y); free(d->z); free(d);
}
int ant_cpu(const char *id, AntWork *w) {
    static const char *names[] = {"cpu-crc", "cpu-sort", "cpu-matrix", "cpu-fft", "cpu-stream-small", "cpu-stream-large", "cpu-chase",
        "vmx-saturate", "vmx-permute", "vmx-madd", "cpu-matrix-sticky"};
    Data *d; unsigned kind, i, j; uint32_t seed = 0x416e7469u;
    for (kind = 0; kind < sizeof names / sizeof *names; ++kind) if (!strcmp(id, names[kind])) break;
    if (kind == sizeof names / sizeof *names) return 0;
    d = calloc(1, sizeof *d); if (!d) return 0; d->kind = kind;
    d->n = kind == 0 ? CRC_BYTES / 4 : kind == 1 ? SORT_N : (kind == 2 || kind == 10) ? MATRIX_N * MATRIX_N : kind == 3 ? FFT_N :
        kind == 4 ? 2048 : kind == 5 ? 2097152 : kind == 6 ? 1048576 : 65536;
    if (kind == 2 || kind == 3 || kind == 10) {
        unsigned n = kind == 3 ? FFT_N : MATRIX_N * MATRIX_N;
        d->x = calloc(n, sizeof(double)); d->y = calloc(n, sizeof(double)); d->z = calloc(n, sizeof(double));
        if (!d->x || !d->y || !d->z) { destroy(d); return 0; }
        for (i = 0; i < n; ++i) { d->x[i] = (int)(random_word(&seed) % 31) - 15;
            d->y[i] = (int)(random_word(&seed) % 29) - 14; }
    } else {
        d->a = malloc(d->n * sizeof(uint32_t)); d->b = malloc(d->n * sizeof(uint32_t)); d->c = malloc(d->n * sizeof(uint32_t));
        if (!d->a || !d->b || !d->c) { destroy(d); return 0; }
        for (i = 0; i < d->n; ++i) { d->a[i] = random_word(&seed); d->b[i] = random_word(&seed); }
        if (kind == 6) {
            for (i = 0; i < d->n; ++i) d->b[i] = i;
            for (i = d->n - 1; i; --i) { uint32_t tmp; j = random_word(&seed) % (i + 1); tmp = d->b[i]; d->b[i] = d->b[j]; d->b[j] = tmp; }
            for (i = 0; i < d->n; ++i) d->a[d->b[i]] = d->b[(i + 1) % d->n];
        }
        if (kind >= 7) {
            unsigned char *a = (unsigned char *)d->a, *b = (unsigned char *)d->b;
            seed = 0x564d5831u;
            for (i = 0; i < 262144; ++i) { a[i] = random_word(&seed) >> 24; b[i] = random_word(&seed) >> 24; }
            if (kind == 9) for (i = 0; i < 65536; ++i) {
                ((float *)a)[i] = (float)((int)(i % 127) - 63) / 16;
                ((float *)b)[i] = (float)((int)(i % 97) - 48) / 8;
            }
        }
        if (kind == 0) {
            /* Byte input is identical on big- and little-endian systems. */
            unsigned char *bytes = (unsigned char *)d->a;
            for (i = 0; i < CRC_BYTES; ++i) bytes[i] = (unsigned char)(random_word(&seed) >> 24);
            for (i = 0; i < 256; ++i) { uint32_t c = i; for (j = 0; j < 8; ++j) c = (c >> 1) ^ (0xedb88320u & (0u - (c & 1))); d->crc_table[i] = c; }
            d->expected = crc_slow(bytes, CRC_BYTES);
            if (crc_slow((const unsigned char *)"123456789", 9) != 0xcbf43926u) { destroy(d); return 0; }
        }
    }
    ant_number("seed", (kind>=7 && kind<=9)?0x564d5831u:0x416e7469u); ant_number("working_elements", d->n);
    if(kind==2 || kind==10)ant_text("fp_entry",kind==2?"RN=nearest, XX=0, FX=0":"RN=nearest, XX=1, FX=1");
    w->data = d; w->run = run; w->check = check; w->destroy = destroy; return 1;
}
