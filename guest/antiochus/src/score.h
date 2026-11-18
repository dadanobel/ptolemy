/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef ANTIOCHUS_SCORE_H
#define ANTIOCHUS_SCORE_H
#define ANT_CASE_COUNT 34
enum { ANT_GATE, ANT_CPU, ANT_GPU, ANT_GPU_EXTRA, ANT_DIAGNOSTIC, ANT_GPU_SCENE, ANT_COMBINED };
typedef struct { const char *id; int group; } AntCase;
typedef struct { int attempted, passed, unsupported; double seconds; } AntResult;
typedef struct { double cpu, gpu, total; int complete, failed, optional_failed; double gpu_speed, combined; int gpu_passed, gpu_checked, diagnostic_failed; } AntScore;
extern const AntCase ant_cases[ANT_CASE_COUNT];
AntScore ant_score(const AntResult results[ANT_CASE_COUNT]);
#endif
