/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "score.h"
#include <math.h>
#include <string.h>
const AntCase ant_cases[ANT_CASE_COUNT] = {
    {"cpu-state",ANT_GATE}, {"cpu-crc",ANT_CPU}, {"cpu-sort",ANT_CPU}, {"cpu-matrix",ANT_CPU},
    {"cpu-fft",ANT_CPU}, {"cpu-stream-small",ANT_CPU}, {"cpu-stream-large",ANT_CPU}, {"cpu-chase",ANT_CPU},
    {"vmx-saturate",ANT_CPU}, {"vmx-permute",ANT_CPU}, {"vmx-madd",ANT_CPU},
    {"gpu-fill",ANT_GPU}, {"gpu-texture",ANT_GPU}, {"gpu-depth-blend",ANT_GPU},
    {"gpu-geometry",ANT_GPU}, {"gpu-upload",ANT_GPU}, {"gpu-scene",ANT_GPU},
    {"gpu-render-texture",ANT_GPU_EXTRA}, {"gpu-shader",ANT_GPU_EXTRA},
    {"gpu-shader-branch",ANT_GPU_EXTRA}, {"gpu-msaa",ANT_GPU_EXTRA},
    {"gpu-packed",ANT_GPU}, {"gpu-dxt1",ANT_GPU_EXTRA}, {"gpu-float",ANT_GPU_EXTRA},
    {"cpu-matrix-sticky",ANT_CPU},
    {"gpu-window",ANT_GPU_EXTRA}, {"gpu-window-offscreen",ANT_DIAGNOSTIC},
    {"gpu-shader-branch-fallback",ANT_DIAGNOSTIC}, {"cpu-veclib-fft",ANT_CPU},
    {"gpu-rosetta",ANT_GPU_SCENE}, {"gpu-paneion",ANT_GPU_SCENE},
    {"gpu-phalanx",ANT_GPU_SCENE}, {"gpu-cavalry",ANT_GPU_SCENE}, {"combined-panion",ANT_COMBINED}
};
AntScore ant_score(const AntResult *r) {
    AntScore s; double cpu=0,gpu=0; unsigned i,nc=0,ng=0,good_core=0,extras=0;
    memset(&s,0,sizeof s);s.complete=1;
    for(i=0;i<ANT_CASE_COUNT;++i) {
        double term=0;int good=r[i].passed && !r[i].unsupported && isfinite(r[i].seconds) && r[i].seconds>=1e-12;
        if(!r[i].attempted)s.complete=0;
        if(ant_cases[i].group==ANT_GATE){if(!r[i].passed)++s.failed;continue;}
        if(ant_cases[i].group==ANT_DIAGNOSTIC){
            if(r[i].attempted && !r[i].unsupported && !good)++s.diagnostic_failed;
            continue;
        }
        if(ant_cases[i].group==ANT_GPU || ant_cases[i].group==ANT_GPU_SCENE || ant_cases[i].group==ANT_COMBINED || ant_cases[i].group==ANT_GPU_EXTRA) {
            if(ant_cases[i].group!=ANT_GPU_EXTRA || !r[i].unsupported)++s.gpu_checked;
            if(good)++s.gpu_passed;
        }
        if(ant_cases[i].group==ANT_COMBINED){++extras;if(!good)++s.optional_failed;s.combined=good?1000.0/(60*r[i].seconds):0;continue;}
        if(ant_cases[i].group==ANT_GPU_EXTRA){
            if(!r[i].unsupported){++extras;if(!good)++s.optional_failed;}
            continue;
        }
        if(good)term=log1p((ant_cases[i].group==ANT_GPU_SCENE?1.0/60:.001)/r[i].seconds);else ++s.failed;
        if(ant_cases[i].group==ANT_CPU){cpu+=term;++nc;}
        else {gpu+=term;++ng;if(good)++good_core;}
    }
    if(!s.complete){s.combined=0;return s;}
    s.cpu=r[0].passed?1000*expm1(cpu/nc):0;
    s.gpu_speed=good_core?1000*expm1(gpu/good_core):0;
    /* Core failures retain their weight; extra checks use a linear pass fraction. */
    s.gpu=1000*expm1(gpu/ng)*(extras?(double)(extras-s.optional_failed)/extras:1);
    if(!r[0].passed)s.combined=0;
    s.total=cbrt(s.cpu*s.gpu*s.combined);return s;
}
