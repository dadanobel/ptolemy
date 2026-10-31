/* Guest clock continuity probe (Tiger, devkit ppc-cc). For N seconds of guest time it sleeps 1 ms at a
 * time (a DEC deadline each) and records the largest step of mach_absolute_time (TB) and gettimeofday between samples.
 * A host pause must not show up: the steps stay at the usleep granularity and the elapsed guest time stays N. */
#include <mach/mach_time.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>
#include <unistd.h>

static double tod(void) { struct timeval t; gettimeofday(&t, NULL); return t.tv_sec + t.tv_usec / 1e6; }

int main(int argc, char **argv)
{
    int seconds = argc > 1 ? atoi(argv[1]) : 10;
    mach_timebase_info_data_t tb; mach_timebase_info(&tb);
    uint64_t m0 = mach_absolute_time(), m = m0; double t0 = tod(), t = t0, gm = 0, gt = 0; long n = 0;
    while ((m - m0) * tb.numer / tb.denom < (uint64_t)seconds * 1000000000ull) {
        usleep(1000);
        uint64_t m1 = mach_absolute_time(); double t1 = tod();
        double dm = (m1 - m) * (double)tb.numer / tb.denom / 1e6, dt = (t1 - t) * 1e3;
        if (dm > gm) gm = dm;
        if (dt > gt) gt = dt;
        m = m1; t = t1; n++;
    }
    printf("CLOCK seconds=%d samples=%ld mach_elapsed=%.3f tod_elapsed=%.3f max_step_mach_ms=%.2f max_step_tod_ms=%.2f\n",
           seconds, n, (m - m0) * (double)tb.numer / tb.denom / 1e9, t - t0, gm, gt);
    return 0;
}
