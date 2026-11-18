/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef ANTIOCHUS_BENCH_H
#define ANTIOCHUS_BENCH_H
#include <stdint.h>
#include <stddef.h>
#define ANT_VERSION "1.0-draft4"
#define ANT_PROTOCOL "antiochus-1"
#define ANT_SAMPLES 5
typedef struct {
    void *data;
    void (*run)(void *);
    int (*check)(void *);
    void (*destroy)(void *);
} AntWork;
double ant_now(void);
void ant_text(const char *key, const char *value);
void ant_number(const char *key, double value);
int ant_measure(AntWork *work);
int ant_veclib(AntWork *work);
int ant_cpu(const char *id, AntWork *work);
int ant_gpu(const char *id, const char *directory, AntWork *work);
int ant_vmx(const char *id, AntWork *work);
int ant_ppc_state(void);
int ant_has_vmx(void);
int ant_panorama(const char *id,const char *directory,int live);
int ant_window(const char *id, const char *directory);
int ant_image(const char *directory, const char *name, const unsigned char *rgba, int w, int h);
#endif
