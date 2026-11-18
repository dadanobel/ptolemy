/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "bench.h"
#include <Accelerate/Accelerate.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
enum { FFT_LOG=11, FFT_SIZE=1<<FFT_LOG, FFT_BINS=17 };
typedef struct {
    FFTSetup setup;DSPSplitComplex output;
    float *input_real,*input_imag;void *storage;
    double real[FFT_BINS],imag[FFT_BINS],energy;
} LibraryFFT;
static void run(void *opaque) {
    LibraryFFT *d=opaque;
    memcpy(d->output.realp,d->input_real,FFT_SIZE*sizeof(float));
    memcpy(d->output.imagp,d->input_imag,FFT_SIZE*sizeof(float));
    vDSP_fft_zip(d->setup,&d->output,1,FFT_LOG,FFT_FORWARD);
}
static int check(void *opaque) {
    LibraryFFT *d=opaque;unsigned i,k;double energy=0;
    for(i=0;i<FFT_SIZE;++i) {
        double r=d->output.realp[i],m=d->output.imagp[i];
        if(!isfinite(r) || !isfinite(m))return 0;
        energy+=r*r+m*m;
    }
    for(i=0;i<FFT_BINS;++i) {
        k=(i*127)%FFT_SIZE;
        if(fabs(d->output.realp[k]-d->real[i])>0.002 || fabs(d->output.imagp[k]-d->imag[i])>0.002)return 0;
    }
    return fabs(energy-d->energy)<d->energy*0.00002;
}
static void destroy(void *opaque) {
    LibraryFFT *d=opaque;if(d->setup)vDSP_destroy_fftsetup(d->setup);free(d->storage);free(d);
}
int ant_veclib(AntWork *w) {
    LibraryFFT *d=calloc(1,sizeof *d);unsigned i,j;uint32_t seed=0x416e7466;
    if(!d)return 0;
    d->setup=vDSP_create_fftsetup(FFT_LOG,FFT_RADIX2);d->storage=malloc(4*FFT_SIZE*sizeof(float)+15);
    if(!d->setup || !d->storage){destroy(d);return 0;}
    d->input_real=(float *)(((uintptr_t)d->storage+15)&~(uintptr_t)15);
    d->input_imag=d->input_real+FFT_SIZE;d->output.realp=d->input_imag+FFT_SIZE;d->output.imagp=d->output.realp+FFT_SIZE;
    for(i=0;i<FFT_SIZE;++i) {
        seed=seed*1664525u+1013904223u;d->input_real[i]=((int)(seed>>24)-128)/128.0f;
        seed=seed*1664525u+1013904223u;d->input_imag[i]=((int)(seed>>24)-128)/128.0f;
        d->energy+=(double)d->input_real[i]*d->input_real[i]+(double)d->input_imag[i]*d->input_imag[i];
    }
    d->energy*=FFT_SIZE;
    for(i=0;i<FFT_BINS;++i)for(j=0;j<FFT_SIZE;++j) {
        double angle=-2*3.14159265358979323846*((i*127)%FFT_SIZE)*j/FFT_SIZE,c=cos(angle),s=sin(angle);
        d->real[i]+=d->input_real[j]*c-d->input_imag[j]*s;
        d->imag[i]+=d->input_real[j]*s+d->input_imag[j]*c;
    }
    ant_text("library","Accelerate/vecLib vDSP_fft_zip, complex float, forward");
    ant_text("oracle","17 direct-DFT bins plus Parseval, outside timing; not an Xbench reproduction");
    ant_number("fft_elements",FFT_SIZE);ant_number("seed",0x416e7466);
    w->data=d;w->run=run;w->check=check;w->destroy=destroy;return 1;
}
