/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "bench.h"
#include "panion.h"
#include "conformance.h"
#include <mach/mach_time.h>
#include <sys/sysctl.h>
#include <sys/utsname.h>
#include <sys/time.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

double ant_now(void) {
    static mach_timebase_info_data_t info;
    if (!info.denom) mach_timebase_info(&info);
    return (double)mach_absolute_time() * (double)info.numer / info.denom * 1e-9;
}
void ant_text(const char *key, const char *value) {
    const unsigned char *p = (const unsigned char *)(value ? value : "unknown");
    printf("%s\t", key);
    for (; *p; ++p) putchar(*p == '\t' || *p == '\r' || *p == '\n' ? ' ' : *p);
    putchar('\n'); fflush(stdout);
}
void ant_number(const char *key, double value) { printf("%s\t%.17g\n", key, value); fflush(stdout); }
int ant_has_vmx(void) {
    int value = 0; size_t n = sizeof value;
    return sysctlbyname("hw.optional.altivec", &value, &n, NULL, 0) == 0 && value != 0;
}
static int number_order(const void *a, const void *b) {
    double x = *(const double *)a, y = *(const double *)b;
    return (x > y) - (x < y);
}
static double wall_now(void) {
    struct timeval t;if(gettimeofday(&t,NULL))return -1;
    return (double)t.tv_sec+t.tv_usec*1e-6;
}
int ant_measure(AntWork *w) {
    unsigned batch = 1, i, s;
    double start, elapsed, values[ANT_SAMPLES], sorted[ANT_SAMPLES];
    start = ant_now(); w->run(w->data); elapsed = ant_now() - start;
    ant_number("cold_seconds", elapsed);
    if (!w->check(w->data)) return 0;
    /* Only batch count adapts. Data sizes, work and scoring never do. */
    for (;;) {
        start = ant_now();
        for (i = 0; i < batch; ++i) w->run(w->data);
        elapsed = ant_now() - start;
        if (!w->check(w->data)) return 0;
        if (elapsed >= 0.15 || batch >= 65536) break;
        batch *= 2;
    }
    ant_number("iterations", batch);
    for (s = 0; s < ANT_SAMPLES; ++s) {
        char key[32];
        double wall_start=wall_now(),wall_elapsed;
        start = ant_now();
        for (i = 0; i < batch; ++i) w->run(w->data);
        elapsed = ant_now() - start;
        wall_elapsed=wall_now()-wall_start;
        if (!(elapsed > 0) || !isfinite(elapsed) || !w->check(w->data)) return 0;
        values[s] = elapsed / batch;
        sprintf(key, "sample_%u", s); ant_number(key, values[s]);
        sprintf(key,"wall_sample_%u",s);ant_number(key,wall_elapsed/batch);
        if(wall_start<0 || wall_elapsed<=0 || (elapsed>=0.02 && fabs(wall_elapsed-elapsed)>0.05*elapsed)) {
            ant_text("reason","Guest monotonic and wall clocks disagree");return 0;
        }
    }
    memcpy(sorted, values, sizeof sorted);
    qsort(sorted, ANT_SAMPLES, sizeof *sorted, number_order);
    ant_number("median_seconds", sorted[ANT_SAMPLES / 2]);
    ant_number("spread_fraction", (sorted[ANT_SAMPLES - 1] - sorted[0]) / sorted[ANT_SAMPLES / 2]);
    return 1;
}
int ant_image(const char *dir, const char *name, const unsigned char *pixels, int w, int h) {
    char path[4096]; FILE *f; int x, y, ok = 1;
    if (snprintf(path, sizeof path, "%s/%s.ppm", dir, name) >= (int)sizeof path) return 0;
    f = fopen(path, "wb"); if (!f) return 0;
    if (fprintf(f, "P6\n%d %d\n255\n", w, h) < 0) ok = 0;
    for (y = h - 1; y >= 0; --y) for (x = 0; x < w; ++x)
        if (fwrite(pixels + 4 * (y * w + x), 1, 3, f) != 3) ok = 0;
    if (fclose(f)) ok = 0;
    return ok;
}
int main(int argc, char **argv) {
    AntWork work; int ready, pass; struct utsname u;
    memset(&work, 0, sizeof work);
    if(argc<3 && strstr(argv[0],"/Rosetta"))return ant_panorama("gpu-rosetta",NULL,1)?0:1;
    if(argc==2 && !strcmp(argv[1],"--list-shaders")) return ant_sr_list();
    if(argc==2 && !strcmp(argv[1],"--list-graphics")) { int rc=ant_cf_list(); return rc?rc:ant_sr_list(); }
    if (argc == 2 && !strcmp(argv[1],"--list-conformance")) return ant_cf_list();
    if (argc != 3 || strchr(argv[1], '/') || strlen(argv[1]) > 64) return 64;
    ant_text("protocol", ANT_PROTOCOL); ant_text("version", ANT_VERSION);
    ant_text("id", argv[1]); ant_text("status", "running");
    if (!uname(&u)) { ant_text("machine", u.machine); ant_text("kernel", u.release); }
    ant_number("altivec", ant_has_vmx());
    if (!strncmp(argv[1],"sr-",3)) return ant_sr_run(argv[1],argv[2]);
    if (!strncmp(argv[1],"cf-",3)) return ant_cf_run(argv[1],argv[2]);
    if(pan_stage(argv[1])>=0){pass=ant_panorama(argv[1],argv[2],0);ant_text("status",pass?"pass":"fail");return ferror(stdout)?2:pass?0:1;}
    if (!strcmp(argv[1], "gpu-window") || !strcmp(argv[1], "gpu-window-offscreen")) {
        pass=ant_window(argv[1],argv[2]);
        ant_text("status",pass?"pass":"fail");return ferror(stdout)?2:pass?0:1;
    }
    if (!strcmp(argv[1], "cpu-state")) {
        pass = ant_ppc_state();
        ant_text("status", pass ? "pass" : "fail"); return ferror(stdout) ? 2 : pass ? 0 : 1;
    }
    if (!strcmp(argv[1],"cpu-veclib-fft")) ready=ant_veclib(&work);
    else if (!strncmp(argv[1], "cpu-", 4)) ready = ant_cpu(argv[1], &work);
    else if (!strncmp(argv[1], "vmx-", 4)) {
        ant_text("media_backend", ant_has_vmx() ? "AltiVec" : "scalar");
        ready = ant_has_vmx() ? ant_vmx(argv[1], &work) : ant_cpu(argv[1], &work);
    }
    else if (!strncmp(argv[1], "gpu-", 4)) ready = ant_gpu(argv[1], argv[2], &work);
    else ready = 0;
    if (ready < 0) { ant_text("status", "unsupported"); return 0; }
    if (!ready) { ant_text("status", "error"); return 2; }
    pass = ant_measure(&work);
    if (work.destroy) work.destroy(work.data);
    ant_text("status", pass ? "pass" : "fail"); return ferror(stdout) ? 2 : pass ? 0 : 1;
}
